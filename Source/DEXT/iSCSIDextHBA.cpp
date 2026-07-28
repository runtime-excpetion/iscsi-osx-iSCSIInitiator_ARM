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
 * iSCSIDextHBA.cpp — _Impl method implementations for the iSCSI DEXT HBA.
 *
 * This file implements the _Impl methods declared by the IIG compiler.
 * The IIG-generated dispatch code is included at the start of this file
 * and provides Dispatch(), _Dispatch(), and class registration.
 *
 * NOTE: Do NOT use OSDefineMetaClassAndStructors here — that is handled by
 * the IIG-generated code included below.
 */

#include "../Shared/DextDaemonIPC/iSCSIDextDaemonIPCShared.h"

// DriverKit framework includes needed for _Impl implementations
#include <DriverKit/IOKitKeys.h>
#include <DriverKit/OSNumber.h>
#include <string.h>

// Include the IIG-generated dispatch code (includes iSCSIDextHBA.h and DriverKit.h)
#include "iSCSIDextHBA_impl_gen.cpp"

// Internal shared state (session, connection, pending tasks, outgoing queue)
#include "iSCSIDextInternal.h"

#pragma mark - Storage Definitions (extern in iSCSIDextInternal.h)

DextSessionState      s_sessions[kMaxTargets];
DextConnectionState   s_connections[kMaxTargets][kIPC_MAX_CONNECTIONS];
DextPendingTask       s_pendingTasks[kMaxPendingTasks];
DextOutgoingPDUEntry  s_outgoingPDUQueue[kMaxOutgoingPDUs];
uint32_t              s_nextTaskTag = kTaskTagBase;

#pragma mark - Controller State (internal to this file)

static bool s_controllerInitialized = false;
static bool s_controllerStarted = false;

#pragma mark - Helper Function Implementations

void initSessionState(DextSessionState * session)
{
    session->active = false;
    session->sessionId = 0;
    session->targetId = 0;
    session->cmdSN = 0;
    session->expCmdSN = 0;
    session->maxCmdSN = 0;
    session->maxBurstLength = 262144;
    session->firstBurstLength = 65536;
    session->maxRecvDataSegmentLength = 8192;
    session->maxSendDataSegmentLength = 8192;
    session->immediateData = true;
    session->initialR2T = false;
    session->dataPDUInOrder = true;
    session->dataSequenceInOrder = true;
    session->maxOutstandingR2T = 1;
}

void initConnectionState(DextConnectionState * conn)
{
    conn->active = false;
    conn->connectionId = 0;
    conn->sessionId = 0;
    conn->expStatSN = 0;
    conn->useHeaderDigest = false;
    conn->useDataDigest = false;
}

DextSessionState * getSession(uint8_t targetId)
{
    if (targetId >= kMaxTargets) {
        return NULL;
    }
    return &s_sessions[targetId];
}

DextConnectionState * getConnection(uint8_t targetId, uint32_t connectionId)
{
    if (targetId >= kMaxTargets || connectionId >= kIPC_MAX_CONNECTIONS) {
        return NULL;
    }
    return &s_connections[targetId][connectionId];
}

#pragma mark - Pending Task Management

int dext_add_pending_task(uint8_t targetId, uint64_t LUN,
                           const uint8_t cdb[16], uint8_t taskAttr,
                           bool isRead, uint32_t expectedLength,
                           OSAction * completion)
{
    for (int i = 0; i < kMaxPendingTasks; i++) {
        if (!s_pendingTasks[i].active) {
            s_pendingTasks[i].active = true;
            s_pendingTasks[i].targetId = targetId;
            s_pendingTasks[i].LUN = LUN;
            s_pendingTasks[i].initiatorTaskTag = dext_get_next_task_tag();
            s_pendingTasks[i].controllerTaskID = 0;
            memcpy(s_pendingTasks[i].cdb, cdb, 16);
            s_pendingTasks[i].taskAttr = taskAttr;
            s_pendingTasks[i].isRead = isRead;
            s_pendingTasks[i].expectedDataTransferLength = expectedLength;
            s_pendingTasks[i].transferOffset = 0;
            s_pendingTasks[i].dataBuffer = NULL;
            s_pendingTasks[i].completionAction = completion;
            if (completion) completion->retain();
            s_pendingTasks[i].startTime = 0;
            return i;
        }
    }
    return -1; // no free slot
}

