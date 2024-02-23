/*
 * CXL 2.0 Root Port Implementation
 *
 * Copyright(C) 2020 Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/range.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie_port.h"
#include "hw/pci/msi.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_emulator_packet.h"
#include "hw/cxl/cxl_socket_transport.h"
#include "trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CXL_ROOT_PORT_DID 0x7075

#define CXL_RP_MSI_OFFSET               0x60
#define CXL_RP_MSI_SUPPORTED_FLAGS      PCI_MSI_FLAGS_MASKBIT
#define CXL_RP_MSI_NR_VECTOR            2

/* Copied from the gen root port which we derive */
#define GEN_PCIE_ROOT_PORT_AER_OFFSET 0x100
#define GEN_PCIE_ROOT_PORT_ACS_OFFSET \
    (GEN_PCIE_ROOT_PORT_AER_OFFSET + PCI_ERR_SIZEOF)
#define CXL_ROOT_PORT_DVSEC_OFFSET \
    (GEN_PCIE_ROOT_PORT_ACS_OFFSET + PCI_ACS_SIZEOF)

typedef struct CXLRootPort {
    /*< private >*/
    PCIESlot parent_obj;

    CXLComponentState cxl_cstate;
    PCIResReserve res_reserve;

    char *socket_host;
    uint32_t socket_port;
    uint32_t switch_port;
    int socket_fd;
} CXLRootPort;

#define TYPE_CXL_ROOT_PORT "cxl-rp"
DECLARE_INSTANCE_CHECKER(CXLRootPort, CXL_ROOT_PORT, TYPE_CXL_ROOT_PORT)

bool cxl_is_remote_root_port(PCIDevice *d)
{
    if (!object_dynamic_cast(OBJECT(d), TYPE_CXL_ROOT_PORT)) {
        return false;
    }
    CXLRootPort *crp = CXL_ROOT_PORT(d);
    return crp->socket_host != NULL;
}


PCIDevice *cxl_get_root_port(PCIDevice *d)
{
    PCIBus *bus = pci_get_bus(d);

    while (!pci_bus_is_root(bus)) {
        d = bus->parent_dev;
        if (cxl_is_remote_root_port(d)) {
            return d;
        }

        bus = pci_get_bus(d);
    }
    return NULL;
}


MemTxResult cxl_remote_cxl_mem_read(PCIDevice *d, hwaddr host_addr,
                                    uint64_t *data, unsigned size,
                                    MemTxAttrs attrs)
{
    trace_cxl_root_cxl_cxl_mem_read(host_addr);

    CXLRootPort *crp = CXL_ROOT_PORT(d);

    uint16_t tag;
    if (!send_cxl_mem_mem_read(crp->socket_fd, host_addr, &tag)) {
        trace_cxl_root_debug_message("Failed to send CXL.mem MEM RD request");
        *data = 0xFFFFFFFF;
        return MEMTX_OK;
    }

    cxl_mem_s2m_drs_packet_t *cxl_packet =
        wait_for_cxl_mem_mem_data(crp->socket_fd, tag);
    if (cxl_packet == NULL) {
        release_packet_entry(tag);
        trace_cxl_root_debug_message("Failed to get CXL.mem MEM DATA response");
        *data = 0xFFFFFFFF;
        return MEMTX_OK;
    }

    *data = *(uint64_t *)(cxl_packet->data);
    release_packet_entry(tag);

    return MEMTX_OK;
}


MemTxResult cxl_remote_cxl_mem_write(PCIDevice *d, hwaddr host_addr,
                                     uint64_t data, unsigned size,
                                     MemTxAttrs attrs)
{
    trace_cxl_root_cxl_cxl_mem_write(host_addr);

    CXLRootPort *crp = CXL_ROOT_PORT(d);

    uint16_t tag;
    uint8_t data_bytes[CXL_MEM_ACCESS_UNIT];
    *(uint64_t *)(data_bytes) = data;

    if (!send_cxl_mem_mem_write(crp->socket_fd, host_addr, data_bytes, &tag)) {
        trace_cxl_root_debug_message("Failed to send CXL.mem MEM WR request");
        return MEMTX_OK;
    }

    cxl_mem_s2m_ndr_packet_t *cxl_packet =
        wait_for_cxl_mem_completion(crp->socket_fd, tag);
    release_packet_entry(tag);
    if (cxl_packet == NULL) {
        trace_cxl_root_debug_message("Failed to get CXL.mem MEM DATA response");
        return MEMTX_OK;
    }

    return MEMTX_OK;
}


