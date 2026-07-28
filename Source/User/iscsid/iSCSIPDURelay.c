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
 * iSCSIPDURelay.c — Routes iSCSI PDUs between TCP engine and DEXT IPC.
 *
 * The relay has two modes per session:
 *   - Control phase: Login/text/logout PDUs are handled directly by the
 *     daemon, bypassing the DEXT. The relay sends PDUs via TCPEngineSend()
 *     and delivers received data to iSCSISession.c via a callback.
 *   - Full-feature phase: SCSI command/data PDUs are forwarded between the
 *     DEXT (via DextIPC) and the TCP engine (via TCPEngineSend).
 *
 * PDU boundary parsing:
 *   iSCSI PDUs on the wire consist of a 48-byte BHS followed by an optional
 *   data segment (whose length is determined from the BHS fields). The relay
 *   uses a state machine per connection to track partial receives.
 */

#include "iSCSIPDURelay.h"

#include <stdlib.h>
#include <string.h>
#include <asl.h>

#include "iSCSIPDUShared.h"
#include "crc32c_userspace.h"

#pragma mark - Internal State

/*! Per-session relay state. */
typedef struct {
    bool     inUse;
    bool     active;   /* true = full-feature (forward to DEXT) */
    uint8_t  sessionId;
    uint32_t connectionId;

    /*! PDU reassembly state. */
    bool     awaitingBHS;   /* true = expecting BHS, false = expecting data */
    uint32_t expectedDataLen;
    uint32_t recvdDataLen;

    /*! TCP connection context pointer (set by PDURelayRegisterSession). */
    TCPConnectionContext * tcpCtx;

    /*! CRC32C digest flags (set by PDURelaySetDigestFlags after login). */
    bool     useHeaderDigest;
    bool     useDataDigest;
} RelaySessionState;

/*! Global relay state. */
static struct {
    bool initialized;

    /*! DEXT IPC handle (may be NULL if DEXT not loaded). */
    DextIPCRef dextIPC;

    /*! Cached DEXT IPC handle — set via DextIPCCreate(). */
    DextIPCRef ipcHandle;

    /*! Session state array. */
    RelaySessionState sessions[kPDURelayMaxSessions];

    /*! Callback for control-PDU responses. */
    PDURelayResponseCallback responseCallback;

    /*! Opaque context for the callback. */
    void * context;
} gRelay = { false };

#pragma mark - Internal Helpers

static RelaySessionState * find_session(uint8_t sessionId)
{
    for (int i = 0; i < kPDURelayMaxSessions; i++) {
        if (gRelay.sessions[i].inUse &&
            gRelay.sessions[i].sessionId == sessionId) {
            return &gRelay.sessions[i];
        }
    }
    return NULL;
}

static RelaySessionState * alloc_session(uint8_t sessionId)
{
    // First check if it already exists
    RelaySessionState * existing = find_session(sessionId);
    if (existing) return existing;

    // Find free slot
    for (int i = 0; i < kPDURelayMaxSessions; i++) {
        if (!gRelay.sessions[i].inUse) {
            memset(&gRelay.sessions[i], 0, sizeof(RelaySessionState));
            gRelay.sessions[i].inUse = true;
            gRelay.sessions[i].sessionId = sessionId;
            gRelay.sessions[i].awaitingBHS = true;
            gRelay.sessions[i].active = false;
            return &gRelay.sessions[i];
        }
    }

    asl_log(NULL, NULL, ASL_LEVEL_ERR,
            "PDURelay: no free session slots");
    return NULL;
}

/*!
 * Extracts the data segment length from an iSCSI BHS.
 *
 * For SCSI Command PDUs (opcode 0x01), the expected data transfer length
 * is in bytes 5-8 of the BHS (big-endian). For other PDUs, the data
 * segment length is in bytes 20-23 (big-endian, AHS length adjusted).
 *
 * This is a simplified version; full parsing will be in the DEXT's
 * PDU encoder/decoder.
 */
