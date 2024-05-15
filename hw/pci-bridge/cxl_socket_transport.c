#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/range.h"
#include "qemu/bitops.h"
#include "hw/cxl/cxl_socket_transport.h"
#include "trace.h"

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>

#define MAX_TAG          512
#define MAX_PAYLOAD_SIZE 512
#define MAX_DURATION     5

// For mreq_header_t endianness compatibility
#define EXTRACT_UPPER_56(address) (extract64(address, 2, 56))
#define EXTRACT_LOWER_6(address) (extract64(address, 58, 6))

// For cfg_req_header_t endianness compatibility
#define EXTRACT_EXTENSION_4(reg) (extract16(reg, 6, 4))

typedef struct packet_table_entry {
    uint8_t packet[MAX_PAYLOAD_SIZE];
    size_t packet_size;
} packet_table_entry_t;

packet_table_entry_t packet_entries[512] = { 0 };

static bool wait_for_payload(int socket_fd, uint8_t *buffer, size_t buffer_size,
                             size_t payload_size);
static bool wait_for_system_header(int socket_fd, uint8_t *buffer,
                                   size_t buffer_size);
static uint16_t get_next_tag(void);
static bool process_incoming_packets(int socket_fd);
static packet_table_entry_t *get_packet_entry(uint16_t tag);

bool wait_for_payload(int socket_fd, uint8_t *buffer, size_t buffer_size,
                      size_t payload_size)
{
    time_t start_time = time(NULL); // Record the start time
    time_t current_time;
    size_t total_bytes_read = 0;
    trace_cxl_socket_debug_num("Waiting for payload, Payload Size",
                               payload_size);
    while (total_bytes_read < payload_size) {
        current_time = time(NULL);

        // Check if the time elapsed exceeds the maximum duration
        if (difftime(current_time, start_time) > MAX_DURATION) {
            trace_cxl_socket_debug_msg("Timeout exceeded!");
            return false;
        }

        size_t remaining_size = payload_size - total_bytes_read;
        ssize_t bytes_read =
            read(socket_fd, &buffer[total_bytes_read], remaining_size);
        if (bytes_read <= 0) {
            trace_cxl_socket_debug_msg("Failed to read bytes from socket");
            return false;
        }
        trace_cxl_socket_debug_num("Bytes read", bytes_read);
        if (bytes_read > 0 && bytes_read + total_bytes_read > buffer_size) {
            trace_cxl_socket_debug_msg("Buffer overflowed");
            return false;
        }
        if (bytes_read > 0) {
            total_bytes_read += bytes_read;
        }
    }

    trace_cxl_socket_debug_msg("Done Waiting for payload");
    return true;
}

bool wait_for_system_header(int socket_fd, uint8_t *buffer, size_t buffer_size)
{
    size_t payload_size = sizeof(system_header_packet_t);
    return wait_for_payload(socket_fd, buffer, buffer_size, payload_size);
}

// uint16_t current_tag = 0;

uint16_t get_next_tag(void)
{
    // TODO: Enable this once packets start supporting tag
    // const uint16_t next_tag = current_tag;
    // current_tag += 1;
    // return next_tag;
    return 0;
}

bool process_incoming_packets(int socket_fd)
{
    uint8_t buffer[MAX_PAYLOAD_SIZE];
    size_t buffer_size = sizeof(buffer);

    if (!wait_for_system_header(socket_fd, buffer, buffer_size)) {
        trace_cxl_socket_debug_msg("Failed to get system header");
        return false;
    }

    trace_cxl_socket_debug_msg("Received system header");

    system_header_packet_t *system_header = (system_header_packet_t *)(buffer);
    const size_t system_header_size = sizeof(system_header_packet_t);
    const size_t remaining_payload_size =
        system_header->payload_length - system_header_size;
    const size_t buffer_offset = system_header_size;
    buffer_size = buffer_size - buffer_offset;

    trace_cxl_socket_debug_num("- system_header_size", system_header_size);
    trace_cxl_socket_debug_num("- remaining_payload_size",
                               remaining_payload_size);
    trace_cxl_socket_debug_num("- buffer_offset", buffer_offset);
    trace_cxl_socket_debug_num("- buffer_size", buffer_size);
    if (!wait_for_payload(socket_fd, &buffer[buffer_offset], buffer_size,
                          remaining_payload_size)) {
        trace_cxl_socket_debug_msg("Failed to get packet payload");
        return false;
    }

    const uint16_t tag = 0;
    assert(packet_entries[tag].packet_size == 0);
    memcpy(packet_entries[tag].packet, buffer, system_header->payload_length);
    packet_entries[tag].packet_size = system_header->payload_length;
    return true;
}

