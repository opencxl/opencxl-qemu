/*
 * Emulated CXL Switch Downstream Port
 *
 * Copyright (c) 2022 Huawei Technologies.
 * Copyright (c) 2024 EEUM, Inc.
 *
 * Based on xio3130_downstream.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"

#include "exec/memory.h"
#include "hw/cxl/cxl.h"
#include "hw/pci/msi.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_port.h"
#include "trace.h"

static uint64_t cxl_dsp_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    trace_cxl_dsp_debug_message("Sending MMIO Read");

    PCIDevice *pci_dev = opaque;
    CXLRemoteDownstreamPort *dsp = CXL_REMOTE_DSP(pci_dev);
    PCIDevice *root_port = cxl_get_root_port(pci_dev);
    bool is_remote = root_port != NULL && cxl_is_remote_root_port(root_port);
    uint64_t addr = offset + dsp->bar0.addr;
    uint64_t value = 0xFFFFFFFF;

    assert(is_remote);

    cxl_remote_mem_read(root_port, addr, &value, size);

    trace_cxl_dsp_debug_message("Received MMIO Read Completion");

    return value;
}

static void cxl_dsp_mmio_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    trace_cxl_dsp_debug_message("Sending MMIO Write");

    PCIDevice *pci_dev = opaque;
    CXLRemoteDownstreamPort *dsp = CXL_REMOTE_DSP(pci_dev);
    PCIDevice *root_port = cxl_get_root_port(pci_dev);
    bool is_remote = root_port != NULL && cxl_is_remote_root_port(root_port);
    uint64_t addr = offset + dsp->bar0.addr;

    assert(is_remote);

    cxl_remote_mem_write(root_port, addr, value, size);

    trace_cxl_dsp_debug_message("Received MMIO Write Completion");
}

static const MemoryRegionOps mmio_ops = {
    .read = cxl_dsp_mmio_read,
    .write = cxl_dsp_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid =
        {
            .min_access_size = 4,
            .max_access_size = 8,
            .unaligned = false,
        },
    .impl =
        {
            .min_access_size = 4,
            .max_access_size = 8,
        },
};

static uint32_t cxl_dsp_config_read(PCIDevice *pci_dev, uint32_t addr, int size)
{
    trace_cxl_dsp_debug_message("Sending Config Space Read");

    PCIDevice *root_port = cxl_get_root_port(pci_dev);
    uint32_t val = 0xFFFFFFFF;

    bool is_remote = root_port != NULL && cxl_is_remote_root_port(root_port);

    assert(is_remote);

    uint16_t bdf = pci_get_bdf(pci_dev);

    cxl_remote_config_space_read(root_port, bdf, addr, &val, size);

    trace_cxl_dsp_debug_message("Received Config Space Read Completion");

    return val;
}

static void cxl_dsp_config_write(PCIDevice *pci_dev, uint32_t addr,
                                 uint32_t val, int size)
{
    trace_cxl_dsp_debug_message("Sending Config Space Write");

    PCIDevice *root_port = cxl_get_root_port(pci_dev);
    bool is_remote = root_port != NULL && cxl_is_remote_root_port(root_port);

    assert(is_remote);

    uint16_t bdf = pci_get_bdf(pci_dev);

    pci_bridge_write_config(pci_dev, addr, val, size);

    cxl_remote_config_space_write(root_port, bdf, addr, val, size);

    trace_cxl_dsp_debug_message("Received Config Space Write Completion");
}

static void cxl_dsp_reset(DeviceState *qdev) { }

static void cxl_dsp_realize(PCIDevice *pci_dev, Error **errp)
{
    trace_cxl_usp_debug_message("Realizing CXLDownstreamPort Class instance");

    CXLRemoteDownstreamPort *dsp = CXL_REMOTE_DSP(pci_dev);

    pci_dev->exp.exp_cap = 0x40;
    pci_set_word(&pci_dev->config[0x42], 0b0110 << 4);

    // pci_bridge_initfn adds a new bus to the secondary bus.
    pci_bridge_initfn(pci_dev, TYPE_PCIE_BUS);

    uint64_t mmio_size = 256 * 1024;
    Object *owner = OBJECT(pci_dev);
    memory_region_init(&dsp->bar0, owner, "dsp", mmio_size);
    memory_region_init_io(&dsp->bar0, owner, &mmio_ops, pci_dev, ".mmio",
                          mmio_size);
    pci_register_bar(pci_dev, 0,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_TYPE_32,
                     &dsp->bar0);

    trace_cxl_usp_debug_message("Realized CXLDownstreamPort Class instance");

    return;
}

static void cxl_dsp_exit(PCIDevice *d) { }

static void cxl_dsp_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(oc);

    k->realize = cxl_dsp_realize;
    k->exit = cxl_dsp_exit;
    k->vendor_id = 0x19e5; /* Huawei */
    k->device_id = 0xa129; /* Emulated CXL Switch Downstream Port */
    k->revision = 0;

    k->config_read = cxl_dsp_config_read;
    k->config_write = cxl_dsp_config_write;

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "CXL Switch Downstream Port";
    dc->reset = cxl_dsp_reset;
}

static const TypeInfo cxl_dsp_info = {
    .name = TYPE_CXL_REMOTE_DSP,
    .instance_size = sizeof(CXLRemoteDownstreamPort),
    .parent = TYPE_PCIE_SLOT,
    .class_init = cxl_dsp_class_init,
    .interfaces = (InterfaceInfo[]) { { INTERFACE_PCIE_DEVICE }, {} },
};

static void cxl_dsp_register_type(void)
{
    type_register_static(&cxl_dsp_info);
}

type_init(cxl_dsp_register_type);
