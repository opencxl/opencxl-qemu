/*
 * QEMU CXL Device Type2 DCOH Implementation
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
#include "hw/cxl/cxl_type2_dcoh.h"

static DeviceCoh *dcoh;
static Cache *dcache;
extern QemuSpin ct2d_lock;

static GRand *rng_opc;
static GRand *rng_addr;
static GRand *rng_size;

static CXLMemReq __device_dcoh_assem_request_packet(S2MReq_BISnp opc,
                                                    uint64_t daddr)
{
    CXLMemReq req = {
        0,
    };

    req.MemOpcode = opc;
    daddr += CFMWS_BASE_ADDR;
    req.Address = (daddr & ~(DEVICE_BLKSIZE - 1));

    return req;
}

static CacheState __device_dcoh_response_check(CXLMemReq req, M2SRsp_BIRsp rsp)
{
    CacheState cache_state = CACHE_INVALID;

    if (M2SRsp_BIRspEBlk < rsp) {
        g_assert(0);
    }

    switch (req.MemOpcode) {
    case S2MReq_BISnpCur:
    case S2MReq_BISnpCurBlk:
        cache_state = CACHE_INVALID;
        break;
    case S2MReq_BISnpData:
    case S2MReq_BISnpDataBlk:
        if (rsp == M2SRsp_BIRspI || rsp == M2SRsp_BIRspIBlk)
            cache_state = CACHE_EXCLUSIVE;
        else if (rsp == M2SRsp_BIRspS || rsp == M2SRsp_BIRspSBlk)
            cache_state = CACHE_SHARED;
        break;
    case S2MReq_BISnpInv:
    case S2MReq_BISnpInvBlk:
        cache_state = CACHE_EXCLUSIVE;
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
    CXLType2Dev *ct2d = CXL_TYPE2(d);
    AddressSpace *as = &ct2d->hostmem_as;
    CacheState cache_state;
    CXLMemReq req;
    M2SRsp_BIRsp rsp;
    uint64_t assem_addr, tag, set;
    uint32_t cache_blk;
    uint8_t *blk_addr;

    if (HOST_BIAS == cxl_device_type2_dcoh_bias_lookup(daddr))
        g_assert(0);

    tag = device_cache_extract_tag(dcache, daddr);
    set = device_cache_extract_set(dcache, daddr);

    cache_blk = device_cache_find_valid_block(dcache, tag, set);

    if (cache_blk != -1) {
        if (cmd == CACHE_READ) {
            device_cache_data_read(dcache, daddr, set, cache_blk, data, size);
        } else if (cmd == CACHE_UPDATE) {
            if (g_hash_table_lookup(dcoh->sf_table, (gpointer)daddr)) {
                cache_state =
                    device_cache_extract_block_state(dcache, set, cache_blk);
                g_assert(cache_state != CACHE_INVALID);

                if (cache_state == CACHE_SHARED) {
                    req = __device_dcoh_assem_request_packet(S2MReq_BISnpInv,
                                                             daddr);
                    rsp = cxl_type2_response(req, attrs);
                    if (rsp == M2SRsp_BINoOp) {
                        return MEMTX_ERROR;
                    }
                    cache_state = __device_dcoh_response_check(req, rsp);

                    g_assert(cache_state == CACHE_EXCLUSIVE);
                    g_hash_table_remove(dcoh->sf_table, (gpointer)daddr);
                    device_cache_update_block_state(dcache, tag, set, cache_blk,
                                                    cache_state);
                }
            }
            device_cache_data_write(dcache, daddr, set, cache_blk, data, size);
        }
    } else {
        cache_blk = device_cache_find_invalid_block(dcache, set);

        if (cache_blk == -1) {
            cache_blk = device_cache_find_replace_block(dcache, set);
            blk_addr = device_cache_extract_block_addr(dcache, set, cache_blk);
            assem_addr = device_cache_assem_daddr(dcache, set, cache_blk);

            if (MEMTX_OK != address_space_write(as, assem_addr, attrs, blk_addr,
                                                DEVICE_BLKSIZE)) {
                return MEMTX_ERROR;
            }

            CXL_DCOH_BIAS(assem_addr,
                          "cache miss -> vitctim write -> as write - daddr: "
                          "0x%lx, data: 0x%lx",
                          assem_addr, *(uint64_t *)blk_addr);
            device_cache_print_data_block(dcache, set, cache_blk);
            device_cache_update_block_state(dcache, tag, set, cache_blk,
                                            CACHE_INVALID);
        }

        CXL_DCOH_BIAS(daddr,
                      "cache miss -> read memory -> as read - daddr: 0x%lx",
                      daddr);
        blk_addr = device_cache_extract_block_addr(dcache, set, cache_blk);
        assem_addr = daddr & ~(DEVICE_BLKSIZE - 1);

        if (MEMTX_OK != address_space_read(as, assem_addr, attrs, blk_addr,
                                           DEVICE_BLKSIZE)) {
            return MEMTX_ERROR;
        }

        CXL_DCOH_BIAS(
            daddr,
            "cache miss -> read done -> as read - daddr: 0x%lx, data: 0x%lx",
            assem_addr, *(uint64_t *)blk_addr);
        device_cache_print_data_block(dcache, set, cache_blk);
        device_cache_update_block_state(dcache, tag, set, cache_blk,
                                        CACHE_EXCLUSIVE);
        /*
                        for (uint32_t i = 0; i < DEVICE_BLKSIZE; i+=8) {
                                if (*(uint64_t *)&blk_addr[i] != 0) {
                                        for (uint32_t i = 0; i < DEVICE_BLKSIZE;
           i+=8) { error_report("%x %x %x %x %x %x %x %x",
                                                        dcache->sets[set].blocks[cache_blk].data[i],
                                                        dcache->sets[set].blocks[cache_blk].data[i+1],
                                                        dcache->sets[set].blocks[cache_blk].data[i+2],
                                                        dcache->sets[set].blocks[cache_blk].data[i+3],
                                                        dcache->sets[set].blocks[cache_blk].data[i+4],
                                                        dcache->sets[set].blocks[cache_blk].data[i+5],
                                                        dcache->sets[set].blocks[cache_blk].data[i+6],
                                                        dcache->sets[set].blocks[cache_blk].data[i+7]);
                                        }
                                }
                        }
        */
        if (cmd == CACHE_READ) {
            device_cache_data_read(dcache, daddr, set, cache_blk, data, size);
        } else if (cmd == CACHE_UPDATE) {
            device_cache_data_write(dcache, daddr, set, cache_blk, data, size);
        }
    }

    return MEMTX_OK;
}

