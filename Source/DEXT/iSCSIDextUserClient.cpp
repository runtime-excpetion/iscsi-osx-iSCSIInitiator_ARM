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
 * iSCSIDextUserClient.cpp — IOUserClient ExternalMethod dispatch.
 *
 * Handles all daemon ↔ DEXT IPC via IOConnectCallMethod.
 * The daemon opens this user client via IOServiceOpen on the
 * iSCSIDextHBA service and sends commands using DaemonCommandType selectors.
 *
 * Data path PDUs (SCSI Cmd → daemon, SCSI Rsp/Data-In/R2T → DEXT)
 * flow through kDextCmdGetNextPDU and kDextCmdIncomingPDU.
 */

#include "iSCSIDextInternal.h"
#include "iSCSIDextHBA.h"          // For calling iSCSIDextHBA methods
#include "iSCSIDextUserClient.h"   // IIG-generated header
#include <DriverKit/OSData.h>
#include <string.h>

// Include the IIG-generated dispatch code
#include "iSCSIDextUserClient_impl_gen.cpp"

#pragma mark - IOService Overrides

bool iSCSIDextUserClient::init(void)
{
    return super::init();
}

void iSCSIDextUserClient::free(void)
{
    super::free();
}

kern_return_t iSCSIDextUserClient::Start_Impl(IOService * provider)
{
    kern_return_t ret = super::Start(provider);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    // Provider (iSCSIDextHBA) is retained by super::Start
    return kIOReturnSuccess;
}

kern_return_t iSCSIDextUserClient::Stop_Impl(IOService * provider)
{
    return super::Stop(provider);
}

#pragma mark - ExternalMethod IPC Dispatch

