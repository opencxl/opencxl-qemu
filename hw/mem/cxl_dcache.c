/*
 * QEMU CXL Device Cache Implementation
 *
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_dcache.h"
#include "hw/cxl/cxl_type1_dcoh.h"
#include "hw/cxl/cxl_type2_dcoh.h"

static GRand *rng_set;
static GRand *rng_assoc;

static void __device_cache_priority_init(Cache *cache)
{
    for (uint32_t set = 0; set < cache->num_sets; set++) {
        cache->sets[set].priority = g_new0(uint64_t, cache->assoc);
        cache->sets[set].counter = 0;
    }
}

static void __device_cache_priority_destroy(Cache *cache)
{
    for (uint32_t set = 0; set < cache->num_sets; set++) {
        g_free(cache->sets[set].priority);
    }
}

static void __device_cache_priority_update(Cache *cache, uint32_t set_idx,
                                           int32_t blk_idx)
{
    CacheSet *set = &cache->sets[set_idx];

    set->priority[blk_idx] = cache->sets[set_idx].counter;
    set->counter++;
}

static Cache *__device_cache_init(void)
{
    Cache *cache;

    cache = g_new(Cache, 1);
    cache->assoc = DEVICE_ASSOC;
    cache->cachesize = DEVICE_CACHESIZE;
    cache->num_sets = DEVICE_SET;
    cache->sets = g_new(CacheSet, cache->num_sets);

    for (uint32_t set = 0; set < cache->num_sets; set++) {
        cache->sets[set].blocks = g_new0(CacheBlock, DEVICE_ASSOC);
        for (uint32_t blk = 0; blk < DEVICE_ASSOC; blk++) {
            cache->sets[set].blocks[blk].data = g_new0(uint8_t, DEVICE_BLKSIZE);
        }
    }

    cache->blk_mask = DEVICE_BLKSIZE - 1;
    cache->set_mask = ((cache->num_sets - 1) << DEVICE_BLKSIZE_BIT);
    cache->tag_mask = ~(cache->set_mask | cache->blk_mask);

    __device_cache_priority_init(cache);

    return cache;
}

static void __device_cache_free(Cache *cache)
{
    for (uint64_t set = 0; set < cache->num_sets; set++) {
        g_free(cache->sets[set].blocks);
    }

    __device_cache_priority_destroy(cache);

    g_free(cache->sets);
    g_free(cache);
}

uint64_t device_cache_extract_tag(Cache *cache, uint64_t daddr)
{
    return (daddr & cache->tag_mask) >> (DEVICE_SET_BIT + DEVICE_BLKSIZE_BIT);
}

uint64_t device_cache_extract_set(Cache *cache, uint64_t daddr)
{
    return (daddr & cache->set_mask) >> DEVICE_BLKSIZE_BIT;
}

bool device_cache_extract_block_sf(Cache *cache, uint64_t set, int32_t blk)
{
    return cache->sets[set].blocks[blk].sf;
}

void device_cache_update_block_sf(Cache *cache, uint64_t set, int32_t blk,
                                  bool snoop)
{
    cache->sets[set].blocks[blk].sf = snoop;
}

CacheState device_cache_extract_block_state(Cache *cache, uint64_t set,
                                            int32_t blk)
{
    return cache->sets[set].blocks[blk].state;
}

uint8_t *device_cache_extract_block_addr(Cache *cache, uint64_t set,
                                         int32_t blk)
{
    return cache->sets[set].blocks[blk].data;
}

uint64_t device_cache_assem_daddr(Cache *cache, uint64_t set, int32_t blk)
{
    uint64_t tag = cache->sets[set].blocks[blk].tag;
    g_assert(cache->sets[set].blocks[blk].state != CACHE_INVALID);

    return tag << (DEVICE_SET_BIT + DEVICE_BLKSIZE_BIT) |
           set << DEVICE_BLKSIZE_BIT;
}

void device_cache_update_block_state(Cache *cache, uint64_t tag, uint64_t set,
                                     int32_t blk, CacheState state)
{
    if (state != CACHE_INVALID)
        __device_cache_priority_update(cache, set, blk);

    cache->sets[set].blocks[blk].tag = tag;
    cache->sets[set].blocks[blk].state = state;
}

int32_t device_cache_find_replace_block(Cache *cache, uint64_t set)
{
    uint32_t min_idx, min_priority;

    min_priority = cache->sets[set].priority[0];
    min_idx = 0;

    for (uint32_t idx = 1; idx < cache->assoc; idx++) {
        if (cache->sets[set].priority[idx] < min_priority) {
            min_priority = cache->sets[set].priority[idx];
            min_idx = idx;
        }
    }
    return min_idx;
}

int32_t device_cache_find_invalid_block(Cache *cache, uint64_t set)
{
    for (uint32_t blk = 0; blk < cache->assoc; blk++) {
        if (cache->sets[set].blocks[blk].state == CACHE_INVALID) {
            return blk;
        }
    }
    return -1;
}

int32_t device_cache_find_valid_block(Cache *cache, uint64_t tag, uint64_t set)
{
    for (uint32_t blk = 0; blk < cache->assoc; blk++) {
        if ((cache->sets[set].blocks[blk].tag == tag) &&
            (cache->sets[set].blocks[blk].state != CACHE_INVALID)) {
            return blk;
        }
    }
    return -1;
}

void device_cache_print_data_block(Cache *cache, uint64_t set, int32_t blk)
{
#if (CXL_DUMP_CACHE == 1)
    for (uint32_t i = 0; i < DEVICE_BLKSIZE; i += 8) {
        error_report("%x %x %x %x %x %x %x %x",
                     cache->sets[set].blocks[blk].data[i],
                     cache->sets[set].blocks[blk].data[i + 1],
                     cache->sets[set].blocks[blk].data[i + 2],
                     cache->sets[set].blocks[blk].data[i + 3],
                     cache->sets[set].blocks[blk].data[i + 4],
                     cache->sets[set].blocks[blk].data[i + 5],
                     cache->sets[set].blocks[blk].data[i + 6],
                     cache->sets[set].blocks[blk].data[i + 7]);
    }
#endif
}

/*
static void __device_cache_state_update(Cache *cache, SnpType snp, uint64_t set,
uint32_t blk)
{
        switch (snp) {
        case Snp_SnpInv:
                cache->sets[set].blocks[blk].state = CACHE_INVALID;
                break;
        case Snp_SnpData:
                cache->sets[set].blocks[blk].state = CACHE_SHARED;
                break;
        case Snp_SnpCur:
        case Snp_NoOp:
        default:
                break;
        }
}
*/

