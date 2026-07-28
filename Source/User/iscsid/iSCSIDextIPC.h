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
 * iSCSIDextIPC.h — Daemon-side IPC client for communicating with the iSCSI DEXT.
 *
 * Provides a clean API for the daemon to open/close a connection to the DEXT
 * and send commands (register session, activate connection, forward PDU data,
 * etc.) via IOKit IOConnectCallMethod / ExternalMethod.
 *
 * Data path PDUs (SCSI command/data) are expected to use shared memory
 * ring buffers (IODataQueue) for bulk transfer; this module provides setup
 * for those buffers and the calls to read/write them.
 */

#ifndef __ISCSI_DEXT_IPC_H__
#define __ISCSI_DEXT_IPC_H__

#include <stdint.h>
#include <stdbool.h>
#include <CoreFoundation/CoreFoundation.h>

#include "../../Shared/DextDaemonIPC/iSCSIDextDaemonIPCShared.h"

#pragma mark - Opaque Types

/*! Opaque handle to the daemon↔DEXT IPC channel. */
typedef struct DextIPC * DextIPCRef;

/*!
 * @brief Callback invoked when the DEXT has a PDU ready for TCP transmission.
 *
 * Called from the run loop when the DEXT notifies the daemon of pending data.
 * The full PDU (BHS + inline data segment) is passed directly, ready for
 * TCP send(). The daemon should send the PDU via TCPEngineSend.
 *
 * @param ipc     The IPC handle.
 * @param pduFull The complete PDU including BHS and inline data segment.
 * @param context Opaque user context.
 */
typedef void (*DextOutgoingPDUCallback)(DextIPCRef ipc,
                                         const DextOutgoingPDUFull * pduFull,
                                         void * context);

/*!
 * @brief Callback invoked when the DEXT reports a connection event.
 */
typedef void (*DextConnectionEventCallback)(DextIPCRef ipc,
                                             DextConnectionStatus * status,
                                             void * context);

/*!
 * @brief Callbacks registered by the daemon for DEXT→daemon events.
 *
 * @field outgoingPDU     Called when DEXT has a PDU ready for TCP send.
 *                        Receives the full PDU (BHS + inline data segment).
 * @field connectionEvent Called when DEXT reports a connection status change.
 * @field context         Opaque user context passed to all callbacks.
 */
typedef struct {
    DextOutgoingPDUCallback       outgoingPDU;
    DextConnectionEventCallback   connectionEvent;
    void *                        context;
} DextIPCCallbacks;

#pragma mark - Lifecycle

/*!
 * @brief Create and open the IPC connection to the DEXT.
 *
 * Finds the DEXT service in the IORegistry, opens a user client
 * connection, sets up the notification port and run loop source.
 *
 * @param allocator  CFAllocator (pass NULL for default).
 * @param callbacks  Callbacks for DEXT-initiated events.
 * @return A DextIPCRef on success, or NULL on error.
 */
DextIPCRef DextIPCCreate(CFAllocatorRef allocator,
                          DextIPCCallbacks callbacks);

/*!
 * @brief Schedule the DEXT notification source on a run loop.
 * @param ipc   The IPC handle.
 * @param rl    The run loop to schedule on.
 * @param mode  The run loop mode.
 */
void DextIPCScheduleWithRunLoop(DextIPCRef ipc,
                                 CFRunLoopRef rl,
                                 CFStringRef mode);

/*!
 * @brief Remove the DEXT notification source from the run loop.
 * @param ipc   The IPC handle.
 * @param rl    The run loop.
 * @param mode  The run loop mode.
 */
void DextIPCUnscheduleFromRunLoop(DextIPCRef ipc,
                                   CFRunLoopRef rl,
                                   CFStringRef mode);

/*!
 * @brief Close the IPC connection and release all resources.
 * @param ipc  The IPC handle (may be NULL).
 */
void DextIPCRelease(DextIPCRef ipc);

#pragma mark - Session Management

/*!
 * @brief Register a new session with the DEXT after login negotiation.
 * @param ipc     The IPC handle.
 * @param config  Session configuration (negotiated parameters).
 * @return 0 on success, or an errno value.
 */
errno_t DextIPCRegisterSession(DextIPCRef ipc,
                                DextSessionConfig * config);

/*!
 * @brief Unregister a session from the DEXT.
 * @param ipc        The IPC handle.
 * @param sessionId  The session to remove.
 * @return 0 on success, or an errno value.
 */
errno_t DextIPCUnregisterSession(DextIPCRef ipc,
                                  uint8_t sessionId);

#pragma mark - Connection Management

/*!
 * @brief Activate a connection for full-feature phase iSCSI I/O.
 *
 * After this call, the DEXT will accept SCSI tasks from the macOS stack
 * and route SCSI command PDUs through the daemon for TCP transmission.
 *
 * @param ipc     The IPC handle.
 * @param config  Connection configuration (negotiated parameters).
 * @return 0 on success, or an errno value.
 */
