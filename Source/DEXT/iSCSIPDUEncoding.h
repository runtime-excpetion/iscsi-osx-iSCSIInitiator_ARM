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
 * iSCSIPDUEncoding.h — iSCSI PDU encoding/decoding for the DEXT data path.
 *
 * Provides C functions for constructing and parsing iSCSI PDUs.
 * This module handles the data-path PDUs only (SCSI Command, Data-In/Out,
 * R2T, NOP-Out/In, Task Management). Login/Text/Logout PDUs are handled
 * by the daemon's iSCSIPDUUser.c.
 *
 * All functions are pure C with no DriverKit dependencies, so this header
 * can be included by both the DEXT (C++) and daemon (C) when needed.
 */

#ifndef __ISCSI_PDU_ENCODING_H__
#define __ISCSI_PDU_ENCODING_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#pragma mark - iSCSI Opcodes (from iSCSIPDUShared.h, inlined to avoid MacTypes.h dependency)

/*! Byte size of the data segment length field in all iSCSI PDUs. */
#define kPDUEncodeDataSegmentLengthSize 3

/*! Each PDU must be a multiple of 4 bytes. */
#define kPDUEncodeByteAlignment 4

/*! Initiator opcodes. */
enum {
    kPDUEncodeOpCodeNOPOut     = 0x00,
    kPDUEncodeOpCodeSCSICmd    = 0x01,
    kPDUEncodeOpCodeTaskMgmtReq = 0x02,
    kPDUEncodeOpCodeLoginReq   = 0x03,
    kPDUEncodeOpCodeTextReq    = 0x04,
    kPDUEncodeOpCodeDataOut    = 0x05,
    kPDUEncodeOpCodeLogoutReq  = 0x06,
    kPDUEncodeOpCodeSNACKReq   = 0x10,
};

/*! Target opcodes. */
enum {
    kPDUEncodeOpCodeNOPIn      = 0x20,
    kPDUEncodeOpCodeSCSIRsp    = 0x21,
    kPDUEncodeOpCodeTaskMgmtRsp = 0x22,
    kPDUEncodeOpCodeLoginRsp   = 0x23,
    kPDUEncodeOpCodeTextRsp    = 0x24,
    kPDUEncodeOpCodeDataIn     = 0x25,
    kPDUEncodeOpCodeLogoutRsp  = 0x26,
    kPDUEncodeOpCodeR2T        = 0x31,
    kPDUEncodeOpCodeAsyncMsg   = 0x32,
    kPDUEncodeOpCodeReject     = 0x3F,
};

#pragma mark - Constants

/*! Size of a standard SCSI CDB. */
#define kPDUEncodeCDBSize  16

/*! Size of the iSCSI digest field (CRC32C). */
#define kPDUEncodeDigestSize 4

/*! SCSI command flags for the BHS second byte. */
enum SCSICmdFlags {
    kSCSICmdFlagNoUnsolicitedData = 0x80,
    kSCSICmdFlagRead              = 0x40,
    kSCSICmdFlagWrite             = 0x20,
};

/*! SCSI task attributes (encoded in BHS flags). */
enum SCSICmdTaskAttribute {
    kSCSICmdTaskAttrUntagged = 0x00,
    kSCSICmdTaskAttrSimple   = 0x01,
    kSCSICmdTaskAttrOrdered  = 0x02,
    kSCSICmdTaskAttrHead     = 0x03,
    kSCSICmdTaskAttrACA      = 0x04,
};

/*! Data-In PDU flag bits (second byte). */
enum DataInFlags {
    kDataInFlagFinal  = 0x80,
    kDataInFlagAck    = 0x40,
    kDataInFlagStatus = 0x01,
};

/*! Data-Out PDU flag bits (second byte). */
enum DataOutFlags {
    kDataOutFlagFinal = 0x80,
};

/*! Task management function codes (second byte of task mgmt req). */
enum TaskMgmtFunction {
    kTaskMgmtFuncAbortTask      = 0x01,
    kTaskMgmtFuncAbortTaskSet   = 0x02,
    kTaskMgmtFuncClearACA       = 0x03,
    kTaskMgmtFuncClearTaskSet   = 0x04,
    kTaskMgmtFuncLUNReset       = 0x05,
    kTaskMgmtFuncTargetWarmReset = 0x06,
    kTaskMgmtFuncTargetColdReset = 0x07,
    kTaskMgmtFuncTaskReassign   = 0x08,
};

