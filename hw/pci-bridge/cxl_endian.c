/**
 * @file cxl_endian.c
 * @brief Various useful functions for network-to-host endian conversion and
 * interconversion of packet formatting from switch-side to QEMU-side.
 *
 * @copyright 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * @author Benedict Song
 */

#include "hw/cxl/cxl_endian.h"

uint64_t ntohll(uint64_t netllong)
{
    return ((netllong & 0xFF) << 56) | ((netllong & 0xFF00) << 40) |
           ((netllong & 0xFF0000) << 24) | ((netllong & 0xFF000000) << 8) |
           ((netllong & 0xFF00000000) >> 8) |
           ((netllong & 0xFF0000000000) >> 24) |
           ((netllong & 0xFF000000000000) >> 40) |
           ((netllong & 0xFF00000000000000) >> 56);
}

uint64_t htonll(uint64_t hllong) { return ntohll(hllong); }

void correct_io_hdr(cxl_io_header_t *io_hdr)
{
    QEMU_cxl_io_header_t recv_hdr = { 0 };
    recv_hdr.fmt_type = io_hdr->fmt_type;
    recv_hdr.th = io_hdr->th;
    recv_hdr.rsvd = io_hdr->rsvd;
    recv_hdr.attr_b2 = io_hdr->attr_b2;
    recv_hdr.t8 = io_hdr->t8;
    recv_hdr.tc = io_hdr->tc;
    recv_hdr.t9 = io_hdr->t9;
    recv_hdr.at = io_hdr->at;
    recv_hdr.attr = io_hdr->attr;
    recv_hdr.ep = io_hdr->ep;
    recv_hdr.td = io_hdr->td;
    recv_hdr.length = (io_hdr->length_upper << 8) | io_hdr->length_lower;
    recv_hdr.length = ntohs(recv_hdr.length);

    memcpy(io_hdr, &recv_hdr, sizeof(cxl_io_header_t)); // kind of evil
}

void correct_mreq_hdr(cxl_io_mreq_header_t *mreq_hdr)
{
    QEMU_cxl_io_mreq_header_t recv_hdr = { 0 };
    recv_hdr.req_id = ntohs(mreq_hdr->req_id);
    recv_hdr.tag = mreq_hdr->tag;
    recv_hdr.last_dw_be = mreq_hdr->last_dw_be;
    recv_hdr.first_dw_be = mreq_hdr->first_dw_be;
    recv_hdr.addr = (mreq_hdr->addr_upper << 6) | (mreq_hdr->addr_lower);
    recv_hdr.addr = ntohll(recv_hdr.addr << 2);
    recv_hdr.rsvd = mreq_hdr->rsvd;

    memcpy(mreq_hdr, &recv_hdr, sizeof(cxl_io_mreq_header_t));
}

void correct_cfgq_hdr(cxl_io_cfg_req_header_t *cfgq_hdr)
{
    QEMU_cxl_io_cfg_req_header_t recv_hdr = { 0 };
    recv_hdr.req_id = ntohs(cfgq_hdr->req_id);
    recv_hdr.tag = cfgq_hdr->tag;
    recv_hdr.last_dw_be = cfgq_hdr->last_dw_be;
    recv_hdr.first_dw_be = cfgq_hdr->first_dw_be;
    recv_hdr.dest_id = ntohs(cfgq_hdr->dest_id);
    recv_hdr.rsvd = cfgq_hdr->rsvd;
    recv_hdr.reg_num = ntohs(cfgq_hdr->reg_num);
    recv_hdr.r = cfgq_hdr->r;

    memcpy(cfgq_hdr, &recv_hdr, sizeof(cxl_io_cfg_req_header_t));
}

