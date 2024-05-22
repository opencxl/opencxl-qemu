/**
 * @file cxl_pretty.h
 * @brief Utility functions for pretty-printing CXL packets.
 *
 * @copyright 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * @author Benedict Song
 */

#ifndef CXL_PRETTY_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "hw/cxl/cxl_emulator_packet.h"

/**
 * @brief Writes a pretty string representing the packet to the buffer
 * pointed to by `buf`. At most `at_most` characters are written.
 *
 * @note As of now, only CXL.io packets are supported for pretty-printing.
 *
 * @pre `pckt` points to a valid CXL.io packet
 */
void snpprintpacket(char *buf, void *pckt, size_t at_most);

#define CXL_PRETTY_H
#endif