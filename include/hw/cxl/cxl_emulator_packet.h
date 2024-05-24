/*
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef CXL_EMULATOR_PACKET_H
#define CXL_EMULATOR_PACKET_H

#include <stdint.h>

#include "exec/hwaddr.h"

/*
 * System Header
 */

enum CXL_PAYLOAD_TYPE { CXL_CPI = 0, CXL_IO, CXL_MEM, SIDEBAND = 15 };

typedef struct system_header_packet {
    uint16_t payload_type   : 4;
    uint16_t payload_length : 12;
} system_header_packet_t;

/*
 * Sideband
 */

typedef enum CXL_SIDEBAND_TYPES {
    SIDEBAND_CONNECTION_REQUEST = 0,
    SIDEBAND_CONNECTION_ACCEPT,
    SIDEBAND_CONNECTION_REJECT,
    SIDEBAND_CONNECTION_DISCONNECTED
} sideband_type_t;

typedef struct sideband_header_packet {
    uint8_t type;
} sideband_header_packet_t;

typedef struct base_sideband_packet {
    system_header_packet_t system_header;
    sideband_header_packet_t sideband_header;
} __attribute__((packed)) base_sideband_packet_t;

typedef struct sideband_connection_request_packet {
    system_header_packet_t system_header;
    sideband_header_packet_t sideband_header;
    uint8_t port;
} __attribute__((packed)) sideband_connection_request_packet_t;

/*
 * CXL.io
 */

typedef enum {
    MRD_32B = 0b00000000,
    MRD_64B = 0b00100000,
    MRD_LK_32B = 0b00000001,
    MRD_LK_64B = 0b00100001,
    MWR_32B = 0b01000000,
    MWR_64B = 0b01100000,
    IO_RD = 0b00000010,
    IO_WR = 0b01000010,
    CFG_RD0 = 0b00000100,
    CFG_WR0 = 0b01000100,
    CFG_RD1 = 0b00000101,
    CFG_WR1 = 0b01000101,
    TCFG_RD = 0b00011011,
    D_MRW_32B = 0b01011011,
    D_MRW_64B = 0b01111011,
    CPL = 0b00001010,
    CPL_D = 0b01001010,
    CPL_LK = 0b00001011,
    CPL_D_LK = 0b01001011,
    FETCH_ADD_32B = 0b01001100,
    FETCH_ADD_64B = 0b01101100,
    SWAP_32B = 0b01001101,
    SWAP_64B = 0b01101101,
    CAS_32B = 0b01001110,
    CAS_64B = 0b01101110
} cxl_io_fmt_type_t;

typedef struct {
    cxl_io_fmt_type_t fmt_type : 8;
    uint8_t th                 : 1;
    uint8_t rsvd               : 1;
    uint8_t attr_b2            : 1;
    uint8_t t8                 : 1;
    uint8_t tc                 : 3;
    uint8_t t9                 : 1;
    uint8_t length_upper       : 2;
    uint8_t at                 : 2;
    uint8_t attr               : 2;
    uint8_t ep                 : 1;
    uint8_t td                 : 1;
    uint32_t length_lower      : 8;
} __attribute__((packed)) cxl_io_header_t;


typedef struct {
    uint16_t req_id;
    uint8_t tag;
    uint8_t first_dw_be : 4;
    uint8_t last_dw_be  : 4;
    uint64_t addr_upper : 56; /* Adjusted for 62 bits, loops around the dword */
    uint8_t rsvd        : 2;
    uint64_t addr_lower : 6;
} __attribute__((packed)) cxl_io_mreq_header_t;


typedef struct {
    system_header_packet_t system_header;
    cxl_io_header_t cxl_io_header;
    cxl_io_mreq_header_t mreq_header;
} __attribute__((packed)) cxl_io_mem_rd_packet_t;

typedef struct {
    system_header_packet_t system_header;
    cxl_io_header_t cxl_io_header;
    cxl_io_mreq_header_t mreq_header;
    uint64_t data; /* TODO: Support dynamic data size */
} __attribute__((packed)) cxl_io_mem_wr_packet_t;

typedef struct {
    uint16_t req_id;
    uint8_t tag;
    uint8_t first_dw_be : 4;
    uint8_t last_dw_be  : 4; /* endianness compatibility -- swap order */
    uint16_t dest_id;
    uint8_t ext_reg_num : 4;
    uint8_t rsvd        : 4;
    uint8_t r           : 2;
    uint8_t reg_num     : 6;
} __attribute__((packed)) cxl_io_cfg_req_header_t;

typedef struct {
    system_header_packet_t system_header;
    cxl_io_header_t cxl_io_header;
    cxl_io_cfg_req_header_t cfg_req_header;
} __attribute__((packed)) cxl_io_cfg_rd_packet_t;

typedef struct {
    system_header_packet_t system_header;
    cxl_io_header_t cxl_io_header;
    cxl_io_cfg_req_header_t cfg_req_header;
    uint32_t value;
} __attribute__((packed)) cxl_io_cfg_wr_packet_t;

typedef struct {
    uint16_t cpl_id;
    uint8_t byte_count_upper : 4;
    uint8_t bcm              : 1;
    uint8_t status : 3; /* Python class was changed to reflect 3 bits */
    uint8_t byte_count_lower;
    uint16_t req_id;
    uint8_t tag;
    uint8_t lower_addr : 7;
    uint8_t rsvd       : 1;
} __attribute__((packed)) cxl_io_completion_header_t;

