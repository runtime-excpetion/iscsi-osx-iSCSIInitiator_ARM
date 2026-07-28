/*
 * Copyright (c) 2023, Nareg Sinenian
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * iSCSIDextIPC.c — Daemon-side IPC client for iSCSI DEXT communication.
 *
 * Communicates with the iSCSI DriverKit System Extension via IOKit
 * IOConnectCallMethod / ExternalMethod calls. Manages:
 *   - Service discovery and user client connection
 *   - Control channel (session/connection lifecycle, parameters)
 *   - Data channel (shared memory ring buffer for PDU payloads)
 *   - Async notification port for DEXT→daemon events
 */

#include "iSCSIDextIPC.h"

#include <stdlib.h>
#include <string.h>
#include <asl.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <mach/mach_port.h>
#include <mach/mach_init.h>

#pragma mark - Constants

/*! The DEXT service class name as published in Info.plist IOKitPersonalities. */
#define kDextServiceClassName   "iSCSIDextHBA"

/*! User client type (matching the DEXT's IOUserServer definition). */
#define kDextUserClientType     0

/*! Size of the shared memory data queue (2MB, enough for multiple PDUs). */
#define kDextDataQueueSize      (2 * 1024 * 1024)

#pragma mark - Internal Structures

/*! Opaque IPC handle. */
struct DextIPC {
    /*! CFAllocator used for allocations. */
    CFAllocatorRef allocator;

    /*! The DEXT IOService. */
    io_service_t  service;

    /*! User client connection to the DEXT. */
    io_connect_t  connection;

    /*! Mach port for async notifications from DEXT. */
    mach_port_t   notifyPort;

    /*! CFMachPort wrapping the notification Mach port. */
    CFMachPortRef cfNotifyPort;

    /*! Run loop source for the notification port. */
    CFRunLoopSourceRef notifySource;

    /*! Shared memory descriptor for data queue (DEXT→daemon, outgoing PDUs). */
    // (reserved for future IODataQueue integration)

    /*! Current data queue read position (bytestream offset). */
    uint64_t      dataQueueReadOffset;

    /*! Cached full PDU for the polling model (kDextCmdGetNextPDU). */
    DextOutgoingPDUFull lastPDU;

    /*! Whether lastPDU holds valid data awaiting consumption via CopyOutgoingData. */
    bool          hasPendingPDU;

    /*! Callbacks registered by the daemon. */
    DextOutgoingPDUCallback       outgoingCallback;
    DextConnectionEventCallback   connectionEventCallback;
    void *                        context;
};

#pragma mark - Internal Helpers

static kern_return_t dext_execute_command(
    struct DextIPC * ipc,
    uint32_t selector,
    const uint64_t * scalarInput,
    uint32_t scalarInputCnt,
    const uint8_t * structInput,
    size_t structInputSize,
    uint64_t * scalarOutput,
    uint32_t * scalarOutputCnt,
    uint8_t * structOutput,
    size_t * structOutputSize)
{
    if (!ipc || ipc->connection == 0) {
        return kIOReturnNotOpen;
    }

    return IOConnectCallMethod(
        ipc->connection,
        selector,
        scalarInput, scalarInputCnt,
        structInput, structInputSize,
        scalarOutput, scalarOutputCnt,
        structOutput, structOutputSize);
}

static kern_return_t dext_execute_scalar(
    struct DextIPC * ipc,
    uint32_t selector,
    const uint64_t * scalarInput,
    uint32_t scalarInputCnt,
    uint64_t * scalarOutput,
    uint32_t * scalarOutputCnt)
{
    return dext_execute_command(ipc, selector,
                                scalarInput, scalarInputCnt,
                                NULL, 0,
                                scalarOutput, scalarOutputCnt,
                                NULL, NULL);
}

