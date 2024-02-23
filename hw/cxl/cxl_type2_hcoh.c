/*
 * QEMU CXL Host Type2 HCOH Implementation
 *
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"

#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_hcache.h"
#include "hw/cxl/cxl_type2_hcoh.h"

static HostCoh *hcoh;
static Cache *hcache;
QemuSpin ct2d_lock;

static GRand *rng_opc;
static GRand *rng_addr;
static GRand *rng_size;

static CXLMemReq __host_hcoh_assem_request_packet(M2SReq opc, SnpType snp,
                                                  MetaValue state,
                                                  uint64_t haddr)
{
    CXLMemReq req = {
        0,
    };

    req.MemOpcode = opc;
    req.SnpType = snp;
    req.MetaField = MF_Meta0State;
    req.MetaValue = state;
    req.Address = (haddr & ~(HOST_BLKSIZE - 1));

    return req;
}

static CacheState __host_hcoh_response_check(CXLMemReq req, S2MRsp rsp)
{
    CacheState state;

    switch (req.MemOpcode) {
    case M2SReq_MemInv:
    case M2SReq_MemInvNT:
    case M2SReq_MemRd:
        switch (rsp) {
        case S2MRsp_CMP:
            state = CACHE_INVALID;
            break;
        case S2MRsp_CMP_EXCLUSIVE:
            state = CACHE_EXCLUSIVE;
            break;
        case S2MRsp_CMP_SHARED:
            state = CACHE_SHARED;
            break;
        case S2MRsp_CMP_ERROR:
        default:
            g_assert(0);
        }
        break;
    case M2SReq_MemWr:
    case M2SReq_MemWrPtl:
        switch (req.MetaValue) {
        case MV_Any:
            g_assert(req.SnpType == Snp_NoOp);
            state = CACHE_EXCLUSIVE;
            break;
        case MV_Shared:
            g_assert(req.SnpType == Snp_NoOp);
            state = CACHE_SHARED;
            break;
        case MV_Invalid:
            g_assert(req.SnpType == Snp_NoOp || req.SnpType == Snp_SnpInv);
            state = CACHE_INVALID;
            break;
        default:
            g_assert(0);
        }
        g_assert(rsp == S2MRsp_CMP);
        break;
    default:
        g_assert(0);
    }

    return state;
}

static MemTxResult __host_hcoh_request(MemCommand cmd, PCIDevice *d,
                                       uint64_t haddr, uint8_t *buf,
                                       MemTxAttrs attrs)
{
    CXLMemReq req;
    S2MRsp rsp;
    CacheState cache_state;
    uint64_t tag, set;
    uint32_t cache_blk;

    switch (cmd) {
    case MEM_Read_MemInv:
        req = __host_hcoh_assem_request_packet(M2SReq_MemRd, Snp_SnpInv,
                                               MV_Invalid, haddr);
        break;
    case MEM_NDR_MemInv:
        req = __host_hcoh_assem_request_packet(M2SReq_MemInv, Snp_SnpInv,
                                               MV_Any, haddr);
        break;
    case MEM_NDR_MemShared:
        req = __host_hcoh_assem_request_packet(M2SReq_MemInv, Snp_SnpData,
                                               MV_Shared, haddr);
        break;
    case MEM_NDR_HCacheInv:
        req = __host_hcoh_assem_request_packet(M2SReq_MemInv, Snp_SnpInv,
                                               MV_Invalid, haddr);
        break;
    case MEM_NDR_SpecRd:
        req = __host_hcoh_assem_request_packet(M2SReq_MemSpecRd, Snp_SnpInv,
                                               MV_Invalid, haddr);
        break;
    case MEM_NDR_ClnEvct:
        req = __host_hcoh_assem_request_packet(M2SReq_MemClnEvct, Snp_SnpInv,
                                               MV_Invalid, haddr);
        break;
    }

    rsp = cxl_type2_access(d, req, buf, HOST_BLKSIZE, attrs);
    if (S2MRsp_CMP_ERROR == rsp) {
        return MEMTX_ERROR;
    }

    qemu_spin_lock(&ct2d_lock);
    CXL_THREAD("host hcache lock");

    tag = host_cache_extract_tag(hcache, haddr);
    set = host_cache_extract_set(hcache, haddr);

    cache_blk = host_cache_find_valid_block(hcache, tag, set);
    if (cache_blk != -1) {
        cache_state = __host_hcoh_response_check(req, rsp);
        host_cache_update_block_state(hcache, tag, set, cache_blk, cache_state);
    }

    CXL_THREAD("host hcache unlock");
    qemu_spin_unlock(&ct2d_lock);

    return MEMTX_OK;
}

static MemTxResult __host_hcoh_access(CacheCommand cmd, PCIDevice *d,
                                      uint64_t haddr, uint64_t *data,
                                      uint32_t size, MemTxAttrs attrs)
{
    CacheState cache_state;
    CXLMemReq req;
    S2MRsp rsp;
    uint64_t assem_addr, tag, set;
    int32_t cache_blk;
    uint8_t *blk_addr;
    bool bias_state;

    tag = host_cache_extract_tag(hcache, haddr);
    set = host_cache_extract_set(hcache, haddr);

    cache_blk = host_cache_find_valid_block(hcache, tag, set);

    if (cache_blk != -1) {
        if (cmd == CACHE_READ) {
            host_cache_data_read(hcache, haddr, set, cache_blk, data, size);
        } else if (cmd == CACHE_UPDATE) {
            bias_state = cxl_host_type2_hcoh_bias_lookup(haddr);

            if (DEVICE_BIAS == bias_state) {
                cache_state =
                    host_cache_extract_block_state(hcache, set, cache_blk);
                g_assert(cache_state != CACHE_INVALID);

                if (cache_state == CACHE_SHARED) {
                    req = __host_hcoh_assem_request_packet(
                        M2SReq_MemInv, Snp_SnpInv, MV_Any, haddr);
                    rsp =
                        cxl_type2_access(d, req, (uint8_t *)data, size, attrs);
                    if (S2MRsp_CMP_ERROR == rsp) {
                        return MEMTX_ERROR;
                    }

                    cache_state = __host_hcoh_response_check(req, rsp);
                    g_assert(cache_state == CACHE_EXCLUSIVE);
                    host_cache_update_block_state(hcache, tag, set, cache_blk,
                                                  cache_state);
                }
            }
            host_cache_data_write(hcache, haddr, set, cache_blk, data, size);
        }
    } else {
        cache_blk = host_cache_find_invalid_block(hcache, set);

        if (cache_blk == -1) {
            cache_blk = host_cache_find_replace_block(hcache, set);
            blk_addr = host_cache_extract_block_addr(hcache, set, cache_blk);

            assem_addr = host_cache_assem_haddr(hcache, set, cache_blk);
            bias_state = cxl_host_type2_hcoh_bias_lookup(assem_addr);
            if (HOST_BIAS == bias_state)
                req = __host_hcoh_assem_request_packet(M2SReq_MemWr, Snp_NoOp,
                                                       MV_Any, assem_addr);
            else
                req = __host_hcoh_assem_request_packet(M2SReq_MemWr, Snp_SnpInv,
                                                       MV_Invalid, assem_addr);

            CXL_HCOH_BIAS(
                assem_addr,
                "cache miss -> vitctim write -> haddr: 0x%lx, data: 0x%lx",
                assem_addr, *(uint64_t *)blk_addr);
            host_cache_print_data_block(hcache, set, cache_blk);

            rsp = cxl_type2_access(d, req, blk_addr, HOST_BLKSIZE, attrs);
            if (S2MRsp_CMP_ERROR == rsp) {
                return MEMTX_ERROR;
            }

            cache_state = __host_hcoh_response_check(req, rsp);
            if (HOST_BIAS == bias_state)
                host_cache_update_block_state(hcache, tag, set, cache_blk,
                                              CACHE_EXCLUSIVE);
            else
                host_cache_update_block_state(hcache, tag, set, cache_blk,
                                              cache_state);
        }

        CXL_HCOH_BIAS(haddr, "cache miss -> read request -> haddr: 0x%lx",
                      haddr);
        blk_addr = host_cache_extract_block_addr(hcache, set, cache_blk);
        bias_state = cxl_host_type2_hcoh_bias_lookup(haddr);

        if (HOST_BIAS == bias_state) {
            req = __host_hcoh_assem_request_packet(M2SReq_MemRd, Snp_NoOp,
                                                   MV_Invalid, haddr);
        } else {
            if (cmd == CACHE_READ)
                req = __host_hcoh_assem_request_packet(
                    M2SReq_MemRd, Snp_SnpData, MV_Shared, haddr);
            else if (cmd == CACHE_UPDATE)
                req = __host_hcoh_assem_request_packet(M2SReq_MemRd, Snp_SnpInv,
                                                       MV_Any, haddr);
        }

        rsp = cxl_type2_access(d, req, blk_addr, HOST_BLKSIZE, attrs);
        if (S2MRsp_CMP_ERROR == rsp) {
            return MEMTX_ERROR;
        }

        CXL_HCOH_BIAS(haddr,
                      "cache miss -> read done -> haddr: 0x%lx, data: 0x%lx",
                      haddr, *(uint64_t *)blk_addr);
        host_cache_print_data_block(hcache, set, cache_blk);

        cache_state = __host_hcoh_response_check(req, rsp);
        if (HOST_BIAS == bias_state)
            cache_state = CACHE_EXCLUSIVE;

        host_cache_update_block_state(hcache, tag, set, cache_blk, cache_state);

        if (cmd == CACHE_READ) {
            g_assert((cache_state == CACHE_EXCLUSIVE) ||
                     (cache_state == CACHE_SHARED));
            host_cache_data_read(hcache, haddr, set, cache_blk, data, size);
        } else if (cmd == CACHE_UPDATE) {
            g_assert(cache_state == CACHE_EXCLUSIVE);
            host_cache_data_write(hcache, haddr, set, cache_blk, data, size);
        }
    }

    return MEMTX_OK;
}

static HostCoh *__host_hcoh_init(void)
{
    HostCoh *coh;

    coh = g_new(HostCoh, 1);
    coh->bias_table_size = HOST_BIAS_TABLE_SIZE;
    coh->bias_entry_size = HOST_BIAS_ENTRY_SIZE;
    coh->bias_table = g_new0(uint32_t, coh->bias_table_size);

    coh->bias_table[0] = HOST_BIAS;
    coh->bias_table[1] = DEVICE_BIAS;

    return coh;
}

static void __host_hcoh_free(HostCoh *coh)
{
    g_free(coh->bias_table);
    g_free(coh);
}

static void *__ct2d_host_main(void *opaque)
{
#define ACCESS_DATA_PATTERN 0xFF
#define ACCESS_DATA_SIZE    1

    PCIDevice *d = (PCIDevice *)opaque;
    MemTxAttrs attrs = {
        0,
    };
    MemTxResult result;
    uint32_t opc, size;
    uint64_t haddr, data;
    static uint64_t cnt = 0;

    g_usleep(CXL_BOOT_WAIT_TIME);

    CXL_DEBUG("ct2d host main process starts");

    while (true) {
        g_usleep(CXL_THREAD_DELAY);

        opc = g_rand_int_range(rng_opc, 0, 2);
        haddr = g_rand_int_range(rng_addr, HOST_BIAS_ENTRY_SIZE,
                                 HOST_BIAS_ENTRY_SIZE * 2 - HOST_BLKSIZE) +
                CFMWS_BASE_ADDR;
        size = g_rand_int_range(rng_size, 0, ACCESS_DATA_SIZE) + 1;

        qemu_spin_lock(&ct2d_lock);
        CXL_THREAD("host hcache lock");

        switch (opc) {
        case 0:
            data = 0;
            result =
                __host_hcoh_access(CACHE_READ, d, haddr, &data, size, attrs);
            break;
        case 1:
            data = (ACCESS_DATA_PATTERN << ((size - 1) * 8));
            result =
                __host_hcoh_access(CACHE_UPDATE, d, haddr, &data, size, attrs);
            break;
        default:
            g_assert(0);
        }

        if (result != MEMTX_OK) {
            g_assert(0);
        }
        cnt++;
        if (cnt % 0x100000 == 0)
            error_report("%s processing cnt 0x%lx", __func__, cnt);

        CXL_THREAD("host hcache unlock");
        qemu_spin_unlock(&ct2d_lock);
    }

#undef ACCESS_DATA_PATTERN
#undef ACCESS_DATA_SIZE

    return NULL;
}

BiasState cxl_host_type2_hcoh_bias_lookup(uint64_t haddr)
{
    uint32_t entry_idx = (haddr - CFMWS_BASE_ADDR) / hcoh->bias_entry_size;

    return hcoh->bias_table[entry_idx];
}

MemTxResult cxl_host_type2_hcoh_read(PCIDevice *d, uint64_t haddr,
                                     uint64_t *data, uint32_t size,
                                     MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_OK;
    uint64_t cur_cb_addr = haddr & ~(HOST_BLKSIZE - 1);
    uint64_t next_cb_addr = (haddr + size - 1) & ~(HOST_BLKSIZE - 1);

    qemu_spin_lock(&ct2d_lock);
    CXL_THREAD("host hcache lock");

    if (cur_cb_addr != next_cb_addr) {
        uint64_t next_data;
        uint32_t cur_cb_size = next_cb_addr - haddr;

        if (MEMTX_OK == __host_hcoh_access(CACHE_READ, d, haddr, data,
                                           cur_cb_size, attrs)) {
            if (MEMTX_OK == __host_hcoh_access(CACHE_READ, d, next_cb_addr,
                                               &next_data, size - cur_cb_size,
                                               attrs)) {
                *data |= (next_data << (cur_cb_size * BITS_PER_BYTE));
                goto out;
            }
        }
        result = MEMTX_ERROR;
        goto out;
    }
    result = __host_hcoh_access(CACHE_READ, d, haddr, data, size, attrs);

out:
    CXL_THREAD("host hcache unlock");
    qemu_spin_unlock(&ct2d_lock);

    return result;
}

MemTxResult cxl_host_type2_hcoh_write(PCIDevice *d, uint64_t haddr,
                                      uint64_t data, uint32_t size,
                                      MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_OK;
    uint64_t cur_cb_addr = haddr & ~(HOST_BLKSIZE - 1);
    uint64_t next_cb_addr = (haddr + size - 1) & ~(HOST_BLKSIZE - 1);

    qemu_spin_lock(&ct2d_lock);
    CXL_THREAD("host hcache lock");

    if (cur_cb_addr != next_cb_addr) {
        uint64_t next_data;
        uint32_t cur_cb_size = next_cb_addr - haddr;

        next_data = data >> (cur_cb_size * BITS_PER_BYTE);
        data &= (((uint64_t)1 << (cur_cb_size * BITS_PER_BYTE)) - 1);

        if (MEMTX_OK == __host_hcoh_access(CACHE_UPDATE, d, haddr, &data,
                                           cur_cb_size, attrs)) {
            if (MEMTX_OK == __host_hcoh_access(CACHE_UPDATE, d, next_cb_addr,
                                               &next_data, size - cur_cb_size,
                                               attrs)) {
                goto out;
            }
        }
        result = MEMTX_ERROR;
        goto out;
    }
    result = __host_hcoh_access(CACHE_UPDATE, d, haddr, &data, size, attrs);

out:
    CXL_THREAD("host hcache unlock");
    qemu_spin_unlock(&ct2d_lock);

    return result;
}

MemTxResult cxl_host_type2_hcoh_command(PCIDevice *d, uint64_t haddr,
                                        uint8_t *buf, MemTxAttrs attrs)
{
    MemTxResult result;

    result = __host_hcoh_request(MEM_Read_MemInv, d, haddr, buf, attrs);
    result = __host_hcoh_request(MEM_NDR_MemInv, d, haddr, buf, attrs);
    result = __host_hcoh_request(MEM_NDR_MemShared, d, haddr, buf, attrs);
    result = __host_hcoh_request(MEM_NDR_HCacheInv, d, haddr, buf, attrs);
    result = __host_hcoh_request(MEM_NDR_SpecRd, d, haddr, buf, attrs);
    result = __host_hcoh_request(MEM_NDR_ClnEvct, d, haddr, buf, attrs);

    return result;
}

M2SRsp_BIRsp cxl_host_type2_hcoh_response(CXLMemReq req, MemTxAttrs attrs)
{
    uint64_t tag, set;
    uint32_t cache_blk;
    CacheState cache_state;
    M2SRsp_BIRsp rsp = M2SRsp_BINoOp;

    tag = host_cache_extract_tag(hcache, req.Address);
    set = host_cache_extract_set(hcache, req.Address);

    cache_blk = host_cache_find_valid_block(hcache, tag, set);
    if (cache_blk == -1)
        return M2SRsp_BIRspI;
    else
        cache_state = host_cache_extract_block_state(hcache, set, cache_blk);

    switch (req.MemOpcode) {
    case S2MReq_BISnpCur:
    case S2MReq_BISnpCurBlk:
        if (cache_state == CACHE_SHARED)
            rsp = M2SRsp_BIRspS;
        else if (cache_state == CACHE_EXCLUSIVE ||
                 cache_state == CACHE_MODIFIED)
            rsp = M2SRsp_BIRspE;
        break;
    case S2MReq_BISnpData:
    case S2MReq_BISnpDataBlk:
        if (cache_state == CACHE_SHARED) {
            rsp = M2SRsp_BIRspS;
        } else if (cache_state == CACHE_EXCLUSIVE ||
                   cache_state == CACHE_MODIFIED) {
            host_cache_update_block_state(hcache, tag, set, cache_blk,
                                          CACHE_INVALID);
            rsp = M2SRsp_BIRspI;
        }
        break;
    case S2MReq_BISnpInv:
    case S2MReq_BISnpInvBlk:
        host_cache_update_block_state(hcache, tag, set, cache_blk,
                                      CACHE_INVALID);
        rsp = M2SRsp_BIRspI;
        break;
    default:
        g_assert(0);
    }

    return rsp;
}

void cxl_host_type2_hcoh_init(PCIDevice *d)
{
    QemuThread thread;

    cxl_host_cache_init(&hcache);
    hcoh = __host_hcoh_init();

    rng_opc = g_rand_new();
    rng_addr = g_rand_new();
    rng_size = g_rand_new();

    qemu_spin_lock(&ct2d_lock);

    qemu_thread_create(&thread, "ct2d_host_main", __ct2d_host_main, d,
                       QEMU_THREAD_JOINABLE);

    qemu_spin_unlock(&ct2d_lock);

    CXL_DEBUG("ct2 host hcoh realized");
}

void cxl_host_type2_hcoh_release(void)
{
    __host_hcoh_free(hcoh);
    cxl_host_cache_release(&hcache);

    CXL_DEBUG("ct2 host hcoh released");
}
