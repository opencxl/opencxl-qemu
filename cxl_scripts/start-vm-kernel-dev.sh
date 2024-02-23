#!/bin/bash

# Get total system memory in bytes
total_memory=$(free -b | awk '/Mem/{print $2}')

# Calculate 80% of total memory and convert it to Giga bytes
memory_limit=$((total_memory * 80 / 100 / (1024**3)))

# Get the number of CPU cores
cpu_cores=$(nproc)

# Set the CPU limit to 80% of available cores
cpu_limit=$((cpu_cores * 80 / 100))

cd ../build && ./qemu-system-x86_64 \
	-m "${memory_limit}G" -smp "${cpu_limit}" \
	-machine type=q35,accel=kvm -nographic \
	-hda fedora_39.qcow2 \
	-cdrom seed.qcow2 \
	-D debug.log \
	-nic user,id=vmnic,hostfwd=tcp::2222-:22