static uint32_t get_data_segment_length(const uint8_t * bhs)
{
    if (!bhs) return 0;

    uint8_t opcode = bhs[0] & 0x3F;  // Opcode mask (remove final/initiator bit)

    switch (opcode) {
        case kiSCSIPDUOpCodeSCSICmd:
            // Expected Data Transfer Length is at BHS[20..23]
            return ((uint32_t)bhs[20] << 24) |
                   ((uint32_t)bhs[21] << 16) |
                   ((uint32_t)bhs[22] << 8)  |
                   ((uint32_t)bhs[23]);

        case kiSCSIPDUOpCodeSCSIRsp:
        case kiSCSIPDUOpCodeDataIn:
        case kiSCSIPDUOpCodeDataOut:
        case kiSCSIPDUOpCodeR2T:
            // Data segment length at BHS[20..23] for these
            return ((uint32_t)bhs[20] << 24) |
                   ((uint32_t)bhs[21] << 16) |
                   ((uint32_t)bhs[22] << 8)  |
                   ((uint32_t)bhs[23]);

        case kiSCSIPDUOpCodeLoginReq:
        case kiSCSIPDUOpCodeLoginRsp:
        case kiSCSIPDUOpCodeTextReq:
        case kiSCSIPDUOpCodeTextRsp:
        case kiSCSIPDUOpCodeLogoutReq:
        case kiSCSIPDUOpCodeLogoutRsp:
        case kiSCSIPDUOpCodeSNACKReq:
        case kiSCSIPDUOpCodeReject:
            // TotalAHSLength + DataSegmentLength at BHS[4..7]
            // BHS[4] = total AHS length in 4-byte words, BHS[5..7] = data seg length
            {
                uint8_t ahsWords = bhs[4];
                uint32_t ahsLen = ahsWords * 4;
                uint32_t dataSegLen = ((uint32_t)bhs[5] << 16) |
                                       ((uint32_t)bhs[6] << 8)  |
                                       ((uint32_t)bhs[7]);
                return ahsLen + dataSegLen;
            }

        case kiSCSIPDUOpCodeNOPOut:
        case kiSCSIPDUOpCodeNOPIn:
        case kiSCSIPDUOpCodeAsyncMsg:
        case kiSCSIPDUOpCodeTaskMgmtReq:
        case kiSCSIPDUOpCodeTaskMgmtRsp:
            // Data segment length at BHS[4..7] (TotalAHSLength is 0 for most)
            {
                uint8_t ahsWords = bhs[4];
                uint32_t ahsLen = ahsWords * 4;
                uint32_t dataSegLen = ((uint32_t)bhs[5] << 16) |
                                       ((uint32_t)bhs[6] << 8)  |
                                       ((uint32_t)bhs[7]);
                return ahsLen + dataSegLen;
            }

        default:
            return 0;
    }
}

#pragma mark - API Implementation

void PDURelayInitialize(DextIPCRef dextIPC,
                         PDURelayResponseCallback callback,
                         void * context)
{
    memset(&gRelay, 0, sizeof(gRelay));
    gRelay.initialized = true;
    gRelay.dextIPC = dextIPC;
    gRelay.ipcHandle = dextIPC;
    gRelay.responseCallback = callback;
    gRelay.context = context;

    asl_log(NULL, NULL, ASL_LEVEL_DEBUG,
            "PDURelay: initialized (DEXT %s)",
            dextIPC ? "connected" : "not connected");
}

errno_t PDURelayRegisterSession(uint8_t sessionId,
                                 TCPConnectionContext * ctx)
{
    if (!gRelay.initialized) return EINVAL;
    if (!ctx) return EINVAL;

    RelaySessionState * state = alloc_session(sessionId);
    if (!state) return ENOMEM;

    state->connectionId = ctx->connectionId;
    state->tcpCtx = ctx;
    state->awaitingBHS = true;
    state->expectedDataLen = 0;
    state->recvdDataLen = 0;
    state->active = false;

    return 0;
}

void PDURelayUnregisterSession(uint8_t sessionId)
{
    RelaySessionState * state = find_session(sessionId);
    if (!state) return;

    state->inUse = false;
    state->tcpCtx = NULL;
}