static DeviceCoh *__device_dcoh_init(void)
{
    DeviceCoh *coh;

    coh = g_new(DeviceCoh, 1);

    coh->sf_table = g_hash_table_new(NULL, NULL);

    coh->bias_cache_size = DEVICE_BIAS_CACHE_SIZE;
    coh->bias_entry_size = DEVICE_BIAS_ENTRY_SIZE;
    coh->bias_cache = g_new0(uint32_t, coh->bias_cache_size);

    coh->bias_cache[0] = HOST_BIAS;
    coh->bias_cache[1] = DEVICE_BIAS;

    return coh;
}

static void __device_dcoh_free(DeviceCoh *coh)
{
    g_free(coh->bias_cache);
    g_free(coh);
}

static void *__ct2d_device_main(void *opaque)
{
#define ACCESS_DATA_PATTERN 0x5A
#define ACCESS_DATA_SIZE    1

    PCIDevice *d = (PCIDevice *)opaque;
    CXLType2Dev *ct2d = CXL_TYPE2(d);
    MemoryRegion *mr = host_memory_backend_get_memory(ct2d->hostmem);
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

    CXL_DEBUG("ct2d device main process starts");

    while (true) {
        g_usleep(CXL_THREAD_DELAY);

        opc = g_rand_int_range(rng_opc, 0, 2);
        daddr = g_rand_int_range(rng_addr, DEVICE_BIAS_ENTRY_SIZE,
                                 int128_get64(mr->size) - DEVICE_BLKSIZE);
        size = g_rand_int_range(rng_size, 0, ACCESS_DATA_SIZE) + 1;

        qemu_spin_lock(&ct2d_lock);
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
        qemu_spin_unlock(&ct2d_lock);
    }

#undef ACCESS_DATA_PATTERN
#undef ACCESS_DATA_SIZE

    return NULL;
}

