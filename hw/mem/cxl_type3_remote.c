/*
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/qapi-commands-cxl.h"
#include "hw/mem/memory-device.h"
#include "hw/mem/pc-dimm.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/pmem.h"
#include "qemu/range.h"
#include "qemu/rcu.h"
#include "sysemu/hostmem.h"
#include "sysemu/numa.h"
#include "hw/cxl/cxl.h"
#include "trace.h"

static uint64_t ct3d_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    trace_cxl_type3_remote_debug_message("Sending MMIO Read");

    PCIDevice *pci_dev = opaque;
    CXLType3RemoteDev *ct3d = CXL_TYPE3_REMOTE(pci_dev);
    PCIDevice *root_port = cxl_get_root_port(pci_dev);
    bool is_remote = root_port != NULL && cxl_is_remote_root_port(root_port);
    uint64_t value = 0xFFFFFFFF;
    uint64_t addr = offset + ct3d->bar0.addr;

    assert(is_remote);

    cxl_remote_mem_read(root_port, addr, &value, size);

    trace_cxl_type3_remote_debug_mmio_read(value);

    return value;
}

static void ct3d_mmio_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    trace_cxl_type3_remote_debug_message("Sending MMIO Write");

    PCIDevice *pci_dev = opaque;
    CXLType3RemoteDev *ct3d = CXL_TYPE3_REMOTE(pci_dev);
    PCIDevice *root_port = cxl_get_root_port(pci_dev);
    bool is_remote = root_port != NULL && cxl_is_remote_root_port(root_port);
    uint64_t addr = offset + ct3d->bar0.addr;

    assert(is_remote);

    cxl_remote_mem_write(root_port, addr, value, size);

    trace_cxl_type3_remote_debug_message("Received MMIO Write Completion");
}

static const MemoryRegionOps mmio_ops = {
    .read = ct3d_mmio_read,
    .write = ct3d_mmio_write,
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

static uint32_t ct3d_config_read(PCIDevice *pci_dev, uint32_t addr, int size)
{
    trace_cxl_type3_remote_debug_message("Sending Config Space Read");

    PCIDevice *root_port = cxl_get_root_port(pci_dev);
    uint32_t val = 0xFFFFFFFF;

    bool is_remote = root_port != NULL && cxl_is_remote_root_port(root_port);

    assert(is_remote);

    uint16_t bdf = pci_get_bdf(pci_dev);

    cxl_remote_config_space_read(root_port, bdf, addr, &val, size);

    trace_cxl_type3_remote_debug_config_read(val);

    return val;
}

static void ct3d_config_write(PCIDevice *pci_dev, uint32_t addr, uint32_t val,
                              int size)
{
    trace_cxl_type3_remote_debug_message("Sending Config Space Read");

    PCIDevice *root_port = cxl_get_root_port(pci_dev);
    bool is_remote = root_port != NULL && cxl_is_remote_root_port(root_port);

    assert(is_remote);

    uint16_t bdf = pci_get_bdf(pci_dev);

    pci_default_write_config(pci_dev, addr, val, size);

    cxl_remote_config_space_write(root_port, bdf, addr, val, size);

    trace_cxl_type3_remote_debug_message(
        "Recevied Config Space Write Completion");
}

static void ct3_realize(PCIDevice *pci_dev, Error **errp)
{
    uint8_t *pci_conf = pci_dev->config;
    CXLType3RemoteDev *ct3d = CXL_TYPE3_REMOTE(pci_dev);
    pci_config_set_prog_interface(pci_conf, 0x10);

    uint64_t mmio_size = 128 * 1024;
    Object *owner = OBJECT(pci_dev);
    memory_region_init(&ct3d->bar0, owner, "type3", mmio_size);
    memory_region_init_io(&ct3d->bar0, owner, &mmio_ops, pci_dev, ".mmio",
                          mmio_size);
    pci_register_bar(
        pci_dev, 0,
        PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_32,
        &ct3d->bar0);

    return;
}

static void ct3_exit(PCIDevice *pci_dev)
{
    // CXLType3RemoteDev *ct3d = CXL_TYPE3(pci_dev);
}

static void ct3d_reset(DeviceState *dev)
{
    // CXLType3RemoteDev *ct3d = CXL_TYPE3(dev);
}

static void ct3_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = ct3_realize;
    pc->exit = ct3_exit;
    pc->class_id = PCI_CLASS_MEMORY_CXL;
    pc->vendor_id = PCI_VENDOR_ID_INTEL;
    pc->device_id = 0xd93; /* LVF for now */
    pc->revision = 1;

    pc->config_write = ct3d_config_write;
    pc->config_read = ct3d_config_read;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "CXL Remote Device (Type 3)";
    dc->reset = ct3d_reset;
}

static const TypeInfo ct3d_info = {
    .name = TYPE_CXL_TYPE3_REMOTE,
    .parent = TYPE_PCI_DEVICE,
    .class_size = sizeof(struct CXLType3RemoteClass),
    .class_init = ct3_class_init,
    .instance_size = sizeof(CXLType3RemoteDev),
    .interfaces =
        (InterfaceInfo[]){{INTERFACE_CXL_DEVICE}, {INTERFACE_PCIE_DEVICE}, {}},
};

static void ct3d_registers(void) { type_register_static(&ct3d_info); }

type_init(ct3d_registers);