/*! Task management response codes. */
enum TaskMgmtResponse {
    kTaskMgmtRspFuncComplete      = 0x00,
    kTaskMgmtRspInvalidTask       = 0x01,
    kTaskMgmtRspInvalidLUN        = 0x02,
    kTaskMgmtRspTaskAllegiant     = 0x03,
    kTaskMgmtRspFuncUnsupported   = 0x05,
    kTaskMgmtRspAuthFail          = 0x06,
    kTaskMgmtRspFuncRejected      = 0xFF,
};

/*! SCSI Response PDU response codes (byte 2). */
enum SCSIResponseCode {
    kSCSIRspCmdCompleted   = 0x00,
    kSCSIRspCmdTargetFailure = 0x01,
};

/*! Reserved target transfer tag value. */
#define kPDUEncodeReservedTargetTransferTag 0xFFFFFFFF

/*! Reserved initiator task tag value. */
#define kPDUEncodeReservedInitiatorTaskTag 0xFFFFFFFF

/*! Immediate delivery flag bit in opcode byte. */
#define kPDUEncodeImmediateDeliveryFlag 0x40

/*! iSCSI PDU alignment (4-byte boundary). */
#define kPDUEncodeByteAlignment 4

/*! Reserved flag value used in NOP-Out. */
#define kPDUEncodeNOPOutReservedFlag 0x80

#pragma mark - PDU Structure Overlays

/*!
 * @struct SCSICmdBHS
 * @brief Memory overlay for a SCSI Command PDU BHS.
 *
 * Map this onto the first 48 bytes of a send buffer when constructing
 * a SCSI Command PDU. Set fields directly in network byte order where
 * appropriate (all multi-byte iSCSI fields are big-endian on wire).
 */
typedef struct __attribute__((packed)) {
    uint8_t  opCode;              // kiSCSIPDUOpCodeSCSICmd | Immediate
    uint8_t  flags;               // Read/Write, task attribute
    uint16_t reserved;
    uint8_t  totalAHSLength;
    uint8_t  dataSegmentLength[3]; // Always 0 for SCSI cmd (immediate data in data seg)
    uint64_t LUN;
    uint32_t initiatorTaskTag;
    uint32_t expectedDataTransferLength; // Big-endian on wire
    uint32_t cmdSN;
    uint32_t expStatSN;
    uint8_t  CDB[16];
} PDUEncodingSCSICmdBHS;

/*!
 * @struct DataInBHS
 * @brief Memory overlay for a Data-In PDU BHS.
 */
typedef struct __attribute__((packed)) {
    uint8_t  opCode;
    uint8_t  flags;               // Final, Ack, Status, reserved
    uint8_t  reserved;
    uint8_t  status;              // Valid when flags & kDataInFlagStatus
    uint8_t  totalAHSLength;
    uint8_t  dataSegmentLength[3];
    uint64_t LUN;
    uint32_t initiatorTaskTag;
    uint32_t targetTransferTag;
    uint32_t statSN;
    uint32_t expCmdSN;
    uint32_t maxCmdSN;
    uint32_t dataSN;
    uint32_t bufferOffset;
    uint32_t residualCount;
} PDUEncodingDataInBHS;

/*!
 * @struct DataOutBHS
 * @brief Memory overlay for a Data-Out PDU BHS.
 */
typedef struct __attribute__((packed)) {
    uint8_t  opCode;
    uint8_t  flags;
    uint16_t reserved;
    uint8_t  totalAHSLength;
    uint8_t  dataSegmentLength[3];
    uint64_t LUN;
    uint32_t initiatorTaskTag;
    uint32_t targetTransferTag;
    uint32_t reserved2;
    uint32_t expStatSN;
    uint32_t reserved3;
    uint32_t dataSN;
    uint32_t bufferOffset;
    uint32_t reserved4;
} PDUEncodingDataOutBHS;

