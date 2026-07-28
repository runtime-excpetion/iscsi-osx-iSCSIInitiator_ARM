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

#ifndef __ISCSI_DEXT_DAEMON_IPC_SHARED_H__
#define __ISCSI_DEXT_DAEMON_IPC_SHARED_H__

#include <stdint.h>
#include <stdbool.h>

#pragma mark - IPC Protocol Constants

/*! Maximum size of an iSCSI Basic Header Segment. */
#define kIPC_ISCSI_BHS_SIZE         48

/*! Maximum data segment payload in a single IPC message.
 *  This matches typical iSCSI burst lengths (64KB). */
#define kIPC_MAX_DATA_SEGMENT_SIZE  65536

/*! Number of session slots in the DEXT. */
#define kIPC_MAX_SESSIONS           16

/*! Number of connections per session. */
#define kIPC_MAX_CONNECTIONS        2

#pragma mark - Message Types (Daemon → DEXT)

/*!
 * @enum DaemonCommandType
 * @brief Commands sent from the daemon to the DEXT via ExternalMethod.
 *
 * These selectors are dispatched in the DEXT's ExternalMethod handler.
 * Daemon calls IOConnectCallMethod with these selectors.
 */
enum DaemonCommandType {
    /*! Open connection to the DEXT service. */
    kDextCmdOpen                         = 0,

    /*! Close connection to the DEXT service. */
    kDextCmdClose                        = 1,

    /*! Register a new session after login negotiation is complete.
     *  Daemon has already established TCP connection and completed login. */
    kDextCmdRegisterSession              = 2,

    /*! Remove a previously registered session. */
    kDextCmdUnregisterSession            = 3,

    /*! Activate a connection (enable full-feature phase iSCSI).
     *  After this, DEXT will accept SCSI tasks from the macOS stack. */
    kDextCmdActivateConnection           = 4,

    /*! Deactivate a connection. */
    kDextCmdDeactivateConnection         = 5,

    /*! Set a session parameter (negotiated value from login).
     *  e.g., MaxBurstLength, InitialR2T, ImmediateData, etc. */
    kDextCmdSetSessionParam              = 6,

    /*! Set a connection parameter. */
    kDextCmdSetConnectionParam           = 7,

    /*! Incoming PDU data from TCP (data path).
     *  Daemon received a PDU from the target and is forwarding to DEXT. */
    kDextCmdIncomingPDU                  = 8,

    /*! TCP connection status change notification. */
    kDextCmdConnectionStatus             = 9,

    /*! ASYNC message from target (forwarded by daemon). */
    kDextCmdAsyncMessage                 = 10,

    /*! NOP-In response for latency measurement. */
    kDextCmdNOPInResponse                = 11,

    /*! Dequeue the next outgoing PDU for TCP transmission.
     *  DEXT encodes SCSI Command / Data-Out PDUs and queues them;
     *  daemon calls this to retrieve the next PDU to send on TCP.
     *  Structure output: DextOutgoingPDU (BHS + optional data segment). */
    kDextCmdGetNextPDU                   = 12,

    /*! Number of commands. */
    kDextCmdCount
};

#pragma mark - Event Types (DEXT → Daemon)

/*!
 * @enum DextEventType
 * @brief Events sent from the DEXT to the daemon via OSAction or data queue.
 *
 * The DEXT uses OSAction completions to signal the daemon
 * when it has data to send to the target or when events occur.
 */
enum DextEventType {
    /*! DEXT has a PDU ready for TCP transmission.
     *  Payload is in the shared memory data queue. */
    kDextEventPDUReadyForSend            = 0,

    /*! DEXT has completed a SCSI task (for daemon bookkeeping). */
    kDextEventTaskCompleted              = 1,

    /*! DEXT detected a timeout or abort condition. */
    kDextEventTaskAborted                = 2,

    /*! DEXT is requesting that a connection be established. */
    kDextEventRequestConnect             = 3,

    /*! DEXT is requesting that a connection be torn down. */
    kDextEventRequestDisconnect          = 4,

    /*! Number of events. */
    kDextEventCount
};

#pragma mark - Session Parameter Selectors

/*! Session parameter selectors matching iSCSIHBASessionParameters. */
enum DextSessionParam {
    kDextSessionParamDefaultTime2Retain      = 0,
    kDextSessionParamDefaultTime2Wait        = 1,
    kDextSessionParamErrorRecoveryLevel      = 2,
    kDextSessionParamMaxConnections          = 3,
    kDextSessionParamImmediateData           = 4,
    kDextSessionParamInitialR2T              = 5,
    kDextSessionParamDataPDUInOrder          = 6,
    kDextSessionParamDataSequenceInOrder     = 7,
    kDextSessionParamMaxOutstandingR2T       = 8,
    kDextSessionParamMaxBurstLength          = 9,
    kDextSessionParamFirstBurstLength        = 10,
    kDextSessionParamTargetSessionId         = 11,
    kDextSessionParamTargetPortalGroupTag    = 12
};