/*! ASYNC notification handler called by CFMachPort. */
static void dext_notification_handler(CFMachPortRef port,
                                       void * msg,
                                       CFIndex size,
                                       void * info)
{
    struct DextIPC * ipc = (struct DextIPC *)info;
    if (!ipc) return;

    mach_msg_header_t * hdr = (mach_msg_header_t *)msg;

    // The message payload starts after the mach header
    // DEXT sends messages with a 4-byte type code followed by payload
    if (size < (CFIndex)(sizeof(mach_msg_header_t) + 4)) {
        asl_log(NULL, NULL, ASL_LEVEL_DEBUG,
                "DextIPC: short notification (%ld bytes)", (long)size);
        return;
    }

    uint8_t * payload = (uint8_t *)(hdr + 1);
    uint32_t eventType = *(uint32_t *)payload;
    size_t payloadSize = (size_t)size - sizeof(mach_msg_header_t) - 4;

    switch (eventType) {
        case kDextEventPDUReadyForSend:
            // Poll for PDUs via GetNextPDU and invoke the callback for each
            if (ipc->outgoingCallback) {
                DextOutgoingPDUFull pduFull;
                while (DextIPCReadNextOutgoingPDU(ipc, &pduFull)) {
                    // Pass the full PDU (BHS + inline data) directly to callback.
                    // The daemon sends it via TCP without needing additional copies.
                    ipc->outgoingCallback(ipc, &pduFull, ipc->context);
                }
            }
            break;

        case kDextEventTaskCompleted:
            asl_log(NULL, NULL, ASL_LEVEL_DEBUG,
                    "DextIPC: task completed notification");
            break;

        case kDextEventTaskAborted:
            asl_log(NULL, NULL, ASL_LEVEL_DEBUG,
                    "DextIPC: task aborted notification");
            break;

        case kDextEventRequestConnect:
            asl_log(NULL, NULL, ASL_LEVEL_DEBUG,
                    "DextIPC: DEXT requesting connect");
            break;

        case kDextEventRequestDisconnect:
            asl_log(NULL, NULL, ASL_LEVEL_DEBUG,
                    "DextIPC: DEXT requesting disconnect");
            break;

        default:
            asl_log(NULL, NULL, ASL_LEVEL_DEBUG,
                    "DextIPC: unknown event type %u", (unsigned)eventType);
            break;
    }
}

#pragma mark - Lifecycle

DextIPCRef DextIPCCreate(CFAllocatorRef allocator,
                          DextIPCCallbacks callbacks)
{
    struct DextIPC * ipc = (struct DextIPC *)calloc(1, sizeof(struct DextIPC));
    if (!ipc) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: failed to allocate IPC handle");
        return NULL;
    }

    ipc->allocator = allocator ? allocator : kCFAllocatorDefault;
    ipc->outgoingCallback = callbacks.outgoingPDU;
    ipc->connectionEventCallback = callbacks.connectionEvent;
    ipc->context = callbacks.context;

    // 1. Find the DEXT service in the IORegistry
    CFDictionaryRef matching = IOServiceMatching(kDextServiceClassName);
    if (!matching) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: IOServiceMatching failed");
        goto fail;
    }

    io_iterator_t iterator;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMasterPortDefault,
                                                     matching, &iterator);
    if (kr != KERN_SUCCESS) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: IOServiceGetMatchingServices failed: 0x%x", kr);
        goto fail;
    }

    ipc->service = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    if (!ipc->service) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: DEXT service not found in IORegistry. "
                "Run 'systemextensionsctl list' to check registration.");
        goto fail;
    }

    // 2. Create notification port before opening the connection
    ipc->notifyPort = MACH_PORT_NULL;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
                       &ipc->notifyPort);

    // 3. Open user client connection to the DEXT
    kr = IOServiceOpen(ipc->service, mach_task_self(), kDextUserClientType,
                       &ipc->connection);
    if (kr != KERN_SUCCESS) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: IOServiceOpen failed: 0x%x", kr);
        goto fail;
    }

    // 4. Register the notification port with the DEXT
    kr = IOConnectSetNotificationPort(ipc->connection, kDextCmdOpen,
                                       ipc->notifyPort, 0);
    if (kr != KERN_SUCCESS) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: IOConnectSetNotificationPort failed: 0x%x", kr);
        goto fail;
    }

    // 5. Open communication channel (kDextCmdOpen)
    kr = dext_execute_scalar(ipc, kDextCmdOpen, NULL, 0, NULL, NULL);
    if (kr != KERN_SUCCESS) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: kDextCmdOpen failed: 0x%x", kr);
        goto fail;
    }

    // 6. Set up CFMachPort for the notification port
    CFMachPortContext ctx;
    bzero(&ctx, sizeof(ctx));
    ctx.info = ipc;

    ipc->cfNotifyPort = CFMachPortCreateWithPort(
        ipc->allocator, ipc->notifyPort,
        dext_notification_handler, &ctx, NULL);
    if (!ipc->cfNotifyPort) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: CFMachPortCreateWithPort failed");
        goto fail;
    }

    asl_log(NULL, NULL, ASL_LEVEL_INFO,
            "DextIPC: connected to DEXT service");

    // Allocate shared memory for data queue (DEXT→daemon)
    // Note: Full IODataQueue setup will be done when the DEXT side is ready
    // For now, we allocate a buffer and register it via ExternalMethod

    return ipc;