/*!
 * @struct SCSIRspBHS
 * @brief Memory overlay for a SCSI Response PDU BHS.
 */
typedef struct __attribute__((packed)) {
    uint8_t  opCode;
    uint8_t  flags;
    uint8_t  response;            // 0=command completed, 1=target failure
    uint8_t  status;              // SCSI status (0=GOOD, 2=CHECK CONDITION)
    uint8_t  totalAHSLength;
    uint8_t  dataSegmentLength[3];
    uint64_t reserved2;
    uint32_t initiatorTaskTag;
    uint32_t SNACKTag;
    uint32_t statSN;
    uint32_t expCmdSN;
    uint32_t maxCmdSN;
    uint32_t expDataSN;
    uint32_t biReadResidualCount;
    uint32_t residualCount;
} PDUEncodingSCSIRspBHS;

/*!
 * @struct R2TBHS
 * @brief Memory overlay for an R2T PDU BHS.
 */
typedef struct __attribute__((packed)) {
    uint8_t  opCode;
    uint8_t  flags;
    uint16_t reserved;
    uint8_t  totalAHSLength;
    uint8_t  dataSegmentLength[3];
    uint64_t LUN;
    uint32_t initiatorTaskTag;
    uint32_t targetTransferTag;
    uint32_t statSN;
    uint32_t expCmdSN;
    uint32_t maxCmdSN;
    uint32_t R2TSN;
    uint32_t bufferOffset;
    uint32_t desiredDataTransferLength;
} PDUEncodingR2TBHS;

/*!
 * @struct NOPOutBHS
 * @brief Memory overlay for a NOP-Out PDU BHS.
 */
typedef struct __attribute__((packed)) {
    uint8_t  opCode;
    uint8_t  reserved;
    uint8_t  reserved2;
    uint8_t  reserved3;
    uint8_t  totalAHSLength;
    uint8_t  dataSegmentLength[3];
    uint64_t LUN;
    uint32_t initiatorTaskTag;
    uint32_t targetTransferTag;
    uint32_t cmdSN;
    uint32_t expStatSN;
    uint64_t reserved4;
    uint64_t reserved5;
} PDUEncodingNOPOutBHS;

/*!
 * @struct TaskMgmtReqBHS
 * @brief Memory overlay for a Task Management Request PDU BHS.
 */
typedef struct __attribute__((packed)) {
    uint8_t  opCode;
    uint8_t  function;
    uint16_t reserved;
    uint8_t  totalAHSLength;
    uint8_t  dataSegmentLength[3];
    uint64_t LUN;
    uint32_t initiatorTaskTag;
    uint32_t referencedTaskTag;
    uint32_t cmdSN;
    uint32_t expStatSN;
    uint32_t refCmdSN;
    uint32_t expDataSN;
    uint64_t reserved2;
} PDUEncodingTaskMgmtReqBHS;

#pragma mark - Byte Order Helpers

/*! Convert uint32 from host to big-endian (network byte order). */
static inline uint32_t pdu_encode_hton32(uint32_t val)
{
#if defined(__LITTLE_ENDIAN__) || defined(__LITTLE_ENDIAN)
    return __builtin_bswap32(val);
#else
    return val;
#endif
}

/*! Convert uint32 from big-endian to host byte order. */
static inline uint32_t pdu_encode_ntoh32(uint32_t val)
{
    return pdu_encode_hton32(val);
}

/*! Convert uint64 from host to big-endian. */
static inline uint64_t pdu_encode_hton64(uint64_t val)
{
#if defined(__LITTLE_ENDIAN__) || defined(__LITTLE_ENDIAN)
    return __builtin_bswap64(val);
#else
    return val;
#endif
}

/*! Convert uint64 from big-endian to host. */
static inline uint64_t pdu_encode_ntoh64(uint64_t val)
{
    return pdu_encode_hton64(val);
}

/*! Pack a 3-byte big-endian data segment length into a uint8_t[3] field. */
static inline void pdu_encode_data_length(uint8_t field[3], uint32_t length)
{
    field[0] = (uint8_t)((length >> 16) & 0xFF);
    field[1] = (uint8_t)((length >> 8) & 0xFF);
    field[2] = (uint8_t)(length & 0xFF);
}

