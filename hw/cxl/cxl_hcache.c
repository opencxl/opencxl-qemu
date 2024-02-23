/*
 * QEMU CXL Host Cache Implementation
 *
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"

#include "hw/cxl/cxl_hcache.h"
#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_type1_hcoh.h"
#include "hw/cxl/cxl_type2_hcoh.h"

static void __host_cache_priority_init(Cache *cache)
{
    for (uint32_t set = 0; set < cache->num_sets; set++) {
        cache->sets[set].priority = g_new0(uint64_t, cache->assoc);
        cache->sets[set].counter = 0;
    }
}

static void __host_cache_priority_destroy(Cache *cache)
{
    for (uint32_t set = 0; set < cache->num_sets; set++) {
        g_free(cache->sets[set].priority);
    }
}

static void __host_cache_priority_update(Cache *cache, uint32_t set_idx,
                                         int32_t blk_idx)
{
    CacheSet *set = &cache->sets[set_idx];

    set->priority[blk_idx] = cache->sets[set_idx].counter;
    set->counter++;
}

static Cache *__host_cache_init(void)
{
    Cache *cache;

    cache = g_new(Cache, 1);
    cache->assoc = HOST_ASSOC;
    cache->cachesize = HOST_CACHESIZE;
    cache->num_sets = HOST_SET;
    cache->sets = g_new(CacheSet, cache->num_sets);

    for (uint32_t set = 0; set < cache->num_sets; set++) {
        cache->sets[set].blocks = g_new0(CacheBlock, HOST_ASSOC);
        for (uint32_t blk = 0; blk < HOST_ASSOC; blk++) {
            cache->sets[set].blocks[blk].data = g_new0(uint8_t, HOST_BLKSIZE);
        }
    }

    cache->blk_mask = HOST_BLKSIZE - 1;
    cache->set_mask = ((cache->num_sets - 1) << HOST_BLKSIZE_BIT);
    cache->tag_mask = ~(cache->set_mask | cache->blk_mask);

    __host_cache_priority_init(cache);

    return cache;
}

static void __host_cache_free(Cache *cache)
{
    for (uint64_t set = 0; set < cache->num_sets; set++) {
        g_free(cache->sets[set].blocks);
    }

    __host_cache_priority_destroy(cache);

    g_free(cache->sets);
    g_free(cache);
}

uint64_t host_cache_extract_tag(Cache *cache, uint64_t haddr)
{
    return (haddr & cache->tag_mask) >> (HOST_SET_BIT + HOST_BLKSIZE_BIT);
}

uint64_t host_cache_extract_set(Cache *cache, uint64_t haddr)
{
    return (haddr & cache->set_mask) >> HOST_BLKSIZE_BIT;
}

CacheState host_cache_extract_block_state(Cache *cache, uint64_t set,
                                          int32_t blk)
{
    return cache->sets[set].blocks[blk].state;
}

uint8_t *host_cache_extract_block_addr(Cache *cache, uint64_t set, int32_t blk)
{
    return cache->sets[set].blocks[blk].data;
}

uint64_t host_cache_assem_haddr(Cache *cache, uint64_t set, int32_t blk)
{
    uint64_t tag = cache->sets[set].blocks[blk].tag;

    if (cache->sets[set].blocks[blk].state != CACHE_INVALID) {
        return tag << (HOST_SET_BIT + HOST_BLKSIZE_BIT) |
               set << HOST_BLKSIZE_BIT;
    }
    return -1;
}

void host_cache_update_block_state(Cache *cache, uint64_t tag, uint64_t set,
                                   int32_t blk, CacheState state)
{
    if (state != CACHE_INVALID)
        __host_cache_priority_update(cache, set, blk);

    cache->sets[set].blocks[blk].tag = tag;
    cache->sets[set].blocks[blk].state = state;
}

int32_t host_cache_find_replace_block(Cache *cache, uint64_t set)
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

int32_t host_cache_find_invalid_block(Cache *cache, uint64_t set)
{
    for (uint32_t blk = 0; blk < cache->assoc; blk++) {
        if (cache->sets[set].blocks[blk].state == CACHE_INVALID) {
            return blk;
        }
    }
    return -1;
}

int32_t host_cache_find_valid_block(Cache *cache, uint64_t tag, uint64_t set)
{
    for (uint32_t blk = 0; blk < cache->assoc; blk++) {
        if ((cache->sets[set].blocks[blk].tag == tag) &&
            (cache->sets[set].blocks[blk].state != CACHE_INVALID)) {
            return blk;
        }
    }
    return -1;
}

void host_cache_print_data_block(Cache *cache, uint64_t set, int32_t blk)
{
#if (CXL_DUMP_CACHE == 1)
    for (uint32_t i = 0; i < HOST_BLKSIZE; i += 8) {
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

void host_cache_data_read(Cache *cache, uint64_t haddr, uint64_t set,
                          int32_t blk, uint64_t *data, uint32_t size)
{
    uint32_t offset = haddr & cache->blk_mask;

    memmove(data, &cache->sets[set].blocks[blk].data[offset], size);
    CXL_HCOH_BIAS(haddr,
                  "cache hit -> read haddr: 0x%lx, data: 0x%lx, size: %d",
                  haddr, *data, size);

    __host_cache_priority_update(cache, set, blk);
}

void host_cache_data_write(Cache *cache, uint64_t haddr, uint64_t set,
                           int32_t blk, uint64_t *data, uint32_t size)
{
    uint32_t offset = haddr & cache->blk_mask;

    CXL_HCOH_BIAS(haddr,
                  "cache hit -> update haddr: 0x%lx, data: 0x%lx, size: %d",
                  haddr, *data, size);
    memmove(&cache->sets[set].blocks[blk].data[offset], data, size);
    cache->sets[set].blocks[blk].state = CACHE_MODIFIED;

    __host_cache_priority_update(cache, set, blk);
}

void cxl_host_cache_init(Cache **cache)
{
    *cache = __host_cache_init();

    CXL_DEBUG("ct2 host cache realized");
}

void cxl_host_cache_release(Cache **cache)
{
    __host_cache_free(*cache);

    CXL_DEBUG("ct2 host cache released");
}