/*! Connection parameter selectors matching iSCSIHBAConnectionParameters. */
enum DextConnectionParam {
    kDextConnectionParamUseHeaderDigest         = 0,
    kDextConnectionParamUseDataDigest           = 1,
    kDextConnectionParamUseIFMarker             = 2,
    kDextConnectionParamUseOFMarker             = 3,
    kDextConnectionParamOFMarkInt               = 4,
    kDextConnectionParamIFMarkInt               = 5,
    kDextConnectionParamMaxSendDataSegmentLength = 6,
    kDextConnectionParamMaxRecvDataSegmentLength = 7,
    kDextConnectionParamInitialExpStatSN        = 8
};

#pragma mark - Data Structures

/*!
 * @struct DextSessionConfig
 * @brief Configuration for a session, sent from daemon to DEXT
 *        via kDextCmdRegisterSession.
 */
typedef struct __attribute__((packed)) {
    /*! Session identifier (0-15). */
    uint8_t  sessionId;

    /*! Target identifier (maps to SCSI target ID). */
    uint8_t  targetId;

    /*! Target IQN string (null-terminated). */
    char     targetName[256];

    /*! Initial command sequence number. */
    uint32_t initialCmdSN;

    /*! Maximum burst length (bytes). */
    uint32_t maxBurstLength;

    /*! First burst length (bytes). */
    uint32_t firstBurstLength;

    /*! Maximum receive data segment length. */
    uint32_t maxRecvDataSegmentLength;

    /*! Maximum send data segment length. */
    uint32_t maxSendDataSegmentLength;

    /*! Whether immediate data is supported. */
    bool     immediateData;

    /*! Whether initial R2T is used. */
    bool     initialR2T;

    /*! Whether data PDUs are in order. */
    bool     dataPDUInOrder;

    /*! Whether data sequences are in order. */
    bool     dataSequenceInOrder;

    /*! Maximum outstanding R2T count. */
    uint8_t  maxOutstandingR2T;

    /*! Error recovery level. */
    uint8_t  errorRecoveryLevel;
} DextSessionConfig;

/*!
 * @struct DextConnectionConfig
 * @brief Configuration for a connection, sent from daemon to DEXT
 *        via kDextCmdActivateConnection.
 */
typedef struct __attribute__((packed)) {
    /*! Connection identifier (0 or 1). */
    uint32_t connectionId;

    /*! Session this connection belongs to. */
    uint8_t  sessionId;

    /*! Expected status sequence number (from login response). */
    uint32_t initialExpStatSN;

    /*! Whether header digest is enabled. */
    bool     useHeaderDigest;

    /*! Whether data digest is enabled. */
    bool     useDataDigest;
} DextConnectionConfig;

/*!
 * @struct DextIncomingPDU
 * @brief Structure for forwarding a received PDU from daemon to DEXT.
 */
typedef struct __attribute__((packed)) {
    /*! Session this PDU belongs to. */
    uint8_t  sessionId;

    /*! Connection this PDU arrived on. */
    uint32_t connectionId;

    /*! The 48-byte Basic Header Segment. */
    uint8_t  bhs[kIPC_ISCSI_BHS_SIZE];

    /*! Length of the data segment (0 if no data). */
    uint32_t dataLength;

    /*! Offset into the shared memory buffer where data segment resides.
     *  Set to 0 if dataLength is 0. */
    uint64_t dataBufferOffset;
} DextIncomingPDU;

/*!
 * @struct DextOutgoingPDU
 * @brief Structure for sending a PDU from DEXT to daemon for TCP xmission.
 */
typedef struct __attribute__((packed)) {
    /*! Session this PDU belongs to. */
    uint8_t  sessionId;

    /*! Connection to send on. */
    uint32_t connectionId;

    /*! The 48-byte Basic Header Segment. */
    uint8_t  bhs[kIPC_ISCSI_BHS_SIZE];

    /*! Length of data segment to transmit. */
    uint32_t dataLength;

    /*! Offset into shared memory buffer where data segment is located. */
    uint64_t dataBufferOffset;

    /*! Task tag for matching completion. */
    uint32_t initiatorTaskTag;
} DextOutgoingPDU;

/*!
 * @struct DextOutgoingPDUFull
 * @brief Full PDU data returned to daemon via kDextCmdGetNextPDU.
 *
 * Unlike DextOutgoingPDU which references shared memory, this struct
 * carries the data segment inline for direct TCP transmission.
 */
typedef struct __attribute__((packed)) {
    /*! Session this PDU belongs to. */
    uint8_t  sessionId;

    /*! Connection to send on. */
    uint32_t connectionId;

    /*! Task tag for matching completion. */
    uint32_t initiatorTaskTag;

    /*! The 48-byte Basic Header Segment. */
    uint8_t  bhs[kIPC_ISCSI_BHS_SIZE];

    /*! Length of data segment to transmit (0 if no data). */
    uint32_t dataLength;

    /*! Inline data segment (read only up to dataLength bytes). */
    uint8_t  dataSegment[kIPC_MAX_DATA_SEGMENT_SIZE];
} DextOutgoingPDUFull;

/*!
 * @struct DextConnectionStatus
 * @brief Connection status event sent bi-directionally.
 */
typedef struct __attribute__((packed)) {
    uint8_t  sessionId;
    uint32_t connectionId;
    bool     isConnected;
    int      errorCode;
    char     description[64];
} DextConnectionStatus;

#endif /* __ISCSI_DEXT_DAEMON_IPC_SHARED_H__ */