void cxl_remote_mem_read(PCIDevice *d, uint64_t addr, uint64_t *val, int size)
{
    trace_cxl_root_cxl_io_mmio_read(addr, size);

    CXLRootPort *crp = CXL_ROOT_PORT(d);
    uint16_t tag;

    if (!send_cxl_io_mem_read(crp->socket_fd, addr, size, &tag)) {
        trace_cxl_root_debug_message("Failed to send CXL.io MEM RD request");
        assert(0);
    }

    cxl_io_completion_data_packet_t *cxl_packet =
        wait_for_cxl_io_completion_data(crp->socket_fd, tag);
    if (cxl_packet == NULL) {
        release_packet_entry(tag);
        trace_cxl_root_debug_message("Failed to get CXL.io CPLD response");
        assert(0);
    }

    *val = cxl_packet->data;
    release_packet_entry(tag);
}


void cxl_remote_mem_write(PCIDevice *d, uint64_t addr, uint64_t val, int size)
{
    trace_cxl_root_cxl_io_mmio_write(addr, size, val);

    CXLRootPort *crp = CXL_ROOT_PORT(d);
    uint16_t tag;

    if (!send_cxl_io_mem_write(crp->socket_fd, addr, val, size, &tag)) {
        trace_cxl_root_debug_message("Failed to send CXL.io MEM WR request");
        assert(0);
    }

    cxl_io_completion_packet_t *cxl_packet =
        wait_for_cxl_io_completion(crp->socket_fd, tag);
    release_packet_entry(tag);
    if (cxl_packet == NULL) {
        trace_cxl_root_debug_message("Failed to get CXL.io CPL response");
        assert(0);
    }
}


static bool is_type0_config_request(PCIDevice *root_port, uint16_t bdf)
{
    uint8_t secondary_bus = root_port->config[PCI_SECONDARY_BUS];
    uint16_t bus = bdf >> 8;
    return bus == secondary_bus;
}


static bool is_valid_bdf(PCIDevice *d, uint16_t bdf)
{
    uint8_t secondary_bus = d->config[PCI_SECONDARY_BUS];
    uint8_t subordinate_bus = d->config[PCI_SUBORDINATE_BUS];
    uint16_t bus = bdf >> 8;
    return bus >= secondary_bus && bus <= subordinate_bus;
}


void cxl_remote_config_space_read(PCIDevice *d, uint16_t bdf, uint32_t offset,
                                  uint32_t *val, int size)
{
    if (!is_valid_bdf(d, bdf)) {
        trace_cxl_root_debug_message("Invalid BDF received");
        assert(0);
    }

    CXLRootPort *crp = CXL_ROOT_PORT(d);
    bool type0 = is_type0_config_request(d, bdf);
    uint16_t tag;
    const uint8_t bus = bdf >> 8;
    const uint8_t device = bdf & 0x1F >> 3;
    const uint8_t function = bdf & 0x7;

    if (type0) {
        trace_cxl_root_cxl_io_config_space_read0(bus, device, function, offset,
                                                 size);
    } else {
        trace_cxl_root_cxl_io_config_space_read1(bus, device, function, offset,
                                                 size);
    }

    if (!send_cxl_io_config_space_read(crp->socket_fd, bdf, offset, size, type0,
                                       &tag)) {
        trace_cxl_root_debug_message("Failed to send CXL.io CFG RD request");
        assert(0);
    }

    wait_for_cxl_io_cfg_completion(crp->socket_fd, tag, val);

    release_packet_entry(tag);
}


