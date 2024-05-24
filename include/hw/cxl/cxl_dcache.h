/*
 * QEMU CXL Device Cache Configuration
 *
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef CXL_DCACHE_H
#define CXL_DCACHE_H

/*
 * A CacheSet is a set of cache blocks. A memory block that maps to a set can be
 * put in any of the blocks inside the set. The number of block per set is
 * called the associativity (assoc).
 *
 * Each block contains the stored tag and a valid bit. Since this is not
 * a functional simulator, the data itself is not stored. We only identify
 * whether a block is in the cache or not by searching for its tag.
 *
 * In order to search for memory data in the cache, the set identifier and tag
 * are extracted from the address and the set is probed to see whether a tag
 * match occur.
 *
 * An address is logically divided into three portions: The block offset,
 * the set number, and the tag.
 *
 * The set number is used to identify the set in which the block may exist.
 * The tag is compared against all the tags of a set to search for a match. If a
 * match is found, then the access is a hit.
 *
 * The CacheSet also contains bookkeaping information about eviction details.
 */

#define DEVICE_BLKSIZE_BIT (6)
#define DEVICE_BLKSIZE (1 << DEVICE_BLKSIZE_BIT) // fixed 64B aligned
#define DEVICE_ASSOC_BIT (2)
#define DEVICE_ASSOC (1 << DEVICE_ASSOC_BIT) // # of association (cache columns)
#define DEVICE_SET_BIT (3)
#define DEVICE_SET (1 << DEVICE_SET_BIT) // # of set (cache rows)
#define DEVICE_CACHESIZE (DEVICE_BLKSIZE * DEVICE_ASSOC * DEVICE_SET)

typedef enum {
    CACHE_MISS = 0,
    CACHE_HIT,
} CacheCheck;

typedef enum {
    CACHE_READ = 0,
    CACHE_UPDATE,
} CacheCommand;

typedef enum {
    CACHE_INVALID = 0,
    CACHE_SHARED,
    CACHE_EXCLUSIVE,
    CACHE_MODIFIED,
} CacheState;

typedef struct {
    uint64_t sf    : 1;
    uint64_t state : 2;
    uint64_t tag   : 61;
    uint8_t *data;
} CacheBlock;

typedef struct {
    CacheBlock *blocks;
    uint64_t *priority;
    uint64_t counter;
} CacheSet;

typedef struct {
    CacheSet *sets;
    uint32_t num_sets;
    uint32_t cachesize;
    uint32_t assoc;
    uint64_t blk_mask;
    uint64_t set_mask;
    uint64_t tag_mask;
} Cache;

uint64_t device_cache_extract_tag(Cache *cache, uint64_t daddr);
uint64_t device_cache_extract_set(Cache *cache, uint64_t daddr);
bool device_cache_extract_block_sf(Cache *cache, uint64_t set, int32_t blk);
CacheState device_cache_extract_block_state(Cache *cache, uint64_t set,
                                            int32_t blk);
uint8_t *device_cache_extract_block_addr(Cache *cache, uint64_t set,
                                         int32_t blk);
uint64_t device_cache_assem_daddr(Cache *cache, uint64_t set, int32_t blk);

void device_cache_update_block_sf(Cache *cache, uint64_t set, int32_t blk,
                                  bool snoop);
void device_cache_update_block_state(Cache *cache, uint64_t tag, uint64_t set,
                                     int32_t blk, CacheState state);
int32_t device_cache_find_replace_block(Cache *cache, uint64_t set);
int32_t device_cache_find_invalid_block(Cache *cache, uint64_t set);
int32_t device_cache_find_valid_block(Cache *cache, uint64_t tag, uint64_t set);
void device_cache_print_data_block(Cache *cache, uint64_t set, int32_t blk);

void device_cache_data_read(Cache *cache, uint64_t daddr, uint64_t set,
                            int32_t blk, uint64_t *data, uint32_t size);
void device_cache_data_write(Cache *cache, uint64_t daddr, uint64_t set,
                             int32_t blk, uint64_t *data, uint32_t size);
uint64_t device_cache_rand_valid_block(Cache *cache);

void cxl_device_cache_init(Cache **cache);
void cxl_device_cache_release(Cache **cache);

#endif
