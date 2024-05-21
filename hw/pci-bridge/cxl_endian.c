/*
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "hw/cxl/cxl_endian.h"

uint64_t ntohll(uint64_t netllong) 
{
    return ((netllong & 0xFF) << 56) |
           ((netllong & 0xFF00) << 40) | 
           ((netllong & 0xFF0000) << 24) |
           ((netllong & 0xFF000000) << 8) |
           ((netllong & 0xFF00000000) >> 8) | 
           ((netllong & 0xFF0000000000) >> 24) |
           ((netllong & 0xFF000000000000) >> 40) |
           ((netllong & 0xFF00000000000000) >> 56);
}

void endian_swap_mreq_hdr(cxl_io_mreq_header_t *mreq_hdr) 
{
    mreq_hdr->req_id = ntohs(mreq_hdr->req_id);
    mreq_hdr->addr_upper = ntohll(mreq_hdr->addr_upper);
    return;
}

void endian_swap_cfgq_hdr(cxl_io_cfg_req_header_t *cfgq_hdr) 
{
    cfgq_hdr->req_id = ntohs(cfgq_hdr->req_id);
    cfgq_hdr->dest_id = ntohs(cfgq_hdr->dest_id);
    return;
}

void endian_swap_compl_hdr(cxl_io_completion_header_t *compl_hdr) 
{
    compl_hdr->cpl_id = ntohs(compl_hdr->cpl_id);
    compl_hdr->req_id = ntohs(compl_hdr->req_id);
    return;
}

void endian_swap_payload_io(uint8_t *payload_bytes, cxl_io_fmt_type_t pld_fmt)
{
    switch (pld_fmt) 
    {
        case MRD_32B:
        case MRD_64B:
        case MRD_LK_32B:
        case MRD_LK_64B:
        case MWR_32B:
        case MWR_64B: 
        {
            // this is NOT undefined (checked with -Wall -fsanitize=undefined)
            endian_swap_mreq_hdr((cxl_io_mreq_header_t *) 
                (payload_bytes + sizeof(cxl_io_header_t)));
            break;
        }

        case CFG_RD0:
        case CFG_RD1:
        case CFG_WR0:
        case CFG_WR1:
        {
            endian_swap_cfgq_hdr((cxl_io_cfg_req_header_t *)
                (payload_bytes + sizeof(cxl_io_header_t)));
            break;
        }

        case CPL:
        case CPL_D:
        {
            endian_swap_compl_hdr((cxl_io_completion_header_t *)
                (payload_bytes + sizeof(cxl_io_header_t)));
            break;
        }

        default:
        {
            // Endian swap not required
        }
    }
} 

void perform_endian_swap(uint8_t *ibstream, size_t sz)
{
    for (size_t b_idx = 0; b_idx < sz; b_idx++)
    {
        uint8_t temp = ibstream[b_idx];
        ibstream[b_idx] = ibstream[sz - b_idx - 1];
        ibstream[sz - b_idx - 1] = temp;
    }
}