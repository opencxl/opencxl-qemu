#!/bin/bash
cd ../build && gdb --args ./qemu-system-x86_64 \
	--trace "cxl_root*" \
    --trace "cxl_usp*" \
    --trace "cxl_socket_cxl_io*" \
    --trace "qdev_device*" \
    --trace "pc_debug*" \
    --trace "vl_debug*" \
    --trace "pci_debug*" \
	-m 8G -smp 4 \
	-machine type=q35,accel=kvm,cxl=on -nographic \
	-hda fedora_39.qcow2 \
	-cdrom seed.qcow2 \
	-D debug.log \
    -object memory-backend-file,id=cxl-mem1,share=on,mem-path=/tmp/cxltest.raw,size=256M \
    -object memory-backend-file,id=cxl-lsa1,share=on,mem-path=/tmp/lsa.raw,size=256M \
    -device pxb-cxl,bus_nr=12,bus=pcie.0,id=cxl.1 \
    -device cxl-rp,port=0,bus=cxl.1,id=root_port0,chassis=0,slot=2,socket-host=0.0.0.0 \
    -M cxl-fmw.0.targets.0=cxl.1,cxl-fmw.0.size=512G \
	-nic user,id=vmnic,hostfwd=tcp::2222-:22