void PDURelayActivateSession(uint8_t sessionId)
{
    RelaySessionState * state = find_session(sessionId);
    if (!state) {
        asl_log(NULL, NULL, ASL_LEVEL_WARNING,
                "PDURelay: cannot activate unknown session %u",
                (unsigned)sessionId);
        return;
    }

    state->active = true;
    state->awaitingBHS = true;
    state->expectedDataLen = 0;
    state->recvdDataLen = 0;

    asl_log(NULL, NULL, ASL_LEVEL_DEBUG,
            "PDURelay: session %u activated (full-feature forwarding)",
            (unsigned)sessionId);
}

void PDURelaySetDigestFlags(uint8_t sessionId,
                             bool headerDigest,
                             bool dataDigest)
{
    RelaySessionState * state = find_session(sessionId);
    if (!state) return;

    state->useHeaderDigest = headerDigest;
    state->useDataDigest = dataDigest;

    asl_log(NULL, NULL, ASL_LEVEL_DEBUG,
            "PDURelay: session %u digest flags header=%d data=%d",
            (unsigned)sessionId, (int)headerDigest, (int)dataDigest);
}

/*! Returns the number of padding bytes for a given data segment length. */
static inline uint32_t padding_len(uint32_t dataLen)
{
    return (4 - (dataLen & 3)) & 3;
}

TCPConnectionContext * PDURelayGetTCPContext(uint8_t sessionId)
{
    RelaySessionState * state = find_session(sessionId);
    if (!state || !state->tcpCtx) return NULL;
    return state->tcpCtx;
}

errno_t PDURelayHandleTCPEvent(uint8_t sessionId, uint32_t events)
{
    if (!gRelay.initialized) return EINVAL;

    RelaySessionState * state = find_session(sessionId);
    if (!state || !state->tcpCtx) {
        return ENOENT;
    }

    if (events & kTCPEventConnected) {
        asl_log(NULL, NULL, ASL_LEVEL_DEBUG,
                "PDURelay: session %u TCP connected", (unsigned)sessionId);
    }

    if (events & kTCPEventDisconnected) {
        asl_log(NULL, NULL, ASL_LEVEL_DEBUG,
                "PDURelay: session %u TCP disconnected", (unsigned)sessionId);
        state->awaitingBHS = true;

        // Notify DEXT of disconnection
        if (gRelay.ipcHandle && state->active) {
            DextConnectionStatus status;
            memset(&status, 0, sizeof(status));
            status.sessionId = sessionId;
            status.connectionId = state->connectionId;
            status.isConnected = false;
            status.errorCode = ECONNRESET;
            snprintf(status.description, sizeof(status.description),
                     "TCP connection lost");
            DextIPCConnectionStatus(gRelay.ipcHandle, &status);
        }
    }

    if (events & kTCPEventConnectFailed) {
        asl_log(NULL, NULL, ASL_LEVEL_WARNING,
                "PDURelay: session %u TCP connect failed",
                (unsigned)sessionId);
    }

    if (events & kTCPEventDataAvailable) {
        return PDURelayProcessReceivedData(state->tcpCtx);
    }

    if (events & kTCPEventCanSend) {
        // Send buffer drained — DEXT may queue more data
        // (handled by the DEXT outgoingPDU callback)
    }

    return 0;
}

/*! Helper: send 4-byte CRC32C digest via TCP. */
static errno_t send_crc_digest(TCPConnectionContext * ctx,
                                 uint32_t crc)
{
    uint8_t dig[4];
    dig[0] = (uint8_t)(crc >> 24);
    dig[1] = (uint8_t)(crc >> 16);
    dig[2] = (uint8_t)(crc >> 8);
    dig[3] = (uint8_t)(crc);
    return TCPEngineSend(ctx, dig, 4);
}

