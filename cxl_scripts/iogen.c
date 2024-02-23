/*
 * Copyright (c) 2024 EEUM, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define SYSFS_PATH_MAX  256
#define DEVICE_PATH_MAX 256

#define DEFAULT_STEP_SIZE   64
#define ALIGNMENT           2097152 // 2 * 1024 * 1024 (2 MiB)
#define DEFAULT_DEVICE_PATH "dax0.0"

void get_device_size(const char *device_path, off_t *device_size)
{
    char sysfs_path[SYSFS_PATH_MAX];
    FILE *sysfs_file;

    snprintf(sysfs_path, SYSFS_PATH_MAX, "/sys/bus/dax/devices/%s/size",
             device_path);

    sysfs_file = fopen(sysfs_path, "r");
    if (sysfs_file == NULL) {
        perror("Failed to open sysfs file");
        exit(1);
    }

    if (fscanf(sysfs_file, "%ld", device_size) != 1) {
        perror("Failed to read device size from sysfs");
        fclose(sysfs_file);
        exit(1);
    }

    fclose(sysfs_file);
}

int main(int argc, char *argv[])
{
    const char *target_device = (argc > 1) ? argv[1] : DEFAULT_DEVICE_PATH;
    char device_path[DEVICE_PATH_MAX];
    snprintf(device_path, DEVICE_PATH_MAX, "/dev/%s", target_device);

    int fd;
    char *mmap_ptr;
    off_t pagesize = sysconf(_SC_PAGESIZE);
    off_t offset;

    printf("Page size: %ld bytes\n", pagesize);

    // Open the character device
    fd = open(device_path, O_RDWR);
    if (fd < 0) {
        perror("Failed to open the device");
        return 1;
    }

    // Get the device size
    off_t device_size;
    get_device_size(target_device, &device_size);
    printf("Device size: %ld bytes\n", device_size);

    // Ensure the device size is at least as large as DEFAULT_STEP_SIZE
    if (device_size < DEFAULT_STEP_SIZE) {
        fprintf(stderr, "Device size is too small for the data.\n");
    }

    // Calculate the offset for alignment
    offset = (ALIGNMENT - (DEFAULT_STEP_SIZE % ALIGNMENT)) % ALIGNMENT;

    // Memory map the device into user space with proper alignment
    const uint64_t capacity = device_size;
    const uint64_t step_size =
        (argc > 2) ? strtoull(argv[2], NULL, 10) : DEFAULT_STEP_SIZE;

    mmap_ptr = mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    printf("MMAP at %p\n", mmap_ptr);

    if (mmap_ptr == MAP_FAILED) {
        perror("Memory mapping failed");
        close(fd);
        return 1;
    }

    // Adjust the pointer to achieve proper alignment
    // mmap_ptr += offset;

    uint64_t io_offset;

    printf("Trying to access %ld bytes of memory with step size %ld bytes\n",
           capacity, step_size);

    while (1) {
        for (io_offset = 0; io_offset < capacity; io_offset += step_size) {
            // Write data to the device (memory-mapped)
            *(uint64_t *)(&mmap_ptr[io_offset]) = io_offset;
            printf("Data 0x%lx written at offset 0x%lx\n", io_offset,
                   io_offset);
        }

        for (io_offset = 0; io_offset < capacity; io_offset += step_size) {
            // Read data from the device (memory-mapped)
            uint64_t data = *(uint64_t *)(&mmap_ptr[offset]);
            printf("Data 0x%lx read from offset 0x%lx\n", data, io_offset);
        }
    }

    // Unmap the memory-mapped region
    if (munmap(mmap_ptr - offset, DEFAULT_STEP_SIZE + offset) == -1) {
        // perror("Unmapping failed");
        close(fd);
        return 1;
    }

    // Close the device
    close(fd);

    return 0;
}
