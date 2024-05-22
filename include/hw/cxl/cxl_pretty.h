#ifndef CXL_PRETTY_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "hw/cxl/cxl_emulator_packet.h"

/**
 * @brief Writes a pretty string representing the packet to the buffer
 * pointed to by `buf`. At most `at_most` characters are written.
 *
 * @pre `pckt` points to a valid CXL.io packet
 */
void snpprintpacket(char *buf, void *pckt, size_t at_most);

#define CXL_PRETTY_H
#endif