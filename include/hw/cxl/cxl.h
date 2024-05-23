/*
 * QEMU CXL Support
 *
 * Copyright (c) 2020 Intel
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#ifndef CXL_H
#define CXL_H

#include "qapi/qapi-types-machine.h"
#include "qapi/qapi-visit-machine.h"
#include "qemu/typedefs.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pcie_port.h"
#include "cxl_pci.h"
#include "cxl_component.h"
#include "cxl_device.h"

#define CXL_COMPONENT_REG_BAR_IDX 0
#define CXL_DEVICE_REG_BAR_IDX 2

#define CXL_WINDOW_MAX 10

typedef struct CXLHost CXLHost;
#define CXL_BOOT_WAIT_TIME 30000000
#define CXL_THREAD_DELAY 20

#define CXL_DUMP_CACHE 0
#define CXL_DEBUG_PRINT 0
#define CXL_THREAD_PRINT 0
#define CXL_HCOH_BIAS_PRINT 0
#define CXL_DCOH_BIAS_PRINT 0

#if (CXL_DEBUG_PRINT == 1)
#define CXL_DEBUG(fmt, args...)                                   \
    do {                                                          \
        error_report("[%s:%d] " fmt, __func__, __LINE__, ##args); \
    } while (0)
#else
#define CXL_DEBUG(fmt, args...) \
    do {                        \
    } while (0)
#endif

#if (CXL_THREAD_PRINT == 1)
#define CXL_THREAD(fmt, args...)                                  \
    do {                                                          \
        error_report("[%s:%d] " fmt, __func__, __LINE__, ##args); \
    } while (0)
#else
#define CXL_THREAD(fmt, args...) \
    do {                         \
    } while (0)
#endif

typedef struct PXBDev PXBDev;

typedef struct PXBDev {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    uint8_t bus_nr;
    uint16_t numa_node;
    bool bypass_iommu;
    bool hdm_for_passthrough;
    struct cxl_dev {
        CXLHost *cxl_host_bridge; /* Pointer to a CXLHost */
    } cxl;
} PXBDev;

#define TYPE_PXB_CXL_DEVICE "pxb-cxl"
DECLARE_INSTANCE_CHECKER(PXBDev, PXB_CXL_DEV, TYPE_PXB_CXL_DEVICE)

typedef struct CXLFixedWindow {
    uint64_t size;
    char **targets;
    PXBDev *target_hbs[8];
    uint8_t num_targets;
    uint8_t enc_int_ways;
    uint8_t enc_int_gran;
    /* Todo: XOR based interleaving */
    MemoryRegion mr;
    hwaddr base;
} CXLFixedWindow;

typedef struct CXLState {
    bool is_enabled;
    MemoryRegion host_mr;
    unsigned int next_mr_idx;
    GList *fixed_windows;
    CXLFixedMemoryWindowOptionsList *cfmw_list;
} CXLState;

struct CXLHost {
    PCIHostState parent_obj;

    CXLComponentState cxl_cstate;
    bool passthrough;
};

#define TYPE_PXB_CXL_HOST "pxb-cxl-host"
OBJECT_DECLARE_SIMPLE_TYPE(CXLHost, PXB_CXL_HOST)

typedef struct CXLRemoteUpstreamPort {
    /*< private >*/
    PCIEPort parent_obj;
    MemoryRegion bar0;
} CXLRemoteUpstreamPort;

#define TYPE_CXL_REMOTE_USP "cxl-remote-upstream"
DECLARE_INSTANCE_CHECKER(CXLRemoteUpstreamPort, CXL_REMOTE_USP,
                         TYPE_CXL_REMOTE_USP)

typedef struct CXLUpstreamPort {
    /*< private >*/
    PCIEPort parent_obj;

    /*< public >*/
    CXLComponentState cxl_cstate;
    DOECap doe_cdat;
} CXLUpstreamPort;

#define TYPE_CXL_USP "cxl-upstream"
DECLARE_INSTANCE_CHECKER(CXLUpstreamPort, CXL_USP, TYPE_CXL_USP)

typedef struct CXLRemoteDownstreamPort {
    /*< private >*/
    PCIESlot parent_obj;
    MemoryRegion bar0;
} CXLRemoteDownstreamPort;

#define TYPE_CXL_REMOTE_DSP "cxl-remote-downstream"
DECLARE_INSTANCE_CHECKER(CXLRemoteDownstreamPort, CXL_REMOTE_DSP,
                         TYPE_CXL_REMOTE_DSP)

typedef struct CXLDownstreamPort {
    /*< private >*/
    PCIESlot parent_obj;

    /*< public >*/
    CXLComponentState cxl_cstate;
} CXLDownstreamPort;

#define TYPE_CXL_DSP "cxl-downstream"
DECLARE_INSTANCE_CHECKER(CXLDownstreamPort, CXL_DSP, TYPE_CXL_DSP)

#endif