void cxl_remote_config_space_write(PCIDevice *d, uint16_t bdf, uint32_t offset,
                                   uint32_t val, int size)
{
    if (!is_valid_bdf(d, bdf)) {
        trace_cxl_root_debug_message("Invalid BDF received");
        assert(0);
    }

    CXLRootPort *crp = CXL_ROOT_PORT(d);
    bool type0 = is_type0_config_request(d, bdf);
    uint16_t tag;
    const uint8_t bus = bdf >> 8;
    const uint8_t device = bdf & 0x1F >> 3;
    const uint8_t function = bdf & 0x7;

    if (type0) {
        trace_cxl_root_cxl_io_config_space_write0(bus, device, function, offset,
                                                  size, val);
    } else {
        trace_cxl_root_cxl_io_config_space_write1(bus, device, function, offset,
                                                  size, val);
    }

    if (!send_cxl_io_config_space_write(crp->socket_fd, bdf, offset, val, size,
                                        type0, &tag)) {
        trace_cxl_root_debug_message("Failed to send CXL.io CFG WR request");
        assert(0);
    }

    wait_for_cxl_io_cfg_completion(crp->socket_fd, tag, NULL);

    release_packet_entry(tag);
}


static uint16_t get_number_of_ports(PCIDevice *usp, PCIDevice *rp)
{
    const uint16_t root_bus = 0;
    const uint16_t usp_bus = 1;

    // Set RP BUS
    rp->config[PCI_SECONDARY_BUS] = root_bus;
    rp->config[PCI_SUBORDINATE_BUS] = usp_bus;

    // Set USP BUS
    cxl_remote_config_space_write(rp, PCI_BUILD_BDF(root_bus, 0),
                                  PCI_SECONDARY_BUS, usp_bus, 1);
    cxl_remote_config_space_write(rp, PCI_BUILD_BDF(root_bus, 0),
                                  PCI_SUBORDINATE_BUS, usp_bus, 1);

    // Scan DSPs
    const uint16_t max_devices = 32;
    uint16_t ports = 0;
    for (uint16_t device_id = 0; device_id < max_devices; ++device_id) {
        const uint16_t devfn = device_id << 3;
        uint32_t val = 0xFFFF;
        cxl_remote_config_space_read(rp, PCI_BUILD_BDF(usp_bus, devfn), 0, &val,
                                     2);
        if (val != 0xFFFF) {
            ports += 1;
        }
    }

    return ports;
}

/*
 * If two MSI vector are allocated, Advanced Error Interrupt Message Number
 * is 1. otherwise 0.
 * 17.12.5.10 RPERRSTS,  32:27 bit Advanced Error Interrupt Message Number.
 */
static uint8_t cxl_rp_aer_vector(const PCIDevice *d)
{
    switch (msi_nr_vectors_allocated(d)) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 4:
    case 8:
    case 16:
    case 32:
    default:
        break;
    }
    abort();
    return 0;
}

static int cxl_rp_interrupts_init(PCIDevice *d, Error **errp)
{
    int rc;

    rc = msi_init(d, CXL_RP_MSI_OFFSET, CXL_RP_MSI_NR_VECTOR,
                  CXL_RP_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_64BIT,
                  CXL_RP_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_MASKBIT,
                  errp);
    if (rc < 0) {
        assert(rc == -ENOTSUP);
    }

    return rc;
}

static void cxl_rp_interrupts_uninit(PCIDevice *d)
{
    msi_uninit(d);
}

static void latch_registers(CXLRootPort *crp)
{
    uint32_t *reg_state = crp->cxl_cstate.crb.cache_mem_registers;
    uint32_t *write_msk = crp->cxl_cstate.crb.cache_mem_regs_write_mask;

    cxl_component_register_init_common(reg_state, write_msk, CXL2_ROOT_PORT);
}

