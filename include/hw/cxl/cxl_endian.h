/**
 * @file cxl_endian.h
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

#ifndef CXL_ENDIAN_H

#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>

#include "hw/cxl/cxl_emulator_packet.h"

/**
 * @brief Converts "network" uint64_t (big-endian) to "host" uint64_t
 * (little-endian). Note that this function is not standardized by the
 * UNIX standard, but we need it anyway.
 */
uint64_t ntohll(uint64_t netllong);

/**
 * @brief Converts "host" uint64_t (little-endian) to "network" uint64_t
 * (big-endian). Note that this function is not standardized by the
 * UNIX standard, but we need it anyway.
 */
uint64_t htonll(uint64_t hllong);

#define CXL_ENDIAN_H
#endif