errno_t PDURelaySendControlPDU(uint8_t sessionId,
                                const uint8_t * bhs,
                                const uint8_t * data,
                                uint32_t dataLen)
{
    if (!gRelay.initialized) return EINVAL;
    if (!bhs) return EINVAL;

    RelaySessionState * state = find_session(sessionId);
    if (!state || !state->tcpCtx) {
        return ENOENT;
    }

    TCPConnectionContext * ctx = state->tcpCtx;

    // Send BHS first
    errno_t err = TCPEngineSend(ctx, bhs, kIPC_ISCSI_BHS_SIZE);
    if (err) return err;

    // Send header digest if enabled (CRC32C of BHS)
    if (state->useHeaderDigest) {
        uint32_t crc = crc32c(0, bhs, kIPC_ISCSI_BHS_SIZE);
        err = send_crc_digest(ctx, crc);
        if (err) return err;
    }

    // Send data segment if present
    if (data && dataLen > 0) {
        err = TCPEngineSend(ctx, data, dataLen);
        if (err) {
            asl_log(NULL, NULL, ASL_LEVEL_ERR,
                    "PDURelay: session %u send data failed: %d",
                    (unsigned)sessionId, err);
            return err;
        }

        // Send padding
        uint32_t padLen = (4 - (dataLen & 3)) & 3;
        if (padLen > 0) {
            static const uint8_t zeroPad[4] = {0, 0, 0, 0};
            err = TCPEngineSend(ctx, zeroPad, padLen);
            if (err) return err;
        }

        // Send data digest if enabled (CRC32C of data segment)
        if (state->useDataDigest) {
            uint32_t crc = crc32c(0, data, dataLen);
            err = send_crc_digest(ctx, crc);
            if (err) return err;
        }
    }
    else if (state->useDataDigest) {
        // Empty data segment still gets a data digest if negotiated
        uint32_t crc = crc32c(0, NULL, 0);
        err = send_crc_digest(ctx, crc);
        if (err) return err;
    }

    return 0;
}

/*! Verifies a CRC32C digest against computed value.
 *  @return true if digest matches, false on mismatch. */
static bool verify_crc32c(uint32_t expectedCRC,
                           const void * data,
                           size_t length)
{
    uint32_t computed = crc32c(0, data, length);
    return computed == expectedCRC;
}

