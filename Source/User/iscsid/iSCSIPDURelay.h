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
 * iSCSIPDURelay.h — Routes iSCSI PDUs between the TCP engine and DEXT IPC.
 *
 * In the new architecture (dext replaces kext), the daemon handles all TCP
 * socket I/O directly. Login/text/logout PDUs are sent/recv'd by the daemon
 * itself. Once a session enters full-feature phase, SCSI command/data PDUs
 * flow between the DEXT (which encodes/decodes them) and the TCP engine
 * (which sends/recvs them on the wire).
 *
 * This relay module bridges the two:
 *   - DEXT→TCP: The DEXT produces DextOutgoingPDU entries in the shared
 *     memory data queue. The relay reads them and calls TCPEngineSend().
 *   - TCP→DEXT: The TCP engine's callback delivers received data. The relay
 *     parses BHS boundaries, reassembles PDUs, and calls
 *     DextIPCForwardIncomingPDU().
 *
 * For login/text/logout PDUs, the relay provides direct send/recv functions
 * that bypass the DEXT entirely (the daemon handles these protocol phases
 * itself, as it does today).
 */

#ifndef __ISCSI_PDU_RELAY_H__
#define __ISCSI_PDU_RELAY_H__

#include <stdint.h>
#include <stdbool.h>

#include "iSCSITCPEngine.h"
#include "iSCSIDextIPC.h"
#include "../../Shared/DextDaemonIPC/iSCSIDextDaemonIPCShared.h"

#pragma mark - Session Tracking

/*!
 * @brief Max number of sessions tracked by the PDU relay.
 */
#define kPDURelayMaxSessions    kIPC_MAX_SESSIONS

/*!
 * @brief Session state tracked by the relay.
 */
typedef struct {
    /*! Whether this slot is in use. */
    bool inUse;

    /*! iSCSI session ID. */
    uint8_t sessionId;

    /*! TCP connection context for this session's data connection. */
    TCPConnectionContext tcpCtx;

    /*! The relay function processes received data into PDUs.
     *  Points to the relevant recv buffer in tcpCtx. */
} PDURelaySession;

#pragma mark - Callback for Login/Text/Logout PDUs

/*!
 * @brief Callback invoked when a PDU response arrives on a session's
 *        TCP connection during login/text/logout phases.
 *
 * The daemon uses this to receive login responses, text negotiation
 * responses, and logout responses. Once the session is in full-feature
 * phase, PDUs are forwarded to the DEXT instead.
 *
 * @param sessionId   The session the PDU belongs to.
 * @param connectionId The connection.
 * @param bhs         The 48-byte Basic Header Segment.
 * @param dataLen     Length of the data segment (may be 0).
 * @param data        Pointer to data segment payload (may be NULL).
 * @param context     Opaque user context.
 */
typedef void (*PDURelayResponseCallback)(
    uint8_t sessionId,
    uint32_t connectionId,
    const uint8_t * bhs,
    uint32_t dataLen,
    const uint8_t * data,
    void * context);

#pragma mark - API

/*!
 * @brief Initialize the PDU relay module.
 *
 * Must be called once before any other PDURelay functions.
 *
 * @param dextIPC    An initialized DEXT IPC handle (may be NULL if DEXT is
 *                   not yet available).
 * @param callback   Callback for PDU responses during login/text/logout.
 * @param context    Opaque context for the callback.
 */
void PDURelayInitialize(DextIPCRef dextIPC,
                         PDURelayResponseCallback callback,
                         void * context);

/*!
 * @brief Register a session/TCP connection pair with the relay.
 *
 * Called after a new TCP connection is established for an iSCSI session.
 *
 * @param sessionId  The iSCSI session ID.
 * @param ctx        The TCP connection context for this session's data path.
 * @return 0 on success, or an errno value.
 */
errno_t PDURelayRegisterSession(uint8_t sessionId,
                                 TCPConnectionContext * ctx);

/*!
 * @brief Unregister a session from the relay.
 * @param sessionId  The session ID to remove.
 */
void PDURelayUnregisterSession(uint8_t sessionId);

/*!
 * @brief Activate full-feature phase forwarding for a session.
 *
 * After login negotiation completes, call this to switch the session from
 * "daemon handles PDUs directly" to "PDUs forwarded between DEXT and TCP".
 *
 * @param sessionId  The session to activate.
 */
void PDURelayActivateSession(uint8_t sessionId);

/*!
 * @brief Handle a TCP event on a session's data connection.
 *
 * Called from the TCP engine callback when socket events occur on a
 * connection that belongs to a registered session.
 *
 * For sessions in login/text/logout phase, received data is reassembled
 * into PDUs and passed to the response callback.
 *
 * For sessions in full-feature phase, received data is reassembled into
 * PDUs and forwarded to the DEXT via DextIPCForwardIncomingPDU().
 *
 * @param sessionId  The session ID.
 * @param events     Bitmask of TCPEngineEventType.
 * @return 0 on success, or an errno value.
 */
errno_t PDURelayHandleTCPEvent(uint8_t sessionId,
                                uint32_t events);

/*!
 * @brief Get the TCP connection context for a session, or NULL if not found.
 *
 * Used by the daemon's DEXT outgoing PDU handler to send PDUs on the
 * correct TCP connection without duplicating the session↔TCP mapping.
 *
 * @param sessionId  The iSCSI session ID.
 * @return The TCPConnectionContext pointer, or NULL if session not found.
 */
TCPConnectionContext * PDURelayGetTCPContext(uint8_t sessionId);

/*!
 * @brief Send a login/text/logout PDU directly via TCP.
 *
 * Bypasses the DEXT — used during iSCSI login negotiation.
 *
 * @param sessionId  The session.
 * @param bhs        The 48-byte BHS.
 * @param data       Data segment (may be NULL).
 * @param dataLen    Data segment length.
 * @return 0 on success, or an errno value.
 */
errno_t PDURelaySendControlPDU(uint8_t sessionId,
                                const uint8_t * bhs,
                                const uint8_t * data,
                                uint32_t dataLen);

/*!
 * @brief Process incoming data from the TCP engine callback.
 *
 * Reads data from the connection context's recv buffer, parses PDU
 * boundaries (BHS + optional data segment), and dispatches to either
 * the response callback or the DEXT IPC.
 *
 * @param ctx  The TCP connection context (from the event callback).
 * @return 0 on success, or an errno value.
 */
errno_t PDURelayProcessReceivedData(TCPConnectionContext * ctx);

/*!
 * @brief Set digest negotiation flags for a session.
 *
 * Called after login negotiation to inform the relay that PDUs on this
 * session carry CRC32C header and/or data digests.  When set, the relay
 * verifies digests on received PDUs before forwarding them, and the
 * daemon's send path appends digests to outgoing PDUs.
 *
 * Both flags default to false at session registration time.
 *
 * @param sessionId   The iSCSI session ID.
 * @param headerDigest  true if HeaderDigest=CRC32C was negotiated.
 * @param dataDigest    true if DataDigest=CRC32C was negotiated.
 */
void PDURelaySetDigestFlags(uint8_t sessionId,
                             bool headerDigest,
                             bool dataDigest);

#endif /* __ISCSI_PDU_RELAY_H__ */