void dext_remove_pending_task(uint32_t initiatorTaskTag)
{
    for (int i = 0; i < kMaxPendingTasks; i++) {
        if (s_pendingTasks[i].active &&
            s_pendingTasks[i].initiatorTaskTag == initiatorTaskTag) {
            if (s_pendingTasks[i].completionAction) {
                s_pendingTasks[i].completionAction->release();
                s_pendingTasks[i].completionAction = NULL;
            }
            if (s_pendingTasks[i].dataBuffer) {
                s_pendingTasks[i].dataBuffer->release();
                s_pendingTasks[i].dataBuffer = NULL;
            }
            memset(&s_pendingTasks[i], 0, sizeof(DextPendingTask));
            break;
        }
    }
}

DextPendingTask * dext_get_pending_task(uint32_t initiatorTaskTag)
{
    for (int i = 0; i < kMaxPendingTasks; i++) {
        if (s_pendingTasks[i].active &&
            s_pendingTasks[i].initiatorTaskTag == initiatorTaskTag) {
            return &s_pendingTasks[i];
        }
    }
    return NULL;
}

uint32_t dext_get_next_task_tag(void)
{
    uint32_t tag = s_nextTaskTag;
    s_nextTaskTag++;
    // Avoid reserved tags (0x00000000 and 0xFFFFFFFF)
    if (s_nextTaskTag == 0 || s_nextTaskTag == 0xFFFFFFFF) {
        s_nextTaskTag = kTaskTagBase;
    }
    return tag;
}

#pragma mark - Outgoing PDU Queue Management

bool dext_enqueue_outgoing_pdu(uint8_t sessionId, uint32_t connectionId,
                                uint32_t initiatorTaskTag,
                                const uint8_t bhs[48],
                                const uint8_t * data, uint32_t dataLength)
{
    for (int i = 0; i < kMaxOutgoingPDUs; i++) {
        if (!s_outgoingPDUQueue[i].active) {
            s_outgoingPDUQueue[i].active = true;
            s_outgoingPDUQueue[i].sessionId = sessionId;
            s_outgoingPDUQueue[i].connectionId = connectionId;
            s_outgoingPDUQueue[i].initiatorTaskTag = initiatorTaskTag;
            memcpy(s_outgoingPDUQueue[i].bhs, bhs, 48);
            s_outgoingPDUQueue[i].dataLength = 0;
            s_outgoingPDUQueue[i].data = NULL;

            // Copy data segment if present
            if (data && dataLength > 0) {
                uint32_t copyLen = (dataLength <= kIPC_MAX_DATA_SEGMENT_SIZE)
                                    ? dataLength : kIPC_MAX_DATA_SEGMENT_SIZE;
                s_outgoingPDUQueue[i].data = new uint8_t[copyLen];
                if (s_outgoingPDUQueue[i].data) {
                    memcpy(s_outgoingPDUQueue[i].data, data, copyLen);
                    s_outgoingPDUQueue[i].dataLength = copyLen;
                }
            }
            return true;
        }
    }
    return false; // queue full
}

bool dext_dequeue_outgoing_pdu(DextOutgoingPDUEntry * entry)
{
    for (int i = 0; i < kMaxOutgoingPDUs; i++) {
        if (s_outgoingPDUQueue[i].active) {
            // Transfer ownership of data pointer to caller
            memcpy(entry, &s_outgoingPDUQueue[i], sizeof(DextOutgoingPDUEntry));
            s_outgoingPDUQueue[i].active = false;
            s_outgoingPDUQueue[i].data = NULL;
            s_outgoingPDUQueue[i].dataLength = 0;
            return true;
        }
    }
    return false; // queue empty
}