fail:
    if (ipc->cfNotifyPort) {
        CFMachPortInvalidate(ipc->cfNotifyPort);
        CFRelease(ipc->cfNotifyPort);
        ipc->cfNotifyPort = NULL;
    }
    if (ipc->notifyPort != MACH_PORT_NULL) {
        mach_port_destroy(mach_task_self(), ipc->notifyPort);
        ipc->notifyPort = MACH_PORT_NULL;
    }
    if (ipc->connection) {
        IOServiceClose(ipc->connection);
        ipc->connection = 0;
    }
    if (ipc->service) {
        IOObjectRelease(ipc->service);
        ipc->service = 0;
    }
    free(ipc);
    return NULL;
}

void DextIPCScheduleWithRunLoop(DextIPCRef ipc,
                                 CFRunLoopRef rl,
                                 CFStringRef mode)
{
    if (!ipc || !ipc->cfNotifyPort) return;

    ipc->notifySource = CFMachPortCreateRunLoopSource(
        ipc->allocator, ipc->cfNotifyPort, 0);
    if (ipc->notifySource) {
        CFRunLoopAddSource(rl, ipc->notifySource, mode);
    }
}

void DextIPCUnscheduleFromRunLoop(DextIPCRef ipc,
                                   CFRunLoopRef rl,
                                   CFStringRef mode)
{
    if (!ipc) return;

    if (ipc->notifySource) {
        if (rl) {
            CFRunLoopRemoveSource(rl, ipc->notifySource, mode);
        }
        CFRelease(ipc->notifySource);
        ipc->notifySource = NULL;
    }
}

void DextIPCRelease(DextIPCRef ipc)
{
    if (!ipc) return;

    DextIPCUnscheduleFromRunLoop(ipc, NULL, NULL);

    // Close the DEXT communication channel
    dext_execute_scalar(ipc, kDextCmdClose, NULL, 0, NULL, NULL);

    if (ipc->cfNotifyPort) {
        CFMachPortInvalidate(ipc->cfNotifyPort);
        CFRelease(ipc->cfNotifyPort);
        ipc->cfNotifyPort = NULL;
    }

    if (ipc->notifyPort != MACH_PORT_NULL) {
        mach_port_destroy(mach_task_self(), ipc->notifyPort);
        ipc->notifyPort = MACH_PORT_NULL;
    }

    if (ipc->connection) {
        IOServiceClose(ipc->connection);
        ipc->connection = 0;
    }

    if (ipc->service) {
        IOObjectRelease(ipc->service);
        ipc->service = 0;
    }

    free(ipc);
}

#pragma mark - Session Management

errno_t DextIPCRegisterSession(DextIPCRef ipc,
                                DextSessionConfig * config)
{
    if (!ipc || !config) return EINVAL;

    uint64_t scalarInput = config->sessionId;
    kern_return_t kr = dext_execute_command(
        ipc, kDextCmdRegisterSession,
        &scalarInput, 1,
        (const uint8_t *)config, sizeof(*config),
        NULL, NULL, NULL, NULL);

    if (kr != KERN_SUCCESS) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: register session %u failed: 0x%x",
                (unsigned)config->sessionId, kr);
        return EIO;
    }

    asl_log(NULL, NULL, ASL_LEVEL_DEBUG,
            "DextIPC: registered session %u for target '%s'",
            (unsigned)config->sessionId, config->targetName);
    return 0;
}