void device_cache_data_read(Cache *cache, uint64_t daddr, uint64_t set,
                            int32_t blk, uint64_t *data, uint32_t size)
{
    uint32_t offset = daddr & cache->blk_mask;

    memmove(data, &cache->sets[set].blocks[blk].data[offset], size);
    CXL_DCOH_BIAS(daddr,
                  "cache hit -> read daddr: 0x%lx, data: 0x%lx, size: %d",
                  daddr, *data, size);

    __device_cache_priority_update(cache, set, blk);
}

void device_cache_data_write(Cache *cache, uint64_t daddr, uint64_t set,
                             int32_t blk, uint64_t *data, uint32_t size)
{
    uint32_t offset = daddr & cache->blk_mask;

    CXL_DCOH_BIAS(daddr,
                  "cache hit -> update daddr: 0x%lx, data: 0x%lx, size: %d",
                  daddr, *data, size);
    memmove(&cache->sets[set].blocks[blk].data[offset], data, size);
    cache->sets[set].blocks[blk].state = CACHE_MODIFIED;

    __device_cache_priority_update(cache, set, blk);
}

uint64_t device_cache_rand_valid_block(Cache *cache)
{
    uint64_t valid_daddr = -1;
    uint64_t set = g_rand_int_range(rng_set, 0, DEVICE_SET);
    uint32_t blk = g_rand_int_range(rng_assoc, 0, DEVICE_ASSOC);

    if (cache->sets[set].blocks[blk].state != CACHE_INVALID) {
        valid_daddr = device_cache_assem_daddr(cache, set, blk);
    }

    return valid_daddr;
}

void cxl_device_cache_init(Cache **cache)
{
    *cache = __device_cache_init();

    rng_set = g_rand_new();
    rng_assoc = g_rand_new();

    CXL_DEBUG("ct2 device cache realized");
}

void cxl_device_cache_release(Cache **cache)
{
    __device_cache_free(*cache);

    CXL_DEBUG("ct2 device cache released");
}
