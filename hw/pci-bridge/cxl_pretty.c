/**
 * @file cxl_pretty.c
 * @brief Utility functions for pretty-printing CXL packets.
 *
 * @copyright 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * @author Benedict Song
 */

#include "hw/cxl/cxl_pretty.h"

void snpprintpacket(char *buf, void *pckt, size_t at_most)
{
    uint8_t *pckt_data = (uint8_t *)pckt;
    enum CXL_PAYLOAD_TYPE pload_type =
        ((system_header_packet_t *)pckt_data)->payload_type;
    if (pload_type != CXL_IO)
        return; /* ignore a non-CXL.io packet */

    cxl_io_fmt_type_t io_fmt =
        ((cxl_io_header_t *)(pckt_data + sizeof(system_header_packet_t)))
            ->fmt_type;

    void *pload_ptr = (void *)(pckt_data + sizeof(system_header_packet_t) +
                               sizeof(cxl_io_header_t));

    at_most -= snprintf(buf, at_most, "[CXL.io PACKET] \n");

    if (at_most == 0)
        return;

    switch (io_fmt) {
    case MRD_32B:
    case MRD_64B:
    case MRD_LK_32B:
    case MRD_LK_64B: {
        cxl_io_mreq_header_t pload = *(cxl_io_mreq_header_t *)pload_ptr;
        snprintf(buf, at_most,
                 "[MRD PACKET] \n"
                 "req_id: %x \n tag: %x \n first_dw_be: %x \n last_dw_be: %x \n"
                 "addr_upper: %lx \n rsvd: %x \n addr_lower: %x \n",
                 pload.req_id, pload.tag, pload.first_dw_be, pload.last_dw_be,
                 (uint64_t)pload.addr_upper, pload.rsvd, pload.addr_lower);
        break;
    }

    case MWR_32B:
    case MWR_64B: {
        cxl_io_mreq_header_t pload = *(cxl_io_mreq_header_t *)pload_ptr;
        uint64_t data = *(uint64_t *)(pload_ptr + sizeof(cxl_io_mreq_header_t));
        snprintf(
            buf, at_most,
            "[MWR PACKET] \n"
            "req_id: %x \n tag: %x \n first_dw_be: %x \n last_dw_be: %x \n"
            "addr_upper: %lx \n rsvd: %x \n addr_lower: %x \n data: %lx \n",
            pload.req_id, pload.tag, pload.first_dw_be, pload.last_dw_be,
            (uint64_t)pload.addr_upper, pload.rsvd, pload.addr_lower, data);
        break;
    }

    case CFG_RD0:
    case CFG_RD1: {
        cxl_io_cfg_req_header_t pload = *(cxl_io_cfg_req_header_t *)pload_ptr;
        snprintf(buf, at_most,
                 "[CFG WR PACKET] \n"
                 "req_id: %x \n tag: %x \n first_dw_be: %x \n last_dw_be: %x \n"
                 "dest_id: %x \n ext_reg_num: %x \n rsvd: %x \n r: %x \n"
                 "reg_num: %x \n",
                 pload.req_id, pload.tag, pload.first_dw_be, pload.last_dw_be,
                 pload.dest_id, pload.ext_reg_num, pload.rsvd, pload.r,
                 pload.reg_num);
        break;
    }

    case CFG_WR0:
    case CFG_WR1: {
        cxl_io_cfg_req_header_t pload = *(cxl_io_cfg_req_header_t *)pload_ptr;
        uint32_t val = *(uint32_t *)(pload_ptr + sizeof(cxl_io_mreq_header_t));
        snprintf(buf, at_most,
                 "[CFG WR PACKET] \n"
                 "req_id: %x \n tag: %x \n first_dw_be: %x \n last_dw_be: %x \n"
                 "dest_id: %x \n ext_reg_num: %x \n rsvd: %x \n r: %x \n"
                 "reg_num: %x \n value: %x \n",
                 pload.req_id, pload.tag, pload.first_dw_be, pload.last_dw_be,
                 pload.dest_id, pload.ext_reg_num, pload.rsvd, pload.r,
                 pload.reg_num, val);
        break;
    }

    case CPL: {
        cxl_io_completion_header_t pload =
            *(cxl_io_completion_header_t *)pload_ptr;
        snprintf(
            buf, at_most,
            "[CPL PACKET] \n"
            "cpl_id: %x \n bcu: %x \n bcm: %x \n status: %x \n"
            "bcl: %x \n req_id: %x \n tag: %x \n lower_addr: %x \n rsvd: %x \n",
            pload.cpl_id, pload.byte_count_upper, pload.bcm, pload.status,
            pload.byte_count_lower, pload.req_id, pload.tag, pload.lower_addr,
            pload.rsvd);
        break;
    }

    case CPL_D: {
        cxl_io_completion_header_t pload =
            *(cxl_io_completion_header_t *)pload_ptr;
        uint64_t data =
            *(uint64_t *)(pload_ptr + sizeof(cxl_io_completion_header_t));
        snprintf(buf, at_most,
                 "[CPL PACKET + DATA] \n"
                 "cpl_id: %x \n bcu: %x \n bcm: %x \n status: %x \n"
                 "bcl: %x \n req_id: %x \n tag: %x \n lower_addr: %x \n"
                 "rsvd: %x \n data: %lx \n",
                 pload.cpl_id, pload.byte_count_upper, pload.bcm, pload.status,
                 pload.byte_count_lower, pload.req_id, pload.tag,
                 pload.lower_addr, pload.rsvd, data);
        break;
    }

    default:
        snprintf(buf, at_most, "[UNRECOGNIZED PACKET TYPE]\n");
    }
}