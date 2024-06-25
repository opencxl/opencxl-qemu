/*
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef CXL_SOCKET_TRANSPORT_H
#define CXL_SOCKET_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "cxl_emulator_packet.h"

bool release_packet_entry(uint16_t tag);

// Sideband

bool send_sideband_connection_request(int socket_fd, uint32_t port);
base_sideband_packet_t *wait_for_base_sideband_packet(int socket_fd);

// CXL.mem

bool send_cxl_mem_mem_write(int socket_fd, hwaddr hpa, uint8_t *data,
                            uint16_t *tag);
bool send_cxl_mem_mem_read(int socket_fd, hwaddr hpa, uint16_t *tag);
cxl_mem_s2m_ndr_packet_t *wait_for_cxl_mem_completion(int socket_fd,
                                                      uint16_t tag);
cxl_mem_s2m_drs_packet_t *wait_for_cxl_mem_mem_data(int socket_fd,
                                                    uint16_t tag);

// CXL.io

bool send_cxl_io_mem_read(int socket_fd, hwaddr hpa, int size, uint16_t *tag);
bool send_cxl_io_mem_write(int socket_fd, hwaddr hpa, uint64_t val, int size,
                           uint16_t *tag);
bool send_cxl_io_config_space_read(int socket_fd, uint16_t bdf, uint32_t offset,
                                   int size, bool type0, uint16_t *tag);
bool send_cxl_io_config_space_write(int socket_fd, uint16_t bdf,
                                    uint32_t offset, uint32_t val, int size,
                                    bool type0, uint16_t *tag);
cxl_io_completion_packet_t *wait_for_cxl_io_completion(int socket_fd,
                                                       uint16_t tag);
uint64_t wait_for_cxl_io_completion_data(int socket_fd, uint16_t tag,
                                         uint64_t *data);
void wait_for_cxl_io_cfg_completion(int socket_fd, uint16_t tag,
                                    uint32_t *data);

// Socket

int32_t create_socket_client(const char *host, uint32_t port);

#endif // CXL_SOCKET_TRANSPORT_H