kern_return_t iSCSIDextUserClient::ExternalMethod(
    uint64_t                              selector,
    IOUserClientMethodArguments         * arguments,
    const IOUserClientMethodDispatch    * dispatch,
    OSObject                            * target,
    void                                * reference)
{
    kern_return_t ret = kIOReturnSuccess;

    (void)dispatch;
    (void)target;
    (void)reference;

    // Validate arguments struct
    if (!arguments) {
        return kIOReturnBadArgument;
    }

    switch (selector) {

#pragma mark - Connection Lifecycle

        case kDextCmdOpen: {
            // Daemon connecting — acknowledge via scalar output
            if (arguments->scalarOutput && arguments->scalarOutputCount >= 1) {
                arguments->scalarOutput[0] = 1; // IPC protocol version
            }
            break;
        }

        case kDextCmdClose: {
            // Daemon disconnecting — clean up all sessions
            for (uint32_t i = 0; i < kMaxTargets; i++) {
                initSessionState(&s_sessions[i]);
                for (uint32_t j = 0; j < kIPC_MAX_CONNECTIONS; j++) {
                    initConnectionState(&s_connections[i][j]);
                }
            }
            // Clear pending tasks (release completion actions + data buffers)
            for (int i = 0; i < kMaxPendingTasks; i++) {
                if (s_pendingTasks[i].active) {
                    if (s_pendingTasks[i].completionAction) {
                        s_pendingTasks[i].completionAction->release();
                    }
                    if (s_pendingTasks[i].dataBuffer) {
                        s_pendingTasks[i].dataBuffer->release();
                    }
                }
            }
            memset(s_pendingTasks, 0, sizeof(s_pendingTasks));
            // Free dynamically allocated PDU data
            dext_free_outgoing_pdu_queue();
            break;
        }

#pragma mark - Session Management

        case kDextCmdRegisterSession: {
            if (!arguments->structureInput) {
                return kIOReturnBadArgument;
            }
            OSData * inputData = arguments->structureInput;
            if (inputData->getLength() < sizeof(DextSessionConfig)) {
                return kIOReturnBadArgument;
            }
            const DextSessionConfig * config =
                (const DextSessionConfig *)inputData->getBytesNoCopy();
            if (!config) return kIOReturnBadArgument;
            if (config->targetId >= kMaxTargets) {
                return kIOReturnNoSpace;
            }
            DextSessionState * session = getSession(config->targetId);
            if (!session) return kIOReturnNotFound;

            initSessionState(session);
            session->active      = true;
            session->sessionId   = config->sessionId;
            session->targetId    = config->targetId;
            session->cmdSN       = config->initialCmdSN;
            session->expCmdSN    = config->initialCmdSN;
            session->maxCmdSN    = config->initialCmdSN + 1;

            session->maxBurstLength           = config->maxBurstLength;
            session->firstBurstLength         = config->firstBurstLength;
            session->maxRecvDataSegmentLength = config->maxRecvDataSegmentLength;
            session->maxSendDataSegmentLength = config->maxSendDataSegmentLength;
            session->immediateData            = config->immediateData;
            session->initialR2T               = config->initialR2T;
            session->dataPDUInOrder           = config->dataPDUInOrder;
            session->dataSequenceInOrder      = config->dataSequenceInOrder;
            session->maxOutstandingR2T        = config->maxOutstandingR2T;
            break;
        }

        case kDextCmdUnregisterSession: {
            if (arguments->scalarInputCount < 1) {
                return kIOReturnBadArgument;
            }
            uint64_t rawTargetId = arguments->scalarInput[0];
            if (rawTargetId < kMaxTargets) {
                uint8_t targetId = (uint8_t)rawTargetId;
                initSessionState(&s_sessions[targetId]);
                for (uint32_t j = 0; j < kIPC_MAX_CONNECTIONS; j++) {
                    initConnectionState(&s_connections[targetId][j]);
                }
                // Also clear pending tasks for this target
                for (int i = 0; i < kMaxPendingTasks; i++) {
                    if (s_pendingTasks[i].active && s_pendingTasks[i].targetId == targetId) {
                        dext_remove_pending_task(s_pendingTasks[i].initiatorTaskTag);
                    }
                }
            }
            break;
        }

#pragma mark - Connection Management

        case kDextCmdActivateConnection: {
            if (!arguments->structureInput) {
                return kIOReturnBadArgument;
            }
            OSData * inputData = arguments->structureInput;
            if (inputData->getLength() < sizeof(DextConnectionConfig)) {
                return kIOReturnBadArgument;
            }
            const DextConnectionConfig * config =
                (const DextConnectionConfig *)inputData->getBytesNoCopy();
            if (!config) return kIOReturnBadArgument;

            DextConnectionState * conn =
                getConnection(config->sessionId, config->connectionId);
            if (!conn) return kIOReturnNotFound;

            conn->active          = true;
            conn->connectionId    = config->connectionId;
            conn->sessionId       = config->sessionId;
            conn->expStatSN       = config->initialExpStatSN;
            conn->useHeaderDigest = config->useHeaderDigest;
            conn->useDataDigest   = config->useDataDigest;
            break;
        }

        case kDextCmdDeactivateConnection: {
            if (arguments->scalarInputCount < 2) {
                return kIOReturnBadArgument;
            }
            uint8_t  rawSessionId = (uint8_t)arguments->scalarInput[0];
            uint32_t rawConnId    = (uint32_t)arguments->scalarInput[1];
            DextConnectionState * conn =
                getConnection(rawSessionId, rawConnId);
            if (conn) {
                initConnectionState(conn);
            }
            break;
        }

#pragma mark - Parameter Setting

        case kDextCmdSetSessionParam: {
            if (arguments->scalarInputCount < 3) {
                return kIOReturnBadArgument;
            }
            uint8_t  targetId = (uint8_t)arguments->scalarInput[0];
            uint32_t param    = (uint32_t)arguments->scalarInput[1];
            uint64_t value    = arguments->scalarInput[2];

            DextSessionState * session = getSession(targetId);
            if (!session || !session->active) return kIOReturnNotReady;

            switch ((DextSessionParam)param) {
                case kDextSessionParamMaxBurstLength:
                    session->maxBurstLength = (uint32_t)value; break;
                case kDextSessionParamFirstBurstLength:
                    session->firstBurstLength = (uint32_t)value; break;
                case kDextSessionParamImmediateData:
                    session->immediateData = (bool)value; break;
                case kDextSessionParamInitialR2T:
                    session->initialR2T = (bool)value; break;
                case kDextSessionParamDataPDUInOrder:
                    session->dataPDUInOrder = (bool)value; break;
                case kDextSessionParamDataSequenceInOrder:
                    session->dataSequenceInOrder = (bool)value; break;
                case kDextSessionParamMaxOutstandingR2T:
                    session->maxOutstandingR2T = (uint8_t)value; break;
                default:
                    break;
            }
            break;
        }

        case kDextCmdSetConnectionParam: {
            if (arguments->scalarInputCount < 3) {
                return kIOReturnBadArgument;
            }
            uint8_t  sessionId  = (uint8_t)arguments->scalarInput[0];
            uint32_t param      = (uint32_t)arguments->scalarInput[1];
            uint64_t value      = arguments->scalarInput[2];

            for (uint32_t j = 0; j < kIPC_MAX_CONNECTIONS; j++) {
                DextConnectionState * conn = &s_connections[sessionId][j];
                if (conn->active) {
                    switch ((DextConnectionParam)param) {
                        case kDextConnectionParamUseHeaderDigest:
                            conn->useHeaderDigest = (bool)value; break;
                        case kDextConnectionParamUseDataDigest:
                            conn->useDataDigest = (bool)value; break;
                        case kDextConnectionParamInitialExpStatSN:
                            conn->expStatSN = (uint32_t)value; break;
                        default:
                            break;
                    }
                }
            }
            break;
        }

#pragma mark - Data Path: Incoming PDU (Daemon → DEXT)

        case kDextCmdIncomingPDU: {
            // Daemon forwards a PDU received from TCP to the DEXT.
            // The structure input contains:
            //   [sizeof(DextIncomingPDU) bytes of header]
            //   [optional data segment: inputLength - sizeof(DextIncomingPDU)]
            //
            // PDU types handled:
            //   SCSI Response (0x21) — complete task with status/sense
            //   Data-In (0x25) — copy data to IOBufferMemoryDescriptor
            //   R2T (0x31) — build Data-Out PDU from buffer
            //   NOP-In (0x20) — acknowledge
            //   Task Management Response (0x22) — complete task mgmt

            if (!arguments->structureInput) {
                return kIOReturnBadArgument;
            }
            OSData * inputData = arguments->structureInput;
            uint32_t inputLength = inputData->getLength();
            if (inputLength < sizeof(DextIncomingPDU)) {
                return kIOReturnBadArgument;
            }

            const uint8_t * bytes = (const uint8_t *)inputData->getBytesNoCopy();
            if (!bytes) return kIOReturnBadArgument;

            const DextIncomingPDU * incoming =
                (const DextIncomingPDU *)bytes;
            uint32_t dataSegmentLength = inputLength - sizeof(DextIncomingPDU);
            const uint8_t * dataSegment =
                dataSegmentLength > 0 ? (bytes + sizeof(DextIncomingPDU)) : NULL;

            // Extract initiator task tag from BHS (bytes 16-19)
            uint32_t taskTagBE;
            memcpy(&taskTagBE, &incoming->bhs[16], 4);
            uint32_t initiatorTaskTag = pdu_encode_ntoh32(taskTagBE);
            uint8_t opcode = pdu_get_opcode(incoming->bhs);

            // Get the HBA reference for ParallelTaskCompletion
            IOService * prov = GetProvider();
            iSCSIDextHBA * hba = (iSCSIDextHBA *)prov;

            switch (opcode) {

                case kPDUEncodeOpCodeSCSIRsp: {
                    // SCSI Response — complete the pending task
                    uint8_t  status   = 0;
                    uint8_t  response = 0;
                    uint32_t statSN   = 0;
                    uint32_t expCmdSN = 0;
                    uint32_t maxCmdSN = 0;
                    uint32_t residual = 0;
                    uint32_t senseLen = 0;

                    if (!pdu_parse_scsi_rsp(incoming->bhs, &status, &response,
                                            &statSN, &expCmdSN, &maxCmdSN,
                                            &residual, &senseLen)) {
                        return kIOReturnIPCError;
                    }

                    // Update session sequence numbers
                    DextSessionState * session = getSession(incoming->sessionId);
                    if (session) {
                        if (expCmdSN > session->expCmdSN) session->expCmdSN = expCmdSN;
                        if (maxCmdSN > session->maxCmdSN) session->maxCmdSN = maxCmdSN;
                    }

                    // Find pending task
                    DextPendingTask * task = dext_get_pending_task(initiatorTaskTag);
                    if (!task || !task->completionAction) {
                        return kIOReturnNotFound;
                    }

                    // Build SCSIUserParallelResponse
                    SCSIUserParallelResponse scsiResponse;
                    memset(&scsiResponse, 0, sizeof(scsiResponse));
                    scsiResponse.version = kScsiUserParallelTaskResponseCurrentVersion1;
                    scsiResponse.fControllerTaskIdentifier = task->controllerTaskID;
                    scsiResponse.fTargetID = task->targetId;

                    if (response == 0) {
                        // Command completed
                        scsiResponse.fServiceResponse = kSCSIServiceResponse_TASK_COMPLETE;
                        scsiResponse.fCompletionStatus = (SCSITaskStatus)status;
                        scsiResponse.fBytesTransferred = task->expectedDataTransferLength - residual;

                        // Copy sense data if present (CHECK CONDITION)
                        if (status == 2 && senseLen > 0 && dataSegment) {
                            uint32_t copyLen = (senseLen < sizeof(scsiResponse.fSenseData))
                                               ? senseLen : sizeof(scsiResponse.fSenseData);
                            memcpy(scsiResponse.fSenseData, dataSegment, copyLen);
                            scsiResponse.fSenseLength = copyLen;
                        }
                    } else {
                        // Target failure
                        scsiResponse.fServiceResponse =
                            kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE;
                        scsiResponse.fCompletionStatus = kSCSITaskStatus_GOOD;
                        scsiResponse.fBytesTransferred = 0;
                    }

                    // Complete the SCSI task via HBA
                    if (hba) {
                        hba->ParallelTaskCompletion(task->completionAction, scsiResponse);
                    }

                    // Clean up pending task (completion action is released by the framework)
                    dext_remove_pending_task(initiatorTaskTag);
                    break;
                }

                case kPDUEncodeOpCodeDataIn: {
                    // Data-In — copy data to the task's data buffer.
                    // Task completion is handled by a subsequent SCSI Response PDU.
                    uint8_t  flags    = 0;
                    uint8_t  dataStatus = 0;
                    uint32_t dataStatSN = 0;
                    uint32_t dataSN   = 0;
                    uint32_t bufferOffset = 0;
                    uint32_t dataLen  = 0;

                    if (!pdu_parse_data_in(incoming->bhs, &flags, &dataStatus,
                                           &dataStatSN, &dataSN,
                                           &bufferOffset, &dataLen, NULL)) {
                        return kIOReturnIPCError;
                    }

                    DextPendingTask * task = dext_get_pending_task(initiatorTaskTag);
                    if (!task) {
                        return kIOReturnNotFound;
                    }

                    // Copy data segment to the task's I/O buffer
                    if (dataLen > 0 && dataSegment && task->dataBuffer) {
                        IOBufferMemoryDescriptor * buf = task->dataBuffer;
                        uint64_t bufLen = buf->getLength();
                        if (bufferOffset + dataLen <= bufLen) {
                            IOMemoryMap * mapping = NULL;
                            kern_return_t mapRet = buf->CreateMapping(0, 0, 0, 0, &mapping);
                            if (mapRet == kIOReturnSuccess && mapping) {
                                uint64_t mapAddr = 0;
                                uint64_t mapLen  = 0;
                                mapping->GetAddressRange(&mapAddr, &mapLen);

                                if (mapAddr && mapLen >= bufferOffset + dataLen) {
                                    memcpy((void *)(mapAddr + bufferOffset),
                                           dataSegment, dataLen);
                                    task->transferOffset = bufferOffset + dataLen;
                                }

                                mapping->release();
                                mapping = NULL;
                            }
                        }
                    }
                    break;
                }

                case kPDUEncodeOpCodeR2T: {
                    // R2T — build Data-Out PDU from the task's data buffer
                    uint32_t targetTag    = 0;
                    uint32_t r2tOffset   = 0;
                    uint32_t desiredLen  = 0;
                    uint32_t r2tSN       = 0;
                    uint32_t r2tStatSN   = 0;

                    if (!pdu_parse_r2t(incoming->bhs, &targetTag, &r2tOffset,
                                       &desiredLen, &r2tSN, &r2tStatSN)) {
                        return kIOReturnIPCError;
                    }

                    DextPendingTask * task = dext_get_pending_task(initiatorTaskTag);
                    if (!task || !task->dataBuffer) {
                        return kIOReturnNotFound;
                    }

                    // Determine how much data to send
                    uint32_t sendLen = desiredLen;
                    uint32_t remaining = task->expectedDataTransferLength - r2tOffset;
                    if (sendLen > remaining) sendLen = remaining;
                    if (sendLen > kIPC_MAX_DATA_SEGMENT_SIZE) {
                        sendLen = kIPC_MAX_DATA_SEGMENT_SIZE;
                    }

                    // Get the data from the buffer
                    uint8_t dataOutData[kIPC_MAX_DATA_SEGMENT_SIZE];
                    memset(dataOutData, 0, sizeof(dataOutData));

                    IOBufferMemoryDescriptor * buf = task->dataBuffer;
                    IOMemoryMap * mapping = NULL;
                    kern_return_t mapRet = buf->CreateMapping(0, 0, 0, 0, &mapping);
                    if (mapRet == kIOReturnSuccess && mapping) {
                        uint64_t mapAddr = 0;
                        uint64_t mapLen  = 0;
                        mapping->GetAddressRange(&mapAddr, &mapLen);

                        if (mapAddr && mapLen >= r2tOffset + sendLen) {
                            memcpy(dataOutData, (void *)(mapAddr + r2tOffset), sendLen);
                        }
                        mapping->release();
                        mapping = NULL;
                    }

                    // Build Data-Out PDU BHS
                    uint8_t dataOutBHS[48];
                    DextSessionState * session = getSession(incoming->sessionId);
                    // The Data-Out PDU uses the connection's expStatSN, not the
                    // expCmdSN from the R2T BHS (bytes 28-31).
                    DextConnectionState * conn =
                        getConnection(incoming->sessionId, incoming->connectionId);
                    uint32_t expStatSN = conn ? conn->expStatSN : 0;

                    bool isFinal = (r2tOffset + sendLen >= task->expectedDataTransferLength);

                    // Use the same task tag for the Data-Out
                    pdu_build_data_out(dataOutBHS, task->LUN,
                                       initiatorTaskTag, targetTag,
                                       r2tSN, r2tOffset,
                                       sendLen, expStatSN, isFinal);

                    // Enqueue the Data-Out PDU
                    dext_enqueue_outgoing_pdu(incoming->sessionId,
                                              incoming->connectionId,
                                              initiatorTaskTag,
                                              dataOutBHS,
                                              sendLen > 0 ? dataOutData : NULL,
                                              sendLen);
                    break;
                }

                case kPDUEncodeOpCodeNOPIn: {
                    // NOP-In parsing — bytes 20-23 are the Target Transfer Tag (TTT).
                    // TTT == 0xFFFFFFFF (reserved) → response to our NOP-Out heartbeat,
                    // just consume. TTT != reserved → target-initiated ping, must
                    // respond with NOP-Out echoing the target transfer tag.
                    uint32_t targetTT = 0xFFFFFFFF;
                    uint32_t targetTTBE;
                    memcpy(&targetTTBE, &incoming->bhs[20], 4);
                    targetTT = pdu_encode_ntoh32(targetTTBE);

                    if (targetTT != 0xFFFFFFFF) {
                        // Target-initiated NOP-In — respond with NOP-Out.
                        DextSessionState * session =
                            getSession(incoming->sessionId);
                        uint32_t cmdSN = session ? session->cmdSN : 0;
                        uint32_t expStatSN = 0;
                        DextConnectionState * conn =
                            getConnection(incoming->sessionId,
                                          incoming->connectionId);
                        if (conn) {
                            expStatSN = conn->expStatSN;
                        }

                        // Extract initiator task tag from incoming NOP-In BHS
                        uint32_t taskTagBE;
                        memcpy(&taskTagBE, &incoming->bhs[16], 4);
                        uint32_t initiatorTaskTag = pdu_encode_ntoh32(taskTagBE);

                        uint8_t nopOutBHS[48];
                        memset(nopOutBHS, 0, sizeof(nopOutBHS));
                        pdu_build_nop_out(nopOutBHS,
                                           initiatorTaskTag,
                                           targetTT,   // echo target TTT
                                           cmdSN, expStatSN);

                        dext_enqueue_outgoing_pdu(incoming->sessionId,
                                                  incoming->connectionId,
                                                  0,          // no task tag
                                                  nopOutBHS,
                                                  NULL, 0);

                        // Advance cmdSN for the NOP-Out we just queued
                        if (session) {
                            session->cmdSN++;
                        }
                    }
                    // else: response to our heartbeat — consume silently
                    break;
                }

                default:
                    // Unknown opcode — ignore
                    break;
            }
            break;
        }

#pragma mark - Data Path: Get Next Outgoing PDU

        case kDextCmdGetNextPDU: {
            // Daemon requests the next PDU to send on TCP.
            // Returns a DextOutgoingPDUFull via structure output.
            // The caller must free entry.data after use.

            DextOutgoingPDUEntry entry;
            if (!dext_dequeue_outgoing_pdu(&entry)) {
                // No PDU pending — return empty via scalar output
                if (arguments->scalarOutput && arguments->scalarOutputCount >= 1) {
                    arguments->scalarOutput[0] = 0; // no data
                }
                break;
            }

            // Ensure output buffer is large enough
            if (arguments->structureOutputMaximumSize < sizeof(DextOutgoingPDUFull)) {
                dext_clear_outgoing_pdu_entry(&entry);
                return kIOReturnNoSpace;
            }

            uint32_t pduDataLength = entry.dataLength;
            if (pduDataLength > kIPC_MAX_DATA_SEGMENT_SIZE) {
                pduDataLength = kIPC_MAX_DATA_SEGMENT_SIZE;
            }

            OSData * outputData = OSData::withCapacity(sizeof(DextOutgoingPDUFull));
            if (!outputData) {
                dext_clear_outgoing_pdu_entry(&entry);
                return kIOReturnNoMemory;
            }

            // Build the DextOutgoingPDUFull with actual data
            DextOutgoingPDUFull pduOut;
            memset(&pduOut, 0, sizeof(pduOut));
            pduOut.sessionId       = entry.sessionId;
            pduOut.connectionId    = entry.connectionId;
            pduOut.initiatorTaskTag = entry.initiatorTaskTag;
            memcpy(pduOut.bhs, entry.bhs, 48);
            pduOut.dataLength = pduDataLength;

            // Copy data segment from dynamically allocated buffer
            if (pduDataLength > 0 && entry.data) {
                memcpy(pduOut.dataSegment, entry.data, pduDataLength);
            }

            // Free the dynamically allocated data from the entry
            dext_clear_outgoing_pdu_entry(&entry);

            outputData->appendBytes(&pduOut, sizeof(DextOutgoingPDUFull));
            arguments->structureOutput = outputData;

            // Signal that we have data
            if (arguments->scalarOutput && arguments->scalarOutputCount >= 1) {
                arguments->scalarOutput[0] = 1; // data available
            }
            break;
        }

#pragma mark - Connection Status

        case kDextCmdConnectionStatus: {
            if (arguments->structureInput) {
                OSData * inputData = arguments->structureInput;
                if (inputData->getLength() >= sizeof(DextConnectionStatus)) {
                    const DextConnectionStatus * status =
                        (const DextConnectionStatus *)inputData->getBytesNoCopy();
                    if (status && !status->isConnected) {
                        DextConnectionState * conn =
                            getConnection(status->sessionId, status->connectionId);
                        if (conn) {
                            conn->active = false;
                        }
                    }
                }
            }
            break;
        }

#pragma mark - Async / NOP

        case kDextCmdAsyncMessage:
        case kDextCmdNOPInResponse:
            // Acknowledge without action
            break;

        default:
            ret = kIOReturnUnsupported;
            break;
    }

    return ret;
}
