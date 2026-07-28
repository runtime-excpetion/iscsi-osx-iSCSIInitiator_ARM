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
 * iSCSIDextInternal.h — Internal state shared between iSCSIDextHBA
 * and iSCSIDextUserClient.
 *
 * Contains session/connection state structures, pending task tracking,
 * and outgoing PDU queue declarations. Implementation lives in
 * iSCSIDextHBA.cpp.
 */

#ifndef __ISCSI_DEXT_INTERNAL_H__
#define __ISCSI_DEXT_INTERNAL_H__

#include "../Shared/DextDaemonIPC/iSCSIDextDaemonIPCShared.h"
#include "iSCSIPDUEncoding.h"

#include <DriverKit/IOBufferMemoryDescriptor.h>
#include <DriverKit/OSAction.h>
#include <stdint.h>
#include <stdbool.h>

#pragma mark - Constants

/*! Maximum number of targets supported. */
#define kMaxTargets     16

/*! Maximum number of LUNs per target. */
#define kMaxLUNs        64

/*! Maximum pending SCSI tasks. */
#define kMaxPendingTasks 256

/*! Maximum entries in the outgoing PDU queue. */
#define kMaxOutgoingPDUs 64

/*! Default timeout for SCSI commands (seconds). */
#define kDefaultTimeout 30

/*! iSCSI initiator task tag base — incremented per command. */
#define kTaskTagBase    0x80000000UL

#pragma mark - Session State

struct DextSessionState {
    bool                active;
    uint8_t             sessionId;
    uint8_t             targetId;
    uint32_t            cmdSN;
    uint32_t            expCmdSN;
    uint32_t            maxCmdSN;
    uint32_t            maxBurstLength;
    uint32_t            firstBurstLength;
    uint32_t            maxRecvDataSegmentLength;
    uint32_t            maxSendDataSegmentLength;
    bool                immediateData;
    bool                initialR2T;
    bool                dataPDUInOrder;
    bool                dataSequenceInOrder;
    uint8_t             maxOutstandingR2T;
};

#pragma mark - Connection State

struct DextConnectionState {
    bool                active;
    uint32_t            connectionId;
    uint8_t             sessionId;
    uint32_t            expStatSN;
    bool                useHeaderDigest;
    bool                useDataDigest;
};

#pragma mark - Pending SCSI Task

struct DextPendingTask {
    bool                active;
    uint8_t             targetId;
    uint64_t            LUN;
    uint32_t            initiatorTaskTag;
    uint64_t            controllerTaskID;
    uint8_t             cdb[16];
    uint8_t             taskAttr;
    bool                isRead;
    uint32_t            expectedDataTransferLength;
    uint32_t            transferOffset;
    IOBufferMemoryDescriptor * dataBuffer;
    OSAction *          completionAction;
    uint64_t            startTime;
};

#pragma mark - Outgoing PDU Queue Entry

struct DextOutgoingPDUEntry {
    bool                active;
    uint8_t             sessionId;
    uint32_t            connectionId;
    uint32_t            initiatorTaskTag;
    uint8_t             bhs[48];
    uint32_t            dataLength;
    uint8_t *           data;       // dynamically allocated if dataLength > 0
};

#pragma mark - Storage (extern, defined in iSCSIDextHBA.cpp)

extern DextSessionState      s_sessions[kMaxTargets];
extern DextConnectionState   s_connections[kMaxTargets][kIPC_MAX_CONNECTIONS];
extern DextPendingTask       s_pendingTasks[kMaxPendingTasks];
extern DextOutgoingPDUEntry  s_outgoingPDUQueue[kMaxOutgoingPDUs];
extern uint32_t              s_nextTaskTag;

#pragma mark - Session/Connection Helper Functions

void initSessionState(DextSessionState * session);
void initConnectionState(DextConnectionState * conn);
DextSessionState * getSession(uint8_t targetId);
DextConnectionState * getConnection(uint8_t targetId, uint32_t connectionId);

#pragma mark - Pending Task Management

int  dext_add_pending_task(uint8_t targetId, uint64_t LUN,
                           const uint8_t cdb[16], uint8_t taskAttr,
                           bool isRead, uint32_t expectedLength,
                           OSAction * completion);

void dext_remove_pending_task(uint32_t initiatorTaskTag);

DextPendingTask * dext_get_pending_task(uint32_t initiatorTaskTag);

uint32_t dext_get_next_task_tag(void);

#pragma mark - Outgoing PDU Queue Management

bool dext_enqueue_outgoing_pdu(uint8_t sessionId, uint32_t connectionId,
                               uint32_t initiatorTaskTag,
                               const uint8_t bhs[48],
                               const uint8_t * data, uint32_t dataLength);

bool dext_dequeue_outgoing_pdu(DextOutgoingPDUEntry * entry);

void dext_clear_outgoing_pdu_entry(DextOutgoingPDUEntry * entry);

void dext_free_outgoing_pdu_queue(void);

uint32_t dext_outgoing_pdu_count(void);

#pragma mark - Target Session State

bool dext_is_target_ready(uint8_t targetId);
bool dext_is_connection_active(uint8_t targetId, uint32_t connectionId);

#endif /* __ISCSI_DEXT_INTERNAL_H__ */