/*! Unpack a 3-byte big-endian field into a uint32_t length. */
static inline uint32_t pdu_decode_data_length(const uint8_t field[3])
{
    return ((uint32_t)field[0] << 16) |
           ((uint32_t)field[1] << 8)  |
           ((uint32_t)field[2]);
}

/*! Compute padded length: round up to next multiple of 4. */
static inline uint32_t pdu_padded_length(uint32_t length)
{
    return (length + 3) & ~3;
}

#pragma mark - SCSI Command PDU Construction

/*!
 * Build a SCSI Command PDU Basic Header Segment.
 *
 * @param bhs           Output buffer (must be at least 48 bytes).
 * @param LUN           Logical Unit Number (use encoded format per RFC 3720).
 * @param taskTag       Initiator task tag (unique per pending command).
 * @param dataLength    Expected data transfer length (0 for no data).
 * @param isRead        true = read, false = write (if dataLength>0).
 * @param cmdSN         Command sequence number for this session.
 * @param expStatSN     Expected status sequence number.
 * @param cdb           SCSI CDB (16 bytes).
 * @param taskAttr      Task attribute (simple, ordered, head, ACA).
 * @param immediateData true if immediate data follows the BHS.
 */
void pdu_build_scsi_cmd(uint8_t bhs[kPDUEncodeCDBSize * 3],
                        uint64_t LUN,
                        uint32_t taskTag,
                        uint32_t dataLength,
                        bool isRead,
                        uint32_t cmdSN,
                        uint32_t expStatSN,
                        const uint8_t cdb[16],
                        uint8_t taskAttr,
                        bool immediateData);

#pragma mark - Data-Out PDU Construction

/*!
 * Build a Data-Out PDU Basic Header Segment.
 *
 * @param bhs           Output buffer (48 bytes).
 * @param LUN           Logical Unit Number.
 * @param taskTag       Initiator task tag (matching the SCSI command).
 * @param targetTag     Target transfer tag (from R2T).
 * @param dataSN        Data sequence number.
 * @param bufferOffset  Offset into the data buffer.
 * @param dataLength    Length of data segment in this PDU.
 * @param expStatSN     Expected status sequence number.
 * @param isFinal       true if this is the last (or only) Data-Out.
 */
void pdu_build_data_out(uint8_t bhs[48],
                        uint64_t LUN,
                        uint32_t taskTag,
                        uint32_t targetTag,
                        uint32_t dataSN,
                        uint32_t bufferOffset,
                        uint32_t dataLength,
                        uint32_t expStatSN,
                        bool isFinal);

#pragma mark - NOP-Out PDU Construction

/*!
 * Build a NOP-Out PDU Basic Header Segment.
 *
 * @param bhs       Output buffer (48 bytes).
 * @param taskTag   Initiator task tag (0 for ping, unique for I_T nexus).
 * @param targetTag Target transfer tag (0xFFFFFFFF if not applicable).
 * @param cmdSN     Command sequence number.
 * @param expStatSN Expected status sequence number.
 */
void pdu_build_nop_out(uint8_t bhs[48],
                       uint32_t taskTag,
                       uint32_t targetTag,
                       uint32_t cmdSN,
                       uint32_t expStatSN);

#pragma mark - Task Management PDU Construction

/*!
 * Build a Task Management Request PDU Basic Header Segment.
 *
 * @param bhs           Output buffer (48 bytes).
 * @param LUN           Logical Unit Number (or 0 for target-level functions).
 * @param taskTag       Initiator task tag for this management request.
 * @param refTaskTag    Referenced task tag (for abort task).
 * @param function      Task management function (kTaskMgmtFunc*).
 * @param cmdSN         Command sequence number.
 * @param expStatSN     Expected status sequence number.
 * @param refCmdSN      Referenced command SN (for aborts with LUN=0).
 */
void pdu_build_task_mgmt(uint8_t bhs[48],
                         uint64_t LUN,
                         uint32_t taskTag,
                         uint32_t refTaskTag,
                         uint8_t function,
                         uint32_t cmdSN,
                         uint32_t expStatSN,
                         uint32_t refCmdSN);

