/*
 * QEMU CXL Device Type2 DCOH Configuration
 *
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef CXL_TYPE2_DCOH_H
#define CXL_TYPE2_DCOH_H

#define CFMWS_BASE_ADDR (0x490000000)
#define DEVICE_BIAS_CACHE_SIZE (2)
#define DEVICE_BIAS_ENTRY_SIZE (0x8000000)

typedef struct {
    GHashTable *sf_table;
    uint32_t *bias_cache;
    uint32_t bias_cache_size;
    uint32_t bias_entry_size;
} DeviceCoh;

#if (CXL_DCOH_BIAS_PRINT == 1)
#define CXL_DCOH_BIAS(addr, fmt, args...)                             \
    do {                                                              \
        if (DEVICE_BIAS == cxl_device_type2_dcoh_bias_lookup(addr)) { \
            error_report("[%s:%d] " fmt, __func__, __LINE__, ##args); \
        }                                                             \
    } while (0)
#else
#define CXL_DCOH_BIAS(fmt, args...) \
    do {                            \
    } while (0)
#endif

BiasState cxl_device_type2_dcoh_bias_lookup(uint64_t daddr);
S2MRsp cxl_device_type2_dcoh_access(AddressSpace *as, uint64_t daddr,
                                    CXLMemReq req, uint8_t *buf, uint32_t size,
                                    MemTxAttrs attrs);

void cxl_device_type2_dcoh_init(PCIDevice *d);
void cxl_device_type2_dcoh_release(void);

#endif
