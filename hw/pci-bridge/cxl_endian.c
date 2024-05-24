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

uint64_t htonll(uint64_t hllong)
{
    return ntohll(hllong);
}
