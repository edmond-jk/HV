
#
# choices of iozone tests
#
io_test="iozone –Ra –g 2G –i 0 –i 1"
#io_test="iozone -a"

#
# Mount BSM/MMLS file system
#
function hv_mount {
	echo "n
p
1


w
"	|fdisk /dev/hv_bsm0 &> /dev/null; 
	sudo mkfs /dev/hv_bsm0p1 &> /dev/null
	sudo mount /dev/hv_bsm0p1 bsm
	cd bsm
	if [ -e "lost+found" ]; then
		echo -e "\nMount file system on BSM passed\n" >> ../scripts/block.log
	else
		echo -e "\nMount file system on BSM failed\n" >> ../scripts/block.log
		cat ../scripts/block.log
#		exit
	fi
	cd ..

	echo "n
p
1


w
"	|fdisk /dev/hv_mmls0 &> /dev/null; 
	sudo mkfs /dev/hv_mmls0p1 &> /dev/null
	sudo mount /dev/hv_mmls0p1 mmls
	cd mmls
	if [ -e "lost+found" ]; then
		echo -e "Mount file system on MMLS passed\n" >> ../scripts/block.log
	else
		echo -e "Mount file system on MMLS failed\n" >> ../scripts/block.log
		cat ../scripts/block.log
#		exit
	fi
	cd ..
}

#
# Unmount BSM/MMLS file system
#
function hv_unmount {
	sudo umount bsm
	echo "d
	w
	"|fdisk /dev/hv_bsm0 &> /dev/null; 
	cd bsm
	if [ ! -e "lost+found" ]; then
		echo -e "Unmount file system on BSM passed\n" >> ../scripts/block.log
	else
		echo -e "Unmount file system on BSM failed\n" >> ../scripts/block.log
		cat ../scripts/block.log
#		exit
	fi
	cd ..

	sudo umount mmls
	echo "d
	w
	"|fdisk /dev/hv_mmls0 &> /dev/null; 
	cd mmls
	if [ ! -e "lost+found" ]; then
		echo -e "Unmount file system on MMLS passed\n" >> ../scripts/block.log
	else
		echo -e "Unmount file system on MMLS failed\n" >> ../scripts/block.log
		cat ../scripts/block.log
#		exit
	fi
	cd ..
}

#
# Remove existing log file
#
rm scripts/block.log &> /dev/null
mkdir bsm &> /dev/null
mkdir mmls &> /dev/null

#
# Unmount HV file system
#
echo -e "\nUnmount existing HV file system\n"
hv_unmount

#
# remove existing and install new HV kernel module

# set ramdisk to 1 to use RAMDISK and also configure ramdisk_start accordingly
# block devices /dev/hv_bsm0 and /dev/hv__mmls0 should be created afterwards
echo -e "\nInstall HV kernel module\n"

# remove hv kernel module
sudo rmmod hv

# remove discovery kernel module
sudo rmmod dis

# install discovery kernel module
cd discovery
sudo insmod dis.ko
cd ..

sudo insmod hv.ko \
	hv_mmap_type=0 \
	async_mode=0 \
	queue_size=16 \
	use_memmap=1 \
	ramdisk=1 \
	ramdisk_start=0x100000000 \
	single_cmd_test=0 \
	cache_enabled=1

bsm=/dev/hv_bsm0
mmls=/dev/hv_mmls0
if [ ! -e $bsm ]; then
	echo -e "No BSM block device found!!!\n"
#	exit
fi
if [ ! -e $mmls ]; then
	echo -e "No MMLS block device found!!!\n"
#	exit
fi

#
# Create block test log file
#
echo -e "\nCreated scripts/block.log file\n"
echo -e "\nCreated scripts/block.log file\n" > scripts/block.log

#
# Run perform sequential read/write test using dd
#
echo -e "Perform dd on BSM/MMLS\n"
echo -e "Perform dd on BSM/MMLS\n" >> scripts/block.log
sudo sh scripts/dd.sh &>> scripts/block.log

#
# Mount HV file system
#
hv_mount

#
# Perform iozone test on BSM
#
echo -e "Perform \"$io_test\" on BSM\n"
echo -e "Perform \"$io_test\" on BSM\n" >> scripts/block.log
cd bsm
$io_test >> ../scripts/block.log
cd ..

#
# Perform iozone test on MMLS
#
echo -e "Perform \"$io_test\" on MMLS\n"
echo -e "Perform \"$io_test\" on MMLS\n" >> scripts/block.log
cd mmls
$io_test >> ../scripts/block.log
cd ..

#
# Unmount HV file system
#
hv_unmount

#
# remove test folders
#
rm -rf bsm &> /dev/null
rm -rf mmls &> /dev/null

#
# Display test result
#
cat scripts/block.log