errno_t DextIPCActivateConnection(DextIPCRef ipc,
                                   DextConnectionConfig * config);

/*!
 * @brief Deactivate a previously activated connection.
 * @param ipc           The IPC handle.
 * @param sessionId     The session containing the connection.
 * @param connectionId  The connection to deactivate.
 * @return 0 on success, or an errno value.
 */
errno_t DextIPCDeactivateConnection(DextIPCRef ipc,
                                     uint8_t sessionId,
                                     uint32_t connectionId);

#pragma mark - Parameter Management

/*!
 * @brief Set a session parameter after negotiation.
 * @param ipc        The IPC handle.
 * @param sessionId  Target session.
 * @param param      The parameter selector.
 * @param value      Parameter value.
 * @return 0 on success, or an errno value.
 */
errno_t DextIPCSetSessionParam(DextIPCRef ipc,
                                uint8_t sessionId,
                                enum DextSessionParam param,
                                uint64_t value);

/*!
 * @brief Set a connection parameter after negotiation.
 * @param ipc        The IPC handle.
 * @param sessionId  Target session.
 * @param connId     Target connection.
 * @param param      The parameter selector.
 * @param value      Parameter value.
 * @return 0 on success, or an errno value.
 */
errno_t DextIPCSetConnectionParam(DextIPCRef ipc,
                                   uint8_t sessionId,
                                   uint32_t connId,
                                   enum DextConnectionParam param,
                                   uint64_t value);

#pragma mark - Data Path

/*!
 * @brief Forward a received PDU (BHS + data) from TCP to the DEXT.
 *
 * The daemon should call this after receiving a PDU from the target
 * via TCP recv(). The data is written to shared memory and the DEXT
 * is notified via ExternalMethod.
 *
 * @param ipc     The IPC handle.
 * @param pdu     The incoming PDU descriptor (BHS, data buffer info).
 * @param data    Pointer to the data segment (may be NULL if dataLen==0).
 * @return 0 on success, or an errno value.
 */
errno_t DextIPCForwardIncomingPDU(DextIPCRef ipc,
                                   DextIncomingPDU * pdu,
                                   const uint8_t * data);

/*!
 * @brief Notify the DEXT of a TCP connection status change.
 * @param ipc     The IPC handle.
 * @param status  Status descriptor.
 * @return 0 on success, or an errno value.
 */
errno_t DextIPCConnectionStatus(DextIPCRef ipc,
                                 DextConnectionStatus * status);

/*!
 * @brief Forward an ASYNC message from the target to the DEXT.
 * @param ipc         The IPC handle.
 * @param sessionId   The session.
 * @param connectionId  The connection.
 * @param asyncData   ASYNC message data (BHS-sized).
 * @return 0 on success, or an errno value.
 */
errno_t DextIPCAsyncMessage(DextIPCRef ipc,
                             uint8_t sessionId,
                             uint32_t connectionId,
                             uint8_t asyncData[kIPC_ISCSI_BHS_SIZE]);

#pragma mark - Queue Access

/*!
 * @brief Poll for the next outgoing PDU via kDextCmdGetNextPDU (polling model).
 *
 * Unlike DextIPCReadOutgoingPDU which uses the shared memory data queue, this
 * function directly calls the DEXT's ExternalMethod to retrieve the next queued
 * PDU for TCP transmission. The DEXT returns a DextOutgoingPDUFull which includes
 * the data segment inline.
 *
 * @param ipc     The IPC handle.
 * @param pduFull [out] Receives the full PDU including inline data segment.
 * @return true if a PDU was available, false if queue is empty or error.
 */
bool DextIPCReadNextOutgoingPDU(DextIPCRef ipc,
                                 DextOutgoingPDUFull * pduFull);

/*!
 * @brief Read the next outgoing PDU from the shared memory data queue.
 *
 * Called from the DEXT→daemon data queue callback. Returns true and
 * fills in @p pdu if a PDU was available, or false if the queue is empty.
 *
 * @param ipc  The IPC handle.
 * @param pdu  [out] Receives the PDU descriptor.
 * @return true if a PDU was read, false if empty.
 */
bool DextIPCReadOutgoingPDU(DextIPCRef ipc,
                             DextOutgoingPDU * pdu);

/*!
 * @brief Copy data segment data from the shared memory buffer.
 *
 * After reading a DextOutgoingPDU, call this to get the actual data
 * payload (if pdu->dataLength > 0).
 *
 * @param ipc    The IPC handle.
 * @param pdu    The PDU descriptor (from DextIPCReadOutgoingPDU).
 * @param buf    [out] Buffer to receive the data.
 * @param len    [in/out] On input, max bytes to read. On output, actual bytes.
 * @return 0 on success, or an errno value.
 */
errno_t DextIPCCopyOutgoingData(DextIPCRef ipc,
                                 DextOutgoingPDU * pdu,
                                 uint8_t * buf,
                                 size_t * len);

#endif /* __ISCSI_DEXT_IPC_H__ */
