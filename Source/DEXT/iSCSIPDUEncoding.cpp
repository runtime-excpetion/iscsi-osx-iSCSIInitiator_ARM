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
 * iSCSIPDUEncoding.cpp — iSCSI PDU encoding/decoding for the DEXT data path.
 *
 * Implements the functions declared in iSCSIPDUEncoding.h for building
 * and parsing iSCSI PDUs in the DEXT SCSI data path.
 *
 * All multi-byte fields on the wire are big-endian (network byte order).
 * These functions convert between CPU byte order and network byte order
 * as appropriate.
 */

#include "iSCSIPDUEncoding.h"

#pragma mark - SCSI Command PDU

void pdu_build_scsi_cmd(uint8_t bhs[48],
                         uint64_t LUN,
                         uint32_t taskTag,
                         uint32_t dataLength,
                         bool isRead,
                         uint32_t cmdSN,
                         uint32_t expStatSN,
                         const uint8_t cdb[16],
                         uint8_t taskAttr,
                         bool immediateData)
{
    if (!bhs || !cdb) return;

    // Zero out the BHS first
    memset(bhs, 0, 48);

    // Byte 0: Opcode
    bhs[0] = kPDUEncodeOpCodeSCSICmd;
    if (immediateData) {
        bhs[0] |= kPDUEncodeImmediateDeliveryFlag;
    }

    // Byte 1: Flags — task attribute (bits 2:0) | read/write flag
    bhs[1] = taskAttr & 0x07;
    if (isRead) {
        bhs[1] |= kSCSICmdFlagRead;
    } else if (dataLength > 0) {
        bhs[1] |= kSCSICmdFlagWrite;
    }
    // Note: immediate data flag for unsolicited data
    if (dataLength > 0 && !isRead && !immediateData) {
        bhs[1] |= kSCSICmdFlagNoUnsolicitedData;
    }

    // Byte 4: TotalAHSLength (0 — no AHS for standard CDBs)
    bhs[4] = 0;

    // Bytes 5-7: DataSegmentLength (0 for SCSI cmd — data is separate)
    // This field holds the immediate data length if immediateData is true.
    // For standard SCSI commands, it's 0.
    if (immediateData && dataLength > 0 && !isRead) {
        pdu_encode_data_length(&bhs[5], dataLength);
    }

    // Bytes 8-15: LUN (8 bytes, big-endian)
    // iSCSI uses a 64-bit LUN format per SAM/SPC
    uint64_t lunBe = pdu_encode_hton64(LUN);
    memcpy(&bhs[8], &lunBe, 8);

    // Bytes 16-19: Initiator Task Tag
    uint32_t taskTagBe = pdu_encode_hton32(taskTag);
    memcpy(&bhs[16], &taskTagBe, 4);

    // Bytes 20-23: Expected Data Transfer Length (big-endian)
    uint32_t dataLenBe = pdu_encode_hton32(dataLength);
    memcpy(&bhs[20], &dataLenBe, 4);

    // Bytes 24-27: Command Sequence Number (cmdSN)
    uint32_t cmdSNBe = pdu_encode_hton32(cmdSN);
    memcpy(&bhs[24], &cmdSNBe, 4);

    // Bytes 28-31: Expected Status Sequence Number (expStatSN)
    uint32_t expStatSNBe = pdu_encode_hton32(expStatSN);
    memcpy(&bhs[28], &expStatSNBe, 4);

    // Bytes 32-47: CDB (16 bytes, stored as-is — already network-order per SPC)
    memcpy(&bhs[32], cdb, 16);
}

#pragma mark - Data-Out PDU

void pdu_build_data_out(uint8_t bhs[48],
                         uint64_t LUN,
                         uint32_t taskTag,
                         uint32_t targetTag,
                         uint32_t dataSN,
                         uint32_t bufferOffset,
                         uint32_t dataLength,
                         uint32_t expStatSN,
                         bool isFinal)
{
    if (!bhs) return;

    memset(bhs, 0, 48);

    // Byte 0: Opcode
    bhs[0] = kPDUEncodeOpCodeDataOut;

    // Byte 1: Flags
    bhs[1] = isFinal ? kDataOutFlagFinal : 0;

    // Bytes 5-7: DataSegmentLength
    pdu_encode_data_length(&bhs[5], dataLength);

    // Bytes 8-15: LUN
    uint64_t lunBe = pdu_encode_hton64(LUN);
    memcpy(&bhs[8], &lunBe, 8);

    // Bytes 16-19: Initiator Task Tag
    uint32_t taskTagBe = pdu_encode_hton32(taskTag);
    memcpy(&bhs[16], &taskTagBe, 4);

    // Bytes 20-23: Target Transfer Tag
    uint32_t targetTagBe = pdu_encode_hton32(targetTag);
    memcpy(&bhs[20], &targetTagBe, 4);

    // Bytes 24-27: Reserved (0)

    // Bytes 28-31: ExpStatSN
    uint32_t expStatSNBe = pdu_encode_hton32(expStatSN);
    memcpy(&bhs[28], &expStatSNBe, 4);

    // Bytes 36-39: DataSN
    uint32_t dataSNBe = pdu_encode_hton32(dataSN);
    memcpy(&bhs[36], &dataSNBe, 4);

    // Bytes 40-43: Buffer Offset
    uint32_t boBe = pdu_encode_hton32(bufferOffset);
    memcpy(&bhs[40], &boBe, 4);
}