typedef struct {
    system_header_packet_t system_header;
    cxl_io_header_t cxl_io_header;
    cxl_io_completion_header_t cpl_header;
} __attribute__((packed)) cxl_io_completion_packet_t;

typedef struct {
    system_header_packet_t system_header;
    cxl_io_header_t cxl_io_header;
    cxl_io_completion_header_t cpl_header;
    uint64_t data; /* TODO: Support dynamic data size */
} __attribute__((packed)) cxl_io_completion_data_packet_t;

//
// CXL.mem
//

#define CXL_RW_NUM_BUFFERS 2
#define CXL_MEM_ACCESS_UNIT 64
#define CXL_MEM_ACCESS_OFFSET_MASK (CXL_MEM_ACCESS_UNIT - 1)

typedef enum cxl_mem_channel {
    M2S_REQ = 1,
    M2S_RWD = 2,
    M2S_BIRSP = 3,
    S2M_BISNP = 4,
    S2M_NDR = 5,
    S2M_DRS = 6
} cxl_mem_channel_t;

typedef struct cxl_mem_header_packet {
    uint8_t port_index;
    uint8_t cxl_mem_channel_t;
} cxl_mem_header_packet_t;

typedef enum cxl_mem_m2s_req_opcode { MEM_RD = 1 } cxl_mem_m2s_req_opcode_t;

typedef enum cxl_mem_m2s_rwd_opcode { MEM_WR = 1 } cxl_mem_m2s_rwd_opcode_t;

typedef struct {
    uint8_t valid      : 1; // Bit 0
    uint8_t mem_opcode : 4; // Bits 1-4
    uint8_t snp_type   : 3; // Bits 5-7
    uint8_t meta_field : 2; // Bits 8-9
    uint8_t meta_value : 2; // Bits 10-11
    uint16_t tag       : 16; // Bits 12-27
    uint64_t addr      : 46; // Bits 28-73
    uint8_t ld_id      : 4; // Bits 74-77
    uint32_t rsvd      : 20; // Bits 78-97
    uint8_t tc         : 2; // Bits 98-99
    uint8_t padding    : 4; // Padding (Bits 100-103)
} __attribute__((packed)) cxl_mem_m2s_req_header_t;

typedef struct {
    system_header_packet_t system_header;
    cxl_mem_header_packet_t cxl_mem_header;
    cxl_mem_m2s_req_header_t m2s_req_header;
} __attribute__((packed)) cxl_mem_m2s_req_packet_t;

typedef struct {
    uint8_t valid      : 1; // Bit 0
    uint8_t mem_opcode : 4; // Bits 1-4
    uint8_t snp_type   : 3; // Bits 5-7
    uint8_t meta_field : 2; // Bits 8-9
    uint8_t meta_value : 2; // Bits 10-11
    uint16_t tag       : 16; // Bits 12-27
    uint64_t addr      : 46; // Bits 28-73
    uint8_t poison     : 1; // Bit 74
    uint8_t bep        : 1; // Bit 75
    uint8_t ld_id      : 4; // Bits 76-79
    uint32_t rsvd      : 22; // Bits 80-101
    uint8_t tc         : 2; // Bits 102-103
} __attribute__((packed)) cxl_mem_m2s_rwd_header_t;

typedef struct {
    system_header_packet_t system_header;
    cxl_mem_header_packet_t cxl_mem_header;
    cxl_mem_m2s_rwd_header_t m2s_rwd_header;
    uint64_t data[8];
} __attribute__((packed)) cxl_mem_m2s_rwd_packet_t;

typedef struct {
    uint8_t valid      : 1; // Bit 0
    uint8_t opcode     : 3; // Bits 1-3
    uint8_t meta_field : 2; // Bits 4-5
    uint8_t meta_value : 2; // Bits 6-7
    uint16_t tag       : 16; // Bits 8-23
    uint8_t ld_id      : 4; // Bits 24-27
    uint8_t dev_load   : 2; // Bits 28-29
    uint16_t rsvd      : 10; // Bits 30-39
} __attribute__((packed)) cxl_mem_s2m_ndr_header_t;

typedef struct {
    system_header_packet_t system_header;
    cxl_mem_header_packet_t cxl_mem_header;
    cxl_mem_s2m_ndr_header_t s2m_ndr;
} __attribute__((packed)) cxl_mem_s2m_ndr_packet_t;

typedef struct {
    uint8_t valid      : 1; // Bit 0
    uint8_t opcode     : 3; // Bits 1-3
    uint8_t meta_field : 2; // Bits 4-5
    uint8_t meta_value : 2; // Bits 6-7
    uint16_t tag       : 16; // Bits 8-23
    uint8_t poison     : 1; // Bit 24
    uint8_t ld_id      : 4; // Bits 25-28
    uint8_t dev_load   : 2; // Bits 29-30
    uint16_t rsvd      : 9; // Bits 31-39
} __attribute__((packed)) cxl_mem_s2m_drs_header_t;

typedef struct {
    system_header_packet_t system_header;
    cxl_mem_header_packet_t cxl_mem_header;
    cxl_mem_s2m_drs_header_t s2m_drs;
    uint64_t data[8];
} __attribute__((packed)) cxl_mem_s2m_drs_packet_t;

#endif /* CXL_EMULATOR_PACKET_H */