void dext_clear_outgoing_pdu_entry(DextOutgoingPDUEntry * entry)
{
    if (!entry) return;
    if (entry->data) {
        delete[] entry->data;
        entry->data = NULL;
    }
    memset(entry, 0, sizeof(DextOutgoingPDUEntry));
}

void dext_free_outgoing_pdu_queue(void)
{
    for (int i = 0; i < kMaxOutgoingPDUs; i++) {
        dext_clear_outgoing_pdu_entry(&s_outgoingPDUQueue[i]);
    }
}

uint32_t dext_outgoing_pdu_count(void)
{
    uint32_t count = 0;
    for (int i = 0; i < kMaxOutgoingPDUs; i++) {
        if (s_outgoingPDUQueue[i].active) count++;
    }
    return count;
}

bool dext_is_target_ready(uint8_t targetId)
{
    if (targetId >= kMaxTargets) return false;
    return s_sessions[targetId].active;
}

bool dext_is_connection_active(uint8_t targetId, uint32_t connectionId)
{
    if (targetId >= kMaxTargets || connectionId >= kIPC_MAX_CONNECTIONS) return false;
    return s_connections[targetId][connectionId].active;
}

#pragma mark - IOService Overrides (VirtualMethods)

bool iSCSIDextHBA::init(void)
{
    if (!super::init()) {
        return false;
    }

    // Initialize all state tables
    for (uint32_t i = 0; i < kMaxTargets; i++) {
        initSessionState(&s_sessions[i]);
        for (uint32_t j = 0; j < kIPC_MAX_CONNECTIONS; j++) {
            initConnectionState(&s_connections[i][j]);
        }
    }
    memset(s_pendingTasks, 0, sizeof(s_pendingTasks));
    dext_free_outgoing_pdu_queue();

    return true;
}

void iSCSIDextHBA::free(void)
{
    super::free();
}

#pragma mark - Controller Lifecycle (_Impl methods)

