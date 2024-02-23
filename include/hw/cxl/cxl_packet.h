/*
 * QEMU CXL CACHEMEM Packet Definition
 *
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef CXL_PACKET_H
#define CXL_PACKET_H

/* CXL.mem */
typedef enum {
    HOST_BIAS = 0,
    DEVICE_BIAS,
} BiasState;

typedef enum {
    M2SReq_MemInv = 0,
    M2SReq_MemRd,
    M2SReq_MemRdData,
    M2SReq_MemRdFwd,
    M2SReq_MemWrFwd,
    M2SReq_MemSpecRd,
    M2SReq_MemInvNT,
    M2SReq_MemClnEvct,
    M2SReq_MemWr,
    M2SReq_MemWrPtl,
    M2SReq_BIConflict,
} M2SReq;

typedef enum {
    Snp_NoOp = 0,
    Snp_SnpData,
    Snp_SnpCur,
    Snp_SnpInv,
} SnpType;

typedef enum {
    MF_Meta0State = 0,
    MF_NoOp,
} MetaField;

typedef enum {
    MV_Invalid = 0,
    MV_Any,
    MV_Shared,
} MetaValue;

typedef enum {
    S2MRsp_CMP = 0,
    S2MRsp_CMP_SHARED,
    S2MRsp_CMP_EXCLUSIVE,
    S2MRsp_BI_ConflictAck,
    S2MRsp_CMP_ERROR,
} S2MRsp;

typedef enum {
    S2MReq_BISnpCur = 0,
    S2MReq_BISnpData,
    S2MReq_BISnpInv,
    S2MReq_BISnpCurBlk,
    S2MReq_BISnpDataBlk,
    S2MReq_BISnpInvBlk,
} S2MReq_BISnp;

typedef enum {
    M2SRsp_BINoOp = 0,
    M2SRsp_BIRspI,
    M2SRsp_BIRspS,
    M2SRsp_BIRspE,
    M2SRsp_BIRspIBlk,
    M2SRsp_BIRspSBlk,
    M2SRsp_BIRspEBlk,
} M2SRsp_BIRsp;

typedef struct CXLMemReq {
    uint64_t MemOpcode : 4;
    uint64_t SnpType   : 3;
    uint64_t MetaField : 2;
    uint64_t MetaValue : 2;
    uint64_t Address   : 46;
    uint64_t Reserved  : 7;
} CXLMemReq;

/* CXL.cache */
typedef enum {
    H2DReq_SnpData = 0,
    H2DReq_SnpInv,
    H2DReq_SnpCur,
} H2DReq;

typedef enum {
    D2HRsp_RspIHitI = 0,
    D2HRsp_RspVHitV,
    D2HRsp_RspIHitSE,
    D2HRsp_RspSHitSE,
    D2HRsp_RspSFwdM,
    D2HRsp_RspIFwdM,
    D2HRsp_RspVFwdV,
    D2HRsp_RspError,
} D2HRsp;

typedef enum {
    D2HReq_RdCurr = 0,
    D2HReq_RdOwn,
    D2HReq_RdShared,
    D2HReq_RdAny,
    D2HReq_RdOwnNoData,
    D2HReq_ItoMWr,
    D2HReq_WrCur,
    D2HReq_CLFlush,
    D2HReq_CleanEvict,
    D2HReq_DirtyEvict,
    D2HReq_CleanEvictNoData,
    D2HReq_WOWrInv,
    D2HReq_WOWrInvF,
    D2HReq_WrInv,
    D2HReq_CacheFlushed,
} D2HReq;

typedef enum {
    H2DRsp_WritePull = 0,
    H2DRsp_GO,
    H2DRsp_GO_WritePull,
    H2DRsp_ExtCmp,
    H2DRsp_WritePull_Drop,
    H2DRsp_Fast_GO_WritePull,
    H2DRsp_GO_ERR_WritePull,
} H2DRsp_Opcode;

typedef enum {
    H2DRsp_Invalid = 0,
    H2DRsp_Shared,
    H2DRsp_Exclusive,
    H2DRsp_Modified,
    H2DRsp_Error,
} H2DRsp_Data;

typedef struct H2DRsp {
    uint32_t RspOpcode : 4;
    uint32_t RspPre    : 2;
    uint32_t RspData   : 12;
    uint32_t Reserved  : 14;
} H2DRsp;

typedef struct CXLCacheReq {
    uint64_t CacheOpcode : 5;
    uint64_t Address     : 46;
    uint64_t Reserved    : 13;
} CXLCacheReq;

#endif
