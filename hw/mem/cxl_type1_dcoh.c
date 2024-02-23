/*
 * QEMU CXL Device Type1 DCOH Implementation
 *
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/hostmem.h"

#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_dcache.h"
#include "hw/cxl/cxl_type1_dcoh.h"

static Cache *dcache;
extern QemuSpin ct1d_lock;

static GRand *rng_opc;
static GRand *rng_addr;
static GRand *rng_size;

static CXLCacheReq __device_dcoh_assem_request_packet(D2HReq opc,
                                                      uint64_t daddr)
{
    CXLCacheReq req = {
        0,
    };

    req.CacheOpcode = opc;
    daddr += CFMWS_BASE_ADDR;
    req.Address = (daddr & ~(DEVICE_BLKSIZE - 1));

    return req;
}

static CacheState __device_dcoh_response_check(CXLCacheReq req, H2DRsp rsp)
{
    CacheState cache_state;

    switch (rsp.RspOpcode) {
    case H2DRsp_GO:
        switch (rsp.RspData) {
        case H2DRsp_Invalid:
            cache_state = CACHE_INVALID;
            break;
        case H2DRsp_Shared:
            cache_state = CACHE_SHARED;
            break;
        case H2DRsp_Exclusive:
            cache_state = CACHE_EXCLUSIVE;
            break;
        case H2DRsp_Modified:
            cache_state = CACHE_MODIFIED;
            break;
        case H2DRsp_Error:
        default:
            g_assert(0);
        }
        break;
    case H2DRsp_GO_WritePull:
    case H2DRsp_Fast_GO_WritePull:
    case H2DRsp_ExtCmp:
        cache_state = CACHE_INVALID;
        break;
    default:
        g_assert(0);
    }

    return cache_state;
}

static MemTxResult __device_dcoh_access(CacheCommand cmd, PCIDevice *d,
                                        uint64_t daddr, uint64_t *data,
                                        uint32_t size, MemTxAttrs attrs)
{
    CacheState cache_cstate, cache_nstate;
    CXLCacheReq req;
    D2HReq opc;
    H2DRsp rsp;
    uint64_t assem_addr, tag, set;
    uint32_t cache_blk;
    uint8_t *blk_addr;

    tag = device_cache_extract_tag(dcache, daddr);
    set = device_cache_extract_set(dcache, daddr);

    cache_blk = device_cache_find_valid_block(dcache, tag, set);

    if (cache_blk != -1) {
        if (cmd == CACHE_READ) {
            device_cache_data_read(dcache, daddr, set, cache_blk, data, size);
        } else if (cmd == CACHE_UPDATE) {
            cache_cstate =
                device_cache_extract_block_state(dcache, set, cache_blk);
            g_assert(cache_cstate != CACHE_INVALID);

            if (cache_cstate == CACHE_SHARED) {
                req = __device_dcoh_assem_request_packet(D2HReq_RdOwnNoData,
                                                         daddr);
                rsp = cxl_type1_response(d, req, NULL, 0, attrs);
                if (rsp.RspOpcode == H2DRsp_GO && rsp.RspData == H2DRsp_Error) {
                    return MEMTX_ERROR;
                }
                cache_nstate = __device_dcoh_response_check(req, rsp);

                g_assert(cache_nstate == CACHE_EXCLUSIVE);
                device_cache_update_block_state(dcache, tag, set, cache_blk,
                                                cache_nstate);
            }
            device_cache_data_write(dcache, daddr, set, cache_blk, data, size);
        }
    } else {
        cache_blk = device_cache_find_invalid_block(dcache, set);

        if (cache_blk == -1) {
            cache_blk = device_cache_find_replace_block(dcache, set);
            blk_addr = device_cache_extract_block_addr(dcache, set, cache_blk);
            assem_addr = device_cache_assem_daddr(dcache, set, cache_blk);
            cache_cstate =
                device_cache_extract_block_state(dcache, set, cache_blk);

            if (cache_cstate == CACHE_MODIFIED)
                opc = D2HReq_DirtyEvict;
            else if (cache_cstate == CACHE_EXCLUSIVE)
                opc = D2HReq_CleanEvict;
            else /* CACHE_SHARED */
                opc = D2HReq_CleanEvictNoData;
            req = __device_dcoh_assem_request_packet(opc, assem_addr);

            rsp = cxl_type1_response(d, req, blk_addr, DEVICE_BLKSIZE, attrs);
            if (rsp.RspOpcode == H2DRsp_GO && rsp.RspData == H2DRsp_Error) {
                return MEMTX_ERROR;
            }

            CXL_DEBUG("cache miss -> vitctim write -> host write - daddr: "
                      "0x%lx, data: 0x%lx",
                      assem_addr, *(uint64_t *)blk_addr);
            device_cache_print_data_block(dcache, set, cache_blk);

            cache_nstate = __device_dcoh_response_check(req, rsp);
            g_assert(cache_nstate == CACHE_INVALID);

            device_cache_update_block_state(dcache, tag, set, cache_blk,
                                            cache_nstate);
        }

        CXL_DEBUG("cache miss -> read request -> host read - daddr: 0x%lx",
                  daddr);
        blk_addr = device_cache_extract_block_addr(dcache, set, cache_blk);

        if (cmd == CACHE_READ)
            req = __device_dcoh_assem_request_packet(D2HReq_RdAny, daddr);
        else if (cmd == CACHE_UPDATE)
            req = __device_dcoh_assem_request_packet(D2HReq_RdOwn, daddr);

        rsp = cxl_type1_response(d, req, blk_addr, DEVICE_BLKSIZE, attrs);
        if (rsp.RspOpcode == H2DRsp_GO && rsp.RspData == H2DRsp_Error) {
            return MEMTX_ERROR;
        }

        CXL_DEBUG(
            "cache miss -> read done -> host read - daddr: 0x%lx, data: 0x%lx",
            daddr, *(uint64_t *)blk_addr);
        device_cache_print_data_block(dcache, set, cache_blk);

        cache_nstate = __device_dcoh_response_check(req, rsp);
        device_cache_update_block_state(dcache, tag, set, cache_blk,
                                        cache_nstate);
	/*
        for (uint32_t i = 0; i < DEVICE_BLKSIZE; i += 8) {
            if (*(uint64_t *)&blk_addr[i] != 0) {
                for (uint32_t i = 0; i < DEVICE_BLKSIZE; i += 8) {
                    CXL_DEBUG("%x %x %x %x %x %x %x %x",
                              dcache->sets[set].blocks[cache_blk].data[i],
                              dcache->sets[set].blocks[cache_blk].data[i + 1],
                              dcache->sets[set].blocks[cache_blk].data[i + 2],
                              dcache->sets[set].blocks[cache_blk].data[i + 3],
                              dcache->sets[set].blocks[cache_blk].data[i + 4],
                              dcache->sets[set].blocks[cache_blk].data[i + 5],
                              dcache->sets[set].blocks[cache_blk].data[i + 6],
                              dcache->sets[set].blocks[cache_blk].data[i + 7]);
                }
            }
        }
	*/
        if (cmd == CACHE_READ) {
            g_assert(cache_nstate != CACHE_INVALID);
            device_cache_data_read(dcache, daddr, set, cache_blk, data, size);
        } else if (cmd == CACHE_UPDATE) {
            g_assert(cache_nstate >= CACHE_EXCLUSIVE);
            device_cache_data_write(dcache, daddr, set, cache_blk, data, size);
        }
    }

    return MEMTX_OK;
}

