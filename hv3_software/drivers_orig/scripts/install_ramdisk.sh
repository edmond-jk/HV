#!/bin/bash

# remove hv kernel module
sudo rmmod hv

# remove discovery kernel module
sudo rmmod dis

# install ioctl defs.
if ! [ -d /usr/include/uapi/linux ]
then
	mkdir /usr/include/uapi/linux/
fi

if [ -f ../hv_cdev_uapi.h ]
then
	cp ../hv_cdev_uapi.h /usr/include/uapi/linux/
else
	cp hv_cdev_uapi.h /usr/include/uapi/linux/
fi

# install discovery kernel module
cd discovery
sudo insmod dis.ko
cd ..

# install hv kernel module
#	hv_mmap_type:
#		HV mem mmap type: 0: wb, 1: wc, 2: uncached
#	async_mode: 0-disabled (default) 1-enabled
#		when disabled, driver confirms a HV command has completed before
#		sending next command. when enabled, driver is allowed to send 
#		multiple HV commands per queue_size
#	queue_size: 2-64 (default 64)
#		size of queue when async_mode is set to 1
#	use_memmap: 0 (default)
#		use memmap when reserving HVDIMM space during linux boot
#	ramdisk: 0-disabled 1-enabled (default)
#		use system memory as storage instead of HVDIMM
#	ramdisk_start: (default 0x100000000)
#		beginning physical address of system memory used as ramdisk
# 	single_cmd_test: 1-enable hv_cmd debugfs, 0-disable (default)
#	cache_enabled: 1-enable 0-disable (default)
#
sudo insmod hv.ko \
	hv_mmap_type=0 \
	async_mode=0 \
	queue_size=16 \
	use_memmap=1 \
	ramdisk=1 \
	ramdisk_start=0x100000000 \
	single_cmd_test=0 \
	cache_enabled=0

dmesg | tail -n20