static void build_dvsecs(CXLComponentState *cxl)
{
    uint8_t *dvsec;

    dvsec = (uint8_t *)&(CXLDVSECPortExtensions){ 0 };
    cxl_component_create_dvsec(cxl, CXL2_ROOT_PORT,
                               EXTENSIONS_PORT_DVSEC_LENGTH,
                               EXTENSIONS_PORT_DVSEC,
                               EXTENSIONS_PORT_DVSEC_REVID, dvsec);

    dvsec = (uint8_t *)&(CXLDVSECPortGPF){
        .rsvd        = 0,
        .phase1_ctrl = 1, /* 1μs timeout */
        .phase2_ctrl = 1, /* 1μs timeout */
    };
    cxl_component_create_dvsec(cxl, CXL2_ROOT_PORT,
                               GPF_PORT_DVSEC_LENGTH, GPF_PORT_DVSEC,
                               GPF_PORT_DVSEC_REVID, dvsec);

    dvsec = (uint8_t *)&(CXLDVSECPortFlexBus){
        .cap                     = 0x26, /* IO, Mem, non-MLD */
        .ctrl                    = 0x2,
        .status                  = 0x26, /* same */
        .rcvd_mod_ts_data_phase1 = 0xef,
    };
    cxl_component_create_dvsec(cxl, CXL2_ROOT_PORT,
                               PCIE_FLEXBUS_PORT_DVSEC_LENGTH_2_0,
                               PCIE_FLEXBUS_PORT_DVSEC,
                               PCIE_FLEXBUS_PORT_DVSEC_REVID_2_0, dvsec);

    dvsec = (uint8_t *)&(CXLDVSECRegisterLocator){
        .rsvd         = 0,
        .reg0_base_lo = RBI_COMPONENT_REG | CXL_COMPONENT_REG_BAR_IDX,
        .reg0_base_hi = 0,
    };
    cxl_component_create_dvsec(cxl, CXL2_ROOT_PORT,
                               REG_LOC_DVSEC_LENGTH, REG_LOC_DVSEC,
                               REG_LOC_DVSEC_REVID, dvsec);
}


static bool cxl_rp_init_socket_client(CXLRootPort *crp)
{
    crp->socket_fd = create_socket_client(crp->socket_host, crp->socket_port);
    if (crp->socket_fd < 0) {
        return false;
    }

    if (!send_sideband_connection_request(crp->socket_fd, crp->switch_port)) {
        trace_cxl_root_debug_message(
            "CXL Root Port: Failed to send connection request");
        return false;
    }

    base_sideband_packet_t *packet =
        wait_for_base_sideband_packet(crp->socket_fd);
    const uint16_t tag = 0;
    if (packet == NULL) {
        release_packet_entry(tag);
        trace_cxl_root_debug_message(
            "CXL Root Port: Failed to get connection response");
        return false;
    }

    if (packet->sideband_header.type != SIDEBAND_CONNECTION_ACCEPT) {
        release_packet_entry(tag);
        trace_cxl_root_debug_message(
            "CXL Root Port: Connection request was not accepted");
        return false;
    }
    release_packet_entry(tag);
    trace_cxl_root_debug_message(
        "CXL Root Port: Successfully connected to switch");

    return true;
}


