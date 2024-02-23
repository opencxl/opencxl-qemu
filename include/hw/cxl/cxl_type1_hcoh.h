/*
 * QEMU CXL Host Type1 HCOH Configuration
 *
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef CXL_TYPE1_HCOH_H
#define CXL_TYPE1_HCOH_H

#define CFMWS_BASE_ADDR (0x490000000)

BiasState cxl_host_type1_hcoh_bias_lookup(uint64_t haddr);
MemTxResult cxl_host_type1_hcoh_read(PCIDevice *d, uint64_t haddr,
                                     uint64_t *data, uint32_t size,
                                     MemTxAttrs attrs);
MemTxResult cxl_host_type1_hcoh_write(PCIDevice *d, uint64_t haddr,
                                      uint64_t data, uint32_t size,
                                      MemTxAttrs attrs);
H2DRsp cxl_host_type1_hcoh_response(PCIDevice *d, CXLCacheReq req, uint8_t *buf,
                                    unsigned size, MemTxAttrs attrs);

void cxl_host_type1_hcoh_init(PCIDevice *d);
void cxl_host_type1_hcoh_release(void);

#endif