void correct_compl_hdr(cxl_io_completion_header_t *compl_hdr)
{
    QEMU_cxl_io_completion_header_t recv_hdr = { 0 };
    recv_hdr.cpl_id = ntohs(compl_hdr->cpl_id);
    recv_hdr.status = compl_hdr->status;
    recv_hdr.bcm = compl_hdr->bcm;
    recv_hdr.byte_count =
        (compl_hdr->byte_count_upper << 8) | compl_hdr->byte_count_lower;
    recv_hdr.byte_count = ntohs(recv_hdr.byte_count);
    recv_hdr.req_id = ntohs(compl_hdr->req_id);
    recv_hdr.rsvd = compl_hdr->rsvd;
    recv_hdr.tag = compl_hdr->tag;
    recv_hdr.lower_addr = compl_hdr->lower_addr;

    memcpy(compl_hdr, &recv_hdr, sizeof(cxl_io_completion_header_t));
}

void correct_payload_io(uint8_t *payload_bytes, cxl_io_fmt_type_t pld_fmt)
{
    correct_io_hdr((cxl_io_header_t *)(payload_bytes));

    switch (pld_fmt) {
    case MRD_32B:
    case MRD_64B:
    case MRD_LK_32B:
    case MRD_LK_64B:
    case MWR_32B:
    case MWR_64B: {
        // this is NOT undefined (checked with -Wall -fsanitize=undefined)
        correct_mreq_hdr(
            (cxl_io_mreq_header_t *)(payload_bytes + sizeof(cxl_io_header_t)));
        break;
    }

    case CFG_RD0:
    case CFG_RD1:
    case CFG_WR0:
    case CFG_WR1: {
        correct_cfgq_hdr((cxl_io_cfg_req_header_t *)(payload_bytes +
                                                     sizeof(cxl_io_header_t)));
        break;
    }

    case CPL: {
        correct_compl_hdr(
            (cxl_io_completion_header_t *)(payload_bytes +
                                           sizeof(cxl_io_header_t)));
        break;
    }

    case CPL_D: {
        correct_compl_hdr(
            (cxl_io_completion_header_t *)(payload_bytes +
                                           sizeof(cxl_io_header_t)));
        break;
    }

    default: {
        // Correction not required
    }
    }
}

void perform_endian_swap(uint8_t *ibstream, size_t sz)
{
    for (size_t b_idx = 0; b_idx < sz; b_idx++) {
        uint8_t temp = ibstream[b_idx];
        ibstream[b_idx] = ibstream[sz - b_idx - 1];
        ibstream[sz - b_idx - 1] = temp;
    }
}

void perform_bit_flip(uint8_t *ibstream, size_t sz)
{
    static const uint8_t table[] = {
        0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0,
        0x30, 0xb0, 0x70, 0xf0, 0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
        0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8, 0x04, 0x84, 0x44, 0xc4,
        0x24, 0xa4, 0x64, 0xe4, 0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
        0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec, 0x1c, 0x9c, 0x5c, 0xdc,
        0x3c, 0xbc, 0x7c, 0xfc, 0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
        0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2, 0x0a, 0x8a, 0x4a, 0xca,
        0x2a, 0xaa, 0x6a, 0xea, 0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
        0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6, 0x16, 0x96, 0x56, 0xd6,
        0x36, 0xb6, 0x76, 0xf6, 0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
        0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe, 0x01, 0x81, 0x41, 0xc1,
        0x21, 0xa1, 0x61, 0xe1, 0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
        0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9, 0x19, 0x99, 0x59, 0xd9,
        0x39, 0xb9, 0x79, 0xf9, 0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
        0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5, 0x0d, 0x8d, 0x4d, 0xcd,
        0x2d, 0xad, 0x6d, 0xed, 0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
        0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3, 0x13, 0x93, 0x53, 0xd3,
        0x33, 0xb3, 0x73, 0xf3, 0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
        0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb, 0x07, 0x87, 0x47, 0xc7,
        0x27, 0xa7, 0x67, 0xe7, 0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
        0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef, 0x1f, 0x9f, 0x5f, 0xdf,
        0x3f, 0xbf, 0x7f, 0xff,
    };
    for (size_t b_idx = 0; b_idx < sz; b_idx++) {
        ibstream[b_idx] = table[ibstream[b_idx]];
    }
}