#pragma mark - NOP-Out PDU

void pdu_build_nop_out(uint8_t bhs[48],
                        uint32_t taskTag,
                        uint32_t targetTag,
                        uint32_t cmdSN,
                        uint32_t expStatSN)
{
    if (!bhs) return;

    memset(bhs, 0, 48);

    // Byte 0: Opcode
    bhs[0] = kPDUEncodeOpCodeNOPOut;

    // Byte 1: Reserved (must be 0x80 per RFC 3720)
    bhs[1] = kPDUEncodeNOPOutReservedFlag;

    // Bytes 16-19: Initiator Task Tag
    uint32_t taskTagBe = pdu_encode_hton32(taskTag);
    memcpy(&bhs[16], &taskTagBe, 4);

    // Bytes 20-23: Target Transfer Tag (0xFFFFFFFF = reserved = no specific target)
    uint32_t targetTagBe = pdu_encode_hton32(targetTag);
    memcpy(&bhs[20], &targetTagBe, 4);

    // Bytes 24-27: cmdSN
    uint32_t cmdSNBe = pdu_encode_hton32(cmdSN);
    memcpy(&bhs[24], &cmdSNBe, 4);

    // Bytes 28-31: ExpStatSN
    uint32_t expStatSNBe = pdu_encode_hton32(expStatSN);
    memcpy(&bhs[28], &expStatSNBe, 4);
}

#pragma mark - Task Management PDU

void pdu_build_task_mgmt(uint8_t bhs[48],
                          uint64_t LUN,
                          uint32_t taskTag,
                          uint32_t refTaskTag,
                          uint8_t function,
                          uint32_t cmdSN,
                          uint32_t expStatSN,
                          uint32_t refCmdSN)
{
    if (!bhs) return;

    memset(bhs, 0, 48);

    // Byte 0: Opcode
    bhs[0] = kPDUEncodeOpCodeTaskMgmtReq;

    // Byte 1: Function code
    bhs[1] = function | 0x80;  // Function flag per RFC

    // Bytes 8-15: LUN
    // For target-level functions (target reset), LUN should be 0
    uint64_t lunBe = pdu_encode_hton64(LUN);
    memcpy(&bhs[8], &lunBe, 8);

    // Bytes 16-19: Initiator Task Tag
    uint32_t taskTagBe = pdu_encode_hton32(taskTag);
    memcpy(&bhs[16], &taskTagBe, 4);

    // Bytes 20-23: Referenced Task Tag
    uint32_t refTagBe = pdu_encode_hton32(refTaskTag);
    memcpy(&bhs[20], &refTagBe, 4);

    // Bytes 24-27: cmdSN
    uint32_t cmdSNBe = pdu_encode_hton32(cmdSN);
    memcpy(&bhs[24], &cmdSNBe, 4);

    // Bytes 28-31: ExpStatSN
    uint32_t expStatSNBe = pdu_encode_hton32(expStatSN);
    memcpy(&bhs[28], &expStatSNBe, 4);

    // Bytes 32-35: RefCmdSN
    uint32_t refCmdSNBe = pdu_encode_hton32(refCmdSN);
    memcpy(&bhs[32], &refCmdSNBe, 4);
}

#pragma mark - SCSI Response Parsing