packet_table_entry_t *get_packet_entry(uint16_t tag)
{
    if (tag >= MAX_TAG) {
        return NULL;
    }
    trace_cxl_socket_debug_num("Getting packet entry for tag", tag);
    return &packet_entries[tag];
}

bool release_packet_entry(uint16_t tag)
{
    if (tag >= MAX_TAG) {
        trace_cxl_socket_debug_num("Failed to release tag", tag);
        return false;
    }
    trace_cxl_socket_debug_num("Releasing tag", tag);
    packet_entries[tag].packet_size = 0;
    return true;
}

//
// Sideband
//

bool send_sideband_connection_request(int socket_fd, uint32_t port)
{
    trace_cxl_socket_debug_msg("Sending Sideband Connection Request Packet");
    sideband_connection_request_packet_t packet = {};
    packet.system_header.payload_type = SIDEBAND;
    packet.system_header.payload_length = sizeof(packet);
    packet.sideband_header.type = SIDEBAND_CONNECTION_REQUEST;
    packet.port = port;

    if (write(socket_fd, &packet, sizeof(packet)) == -1) {
        // TODO: Add trace for warning
        return false;
    }

    return true;
}

base_sideband_packet_t *wait_for_base_sideband_packet(int socket_fd)
{
    trace_cxl_socket_debug_msg("Waiting for Base Sideband Packet");
    // NOTE: Always use 0 for tag when receiving packets for sideband.
    const uint16_t tag = 0;
    while (true) {
        packet_table_entry_t *entry = get_packet_entry(tag);
        if (entry->packet_size == sizeof(base_sideband_packet_t)) {
            trace_cxl_socket_debug_msg("Received Base Sideband Packet");
            return (base_sideband_packet_t *)(entry->packet);
        }
        if (!process_incoming_packets(socket_fd)) {
            return NULL;
        }
    }
}

//
// CXL.mem
//

bool send_cxl_mem_mem_write(int socket_fd, hwaddr hpa, uint8_t *data,
                            uint16_t *tag)
{
    trace_cxl_socket_debug_msg("[Sending Packet] START");

    *tag = get_next_tag();

    cxl_mem_m2s_rwd_packet_t packet = {};
    packet.system_header.payload_type = CXL_MEM;
    packet.system_header.payload_length = sizeof(packet);
    packet.cxl_mem_header.cxl_mem_channel_t = M2S_RWD;
    packet.m2s_rwd_header.mem_opcode = MEM_WR;
    packet.m2s_rwd_header.addr = hpa >> 6;
    memcpy(packet.data, data, CXL_MEM_ACCESS_UNIT);

    trace_cxl_socket_debug_num("CXL.mem M2S_RWD Packet Size", sizeof(packet));

    bool successful = write(socket_fd, &packet, sizeof(packet)) != -1;

    trace_cxl_socket_debug_msg("[Sending Packet] END");

    return successful;
}

