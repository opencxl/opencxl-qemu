/*
 * QEMU CXL Device Type1 DCOH Configuration
 *
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef CXL_TYPE1_DCOH_H
#define CXL_TYPE1_DCOH_H

#define CFMWS_BASE_ADDR (0x490000000)

BiasState cxl_device_type1_dcoh_bias_lookup(uint64_t daddr);
D2HRsp cxl_device_type1_dcoh_access(AddressSpace *as, uint64_t daddr,
                                    CXLCacheReq req, uint8_t *buf,
                                    uint32_t size, MemTxAttrs attrs);

void cxl_device_type1_dcoh_init(PCIDevice *d);
void cxl_device_type1_dcoh_release(void);

#endif