errno_t PDURelayProcessReceivedData(TCPConnectionContext * ctx)
{
    if (!gRelay.initialized) return EINVAL;
    if (!ctx) return EINVAL;

    // Find the session for this context
    RelaySessionState * state = NULL;
    for (int i = 0; i < kPDURelayMaxSessions; i++) {
        if (gRelay.sessions[i].inUse &&
            gRelay.sessions[i].tcpCtx == ctx) {
            state = &gRelay.sessions[i];
            break;
        }
    }
    if (!state) {
        // Unknown connection — data received but no session registered
        ctx->recvLen = 0; // Discard
        return ENOENT;
    }

    uint8_t * buf = ctx->recvBuf;
    size_t bufLen = ctx->recvLen;

    while (bufLen > 0) {
        if (state->awaitingBHS) {
            // Need at least 48 bytes for a BHS
            if (bufLen < kIPC_ISCSI_BHS_SIZE) {
                break; // Wait for more data
            }

            // Extract data segment length from BHS
            uint32_t dataSegLen = get_data_segment_length(buf);
            if (dataSegLen > 256 * 1024) {
                asl_log(NULL, NULL, ASL_LEVEL_ERR,
                        "PDURelay: suspicious data segment length %u, truncating",
                        (unsigned)dataSegLen);
                dataSegLen = 256 * 1024;
            }

            state->expectedDataLen = dataSegLen;
            state->recvdDataLen = 0;

            if (dataSegLen == 0) {
                // PDU without data segment.
                // Wire size: BHS(48) + [hdrDigest(4)] + [dataDigest(4)]
                uint32_t hdrDig = state->useHeaderDigest ? 4U : 0;
                uint32_t datDig = state->useDataDigest  ? 4U : 0;
                uint32_t wireSize = kIPC_ISCSI_BHS_SIZE + hdrDig + datDig;

                if (bufLen < wireSize) {
                    break; // Wait for more data
                }

                uint8_t bhs[kIPC_ISCSI_BHS_SIZE];
                memcpy(bhs, buf, kIPC_ISCSI_BHS_SIZE);

                // Verify header digest if enabled
                if (state->useHeaderDigest) {
                    uint32_t wireCRC = ((uint32_t)buf[48] << 24) |
                                       ((uint32_t)buf[49] << 16) |
                                       ((uint32_t)buf[50] << 8)  |
                                       ((uint32_t)buf[51]);
                    if (!verify_crc32c(wireCRC, bhs, kIPC_ISCSI_BHS_SIZE)) {
                        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                                "PDURelay: header CRC mismatch on session %u",
                                (unsigned)state->sessionId);
                        // Skip this PDU to try to re-sync
                        buf += wireSize;
                        bufLen -= wireSize;
                        state->awaitingBHS = true;
                        continue;
                    }
                }

                // Data digest with no data is just the CRC of an empty segment
                if (state->useDataDigest) {
                    uint32_t digOff = kIPC_ISCSI_BHS_SIZE + hdrDig;
                    uint32_t wireCRC = ((uint32_t)buf[digOff] << 24) |
                                       ((uint32_t)buf[digOff+1] << 16) |
                                       ((uint32_t)buf[digOff+2] << 8)  |
                                       ((uint32_t)buf[digOff+3]);
                    if (!verify_crc32c(wireCRC, NULL, 0)) {
                        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                                "PDURelay: data CRC mismatch (empty) on session %u",
                                (unsigned)state->sessionId);
                        buf += wireSize;
                        bufLen -= wireSize;
                        state->awaitingBHS = true;
                        continue;
                    }
                }

                buf += wireSize;
                bufLen -= wireSize;
                state->awaitingBHS = true;

                if (state->active && gRelay.ipcHandle) {
                    DextIncomingPDU pdu;
                    memset(&pdu, 0, sizeof(pdu));
                    pdu.sessionId = state->sessionId;
                    pdu.connectionId = state->connectionId;
                    memcpy(pdu.bhs, bhs, kIPC_ISCSI_BHS_SIZE);
                    pdu.dataLength = 0;
                    DextIPCForwardIncomingPDU(gRelay.ipcHandle, &pdu, NULL);
                } else if (gRelay.responseCallback) {
                    gRelay.responseCallback(state->sessionId,
                                            state->connectionId,
                                            bhs, 0, NULL,
                                            gRelay.context);
                }
            }
            else {
                // PDU has a data segment — start accumulating.
                // After BHS, there may be a header digest before the data.
                uint32_t hdrDigOffset = state->useHeaderDigest ? 4U : 0;
                uint32_t minWireBeforeData = kIPC_ISCSI_BHS_SIZE + hdrDigOffset;

                if (bufLen < minWireBeforeData) {
                    break; // Wait for enough data to reach the data segment
                }

                // Verify header digest if enabled
                if (state->useHeaderDigest) {
                    uint32_t wireCRC = ((uint32_t)buf[48] << 24) |
                                       ((uint32_t)buf[49] << 16) |
                                       ((uint32_t)buf[50] << 8)  |
                                       ((uint32_t)buf[51]);
                    if (!verify_crc32c(wireCRC, buf, kIPC_ISCSI_BHS_SIZE)) {
                        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                                "PDURelay: header CRC mismatch on session %u",
                                (unsigned)state->sessionId);
                        // Skip entire PDU — need rough size: BHS(48) + hdrDig(4)
                        // + dataSegLen + pad + dataDig. Skip minimum to re-sync.
                        size_t skip = kIPC_ISCSI_BHS_SIZE + hdrDigOffset
                                    + state->expectedDataLen
                                    + padding_len(state->expectedDataLen)
                                    + (state->useDataDigest ? 4U : 0);
                        if (skip > bufLen) skip = bufLen;
                        buf += skip;
                        bufLen -= skip;
                        state->awaitingBHS = true;
                        continue;
                    }
                }

                state->awaitingBHS = false;
                // Data segment starts after BHS + optional header digest
                // The recvdDataLen tracks raw data bytes only (no CRC, no padding)
                // We keep buf pointing at BHS start for the accumulation loop.
            }
        }
        else {
            // Accumulating data segment
            uint32_t remaining = state->expectedDataLen - state->recvdDataLen;
            size_t toCopy = (bufLen < remaining) ? bufLen : remaining;

            state->recvdDataLen += (uint32_t)toCopy;

            buf += toCopy;
            bufLen -= toCopy;

            if (state->recvdDataLen >= state->expectedDataLen) {
                // Full data segment received.
                // Compute offsets relative to original BHS start.
                uint32_t hdrDig = state->useHeaderDigest ? 4U : 0;
                uint32_t pad = padding_len(state->expectedDataLen);
                uint32_t datDig = state->useDataDigest ? 4U : 0;

                // Data segment starts after BHS + header digest
                uint32_t dataOff = kIPC_ISCSI_BHS_SIZE + hdrDig;
                // Total bytes consumed from the recv buffer
                uint32_t consumed = dataOff
                                  + state->expectedDataLen
                                  + pad
                                  + datDig;

                // Need total wire bytes in buffer to verify digests
                size_t bufTotal = ctx->recvLen;
                if (consumed > bufTotal) {
                    // Not enough data for padding + digest yet
                    break;
                }

                uint8_t bhs[kIPC_ISCSI_BHS_SIZE];
                memcpy(bhs, ctx->recvBuf, kIPC_ISCSI_BHS_SIZE);
                uint8_t * dataSeg = ctx->recvBuf + dataOff;

                // Verify data digest if enabled
                if (state->useDataDigest) {
                    uint32_t digOff = dataOff + state->expectedDataLen + pad;
                    uint32_t wireCRC = ((uint32_t)ctx->recvBuf[digOff] << 24) |
                                       ((uint32_t)ctx->recvBuf[digOff+1] << 16) |
                                       ((uint32_t)ctx->recvBuf[digOff+2] << 8)  |
                                       ((uint32_t)ctx->recvBuf[digOff+3]);
                    if (!verify_crc32c(wireCRC, dataSeg, state->expectedDataLen)) {
                        asl_log(NULL, NULL, ASL_LEVEL_ERR,
                                "PDURelay: data CRC mismatch on session %u (len=%u)",
                                (unsigned)state->sessionId,
                                (unsigned)state->expectedDataLen);
                        // Skip corrupted PDU
                        if (consumed < bufTotal) {
                            memmove(ctx->recvBuf, ctx->recvBuf + consumed,
                                    bufTotal - consumed);
                        }
                        ctx->recvLen = (uint32_t)(bufTotal - consumed);
                        state->awaitingBHS = true;
                        state->expectedDataLen = 0;
                        state->recvdDataLen = 0;
                        return 0;
                    }
                }

                if (state->active && gRelay.ipcHandle) {
                    DextIncomingPDU pdu;
                    memset(&pdu, 0, sizeof(pdu));
                    pdu.sessionId = state->sessionId;
                    pdu.connectionId = state->connectionId;
                    memcpy(pdu.bhs, bhs, kIPC_ISCSI_BHS_SIZE);
                    pdu.dataLength = state->expectedDataLen;
                    pdu.dataBufferOffset = 0;
                    DextIPCForwardIncomingPDU(gRelay.ipcHandle, &pdu, dataSeg);
                } else if (gRelay.responseCallback) {
                    gRelay.responseCallback(state->sessionId,
                                            state->connectionId,
                                            bhs,
                                            state->expectedDataLen,
                                            dataSeg,
                                            gRelay.context);
                }

                // Remove consumed bytes from recv buffer
                if (consumed < bufTotal) {
                    memmove(ctx->recvBuf, ctx->recvBuf + consumed,
                            bufTotal - consumed);
                }
                ctx->recvLen = (uint32_t)(bufTotal - consumed);

                state->awaitingBHS = true;
                state->expectedDataLen = 0;
                state->recvdDataLen = 0;

                // Buffer was modified — let next callback process remaining data
                return 0;
            }
        }
    }

    // If we consumed everything, reset the buffer
    if (bufLen == 0) {
        ctx->recvLen = 0;
    } else if (buf != ctx->recvBuf) {
        // We partially consumed — compact the buffer
        size_t consumed = buf - ctx->recvBuf;
        if (consumed > 0) {
            memmove(ctx->recvBuf, buf, bufLen);
            ctx->recvLen = (uint32_t)bufLen;
        }
    }

    return 0;
}