bool pdu_parse_scsi_rsp(const uint8_t bhs[48],
                         uint8_t * outStatus,
                         uint8_t * outResponse,
                         uint32_t * outStatSN,
                         uint32_t * outExpCmdSN,
                         uint32_t * outMaxCmdSN,
                         uint32_t * outResidual,
                         uint32_t * outSenseLen)
{
    if (!bhs) return false;

    uint8_t opcode = pdu_get_opcode(bhs);
    if (opcode != kPDUEncodeOpCodeSCSIRsp) {
        return false;
    }

    // Byte 2: Response (0=command completed, 1=target failure)
    if (outResponse) *outResponse = bhs[2];

    // Byte 3: SCSI Status (0=GOOD, 2=CHECK CONDITION)
    if (outStatus) *outStatus = bhs[3];

    // Bytes 28-31: StatSN
    uint32_t statSN;
    memcpy(&statSN, &bhs[28], 4);
    if (outStatSN) *outStatSN = pdu_encode_ntoh32(statSN);

    // Bytes 32-35: ExpCmdSN
    uint32_t expCmdSN;
    memcpy(&expCmdSN, &bhs[32], 4);
    if (outExpCmdSN) *outExpCmdSN = pdu_encode_ntoh32(expCmdSN);

    // Bytes 36-39: MaxCmdSN
    uint32_t maxCmdSN;
    memcpy(&maxCmdSN, &bhs[36], 4);
    if (outMaxCmdSN) *outMaxCmdSN = pdu_encode_ntoh32(maxCmdSN);

    // Bytes 44-47: Residual Count
    uint32_t residual;
    memcpy(&residual, &bhs[44], 4);
    if (outResidual) *outResidual = pdu_encode_ntoh32(residual);

    // Data segment length = sense data length (if status=CHECK CONDITION)
    uint32_t senseLen = pdu_decode_data_length(&bhs[5]);
    if (outSenseLen) *outSenseLen = senseLen;

    return true;
}

#pragma mark - Data-In Parsing

bool pdu_parse_data_in(const uint8_t bhs[48],
                        uint8_t * outFlags,
                        uint8_t * outStatus,
                        uint32_t * outStatSN,
                        uint32_t * outDataSN,
                        uint32_t * outBufferOffset,
                        uint32_t * outDataLength,
                        uint32_t * outExpCmdSN)
{
    if (!bhs) return false;

    uint8_t opcode = pdu_get_opcode(bhs);
    if (opcode != kPDUEncodeOpCodeDataIn) {
        return false;
    }

    // Byte 1: Flags
    if (outFlags) *outFlags = bhs[1];

    // Byte 3: Status (valid when Status flag is set)
    if (outStatus) *outStatus = bhs[3];

    // Data segment length (bytes 5-7)
    if (outDataLength) *outDataLength = pdu_decode_data_length(&bhs[5]);

    // Bytes 28-31: StatSN
    uint32_t statSN;
    memcpy(&statSN, &bhs[28], 4);
    if (outStatSN) *outStatSN = pdu_encode_ntoh32(statSN);

    // Bytes 32-35: ExpCmdSN
    uint32_t expCmdSN;
    memcpy(&expCmdSN, &bhs[32], 4);
    if (outExpCmdSN) *outExpCmdSN = pdu_encode_ntoh32(expCmdSN);

    // Bytes 36-39: DataSN
    uint32_t dataSN;
    memcpy(&dataSN, &bhs[36], 4);
    if (outDataSN) *outDataSN = pdu_encode_ntoh32(dataSN);

    // Bytes 40-43: Buffer Offset
    uint32_t bo;
    memcpy(&bo, &bhs[40], 4);
    if (outBufferOffset) *outBufferOffset = pdu_encode_ntoh32(bo);

    return true;
}

#pragma mark - R2T Parsing

bool pdu_parse_r2t(const uint8_t bhs[48],
                    uint32_t * outTargetTag,
                    uint32_t * outBufferOffset,
                    uint32_t * outDesiredLen,
                    uint32_t * outR2TSN,
                    uint32_t * outStatSN)
{
    if (!bhs) return false;

    uint8_t opcode = pdu_get_opcode(bhs);
    if (opcode != kPDUEncodeOpCodeR2T) {
        return false;
    }

    // Bytes 20-23: Target Transfer Tag
    uint32_t targetTag;
    memcpy(&targetTag, &bhs[20], 4);
    if (outTargetTag) *outTargetTag = pdu_encode_ntoh32(targetTag);

    // Bytes 28-31: StatSN
    uint32_t statSN;
    memcpy(&statSN, &bhs[28], 4);
    if (outStatSN) *outStatSN = pdu_encode_ntoh32(statSN);

    // Bytes 36-39: R2TSN
    uint32_t r2tSN;
    memcpy(&r2tSN, &bhs[36], 4);
    if (outR2TSN) *outR2TSN = pdu_encode_ntoh32(r2tSN);

    // Bytes 40-43: Buffer Offset
    uint32_t bo;
    memcpy(&bo, &bhs[40], 4);
    if (outBufferOffset) *outBufferOffset = pdu_encode_ntoh32(bo);

    // Bytes 44-47: Desired Data Transfer Length
    uint32_t desired;
    memcpy(&desired, &bhs[44], 4);
    if (outDesiredLen) *outDesiredLen = pdu_encode_ntoh32(desired);

    return true;
}
