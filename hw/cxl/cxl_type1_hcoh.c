/*
 * QEMU CXL Host Type1 HCOH Implementation
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
#include "hw/cxl/cxl_type1_hcoh.h"

static Cache *hcache;
QemuSpin ct1d_lock;

static GRand *rng_opc;
static GRand *rng_addr;
static GRand *rng_size;

static CXLCacheReq __host_hcoh_assem_request_packet(H2DReq opc, uint64_t haddr)
{
    CXLCacheReq req = {
        0,
    };

    req.CacheOpcode = opc;
    req.Address = (haddr & ~(HOST_BLKSIZE - 1));

    return req;
}

static CacheState __host_hcoh_response_check(CXLCacheReq req, D2HRsp rsp)
{
    CacheState state;

    switch (req.CacheOpcode) {
    case H2DReq_SnpData:
    case H2DReq_SnpInv:
        switch (rsp) {
        case D2HRsp_RspIHitI:
        case D2HRsp_RspIHitSE:
        case D2HRsp_RspIFwdM:
            state = CACHE_EXCLUSIVE;
            break;
        case D2HRsp_RspSHitSE:
        case D2HRsp_RspSFwdM:
            state = CACHE_SHARED;
            break;
        default:
            g_assert(0);
        }
        break;
    default:
        g_assert(0);
    }

    return state;
}

static MemTxResult __host_hcoh_access(CacheCommand cmd, PCIDevice *d,
                                      uint64_t haddr, uint64_t *data,
                                      uint32_t size, MemTxAttrs attrs)
{
    CacheState cache_cstate, cache_nstate;
    CXLCacheReq req;
    D2HRsp rsp;
    uint64_t assem_addr, tag, set;
    int32_t cache_blk;
    uint8_t *blk_addr;

    tag = host_cache_extract_tag(hcache, haddr);
    set = host_cache_extract_set(hcache, haddr);

    cache_blk = host_cache_find_valid_block(hcache, tag, set);

    if (cache_blk != -1) {
        if (cmd == CACHE_READ) {
            host_cache_data_read(hcache, haddr, set, cache_blk, data, size);
        } else if (cmd == CACHE_UPDATE) {
            cache_cstate =
                host_cache_extract_block_state(hcache, set, cache_blk);
            g_assert(cache_cstate != CACHE_INVALID);

            if (cache_cstate == CACHE_SHARED) {
                blk_addr =
                    host_cache_extract_block_addr(hcache, set, cache_blk);
                req = __host_hcoh_assem_request_packet(H2DReq_SnpInv, haddr);
                rsp = cxl_type1_access(d, req, blk_addr, HOST_BLKSIZE, attrs);
                if (D2HRsp_RspError == rsp)
                    return MEMTX_ERROR;

                cache_nstate = __host_hcoh_response_check(req, rsp);
                g_assert(cache_nstate == CACHE_EXCLUSIVE);
                host_cache_update_block_state(hcache, tag, set, cache_blk,
                                              cache_nstate);
            }
            host_cache_data_write(hcache, haddr, set, cache_blk, data, size);
        }
    } else {
        cache_blk = host_cache_find_invalid_block(hcache, set);

        if (cache_blk == -1) {
            cache_blk = host_cache_find_replace_block(hcache, set);
            blk_addr = host_cache_extract_block_addr(hcache, set, cache_blk);
            assem_addr = host_cache_assem_haddr(hcache, set, cache_blk);
            cache_cstate =
                host_cache_extract_block_state(hcache, set, cache_blk);

            if (cache_cstate == CACHE_SHARED) {
                req =
                    __host_hcoh_assem_request_packet(H2DReq_SnpInv, assem_addr);
                rsp = cxl_type1_access(d, req, blk_addr, HOST_BLKSIZE, attrs);
                if (D2HRsp_RspError == rsp)
                    return MEMTX_ERROR;

                cache_nstate = __host_hcoh_response_check(req, rsp);
                g_assert(cache_nstate == CACHE_EXCLUSIVE);
                host_cache_update_block_state(hcache, tag, set, cache_blk,
                                              cache_nstate);
            }
            if (MEMTX_OK != cxl_type1_write(d, assem_addr, (uint64_t *)blk_addr,
                                            HOST_BLKSIZE, attrs))
                return MEMTX_ERROR;

            CXL_DEBUG("cache miss -> vitctim write -> as write - haddr: 0x%lx, "
                      "data: 0x%lx",
                      assem_addr, *(uint64_t *)blk_addr);
            host_cache_print_data_block(hcache, set, cache_blk);

            host_cache_update_block_state(hcache, tag, set, cache_blk,
                                          CACHE_INVALID);
        }

        CXL_DEBUG("cache miss -> read request -> from device or as read - "
                  "haddr: 0x%lx",
                  haddr);
        blk_addr = host_cache_extract_block_addr(hcache, set, cache_blk);

        if (cmd == CACHE_READ)
            req = __host_hcoh_assem_request_packet(H2DReq_SnpData, haddr);
        else
            req = __host_hcoh_assem_request_packet(H2DReq_SnpInv, haddr);

        rsp = cxl_type1_access(d, req, blk_addr, HOST_BLKSIZE, attrs);
        if (D2HRsp_RspError == rsp)
            return MEMTX_ERROR;

        if ((rsp != D2HRsp_RspIFwdM) && (rsp != D2HRsp_RspSFwdM)) {
            if (MEMTX_OK != cxl_type1_read(d, haddr, (uint64_t *)blk_addr,
                                           HOST_BLKSIZE, attrs))
                return MEMTX_ERROR;
        }

        CXL_DEBUG("cache miss -> read done -> haddr: 0x%lx, data: 0x%lx", haddr,
                  *(uint64_t *)blk_addr);
        host_cache_print_data_block(hcache, set, cache_blk);

        cache_nstate = __host_hcoh_response_check(req, rsp);
        host_cache_update_block_state(hcache, tag, set, cache_blk,
                                      cache_nstate);

        if (cmd == CACHE_READ) {
            g_assert((cache_nstate == CACHE_EXCLUSIVE) ||
                     (cache_nstate == CACHE_SHARED));
            host_cache_data_read(hcache, haddr, set, cache_blk, data, size);
        } else if (cmd == CACHE_UPDATE) {
            g_assert(cache_nstate == CACHE_EXCLUSIVE);
            host_cache_data_write(hcache, haddr, set, cache_blk, data, size);
        }
    }

    return MEMTX_OK;
}

static void *__ct1d_host_main(void *opaque)
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

    CXL_DEBUG("ct1d host main process starts");

    while (true) {
        g_usleep(CXL_THREAD_DELAY);

        opc = g_rand_int_range(rng_opc, 0, 2);
        haddr =
            g_rand_int_range(rng_addr, 0x8000000, 0x10000000 - HOST_BLKSIZE) +
            CFMWS_BASE_ADDR;
        size = g_rand_int_range(rng_size, 0, ACCESS_DATA_SIZE) + 1;

        qemu_spin_lock(&ct1d_lock);
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
        qemu_spin_unlock(&ct1d_lock);
    }

#undef ACCESS_DATA_PATTERN
#undef ACCESS_DATA_SIZE

    return NULL;
}

MemTxResult cxl_host_type1_hcoh_read(PCIDevice *d, uint64_t haddr,
                                     uint64_t *data, uint32_t size,
                                     MemTxAttrs attrs)
{
    uint64_t cur_cb_addr = haddr & ~(HOST_BLKSIZE - 1);
    uint64_t next_cb_addr = (haddr + size - 1) & ~(HOST_BLKSIZE - 1);

    if (cur_cb_addr != next_cb_addr) {
        uint64_t next_data;
        uint32_t cur_cb_size = next_cb_addr - haddr;

        if (MEMTX_OK == __host_hcoh_access(CACHE_READ, d, haddr, data,
                                           cur_cb_size, attrs)) {
            if (MEMTX_OK == __host_hcoh_access(CACHE_READ, d, next_cb_addr,
                                               &next_data, size - cur_cb_size,
                                               attrs)) {
                *data |= (next_data << (cur_cb_size * BITS_PER_BYTE));
                return MEMTX_OK;
            }
        }
        return MEMTX_ERROR;
    }
    return __host_hcoh_access(CACHE_READ, d, haddr, data, size, attrs);
}

MemTxResult cxl_host_type1_hcoh_write(PCIDevice *d, uint64_t haddr,
                                      uint64_t data, uint32_t size,
                                      MemTxAttrs attrs)
{
    uint64_t cur_cb_addr = haddr & ~(HOST_BLKSIZE - 1);
    uint64_t next_cb_addr = (haddr + size - 1) & ~(HOST_BLKSIZE - 1);

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
                return MEMTX_OK;
            }
        }
        return MEMTX_ERROR;
    }
    return __host_hcoh_access(CACHE_UPDATE, d, haddr, &data, size, attrs);
}

H2DRsp cxl_host_type1_hcoh_response(PCIDevice *d, CXLCacheReq req, uint8_t *buf,
                                    unsigned size, MemTxAttrs attrs)
{
    CacheState cache_cstate = CACHE_INVALID;
    CacheState cache_nstate = CACHE_INVALID;
    uint64_t tag, set;
    uint32_t cache_blk;
    bool data_read = false;
    bool data_write = false;
    bool cache_update = false;
    H2DRsp rsp = { H2DRsp_GO, 0, H2DRsp_Invalid, 0 };

    tag = host_cache_extract_tag(hcache, req.Address);
    set = host_cache_extract_set(hcache, req.Address);

    cache_blk = host_cache_find_valid_block(hcache, tag, set);
    if (cache_blk != -1)
        cache_cstate = host_cache_extract_block_state(hcache, set, cache_blk);

    switch (req.CacheOpcode) {
    case D2HReq_RdCurr:
        data_read = true;
        break;
    case D2HReq_RdOwn:
        data_read = true;
        cache_update = true;
        if (cache_cstate == CACHE_MODIFIED)
            rsp.RspData = H2DRsp_Modified;
        else /* CACHE_INVALID || CACHE_EXCLUSIVE || CACHE_SHARED */
            rsp.RspData = H2DRsp_Exclusive;
        break;
    case D2HReq_RdShared:
        g_assert(cache_cstate != CACHE_INVALID);
        data_read = true;
        cache_update = true;
        cache_nstate = CACHE_SHARED;
        rsp.RspData = H2DRsp_Shared;
        break;
    case D2HReq_RdAny:
        data_read = true;
        cache_update = true;
        if (cache_cstate == CACHE_INVALID) {
            rsp.RspData = H2DRsp_Exclusive;
        } else if (cache_cstate == CACHE_MODIFIED) {
            rsp.RspData = H2DRsp_Modified;
        } else /* CACHE_EXCLUSIVE || CACHE_SHARED */ {
            cache_nstate = CACHE_SHARED;
            rsp.RspData = H2DRsp_Shared;
        }
        break;
    case D2HReq_RdOwnNoData:
        cache_update = true;
        if (cache_cstate == CACHE_MODIFIED) {
            buf = host_cache_extract_block_addr(hcache, set, cache_blk);
            data_write = true;
        }
        rsp.RspData = H2DRsp_Exclusive;
        break;
    case D2HReq_ItoMWr:
    case D2HReq_WrCur:
        cache_update = true;
        cache_nstate = CACHE_EXCLUSIVE;
        data_write = true;
        rsp.RspOpcode = H2DRsp_GO_WritePull;
        break;
    case D2HReq_CLFlush:
        cache_update = true;
        if (cache_cstate == CACHE_MODIFIED) {
            buf = host_cache_extract_block_addr(hcache, set, cache_blk);
            data_write = true;
        }
        rsp.RspData = H2DRsp_Invalid;
        break;
    case D2HReq_CleanEvict:
        g_assert(cache_cstate == CACHE_INVALID);
        data_write = true;
        rsp.RspOpcode = H2DRsp_GO_WritePull;
        break;
    case D2HReq_DirtyEvict:
        g_assert(cache_cstate == CACHE_INVALID);
        data_write = true;
        rsp.RspOpcode = H2DRsp_GO_WritePull;
        break;
    case D2HReq_CleanEvictNoData:
        if (cache_cstate == CACHE_SHARED) {
            cache_nstate = CACHE_EXCLUSIVE;
            cache_update = true;
        }
        rsp.RspData = H2DRsp_Invalid;
        break;
    case D2HReq_WOWrInv:
        /* size == byte enables */
    case D2HReq_WOWrInvF:
        /* to do WOWr */
        g_assert(cache_cstate != CACHE_MODIFIED);
        cache_update = true;
        data_write = true;
        rsp.RspData = H2DRsp_Fast_GO_WritePull;
        rsp.RspData = H2DRsp_ExtCmp;
        break;
    case D2HReq_WrInv:
        /* size == byte enables */
        g_assert(cache_cstate != CACHE_MODIFIED);
        cache_update = true;
        data_write = true;
        rsp.RspData = H2DRsp_Invalid;
        break;
    case D2HReq_CacheFlushed:
        rsp.RspData = H2DRsp_Invalid;
        break;
    default:
        g_assert(0);
    }

    if (data_read == true) {
        if (cache_cstate != CACHE_INVALID) {
            host_cache_data_read(hcache, req.Address, set, cache_blk,
                                 (uint64_t *)buf, size);
        } else {
            if (MEMTX_OK !=
                cxl_type1_read(d, req.Address, (uint64_t *)buf, size, attrs)) {
                memset(buf, -1, size);
                rsp.RspData = H2DRsp_Error;
                return rsp;
            }
        }
    }
    if (data_write == true) {
        if (MEMTX_OK !=
            cxl_type1_write(d, req.Address, (uint64_t *)buf, size, attrs)) {
            rsp.RspData = H2DRsp_Error;
            return rsp;
        }
    }
    if (cache_update == true) {
        if (cache_cstate != CACHE_INVALID) {
            host_cache_update_block_state(hcache, tag, set, cache_blk,
                                          cache_nstate);
        }
    }

    return rsp;
}

void cxl_host_type1_hcoh_init(PCIDevice *d)
{
    QemuThread thread;

    cxl_host_cache_init(&hcache);
    // cache_lock = g_new0(GMutex, 1);

    rng_opc = g_rand_new();
    rng_addr = g_rand_new();
    rng_size = g_rand_new();

    qemu_spin_lock(&ct1d_lock);

    qemu_thread_create(&thread, "ct1d_host_main", __ct1d_host_main, d,
                       QEMU_THREAD_DETACHED);

    qemu_spin_unlock(&ct1d_lock);

    CXL_DEBUG("ct1 host hcoh realized");
}

void cxl_host_type1_hcoh_release(void)
{
    cxl_host_cache_release(&hcache);

    CXL_DEBUG("ct1 host hcoh released");
}