errno_t DextIPCUnregisterSession(DextIPCRef ipc,
                                  uint8_t sessionId)
{
    if (!ipc) return EINVAL;

    uint64_t scalarInput = sessionId;
    kern_return_t kr = dext_execute_scalar(
        ipc, kDextCmdUnregisterSession,
        &scalarInput, 1, NULL, NULL);

    if (kr != KERN_SUCCESS) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: unregister session %u failed: 0x%x",
                (unsigned)sessionId, kr);
        return EIO;
    }

    return 0;
}

#pragma mark - Connection Management

errno_t DextIPCActivateConnection(DextIPCRef ipc,
                                   DextConnectionConfig * config)
{
    if (!ipc || !config) return EINVAL;

    uint64_t scalarInput = config->sessionId;
    kern_return_t kr = dext_execute_command(
        ipc, kDextCmdActivateConnection,
        &scalarInput, 1,
        (const uint8_t *)config, sizeof(*config),
        NULL, NULL, NULL, NULL);

    if (kr != KERN_SUCCESS) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: activate connection s%u c%u failed: 0x%x",
                (unsigned)config->sessionId,
                (unsigned)config->connectionId, kr);
        return EIO;
    }

    return 0;
}

errno_t DextIPCDeactivateConnection(DextIPCRef ipc,
                                     uint8_t sessionId,
                                     uint32_t connectionId)
{
    if (!ipc) return EINVAL;

    uint64_t scalars[2] = { sessionId, connectionId };
    kern_return_t kr = dext_execute_scalar(
        ipc, kDextCmdDeactivateConnection,
        scalars, 2, NULL, NULL);

    if (kr != KERN_SUCCESS) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: deactivate connection s%u c%u failed: 0x%x",
                (unsigned)sessionId, (unsigned)connectionId, kr);
        return EIO;
    }

    return 0;
}

#pragma mark - Parameter Management

errno_t DextIPCSetSessionParam(DextIPCRef ipc,
                                uint8_t sessionId,
                                enum DextSessionParam param,
                                uint64_t value)
{
    if (!ipc) return EINVAL;

    uint64_t scalars[3] = { sessionId, (uint64_t)param, value };
    kern_return_t kr = dext_execute_scalar(
        ipc, kDextCmdSetSessionParam,
        scalars, 3, NULL, NULL);

    if (kr != KERN_SUCCESS) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: set session param s%u p%llu failed: 0x%x",
                (unsigned)sessionId, (unsigned long long)param, kr);
        return EIO;
    }

    return 0;
}

errno_t DextIPCSetConnectionParam(DextIPCRef ipc,
                                   uint8_t sessionId,
                                   uint32_t connId,
                                   enum DextConnectionParam param,
                                   uint64_t value)
{
    if (!ipc) return EINVAL;

    uint64_t scalars[4] = { sessionId, connId, (uint64_t)param, value };
    kern_return_t kr = dext_execute_scalar(
        ipc, kDextCmdSetConnectionParam,
        scalars, 4, NULL, NULL);

    if (kr != KERN_SUCCESS) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: set connection param s%u c%u p%llu failed: 0x%x",
                (unsigned)sessionId, (unsigned)connId,
                (unsigned long long)param, kr);
        return EIO;
    }

    return 0;
}

#pragma mark - Data Path

errno_t DextIPCForwardIncomingPDU(DextIPCRef ipc,
                                   DextIncomingPDU * pdu,
                                   const uint8_t * data)
{
    if (!ipc || !pdu) return EINVAL;

    // Pack scalar inputs and struct input
    uint64_t scalars[2] = { pdu->sessionId, pdu->connectionId };

    kern_return_t kr = dext_execute_command(
        ipc, kDextCmdIncomingPDU,
        scalars, 2,
        (const uint8_t *)pdu, sizeof(*pdu),
        NULL, NULL, NULL, NULL);

    if (kr != KERN_SUCCESS) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: forward incoming PDU s%u c%u failed: 0x%x",
                (unsigned)pdu->sessionId,
                (unsigned)pdu->connectionId, kr);
        return EIO;
    }

    return 0;
}

