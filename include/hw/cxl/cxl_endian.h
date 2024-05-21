/*
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef CXL_ENDIAN_H

#include <arpa/inet.h>
#include <stdint.h>
#include "hw/cxl/cxl_emulator_packet.h"

/**
 * @brief Converts "network" uint64_t (big-endian) to "host" uint64_t
 * (little-endian). Note that this function is not standardized by the
 * UNIX standard, but we need it anyway.
 */
uint64_t ntohll(uint64_t netllong);

/**
 * @brief Performs endianness swap internally on a cxl_mreq_header_t.
 * 
 * @param[out] mreq_hdr The header on which to perform endian swap on fields.
 */
void endian_swap_mreq_hdr(cxl_io_mreq_header_t *mreq_hdr);

/**
 * @brief Performs endianness swap internally on a cxl_io_cfg_header_t.
 * 
 * @param[out] cfgq_hdr The header on which to perform endian swap on fields.
 */
void endian_swap_cfgq_hdr(cxl_io_cfg_req_header_t *cfgq_hdr);

/**
 * @brief Performs endianness swap internally on a cxl_completion_header_t.
 * 
 * @param[out] compl_hdr The header on which to perform endian swap on fields.
 */
void endian_swap_compl_hdr(cxl_io_completion_header_t *compl_hdr);

/**
 * @brief Endian-swap the relevant fields in a CXL.io packet payload,
 * based on the type of that payload.
 * 
 * @param[out] payload The payload on which endian-swapping shall be performed.
 */
void endian_swap_payload_io(uint8_t *payload_bytes, cxl_io_fmt_type_t pld_fmt); 

/**
 * @brief Given an input stream of bytes with length `sz`,
 * performs an endianness swap by inverting their order.
 * 
 * @param[out] ibstream The stream of bytes to invert
 */
void perform_endian_swap(uint8_t *ibstream, size_t sz);

#define CXL_ENDIAN_H
#endif 