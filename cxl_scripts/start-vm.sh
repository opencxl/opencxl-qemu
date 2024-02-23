#!/bin/bash

if [ $# -eq 0 ]
then
    SOCKET_HOST=0.0.0.0
else
    SOCKET_HOST=$1
fi
echo "SOCKET_HOST IS $SOCKET_HOST"

cd ../build && ./qemu-system-x86_64 \
	-m 8G -smp 4 \
	-machine type=q35,accel=kvm,cxl=on -nographic \
	-hda fedora_39.qcow2 \
	-cdrom seed.qcow2 \
	-D debug.log \
	-device pxb-cxl,bus_nr=12,bus=pcie.0,id=cxl.1 \
	-device cxl-rp,port=0,bus=cxl.1,id=root_port0,chassis=0,slot=2,socket-host=$SOCKET_HOST \
	-M "cxl-fmw.0.targets.0=cxl.1,cxl-fmw.0.size=512G" \
	-nic user,id=vmnic,hostfwd=tcp::2222-:22