bool send_cxl_mem_mem_read(int socket_fd, hwaddr hpa, uint16_t *tag)
{
    trace_cxl_socket_debug_msg("[Sending Packet] START");

    *tag = get_next_tag();

    cxl_mem_m2s_req_packet_t packet = {};
    packet.system_header.payload_type = CXL_MEM;
    packet.system_header.payload_length = sizeof(packet);
    packet.cxl_mem_header.cxl_mem_channel_t = M2S_REQ;
    packet.m2s_req_header.mem_opcode = MEM_RD;
    packet.m2s_req_header.addr = hpa >> 6;

    trace_cxl_socket_debug_num("CXL.mem M2S_REQ Packet Size", sizeof(packet));

    bool successful = write(socket_fd, &packet, sizeof(packet)) != -1;

    trace_cxl_socket_debug_msg("[Sending Packet] END");

    return successful;
}

cxl_mem_s2m_ndr_packet_t *wait_for_cxl_mem_completion(int socket_fd,
                                                      uint16_t tag)
{
    while (true) {
        packet_table_entry_t *entry = get_packet_entry(tag);
        if (entry->packet_size == sizeof(cxl_mem_s2m_ndr_packet_t)) {
            return (cxl_mem_s2m_ndr_packet_t *)(entry->packet);
        }
        if (!process_incoming_packets(socket_fd)) {
            return NULL;
        }
    }
}

cxl_mem_s2m_drs_packet_t *wait_for_cxl_mem_mem_data(int socket_fd, uint16_t tag)
{
    while (true) {
        packet_table_entry_t *entry = get_packet_entry(tag);
        if (entry->packet_size == sizeof(cxl_mem_s2m_drs_packet_t)) {
            return (cxl_mem_s2m_drs_packet_t *)(entry->packet);
        }
        if (!process_incoming_packets(socket_fd)) {
            return NULL;
        }
    }
}

//
// CXL.io
//

static uint32_t round_up_to_nearest_dword(uint32_t number)
{
    const uint32_t dword_size = 4; // DWORD is 4 bytes
    return (number + dword_size - 1) & ~(dword_size - 1);
}

bool send_cxl_io_mem_read(int socket_fd, hwaddr hpa, int size, uint16_t *tag)
{
    trace_cxl_socket_debug_msg("[Sending Packet] START");

    *tag = get_next_tag();

    trace_cxl_socket_cxl_io_mmio_read(hpa, size);

    assert(size % 4 == 0); // Ensure size is dword aligned

    cxl_io_mem_rd_packet_t packet = {};

    packet.system_header.payload_type = CXL_IO;
    packet.system_header.payload_length = sizeof(packet);

    packet.cxl_io_header.fmt_type = MRD_64B;
    packet.cxl_io_header.length = round_up_to_nearest_dword(size);

    packet.mreq_header.req_id = 0;
    packet.mreq_header.tag = *tag;
    packet.mreq_header.addr_upper = EXTRACT_UPPER_56(hpa); // endianness compatibility
    packet.mreq_header.addr_lower = EXTRACT_LOWER_6(hpa); // ditto

    trace_cxl_socket_debug_num("MRD_64B Packet Size", sizeof(packet));

    bool successful = write(socket_fd, &packet, sizeof(packet)) != -1;

    trace_cxl_socket_debug_msg("[Sending Packet] END");

    return successful;
}

bool send_cxl_io_mem_write(int socket_fd, hwaddr hpa, uint64_t val, int size,
                           uint16_t *tag)
{
    trace_cxl_socket_debug_msg("[Sending Packet] START");

    *tag = get_next_tag();

    trace_cxl_socket_cxl_io_mmio_write(hpa, size, val);

    assert(size % 4 == 0); // Ensure size is dword aligned

    cxl_io_mem_wr_packet_t packet;

    packet.system_header.payload_type = CXL_IO;
    packet.system_header.payload_length = sizeof(packet);

    packet.cxl_io_header.fmt_type = MWR_64B;
    packet.cxl_io_header.length = round_up_to_nearest_dword(size);

    packet.mreq_header.req_id = 0;
    packet.mreq_header.tag = *tag;
    packet.mreq_header.addr_upper = EXTRACT_UPPER_56(hpa);
    packet.mreq_header.addr_lower = EXTRACT_LOWER_6(hpa);

    packet.data = val;

    trace_cxl_socket_debug_num("MRD_64B Packet Size", sizeof(packet));

    bool successful = write(socket_fd, &packet, sizeof(packet)) != -1;

    trace_cxl_socket_debug_msg("[Sending Packet] END");

    return successful;
}