#pragma mark - Response PDU Parsing

/*!
 * Parse a SCSI Response PDU BHS (target → initiator).
 *
 * @param bhs           The 48-byte BHS (must have opcode kiSCSIPDUOpCodeSCSIRsp).
 * @param outStatus     [out] SCSI status byte (0=GOOD, 2=CHECK CONDITION).
 * @param outResponse   [out] iSCSI response code (0=cmd completed).
 * @param outStatSN     [out] Status sequence number.
 * @param outExpCmdSN   [out] Exported command SN.
 * @param outMaxCmdSN   [out] Maximum command SN.
 * @param outResidual   [out] Residual count (if any).
 * @param outSenseLen   [out] Sense data length (from data segment length).
 * @return true if parsing succeeded, false if opcode mismatch.
 */
bool pdu_parse_scsi_rsp(const uint8_t bhs[48],
                        uint8_t * outStatus,
                        uint8_t * outResponse,
                        uint32_t * outStatSN,
                        uint32_t * outExpCmdSN,
                        uint32_t * outMaxCmdSN,
                        uint32_t * outResidual,
                        uint32_t * outSenseLen);

#pragma mark - Data-In PDU Parsing

/*!
 * Parse a Data-In PDU BHS (target → initiator).
 *
 * @param bhs             The 48-byte BHS (opcode kiSCSIPDUOpCodeDataIn).
 * @param outFlags        [out] Flag byte (Final, Ack, Status bits).
 * @param outStatus       [out] SCSI status byte (valid if Status flag set).
 * @param outStatSN       [out] Status sequence number.
 * @param outDataSN       [out] Data sequence number.
 * @param outBufferOffset [out] Buffer offset for this data.
 * @param outDataLength   [out] Data segment length from BHS.
 * @param outExpCmdSN     [out] Exported command SN.
 * @return true if parsing succeeded.
 */
bool pdu_parse_data_in(const uint8_t bhs[48],
                       uint8_t * outFlags,
                       uint8_t * outStatus,
                       uint32_t * outStatSN,
                       uint32_t * outDataSN,
                       uint32_t * outBufferOffset,
                       uint32_t * outDataLength,
                       uint32_t * outExpCmdSN);

#pragma mark - R2T PDU Parsing

/*!
 * Parse an R2T (Ready To Transfer) PDU BHS.
 *
 * @param bhs             The 48-byte BHS (opcode kiSCSIPDUOpCodeR2T).
 * @param outTargetTag    [out] Target transfer tag.
 * @param outBufferOffset [out] Buffer offset for data.
 * @param outDesiredLen   [out] Desired data transfer length.
 * @param outR2TSN        [out] R2T sequence number.
 * @param outStatSN       [out] Status sequence number.
 * @return true if parsing succeeded.
 */
bool pdu_parse_r2t(const uint8_t bhs[48],
                   uint32_t * outTargetTag,
                   uint32_t * outBufferOffset,
                   uint32_t * outDesiredLen,
                   uint32_t * outR2TSN,
                   uint32_t * outStatSN);

#pragma mark - Status / Opcode Helpers

/*! Extract the iSCSI opcode from the first byte of a BHS (mask off flag bits). */
static inline uint8_t pdu_get_opcode(const uint8_t bhs[48])
{
    return bhs[0] & 0x3F;
}

/*! Check if a PDU is an initiator PDU (opcode byte < 0x20). */
static inline bool pdu_is_initiator_opcode(uint8_t opcode)
{
    return (opcode & 0x20) == 0;
}

/*! Check if a PDU is a target PDU (opcode byte >= 0x20). */
static inline bool pdu_is_target_opcode(uint8_t opcode)
{
    return (opcode & 0x20) != 0;
}

/*!
 * Calculate padding needed for an iSCSI data segment.
 * @return 0, 1, 2, or 3 bytes of padding.
 */
static inline uint32_t pdu_padding_needed(uint32_t dataLength)
{
    uint32_t mod = dataLength % kPDUEncodeByteAlignment;
    return mod ? (kPDUEncodeByteAlignment - mod) : 0;
}

#endif /* __ISCSI_PDU_ENCODING_H__ */