errno_t DextIPCConnectionStatus(DextIPCRef ipc,
                                 DextConnectionStatus * status)
{
    if (!ipc || !status) return EINVAL;

    uint64_t scalarInput = status->sessionId;
    kern_return_t kr = dext_execute_command(
        ipc, kDextCmdConnectionStatus,
        &scalarInput, 1,
        (const uint8_t *)status, sizeof(*status),
        NULL, NULL, NULL, NULL);

    if (kr != KERN_SUCCESS) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: connection status s%u failed: 0x%x",
                (unsigned)status->sessionId, kr);
        return EIO;
    }

    return 0;
}

errno_t DextIPCAsyncMessage(DextIPCRef ipc,
                             uint8_t sessionId,
                             uint32_t connectionId,
                             uint8_t asyncData[kIPC_ISCSI_BHS_SIZE])
{
    if (!ipc || !asyncData) return EINVAL;

    uint64_t scalars[2] = { sessionId, connectionId };

    kern_return_t kr = dext_execute_command(
        ipc, kDextCmdAsyncMessage,
        scalars, 2,
        asyncData, kIPC_ISCSI_BHS_SIZE,
        NULL, NULL, NULL, NULL);

    if (kr != KERN_SUCCESS) {
        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                "DextIPC: async message s%u c%u failed: 0x%x",
                (unsigned)sessionId, (unsigned)connectionId, kr);
        return EIO;
    }

    return 0;
}

#pragma mark - Queue Access

bool DextIPCReadNextOutgoingPDU(DextIPCRef ipc,
                                 DextOutgoingPDUFull * pduFull)
{
    if (!ipc || !pduFull) return false;

    size_t outputSize = sizeof(DextOutgoingPDUFull);

    kern_return_t kr = IOConnectCallMethod(
        ipc->connection,
        kDextCmdGetNextPDU,
        NULL, 0,       // no scalar inputs
        NULL, 0,       // no struct inputs
        NULL, NULL,    // no scalar outputs
        pduFull, &outputSize);

    if (kr != KERN_SUCCESS) {
        return false;
    }

    // If DEXT returned a PDU with no meaningful data, treat as empty queue
    if (outputSize < sizeof(DextOutgoingPDUFull)) {
        return false;
    }

    return true;
}

bool DextIPCReadOutgoingPDU(DextIPCRef ipc,
                             DextOutgoingPDU * pdu)
{
    if (!ipc || !pdu) return false;

    // Don't overwrite if previous PDU data is still pending consumption
    if (ipc->hasPendingPDU) return false;

    // Poll the DEXT for the next outgoing PDU via GetNextPDU
    size_t outputSize = sizeof(DextOutgoingPDUFull);

    kern_return_t kr = IOConnectCallMethod(
        ipc->connection,
        kDextCmdGetNextPDU,
        NULL, 0,
        NULL, 0,
        NULL, NULL,
        &ipc->lastPDU, &outputSize);

    if (kr != KERN_SUCCESS || outputSize < sizeof(DextOutgoingPDUFull)) {
        return false;
    }

    // Cache the full PDU for data access via DextIPCCopyOutgoingData
    ipc->hasPendingPDU = true;

    // Populate the DextOutgoingPDU descriptor from the cached full PDU
    pdu->sessionId = ipc->lastPDU.sessionId;
    pdu->connectionId = ipc->lastPDU.connectionId;
    pdu->initiatorTaskTag = ipc->lastPDU.initiatorTaskTag;
    memcpy(pdu->bhs, ipc->lastPDU.bhs, kIPC_ISCSI_BHS_SIZE);
    pdu->dataLength = ipc->lastPDU.dataLength;
    pdu->dataBufferOffset = 0; // Data is inline in polling model

    return true;
}

errno_t DextIPCCopyOutgoingData(DextIPCRef ipc,
                                 DextOutgoingPDU * pdu,
                                 uint8_t * buf,
                                 size_t * len)
{
    if (!ipc || !pdu || !buf || !len) return EINVAL;

    if (!ipc->hasPendingPDU || ipc->lastPDU.dataLength == 0) {
        *len = 0;
        return 0;
    }

    size_t toCopy = (*len < ipc->lastPDU.dataLength) ?
                    *len : ipc->lastPDU.dataLength;
    memcpy(buf, ipc->lastPDU.dataSegment, toCopy);
    *len = toCopy;

    // Clear pending flag once data has been consumed
    if (toCopy >= ipc->lastPDU.dataLength) {
        ipc->hasPendingPDU = false;
    }

    return 0;
}
