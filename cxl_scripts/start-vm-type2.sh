#!/bin/bash
cd ../build && sudo ./qemu-system-x86_64 \
	-drive file=/var/lib/libvirt/images/CXL-Test.qcow2,format=qcow2,index=0,media=disk,id=hd \
	-m 4G,slots=8,maxmem=8G \
	-machine type=q35,cxl=on \
	-nographic \
	-net nic \
	-net user,hostfwd=tcp::2222-:22 \
	-object memory-backend-ram,id=cxl-mem1,share=on,size=256M \
	-smp 4 \
	-device pxb-cxl,bus_nr=12,bus=pcie.0,id=cxl.1 \
	-device cxl-rp,port=0,bus=cxl.1,id=root_port0,chassis=0,slot=0 \
	-device cxl-upstream,bus=root_port0,id=us0 \
	-device cxl-downstream,port=0,bus=us0,id=swport0,chassis=0,slot=4 \
	-device cxl-type2,bus=swport0,memdev=cxl-mem1,id=cxl-vmem0 \
	-M cxl-fmw.0.targets.0=cxl.1,cxl-fmw.0.size=4G