static bool fill_cxl_io_cfg_req_packet(cxl_io_cfg_req_header_t *header,
                                       uint16_t id, uint32_t cfg_addr,
                                       uint8_t size, uint16_t req_id,
                                       uint8_t tag)
{
    if (cfg_addr > 0xFFF) {
        return false;
    }

    uint8_t offset = cfg_addr & 0x03;
    if ((offset + size) > 4) {
        return false;
    }

    uint8_t first_dw_be = 0;
    for (int i = 0; i < size; ++i) {
        first_dw_be |= 1 << offset;
        offset++;
    }

    header->req_id = req_id;
    header->tag = tag;
    header->first_dw_be = first_dw_be;
    header->last_dw_be = 0;
    header->dest_id = id;
    uint16_t reg_num = (cfg_addr >> 2) & 0x3FF;
    header->ext_reg_num = EXTRACT_EXTENSION_4(reg_num);
    header->reg_num = EXTRACT_LOWER_6(reg_num);
    return true;
}

bool send_cxl_io_config_space_read(int socket_fd, uint16_t bdf, uint32_t offset,
                                   int size, bool type0, uint16_t *tag)
{
    trace_cxl_socket_debug_msg("[Sending Packet] START");

    *tag = get_next_tag();

    const uint8_t bus = bdf >> 8;
    const uint8_t device = bdf & 0x1F >> 3;
    const uint8_t function = bdf & 0x7;

    trace_cxl_socket_cxl_io_config_space_read(bus, device, function, offset,
                                              size);

    cxl_io_cfg_rd_packet_t packet = {};

    packet.system_header.payload_type = CXL_IO;
    packet.system_header.payload_length = sizeof(packet);

    packet.cxl_io_header.length = 1;
    packet.cxl_io_header.fmt_type = type0 ? CFG_RD0 : CFG_RD1;

    fill_cxl_io_cfg_req_packet(&packet.cfg_req_header, bdf, offset, size, 0,
                               *tag);

    trace_cxl_socket_debug_num("CFG RD Packet Size", sizeof(packet));

    bool successful = write(socket_fd, &packet, sizeof(packet)) != -1;

    trace_cxl_socket_debug_msg("[Sending Packet] END");

    return successful;
}

bool send_cxl_io_config_space_write(int socket_fd, uint16_t bdf,
                                    uint32_t offset, uint32_t val, int size,
                                    bool type0, uint16_t *tag)
{
    trace_cxl_socket_debug_msg("[Sending Packet] START");

    *tag = get_next_tag();

    const uint8_t bus = bdf >> 8;
    const uint8_t device = bdf & 0x1F >> 3;
    const uint8_t function = bdf & 0x7;
    trace_cxl_socket_cxl_io_config_space_write(bus, device, function, offset,
                                               size, val);

    cxl_io_cfg_wr_packet_t packet = {};

    packet.system_header.payload_type = CXL_IO;
    packet.system_header.payload_length = sizeof(packet);

    packet.cxl_io_header.length = 1;
    packet.cxl_io_header.fmt_type = type0 ? CFG_WR0 : CFG_WR1;

    fill_cxl_io_cfg_req_packet(&packet.cfg_req_header, bdf, offset, size, 0,
                               *tag);

    packet.value = val;

    trace_cxl_socket_debug_num("CFG WR Packet Size", sizeof(packet));

    bool successful = write(socket_fd, &packet, sizeof(packet)) != -1;

    trace_cxl_socket_debug_msg("[Sending Packet] END");

    return successful;
}