static void *__ct1d_device_main(void *opaque)
{
#define ACCESS_DATA_PATTERN 0x5A
#define ACCESS_DATA_SIZE    1

    PCIDevice *d = (PCIDevice *)opaque;
    CXLType1Dev *ct1d = CXL_TYPE1(d);
    MemoryRegion *mr = host_memory_backend_get_memory(ct1d->hostmem);
    if (!mr) {
        g_assert(0);
    }

    MemTxAttrs attrs = {
        0,
    };
    MemTxResult result;
    uint32_t opc, size;
    uint64_t daddr, data;
    static uint64_t cnt;

    g_usleep(CXL_BOOT_WAIT_TIME);

    CXL_DEBUG("ct1d device main process starts");

    while (true) {
        g_usleep(CXL_THREAD_DELAY);

        opc = g_rand_int_range(rng_opc, 0, 2);
        daddr = g_rand_int_range(rng_addr, 0x8000000,
                                 int128_get64(mr->size) - DEVICE_BLKSIZE);
        size = g_rand_int_range(rng_size, 0, ACCESS_DATA_SIZE) + 1;

        qemu_spin_lock(&ct1d_lock);
        CXL_THREAD("device dcache lock");

        switch (opc) {
        case 0:
            data = 0;
            result =
                __device_dcoh_access(CACHE_READ, d, daddr, &data, size, attrs);
            break;
        case 1:
            data = (ACCESS_DATA_PATTERN << ((size - 1) * 8));
            result = __device_dcoh_access(CACHE_UPDATE, d, daddr, &data, size,
                                          attrs);
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

        CXL_THREAD("device dcache unlock");
        qemu_spin_unlock(&ct1d_lock);
    }

#undef ACCESS_DATA_PATTERN
#undef ACCESS_DATA_SIZE

    return NULL;
}

D2HRsp cxl_device_type1_dcoh_access(AddressSpace *as, uint64_t daddr,
                                    CXLCacheReq req, uint8_t *buf,
                                    uint32_t size, MemTxAttrs attrs)
{
    CacheState cache_state = CACHE_INVALID;
    D2HRsp rsp;
    uint64_t tag, set;
    int32_t cache_blk;

    tag = device_cache_extract_tag(dcache, daddr);
    set = device_cache_extract_set(dcache, daddr);

    cache_blk = device_cache_find_valid_block(dcache, tag, set);
    if (cache_blk != -1)
        cache_state = device_cache_extract_block_state(dcache, set, cache_blk);
    else
        return D2HRsp_RspIHitI;

    switch (req.CacheOpcode) {
    case H2DReq_SnpData:
        if (cache_state == CACHE_MODIFIED) {
            device_cache_data_read(dcache, daddr, set, cache_blk,
                                   (uint64_t *)buf, size);
            rsp = D2HRsp_RspSFwdM;
        } else {
            rsp = D2HRsp_RspSHitSE;
        }
        device_cache_update_block_state(dcache, tag, set, cache_blk,
                                        CACHE_SHARED);
        break;
    case H2DReq_SnpInv:
        if (cache_state == CACHE_MODIFIED) {
            device_cache_data_read(dcache, daddr, set, cache_blk,
                                   (uint64_t *)buf, size);
            rsp = D2HRsp_RspIFwdM;
        } else {
            rsp = D2HRsp_RspIHitSE;
        }
        device_cache_update_block_state(dcache, tag, set, cache_blk,
                                        CACHE_INVALID);
        break;
    default:
        return D2HRsp_RspError;
        break;
    }

    return rsp;
}

void cxl_device_type1_dcoh_init(PCIDevice *d)
{
    QemuThread thread;

    cxl_device_cache_init(&dcache);

    rng_opc = g_rand_new();
    rng_addr = g_rand_new();
    rng_size = g_rand_new();

    qemu_spin_lock(&ct1d_lock);

    qemu_thread_create(&thread, "ct1d_device_main", __ct1d_device_main, d,
                       QEMU_THREAD_DETACHED);

    qemu_spin_unlock(&ct1d_lock);

    CXL_DEBUG("ct1 device dcoh realized");
}

void cxl_device_type1_dcoh_release(void)
{
    cxl_device_cache_release(&dcache);

    CXL_DEBUG("ct1 device dcoh released");
}
