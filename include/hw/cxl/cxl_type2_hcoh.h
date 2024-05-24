/*
 * QEMU CXL Host Type2 HCOH Configuration
 *
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef CXL_TYPE2_HCOH_H
#define CXL_TYPE2_HCOH_H

#define CFMWS_BASE_ADDR (0x490000000)
#define HOST_BIAS_TABLE_SIZE (2)
#define HOST_BIAS_ENTRY_SIZE (0x8000000)

typedef struct {
    uint32_t *bias_table;
    uint32_t bias_table_size;
    uint32_t bias_entry_size;
} HostCoh;

typedef enum {
    MEM_Read_MemInv = 0,
    MEM_NDR_MemInv,
    MEM_NDR_MemShared,
    MEM_NDR_HCacheInv,
    MEM_NDR_SpecRd,
    MEM_NDR_ClnEvct,
} MemCommand;

#if (CXL_HCOH_BIAS_PRINT == 1)
#define CXL_HCOH_BIAS(addr, fmt, args...)                             \
    do {                                                              \
        if (1) {                                                      \
            error_report("[%s:%d] " fmt, __func__, __LINE__, ##args); \
        }                                                             \
    } while (0)
#else
#define CXL_HCOH_BIAS(fmt, args...) \
    do {                            \
    } while (0)
#endif

BiasState cxl_host_type2_hcoh_bias_lookup(uint64_t haddr);
MemTxResult cxl_host_type2_hcoh_read(PCIDevice *d, uint64_t haddr,
                                     uint64_t *data, uint32_t size,
                                     MemTxAttrs attrs);
MemTxResult cxl_host_type2_hcoh_write(PCIDevice *d, uint64_t haddr,
                                      uint64_t data, uint32_t size,
                                      MemTxAttrs attrs);
MemTxResult cxl_host_type2_hcoh_command(PCIDevice *d, uint64_t haddr,
                                        uint8_t *buf, MemTxAttrs attrs);
M2SRsp_BIRsp cxl_host_type2_hcoh_response(CXLMemReq request, MemTxAttrs attrs);

void cxl_host_type2_hcoh_init(PCIDevice *d);
void cxl_host_type2_hcoh_release(void);

#endif