cxl_io_completion_packet_t *wait_for_cxl_io_completion(int socket_fd,
                                                       uint16_t tag)
{
    trace_cxl_socket_debug_msg("[Receiving Packet] START");

    cxl_io_completion_packet_t *packet = NULL;

    while (true) {
        packet_table_entry_t *entry = get_packet_entry(tag);
        if (entry->packet_size > 0) {
            assert(entry->packet_size == sizeof(cxl_io_completion_packet_t));
            trace_cxl_socket_cxl_io_cpl();
            packet = (cxl_io_completion_packet_t *)(entry->packet);
            break;
        }
        if (!process_incoming_packets(socket_fd)) {
            break;
        }
    }

    trace_cxl_socket_debug_msg("[Receiving Packet] END");

    return packet;
}

cxl_io_completion_data_packet_t *wait_for_cxl_io_completion_data(int socket_fd,
                                                                 uint16_t tag)
{
    trace_cxl_socket_debug_msg("[Receiving Packet] START");

    cxl_io_completion_data_packet_t *packet = NULL;

    while (true) {
        packet_table_entry_t *entry = get_packet_entry(tag);
        if (entry->packet_size > 0) {
            assert(entry->packet_size ==
                   sizeof(cxl_io_completion_data_packet_t));
            packet = (cxl_io_completion_data_packet_t *)(entry->packet);
            for (uint32_t dword_offset = 0;
                 dword_offset < packet->cxl_io_header.length; ++dword_offset) {
                trace_cxl_socket_cxl_io_cpld(packet->data);
            }
            break;
        }
        if (!process_incoming_packets(socket_fd)) {
            break;
        }
    }

    trace_cxl_socket_debug_msg("[Receiving Packet] END");

    return packet;
}

void wait_for_cxl_io_cfg_completion(int socket_fd, uint16_t tag, uint32_t *data)
{
    trace_cxl_socket_debug_msg("[Receiving Packet] START");

    while (true) {
        packet_table_entry_t *entry = get_packet_entry(tag);
        if (entry->packet_size > 0) {
            if (data == NULL) {
                assert(entry->packet_size ==
                       sizeof(cxl_io_completion_packet_t));
            } else {
                assert(entry->packet_size ==
                           sizeof(cxl_io_completion_packet_t) ||
                       entry->packet_size ==
                           sizeof(cxl_io_completion_data_packet_t));
            }

            if (entry->packet_size == sizeof(cxl_io_completion_packet_t)) {
                if (data != NULL) {
                    *data = 0xFFFFFFFF;
                }
            } else {
                cxl_io_completion_data_packet_t *packet =
                    (cxl_io_completion_data_packet_t *)(entry->packet);
                *data = (uint32_t)(packet->data);
            }
            trace_cxl_socket_cxl_io_cpl();
            break;
        }
        if (!process_incoming_packets(socket_fd)) {
            break;
        }
    }

    trace_cxl_socket_debug_msg("[Receiving Packet] END");
}

int32_t create_socket_client(const char *host, uint32_t port)
{
    // Create a socket
    int32_t sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        trace_cxl_socket_debug_msg("Failed to create socket");
        return -1;
    }

    // Set the socket address
    struct sockaddr_in addr;
    struct hostent *he;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        /* could be a hostname */
        if ((he = gethostbyname(host)) == NULL) {
            trace_cxl_socket_debug_msg("Invalid address or hostname");
            return -1;
        }
        bcopy(he->h_addr_list[0],&addr.sin_addr, he->h_length);
    }

    // Connect to the socket
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        trace_cxl_socket_debug_msg("Failed to connect to socket server");
        return -1;
    }

    struct timeval timeout;
    timeout.tv_sec = MAX_DURATION; // 5 seconds timeout
    timeout.tv_usec = 0; // 0 microseconds

    // Set the receive timeout
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                   sizeof(timeout)) < 0) {
        trace_cxl_socket_debug_msg("setsockopt failed for receive");
    }

    // Set the send timeout
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
                   sizeof(timeout)) < 0) {
        trace_cxl_socket_debug_msg("setsockopt failed for send");
    }

    return sockfd;
}