static bool cxl_rp_enumerate_child_devices(CXLRootPort *crp, Error **errp)
{
    PCIBridge *pci_bridge = PCI_BRIDGE(crp);
    PCIBus *bus = &pci_bridge->sec_bus;

    bus->flags |= PCI_BUS_EXTENDED_CONFIG_SPACE;

    trace_cxl_root_debug_message("Creating CXL Remote USP device");
    DeviceState *usp = qdev_new(TYPE_CXL_REMOTE_USP);
    trace_cxl_root_debug_message("Created CXL Remote USP device");
    qdev_realize(DEVICE(usp), &bus->qbus, errp);

    PCIBridge *usp_bridge = PCI_BRIDGE(usp);
    PCIBus *usp_bus = &usp_bridge->sec_bus;
    PCIDevice *usp_device = PCI_DEVICE(usp);
    usp_device->exp.exp_cap = 0x40;
    pci_set_word(&usp_device->config[0x42], 0b0101 << 4);

    trace_cxl_root_debug_message("Getting number of ports under USP");
    const uint8_t total_ports =
        get_number_of_ports(usp_device, PCI_DEVICE(crp));
    trace_cxl_root_debug_number("Found Ports: ", total_ports);

    for (uint8_t port = 0; port < total_ports; ++port) {
        trace_cxl_root_debug_message("Creating CXL Remote DSP device");
        DeviceState *dsp = qdev_new(TYPE_CXL_REMOTE_DSP);
        PCIESlot *dsp_slot = PCIE_SLOT(dsp);
        PCIEPort *dsp_port = PCIE_PORT(dsp);
        dsp_slot->chassis = 0;
        dsp_slot->slot = 4 + port;
        dsp_port->port = port;
        trace_cxl_root_debug_message("Created CXL Remote DSP device");
        qdev_realize(DEVICE(dsp), &usp_bus->qbus, errp);

        PCIBridge *dsp_bridge = PCI_BRIDGE(dsp);
        PCIBus *dsp_bus = &dsp_bridge->sec_bus;
        PCIDevice *dsp_device = PCI_DEVICE(dsp);
        dsp_device->exp.exp_cap = 0x40;
        pci_set_word(&dsp_device->config[0x42], 0b0110 << 4);

        trace_cxl_root_debug_message("Creating CXL Type3 Remote device");
        DeviceState *ep = qdev_new(TYPE_CXL_TYPE3_REMOTE);
        trace_cxl_root_debug_message("Created CXL Type3 Remote device");
        qdev_realize(DEVICE(ep), &dsp_bus->qbus, errp);
    }

    return true;
}


static void cxl_rp_realize(DeviceState *dev, Error **errp)
{
    PCIDevice *pci_dev     = PCI_DEVICE(dev);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(dev);
    CXLRootPort *crp       = CXL_ROOT_PORT(dev);
    CXLComponentState *cxl_cstate = &crp->cxl_cstate;
    ComponentRegisters *cregs = &cxl_cstate->crb;
    MemoryRegion *component_bar = &cregs->component_registers;
    Error *local_err = NULL;

    trace_cxl_root_debug_message("Realizing CXLRootPort Class instance");

    rpc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    int rc =
        pci_bridge_qemu_reserve_cap_init(pci_dev, 0, crp->res_reserve, errp);
    if (rc < 0) {
        rpc->parent_class.exit(pci_dev);
        return;
    }

    if (!crp->res_reserve.io || crp->res_reserve.io == -1) {
        pci_word_test_and_clear_mask(pci_dev->wmask + PCI_COMMAND,
                                     PCI_COMMAND_IO);
        pci_dev->wmask[PCI_IO_BASE]  = 0;
        pci_dev->wmask[PCI_IO_LIMIT] = 0;
    }

    cxl_cstate->dvsec_offset = CXL_ROOT_PORT_DVSEC_OFFSET;
    cxl_cstate->pdev = pci_dev;
    build_dvsecs(&crp->cxl_cstate);

    cxl_component_register_block_init(OBJECT(pci_dev), cxl_cstate,
                                      TYPE_CXL_ROOT_PORT);

    pci_register_bar(pci_dev, CXL_COMPONENT_REG_BAR_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_TYPE_64,
                     component_bar);

    if (!cxl_is_remote_root_port(pci_dev)) {
        return;
    }

    if (!cxl_rp_init_socket_client(crp)) {
        return;
    }

    if (!cxl_rp_enumerate_child_devices(crp, errp)) {
        return;
    }

    trace_cxl_root_debug_message("Realized CXLRootPort Class instance");
}

static void cxl_rp_reset_hold(Object *obj)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(obj);
    CXLRootPort *crp = CXL_ROOT_PORT(obj);

    if (rpc->parent_phases.hold) {
        rpc->parent_phases.hold(obj);
    }

    latch_registers(crp);
}