kern_return_t iSCSIDextHBA::Start_Impl(IOService * provider)
{
    kern_return_t ret;

    ret = super::Start(provider);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    RegisterService();

    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::Stop_Impl(IOService * provider)
{
    return super::Stop(provider);
}

#pragma mark - User Client Creation

kern_return_t iSCSIDextHBA::NewUserClient_Impl(uint32_t type, IOUserClient ** userClient)
{
    (void)type;
    kern_return_t ret;
    ret = Create(this, "iSCSIDextUserClient", (IOService**)userClient);
    return ret;
}

#pragma mark - Controller Lifecycle

kern_return_t iSCSIDextHBA::UserInitializeController_Impl()
{
    s_controllerInitialized = true;
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserStartController_Impl()
{
    s_controllerStarted = true;
    return kIOReturnSuccess;
}

#pragma mark - Controller Properties & Constraints

kern_return_t iSCSIDextHBA::UserReportHBAHighestLogicalUnitNumber_Impl(uint64_t * value)
{
    if (!value) {
        return kIOReturnBadArgument;
    }
    *value = 63;
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserDoesHBASupportSCSIParallelFeature_Impl(uint32_t theValue, bool * result)
{
    (void)theValue;
    if (!result) {
        return kIOReturnBadArgument;
    }
    // iSCSI is not parallel SCSI — no SPI features supported
    *result = false;
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserReportHBAConstraints_Impl(OSDictionary * constraints)
{
    if (!constraints) {
        return kIOReturnBadArgument;
    }

    OSNumber * num;

    num = OSNumber::withNumber((uint64_t)512, 32);
    if (num) {
        constraints->setObject(kIOMaximumSegmentCountReadKey, num);
        num->release();
    }

    num = OSNumber::withNumber((uint64_t)512, 32);
    if (num) {
        constraints->setObject(kIOMaximumSegmentCountWriteKey, num);
        num->release();
    }

    num = OSNumber::withNumber((uint64_t)2048, 32);
    if (num) {
        constraints->setObject(kIOMaximumSegmentByteCountReadKey, num);
        num->release();
    }

    num = OSNumber::withNumber((uint64_t)2048, 32);
    if (num) {
        constraints->setObject(kIOMaximumSegmentByteCountWriteKey, num);
        num->release();
    }

    // 1-byte alignment minimum
    num = OSNumber::withNumber((uint64_t)1, 32);
    if (num) {
        constraints->setObject(kIOMinimumSegmentAlignmentByteCountKey, num);
        num->release();
    }

    // 64-bit addressable
    num = OSNumber::withNumber((uint64_t)64, 32);
    if (num) {
        constraints->setObject(kIOMaximumSegmentAddressableBitCountKey, num);
        num->release();
    }

    // No special HBA data alignment
    num = OSNumber::withNumber((uint64_t)1, 32);
    if (num) {
        constraints->setObject(kIOMinimumHBADataAlignmentMaskKey, num);
        num->release();
    }

    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserGetDMASpecification_Impl(uint64_t * maxTransferSize,
                                                           uint32_t * alignment,
                                                           uint8_t * numAddressBits,
                                                           DMAOutputSegmentType * segmentType)
{
    if (!maxTransferSize || !alignment || !numAddressBits || !segmentType) {
        return kIOReturnBadArgument;
    }

    *maxTransferSize  = 0x20000000; // 512MB
    *alignment        = 1;          // No alignment requirement
    *numAddressBits   = 64;         // 64-bit address space
    *segmentType      = kDMAOutputSegmentHost64;

    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserMapHBAData_Impl(uint32_t * uniqueTaskID)
{
    if (!uniqueTaskID) {
        return kIOReturnBadArgument;
    }

    static uint32_t s_nextTaskID = 1;
    *uniqueTaskID = s_nextTaskID++;

    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserDoesHBAPerformAutoSense_Impl(bool * result)
{
    if (!result) {
        return kIOReturnBadArgument;
    }
    *result = false;
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserDoesHBASupportMultiPathing_Impl(bool * result)
{
    if (!result) {
        return kIOReturnBadArgument;
    }
    *result = true;
    return kIOReturnSuccess;
}

#pragma mark - Target Management

kern_return_t iSCSIDextHBA::UserCreateTargetForID_Impl(SCSIDeviceIdentifier targetID,
                                                         OSDictionary * targetDict)
{
    (void)targetDict;
    if (targetID >= kMaxTargets) {
        return kIOReturnNoSpace;
    }
    if (!s_sessions[targetID].active) {
        return kIOReturnNotReady;
    }
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserDestroyTargetForID_Impl(SCSITargetIdentifier targetID)
{
    if (targetID < kMaxTargets) {
        initSessionState(&s_sessions[targetID]);
        for (uint32_t j = 0; j < kIPC_MAX_CONNECTIONS; j++) {
            initConnectionState(&s_connections[targetID][j]);
        }
    }
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserInitializeTargetForID_Impl(SCSITargetIdentifier targetID)
{
    if (targetID >= kMaxTargets || !s_sessions[targetID].active) {
        return kIOReturnBadArgument;
    }
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserTargetPresentForID_Impl(SCSIDeviceIdentifier targetID,
                                                          bool * result)
{
    if (!result) {
        return kIOReturnBadArgument;
    }
    if (targetID < kMaxTargets) {
        *result = s_sessions[targetID].active;
    } else {
        *result = false;
    }
    return kIOReturnSuccess;
}

#pragma mark - Initiator / Device Identity

kern_return_t iSCSIDextHBA::UserReportInitiatorIdentifier_Impl(uint64_t * id)
{
    if (!id) {
        return kIOReturnBadArgument;
    }
    *id = 0; // SCSI initiator ID
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserReportHighestSupportedDeviceID_Impl(uint64_t * id)
{
    if (!id) {
        return kIOReturnBadArgument;
    }
    *id = kMaxTargets - 1;
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserReportMaximumTaskCount_Impl(uint32_t * count)
{
    if (!count) {
        return kIOReturnBadArgument;
    }
    *count = 256;
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserDoesHBAPerformDeviceManagement_Impl(bool * result)
{
    if (!result) {
        return kIOReturnBadArgument;
    }
    *result = false;
    return kIOReturnSuccess;
}

#pragma mark - SCSI Task Processing

kern_return_t iSCSIDextHBA::UserProcessParallelTask_Impl(SCSIUserParallelTask parallelRequest,
                                                           uint32_t * response,
                                                           OSAction * completion)
{
    if (!response) {
        return kIOReturnBadArgument;
    }

    uint8_t targetId = (uint8_t)parallelRequest.fTargetID;

    // Validate that target/session is active
    if (!dext_is_target_ready(targetId)) {
        *response = kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE;
        return kIOReturnNotReady;
    }

    DextSessionState * session = getSession(targetId);
    if (!session || !session->active) {
        *response = kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE;
        return kIOReturnNotReady;
    }

    // Extract parameters from the parallel request
    uint64_t LUN = 0;
    memcpy(&LUN, parallelRequest.fLogicalUnitBytes, sizeof(uint64_t));

    uint8_t cdb[16];
    memcpy(cdb, parallelRequest.fCommandDescriptorBlock,
           (parallelRequest.fCommandSize <= 16) ? parallelRequest.fCommandSize : 16);

    bool isRead = (parallelRequest.fTransferDirection == kSCSIDataTransfer_FromTargetToInitiator);
    uint64_t transferLength = parallelRequest.fRequestedTransferCount;
    uint32_t dataLength = (uint32_t)(transferLength > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : transferLength);

    // Task attribute (simple ordered/untagged)
    uint8_t taskAttr;
    switch (parallelRequest.fTaskAttribute) {
        case kSCSITask_ORDERED:   taskAttr = 1; break;
        case kSCSITask_HEAD_OF_QUEUE: taskAttr = 2; break;
        case kSCSITask_ACA:       taskAttr = 3; break;
        default:                           taskAttr = 0; break; // UNTAGGED / SIMPLE
    }

    // Add pending task — retains the completion action
    int taskIdx = dext_add_pending_task(targetId, LUN, cdb, taskAttr,
                                        isRead, dataLength, completion);
    if (taskIdx < 0) {
        *response = kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE;
        return kIOReturnNoResources;
    }

    // Store the controller task identifier for later matching
    s_pendingTasks[taskIdx].controllerTaskID = parallelRequest.fControllerTaskIdentifier;

    // Build the iSCSI SCSI Command PDU BHS
    uint8_t bhs[48];
    pdu_build_scsi_cmd(bhs, LUN,
                       s_pendingTasks[taskIdx].initiatorTaskTag,
                       dataLength, isRead,
                       session->cmdSN, session->expCmdSN,
                       cdb, taskAttr,
                       false); // immediateData = false for now

    // Update session sequence numbers
    session->cmdSN++;

    // Enqueue the SCSI Command PDU for transmission by the daemon
    // For write commands, get the data buffer for eventual Data-Out PDUs
    if (!isRead && dataLength > 0) {
        // Try to obtain the data buffer for write operations
        IOBufferMemoryDescriptor * dataBuffer = NULL;
        UserGetDataBuffer(targetId, parallelRequest.fControllerTaskIdentifier, &dataBuffer);
        if (dataBuffer) {
            s_pendingTasks[taskIdx].dataBuffer = dataBuffer;
            // Buffer is retained by the task, release our temporary reference
            dataBuffer->release();
        }
    }

    // Enqueue the PDU (no inline data — SCSI Cmd PDU has no data segment)
    dext_enqueue_outgoing_pdu(session->sessionId, 0,
                               s_pendingTasks[taskIdx].initiatorTaskTag,
                               bhs, NULL, 0);

    // Tell the SCSI framework we'll complete asynchronously
    *response = kSCSIServiceResponse_Request_In_Process;
    return kIOReturnSuccess;
}

#pragma mark - Task Completion

void iSCSIDextHBA::ParallelTaskCompletion_Impl(OSAction * action,
                                                 SCSIUserParallelResponse response)
{
    // The framework provides the actual implementation for ParallelTaskCompletion
    // via the IOUserSCSIParallelInterfaceController base class.
    // We call the non-virtual _Methods wrapper which sends the RPC to the kernel.
    super::ParallelTaskCompletion(action, response);
}

#pragma mark - Task Management Functions

kern_return_t iSCSIDextHBA::UserAbortTaskRequest_Impl(uint64_t theT,
                                                        uint64_t theL,
                                                        uint64_t theQ,
                                                        uint32_t * response)
{
    if (!response) {
        return kIOReturnBadArgument;
    }

    uint8_t targetId = (uint8_t)theT;
    uint64_t LUN = theL;
    uint64_t controllerTaskID = theQ;

    // Search pending tasks for this controller task ID
    for (int i = 0; i < kMaxPendingTasks; i++) {
        DextPendingTask * task = &s_pendingTasks[i];
        if (task->active &&
            task->targetId == targetId &&
            task->controllerTaskID == controllerTaskID &&
            task->completionAction) {

            // Build failure response
            SCSIUserParallelResponse scsiResponse;
            memset(&scsiResponse, 0, sizeof(scsiResponse));
            scsiResponse.version = kScsiUserParallelTaskResponseCurrentVersion1;
            scsiResponse.fControllerTaskIdentifier = controllerTaskID;
            scsiResponse.fTargetID = targetId;
            scsiResponse.fServiceResponse =
                kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE;
            scsiResponse.fCompletionStatus = kSCSITaskStatus_GOOD;
            scsiResponse.fBytesTransferred = 0;

            // Complete the SCSI task directly (this IS the iSCSIDextHBA)
            this->ParallelTaskCompletion(task->completionAction, scsiResponse);

            // Release the completion action reference
            task->completionAction->release();
            task->completionAction = NULL;

            // Clean up the pending task entry
            if (task->dataBuffer) {
                task->dataBuffer->release();
                task->dataBuffer = NULL;
            }
            memset(task, 0, sizeof(DextPendingTask));

            *response = kSCSIServiceResponse_FUNCTION_COMPLETE;
            return kIOReturnSuccess;
        }
    }

    // Task not found — may already have completed
    *response = kSCSIServiceResponse_FUNCTION_COMPLETE;
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserAbortTaskSetRequest_Impl(uint64_t theT,
                                                           uint64_t theL,
                                                           uint32_t * response)
{
    if (!response) {
        return kIOReturnBadArgument;
    }

    uint8_t targetId = (uint8_t)theT;
    uint64_t LUN = theL;

    // Abort all pending tasks for this target+LUN

    for (int i = 0; i < kMaxPendingTasks; i++) {
        DextPendingTask * task = &s_pendingTasks[i];
        if (task->active &&
            task->targetId == targetId &&
            task->LUN == LUN &&
            task->completionAction) {

            SCSIUserParallelResponse scsiResponse;
            memset(&scsiResponse, 0, sizeof(scsiResponse));
            scsiResponse.version = kScsiUserParallelTaskResponseCurrentVersion1;
            scsiResponse.fControllerTaskIdentifier = task->controllerTaskID;
            scsiResponse.fTargetID = targetId;
            scsiResponse.fServiceResponse =
                kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE;
            scsiResponse.fCompletionStatus = kSCSITaskStatus_GOOD;
            scsiResponse.fBytesTransferred = 0;

            this->ParallelTaskCompletion(task->completionAction, scsiResponse);

            task->completionAction->release();
            task->completionAction = NULL;
            if (task->dataBuffer) {
                task->dataBuffer->release();
                task->dataBuffer = NULL;
            }
            memset(task, 0, sizeof(DextPendingTask));
        }
    }

    *response = kSCSIServiceResponse_FUNCTION_COMPLETE;
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserClearACARequest_Impl(uint64_t theT,
                                                       uint64_t theL,
                                                       uint32_t * response)
{
    (void)theT; (void)theL; (void)response;

    // ACA (Auto Contingent Allegiance) — iSCSI does not use this mechanism.
    // Just acknowledge completion.
    if (!response) {
        return kIOReturnBadArgument;
    }
    *response = kSCSIServiceResponse_FUNCTION_COMPLETE;
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserClearTaskSetRequest_Impl(uint64_t theT,
                                                           uint64_t theL,
                                                           uint32_t * response)
{
    if (!response) {
        return kIOReturnBadArgument;
    }

    uint8_t targetId = (uint8_t)theT;

    // Abort ALL pending tasks for this target (regardless of LUN)

    for (int i = 0; i < kMaxPendingTasks; i++) {
        DextPendingTask * task = &s_pendingTasks[i];
        if (task->active &&
            task->targetId == targetId &&
            task->completionAction) {

            SCSIUserParallelResponse scsiResponse;
            memset(&scsiResponse, 0, sizeof(scsiResponse));
            scsiResponse.version = kScsiUserParallelTaskResponseCurrentVersion1;
            scsiResponse.fControllerTaskIdentifier = task->controllerTaskID;
            scsiResponse.fTargetID = targetId;
            scsiResponse.fServiceResponse =
                kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE;
            scsiResponse.fCompletionStatus = kSCSITaskStatus_GOOD;
            scsiResponse.fBytesTransferred = 0;

            this->ParallelTaskCompletion(task->completionAction, scsiResponse);

            task->completionAction->release();
            task->completionAction = NULL;
            if (task->dataBuffer) {
                task->dataBuffer->release();
                task->dataBuffer = NULL;
            }
            memset(task, 0, sizeof(DextPendingTask));
        }
    }

    *response = kSCSIServiceResponse_FUNCTION_COMPLETE;
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserLogicalUnitResetRequest_Impl(uint64_t theT,
                                                               uint64_t theL,
                                                               uint32_t * response)
{
    if (!response) return kIOReturnBadArgument;

    uint8_t targetId = (uint8_t)theT;
    uint64_t LUN = theL;

    // Abort all pending tasks for this LUN

    for (int i = 0; i < kMaxPendingTasks; i++) {
        DextPendingTask * task = &s_pendingTasks[i];
        if (task->active && task->targetId == targetId &&
            task->LUN == LUN && task->completionAction) {
            SCSIUserParallelResponse scsiResponse;
            memset(&scsiResponse, 0, sizeof(scsiResponse));
            scsiResponse.version = kScsiUserParallelTaskResponseCurrentVersion1;
            scsiResponse.fControllerTaskIdentifier = task->controllerTaskID;
            scsiResponse.fTargetID = targetId;
            scsiResponse.fServiceResponse =
                kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE;
            scsiResponse.fCompletionStatus = kSCSITaskStatus_GOOD;
            this->ParallelTaskCompletion(task->completionAction, scsiResponse);
            task->completionAction->release();
            task->completionAction = NULL;
            if (task->dataBuffer) { task->dataBuffer->release(); task->dataBuffer = NULL; }
            memset(task, 0, sizeof(DextPendingTask));
        }
    }

    *response = kSCSIServiceResponse_FUNCTION_COMPLETE;
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextHBA::UserTargetResetRequest_Impl(uint64_t theT,
                                                          uint32_t * response)
{
    if (!response) return kIOReturnBadArgument;

    uint8_t targetId = (uint8_t)theT;

    // Abort ALL pending tasks for this target

    for (int i = 0; i < kMaxPendingTasks; i++) {
        DextPendingTask * task = &s_pendingTasks[i];
        if (task->active && task->targetId == targetId && task->completionAction) {
            SCSIUserParallelResponse scsiResponse;
            memset(&scsiResponse, 0, sizeof(scsiResponse));
            scsiResponse.version = kScsiUserParallelTaskResponseCurrentVersion1;
            scsiResponse.fControllerTaskIdentifier = task->controllerTaskID;
            scsiResponse.fTargetID = targetId;
            scsiResponse.fServiceResponse =
                kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE;
            scsiResponse.fCompletionStatus = kSCSITaskStatus_GOOD;
            this->ParallelTaskCompletion(task->completionAction, scsiResponse);
            task->completionAction->release();
            task->completionAction = NULL;
            if (task->dataBuffer) { task->dataBuffer->release(); task->dataBuffer = NULL; }
            memset(task, 0, sizeof(DextPendingTask));
        }
    }

    *response = kSCSIServiceResponse_FUNCTION_COMPLETE;
    return kIOReturnSuccess;
}

#pragma mark - Data Buffer Access

kern_return_t iSCSIDextHBA::UserGetDataBuffer_Impl(SCSIDeviceIdentifier targetID,
                                                     uint64_t controllerTaskID,
                                                     IOBufferMemoryDescriptor ** buffer)
{
    if (!buffer) {
        return kIOReturnBadArgument;
    }

    // Search pending tasks for this controllerTaskID
    for (int i = 0; i < kMaxPendingTasks; i++) {
        if (s_pendingTasks[i].active &&
            s_pendingTasks[i].controllerTaskID == controllerTaskID &&
            s_pendingTasks[i].targetId == (uint8_t)targetID) {

            if (s_pendingTasks[i].dataBuffer) {
                *buffer = s_pendingTasks[i].dataBuffer;
                (*buffer)->retain();
                return kIOReturnSuccess;
            }
            break;
        }
    }

    // No existing buffer — this may be the first request for it.
    // The data path (IOUserSCSIParallelInterfaceController) expects us
    // to create or return the buffer. For iSCSI, we use the pending
    // task's stored buffer if available. If not, return NULL.
    *buffer = NULL;
    return kIOReturnSuccess;
}

#pragma mark - Bundled Parallel Task Support

kern_return_t iSCSIDextHBA::UserMapBundledParallelTaskCommandAndResponseBuffers_Impl(
    IOBufferMemoryDescriptor * parallelCommandIOMemoryDescriptor,
    IOBufferMemoryDescriptor * parallelResponseIOMemoryDescriptor)
{
    (void)parallelCommandIOMemoryDescriptor;
    (void)parallelResponseIOMemoryDescriptor;
    // Return failure to use UserProcessParallelTask instead of bundled API
    return kIOReturnError;
}

void iSCSIDextHBA::BundledParallelTaskCompletion_Impl(
    OSAction * action,
    const uint16_t parallelResponseSlotIndices[kMaxBundledParallelTasks],
    uint16_t parallelResponseSlotIndicesCount)
{
    (void)action;
    (void)parallelResponseSlotIndices;
    (void)parallelResponseSlotIndicesCount;
    // Not used since we returned failure from
    // UserMapBundledParallelTaskCommandAndResponseBuffers
}

void iSCSIDextHBA::UserProcessBundledParallelTasks_Impl(
    const uint16_t parallelRequestSlotIndices[kMaxBundledParallelTasks],
    uint16_t parallelRequestSlotIndicesCount,
    OSAction * completion)
{
    (void)parallelRequestSlotIndices;
    (void)parallelRequestSlotIndicesCount;
    (void)completion;
    // Not used since we returned failure from
    // UserMapBundledParallelTaskCommandAndResponseBuffers
}

#pragma mark - Session State Accessors (free functions)

/* getSession() and getConnection() are defined as static file-scope
 * functions above, not as class methods. This avoids needing to add
 * them to the IIG interface definition. */