BiasState cxl_device_type2_dcoh_bias_lookup(uint64_t daddr)
{
    uint32_t entry_idx = daddr / dcoh->bias_entry_size;

    return dcoh->bias_cache[entry_idx];
}

S2MRsp cxl_device_type2_dcoh_access(AddressSpace *as, uint64_t daddr,
                                    CXLMemReq req, uint8_t *buf, uint32_t size,
                                    MemTxAttrs attrs)
{
    CacheState cache_cstate = CACHE_INVALID;
    CacheState cache_nstate = CACHE_INVALID;
    uint64_t tag, set;
    uint32_t cache_blk;
    uint8_t *blk_addr;
    bool data_read = false;
    bool data_write = false;
    bool data_flush = false;
    bool cache_update = false;
    S2MRsp rsp = S2MRsp_CMP;

    tag = device_cache_extract_tag(dcache, daddr);
    set = device_cache_extract_set(dcache, daddr);

    cache_blk = device_cache_find_valid_block(dcache, tag, set);
    if (cache_blk != -1)
        cache_cstate = device_cache_extract_block_state(dcache, set, cache_blk);

    if (HOST_BIAS == cxl_device_type2_dcoh_bias_lookup(daddr)) {
        switch (req.MemOpcode) {
        case M2SReq_MemRd:
        case M2SReq_MemRdData:
            data_read = true;
            QEMU_FALLTHROUGH;
        case M2SReq_MemInv:
        case M2SReq_MemInvNT:
            cache_update = true;
            if (req.MetaValue == MV_Shared)
                cache_nstate = CACHE_SHARED;
            else /* CACHE_INVALID || CACHE_EXCLUSIVE || CACHE_MODIFIED */
                cache_nstate = CACHE_INVALID;
            // rsp = S2MRsp_CMP;
            break;
        case M2SReq_MemSpecRd:
            // non-rsp
            break;
        case M2SReq_MemWr:
        case M2SReq_MemWrPtl:
            g_assert(req.SnpType == Snp_NoOp);
            data_write = true;
            cache_update = true;
            if (req.MetaValue == MV_Shared)
                cache_nstate = CACHE_SHARED;
            else
                cache_nstate = CACHE_INVALID;
            // rsp = S2MRsp_CMP;
            break;
        case M2SReq_BIConflict:
        default:
            rsp = S2MRsp_CMP_ERROR;
            break;
        }

        if (cache_cstate != CACHE_INVALID)
            device_cache_update_block_sf(dcache, set, cache_blk, false);
    } else /* DEVICE_BIAS */ {
        switch (req.MemOpcode) {
        case M2SReq_MemRd:
            data_read = true;
            cache_update = true;
            if (req.MetaField == MF_NoOp) {
                g_assert(req.SnpType == Snp_SnpInv ||
                         req.SnpType == Snp_SnpCur);
                // cache_nstate = CACHE_INVALID;
                // rsp = S2MRsp_CMP;
                if (req.SnpType == Snp_SnpInv)
                    data_flush = true;
                else /* Snp_SnpCur */
                    cache_update = false;
                break;
            }
            switch (req.MetaValue) {
            case MV_Any:
                g_assert(req.SnpType == Snp_SnpInv);
                // cache_nstate = CACHE_INVALID;
                rsp = S2MRsp_CMP_EXCLUSIVE;
                break;
            case MV_Shared:
                g_assert(req.SnpType == Snp_SnpData);
                if (cache_cstate == CACHE_INVALID) {
                    // cache_nstate = CACHE_INVALID;
                    rsp = S2MRsp_CMP_EXCLUSIVE;
                } else /* CACHE_MODIFIED || CACHE_EXCLUSIVE || CACHE_SHARED */ {
                    cache_nstate = CACHE_SHARED;
                    rsp = S2MRsp_CMP_SHARED;
                }
                break;
            case MV_Invalid:
                g_assert(req.SnpType == Snp_SnpInv ||
                         req.SnpType == Snp_SnpCur);
                // cache_nstate = CACHE_INVALID;
                // rsp = S2MRsp_CMP;
                if (req.SnpType == Snp_SnpInv)
                    data_flush = true;
                else /* Snp_SnpCur */
                    cache_update = false;
                break;
            default:
                g_assert(0);
            }
            break;
        case M2SReq_MemInv:
        case M2SReq_MemInvNT:
            cache_update = true;
            if (req.MetaField == MF_NoOp) {
                g_assert(req.SnpType == Snp_SnpInv);
                data_flush = true;
                // cache_nstate = CACHE_INVALID;
                // rsp = S2MRsp_CMP;
                break;
            }
            switch (req.MetaValue) {
            case MV_Any:
                g_assert(req.SnpType == Snp_SnpInv);
                // cache_nstate = CACHE_INVALID;
                rsp = S2MRsp_CMP_EXCLUSIVE;
                break;
            case MV_Shared:
                g_assert(req.SnpType == Snp_SnpData);
                if (cache_cstate == CACHE_INVALID) {
                    // cache_nstate = CACHE_INVALID;
                    rsp = S2MRsp_CMP_EXCLUSIVE;
                } else /* CACHE_MODIFIED || CACHE_EXCLUSIVE || CACHE_SHARED */ {
                    cache_nstate = CACHE_SHARED;
                    rsp = S2MRsp_CMP_SHARED;
                }
                break;
            case MV_Invalid:
                g_assert(req.SnpType == Snp_SnpInv);
                data_flush = true;
                // cache_nstate = CACHE_INVALID;
                // rsp = S2MRsp_CMP;
                break;
            default:
                g_assert(0);
            }
            break;
        case M2SReq_MemRdData:
            g_assert(req.SnpType == Snp_SnpData);
            data_read = true;
            if (cache_cstate == CACHE_INVALID) {
                rsp = S2MRsp_CMP_EXCLUSIVE;
            } else /* CACHE_MODIFIED || CACHE_EXCLUSIVE || CACHE_SHARED */ {
                cache_update = true;
                cache_nstate = CACHE_SHARED;
                rsp = S2MRsp_CMP_SHARED;
            }
            break;
        case M2SReq_MemSpecRd:
            // non-rsp
            break;
        case M2SReq_MemClnEvct:
            g_assert(req.MetaValue == MV_Invalid || req.SnpType == Snp_NoOp);
            // host cache state will be changed to Invalid
            // rsp = S2MRsp_CMP;
            break;
        case M2SReq_MemWr:
        case M2SReq_MemWrPtl: /* to-do partial update */
            data_write = true;
            cache_update = true;
            switch (req.MetaValue) {
            case MV_Any:
                g_assert(req.SnpType == Snp_NoOp);
                // cache_nstate = CACHE_INVALID;
                // rsp = S2MRsp_CMP;
                break;
            case MV_Shared:
                g_assert(req.SnpType == Snp_NoOp);
                // cache_nstate = CACHE_INVALID;
                // rsp = S2MRsp_CMP;
                break;
            case MV_Invalid:
                g_assert(req.SnpType == Snp_SnpInv || req.SnpType == Snp_NoOp);
                // cache_nstate = CACHE_INVALID;
                // rsp = S2MRsp_CMP;
                break;
            default:
                g_assert(0);
            }
            break;
        case M2SReq_BIConflict:
            g_assert(req.SnpType == Snp_NoOp);
            rsp = S2MRsp_BI_ConflictAck;
            break;
        default:
            rsp = S2MRsp_CMP_ERROR;
            break;
        }

        if (cache_cstate != CACHE_INVALID)
            device_cache_update_block_sf(dcache, set, cache_blk, true);
    }

    if (data_read == true) {
        if (cache_cstate != CACHE_INVALID) {
            device_cache_data_read(dcache, daddr, set, cache_blk,
                                   (uint64_t *)buf, size);
        } else {
            if (MEMTX_OK != address_space_read(as, daddr, attrs, buf, size)) {
                return S2MRsp_CMP_ERROR;
            }
        }
    }
    if (data_write == true) {
        if (MEMTX_OK != address_space_write(as, daddr, attrs, buf, size)) {
            return S2MRsp_CMP_ERROR;
        }
    }
    if (data_flush == true) {
        if (cache_cstate != CACHE_INVALID) {
            blk_addr = device_cache_extract_block_addr(dcache, set, cache_blk);
            if (MEMTX_OK !=
                address_space_write(as, daddr, attrs, blk_addr, size)) {
                return S2MRsp_CMP_ERROR;
            }
        }
    }
    if (cache_update == true) {
        if (cache_cstate != CACHE_INVALID) {
            device_cache_update_block_state(dcache, tag, set, cache_blk,
                                            cache_nstate);
        }
    }

    if (rsp == S2MRsp_CMP) {
        g_hash_table_remove(dcoh->sf_table, (gpointer)daddr);
    } else {
        g_hash_table_insert(dcoh->sf_table, (gpointer)daddr, (gpointer) true);
    }

    return rsp;
}
/*
S2MRsp cxl_device_type2_dcoh_access(AddressSpace *as, uint64_t daddr,
                                                                                CXLMemReq req, uint8_t *buf, uint32_t size, MemTxAttrs attrs)
{
        CacheCommand cmd = CACHE_READ;
        S2MRsp rsp = S2MRsp_CMP;
        CacheCheck cache_check = CACHE_MISS;
        uint64_t assem_addr, tag, set;
        int32_t cache_blk;
        uint8_t *blk_addr;

        if (req.MetaValue != MV_Invalid) {
                if (req.SnpType == Snp_SnpInv)
                        rsp = S2MRsp_CMP_EXCLUSIVE;
                else if (req.SnpType == Snp_SnpData)
                        rsp = S2MRsp_CMP_SHARED;
        }

        tag = device_cache_extract_tag(dcache, daddr);
        set = device_cache_extract_set(dcache, daddr);

        cache_blk = device_cache_find_valid_block(dcache, tag, set);

        if (cache_blk != -1)
                cache_check = CACHE_HIT;

        if (HOST_BIAS == cxl_device_type2_dcoh_bias_lookup(daddr) || req.SnpType
== Snp_SnpInv || req.SnpType == Snp_NoOp) { switch (req.MemOpcode)
                {
                case M2SReq_MemInv:
                case M2SReq_MemInvNT:
                        break;
                case M2SReq_MemRd:
                case M2SReq_MemRdData:
                        if (CACHE_HIT == cache_check)
                                device_cache_data_read(dcache, daddr, set,
cache_blk, (uint64_t *)buf, size); else if (MEMTX_OK != address_space_read(as,
daddr, attrs, buf, size)) rsp = S2MRsp_CMP_ERROR; break; case M2SReq_MemWr: case
M2SReq_MemWrPtl: if (CACHE_SHARED == device_cache_extract_block_state(dcache,
set, cache_blk)) { if (device_cache_extract_block_sf(dcache, set, cache_blk) ==
true) { device_cache_update_block_state(dcache, tag, set, cache_blk,
CACHE_INVALID);
                                }
                                else {
                                        device_cache_update_block_state(dcache,
tag, set, cache_blk, CACHE_EXCLUSIVE);
                                }
                        }
                        if (MEMTX_OK != address_space_write(as, daddr, attrs,
buf, size)) rsp = S2MRsp_CMP_ERROR; break; default: rsp = S2MRsp_CMP_ERROR;
                        break;
                }

                if (CACHE_HIT == cache_check) {
                        device_cache_update_block_state(dcache, tag, set,
cache_blk, CACHE_INVALID); device_cache_update_block_sf(dcache, set, cache_blk,
false);
                }

                goto out;
        }

        switch (req.MemOpcode)
        {
        case M2SReq_MemRd:
                if (req.MetaValue == MV_Invalid) {
                        if (MEMTX_OK != address_space_read(as, daddr, attrs,
buf, size)) { rsp = S2MRsp_CMP_ERROR; goto out;
                        }

                        if (CACHE_HIT == cache_check) {
                                if (req.SnpType == Snp_SnpInv) {
                                        device_cache_update_block_state(dcache,
tag, set, cache_blk, CACHE_INVALID);
                                }
                        }
                        goto out;
                }
                else {
                        cmd = CACHE_READ;
                }
                break;

        case M2SReq_MemInv:
        case M2SReq_MemInvNT:
                if (CACHE_HIT == cache_check) {
                        if (req.SnpType == Snp_SnpInv)
                                device_cache_update_block_state(dcache, tag,
set, cache_blk, CACHE_INVALID); else if (req.SnpType == Snp_SnpData)
                                device_cache_update_block_state(dcache, tag,
set, cache_blk, CACHE_SHARED);
                }
                goto out;

        case M2SReq_MemSpecRd:
        case M2SReq_MemClnEvct:
                goto out;

        case M2SReq_MemWr:
                cmd = CACHE_UPDATE;
                break;

        default:
                g_assert(0);
        }

        if (CACHE_HIT == cache_check) {
                if (cmd == CACHE_READ) {
                        device_cache_data_read(dcache, daddr, set, cache_blk,
(uint64_t *)buf, size); if (req.SnpType == Snp_SnpData)
                                device_cache_update_block_state(dcache, tag,
set, cache_blk, CACHE_SHARED);
                }
                else if (cmd == CACHE_UPDATE) {
                        device_cache_data_write(dcache, daddr, set, cache_blk,
(uint64_t *)buf, size);
                }
        }
        else {
                cache_blk = device_cache_find_invalid_block(dcache, set);

                if (cache_blk == -1) {
                        cache_blk = device_cache_find_replace_block(dcache,
set); blk_addr = device_cache_extract_block_addr(dcache, set, cache_blk);

                        assem_addr = device_cache_assem_daddr(dcache, set,
cache_blk);

                        CXL_DCOH_BIAS(assem_addr, "cache miss -> vitctim write
-> as write - daddr: 0x%lx, data: 0x%lx", assem_addr, *(uint64_t *)blk_addr);
                        device_cache_print_data_block(dcache, set, cache_blk);

                        if (MEMTX_OK != address_space_write(as, assem_addr,
attrs, blk_addr, DEVICE_BLKSIZE)) { rsp = S2MRsp_CMP_ERROR; goto out;
                        }
                        device_cache_update_block_state(dcache, tag, set,
cache_blk, CACHE_INVALID);
                }

                CXL_DCOH_BIAS(daddr, "cache miss -> read memory -> as read -
daddr: 0x%lx", daddr);

                blk_addr = device_cache_extract_block_addr(dcache, set,
cache_blk); if (MEMTX_OK != address_space_read(as, daddr, attrs, blk_addr,
DEVICE_BLKSIZE)) { rsp = S2MRsp_CMP_ERROR; goto out;
                }

                CXL_DCOH_BIAS(daddr, "cache miss -> read done -> as read -
daddr: 0x%lx, data: 0x%lx", daddr, *(uint64_t *)blk_addr);
                device_cache_print_data_block(dcache, set, cache_blk);

                if (req.SnpType == Snp_SnpData)
                        device_cache_update_block_state(dcache, tag, set,
cache_blk, CACHE_SHARED); else device_cache_update_block_state(dcache, tag, set,
cache_blk, CACHE_EXCLUSIVE);

                cache_blk = device_cache_find_valid_block(dcache, tag, set);

                if (cache_blk != -1) {
                        if (cmd == CACHE_READ) {
                                device_cache_data_read(dcache, daddr, set,
cache_blk, (uint64_t *)buf, size);
                        }
                        else if (cmd == CACHE_UPDATE) {
                                device_cache_data_write(dcache, daddr, set,
cache_blk, (uint64_t *)buf, size);
                        }
                }
                else {
                        g_assert(0);
                }
        }
        device_cache_update_block_sf(dcache, set, cache_blk, true);

out:
        return rsp;
}
*/
void cxl_device_type2_dcoh_init(PCIDevice *d)
{
    QemuThread thread;

    cxl_device_cache_init(&dcache);
    dcoh = __device_dcoh_init();

    rng_opc = g_rand_new();
    rng_addr = g_rand_new();
    rng_size = g_rand_new();

    qemu_spin_lock(&ct2d_lock);

    qemu_thread_create(&thread, "ct2d_device_main", __ct2d_device_main, d,
                       QEMU_THREAD_JOINABLE);

    qemu_spin_unlock(&ct2d_lock);

    CXL_DEBUG("ct2 device dcoh realized");
}

void cxl_device_type2_dcoh_release(void)
{
    __device_dcoh_free(dcoh);
    cxl_device_cache_release(&dcache);

    CXL_DEBUG("ct2 device dcoh released");
}