static Property gen_rp_props[] = {
    DEFINE_PROP_UINT32("bus-reserve", CXLRootPort, res_reserve.bus, -1),
    DEFINE_PROP_SIZE("io-reserve", CXLRootPort, res_reserve.io, -1),
    DEFINE_PROP_SIZE("mem-reserve", CXLRootPort, res_reserve.mem_non_pref, -1),
    DEFINE_PROP_SIZE("pref32-reserve", CXLRootPort, res_reserve.mem_pref_32,
                     -1),
    DEFINE_PROP_SIZE("pref64-reserve", CXLRootPort, res_reserve.mem_pref_64,
                     -1),
    DEFINE_PROP_STRING("socket-host", CXLRootPort, socket_host),
    DEFINE_PROP_UINT32("socket-port", CXLRootPort, socket_port, 8000),
    DEFINE_PROP_UINT32("switch-port", CXLRootPort, switch_port, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void cxl_rp_dvsec_write_config(PCIDevice *dev, uint32_t addr,
                                      uint32_t val, int len)
{
    CXLRootPort *crp = CXL_ROOT_PORT(dev);

    if (range_contains(&crp->cxl_cstate.dvsecs[EXTENSIONS_PORT_DVSEC], addr)) {
        uint8_t *reg = &dev->config[addr];
        addr -= crp->cxl_cstate.dvsecs[EXTENSIONS_PORT_DVSEC].lob;
        if (addr == PORT_CONTROL_OFFSET) {
            if (pci_get_word(reg) & PORT_CONTROL_UNMASK_SBR) {
                /* unmask SBR */
                qemu_log_mask(LOG_UNIMP, "SBR mask control is not supported\n");
            }
            if (pci_get_word(reg) & PORT_CONTROL_ALT_MEMID_EN) {
                /* Alt Memory & ID Space Enable */
                qemu_log_mask(LOG_UNIMP,
                              "Alt Memory & ID space is not supported\n");
            }
        }
    }
}

static void cxl_rp_aer_vector_update(PCIDevice *d)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(d);

    if (rpc->aer_vector) {
        pcie_aer_root_set_vector(d, rpc->aer_vector(d));
    }
}

static void cxl_rp_write_config(PCIDevice *d, uint32_t address, uint32_t val,
                                int len)
{
    uint16_t slt_ctl, slt_sta;
    uint32_t root_cmd =
        pci_get_long(d->config + d->exp.aer_cap + PCI_ERR_ROOT_COMMAND);

    pcie_cap_slot_get(d, &slt_ctl, &slt_sta);
    pci_bridge_write_config(d, address, val, len);
    cxl_rp_aer_vector_update(d);
    pcie_cap_flr_write_config(d, address, val, len);
    pcie_cap_slot_write_config(d, slt_ctl, slt_sta, address, val, len);
    pcie_aer_write_config(d, address, val, len);
    pcie_aer_root_write_config(d, address, val, len, root_cmd);

    cxl_rp_dvsec_write_config(d, address, val, len);
}

static void cxl_root_port_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc        = DEVICE_CLASS(oc);
    PCIDeviceClass *k      = PCI_DEVICE_CLASS(oc);
    ResettableClass *rc    = RESETTABLE_CLASS(oc);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(oc);

    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = CXL_ROOT_PORT_DID;
    dc->desc     = "CXL Root Port";
    k->revision  = 0;
    device_class_set_props(dc, gen_rp_props);
    k->config_write = cxl_rp_write_config;

    device_class_set_parent_realize(dc, cxl_rp_realize, &rpc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, cxl_rp_reset_hold, NULL,
                                       &rpc->parent_phases);

    rpc->aer_offset = GEN_PCIE_ROOT_PORT_AER_OFFSET;
    rpc->acs_offset = GEN_PCIE_ROOT_PORT_ACS_OFFSET;
    rpc->aer_vector = cxl_rp_aer_vector;
    rpc->interrupts_init = cxl_rp_interrupts_init;
    rpc->interrupts_uninit = cxl_rp_interrupts_uninit;

    dc->hotpluggable = false;
}

static const TypeInfo cxl_root_port_info = {
    .name = TYPE_CXL_ROOT_PORT,
    .parent = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(CXLRootPort),
    .class_init = cxl_root_port_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CXL_DEVICE },
        { }
    },
};

static void cxl_register(void)
{
    type_register_static(&cxl_root_port_info);
}

type_init(cxl_register);
