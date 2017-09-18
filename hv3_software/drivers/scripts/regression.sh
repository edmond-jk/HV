
echo ""
echo "---------------------------------------------------------"
echo "HV COMMAND driver test mode"
echo "run stress test with async mode disabled, cache disabled"
echo ""
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
	ramdisk=0 \
	ramdisk_start=0x100000000 \
	single_cmd_test=1 \
	cache_enabled=0
sh scripts/stress.sh

echo ""
echo "---------------------------------------------------------"
echo "HV COMMAND driver test mode"
echo "run stress test with async mode disabled, cache enabled"
echo ""
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
	ramdisk=0 \
	ramdisk_start=0x100000000 \
	single_cmd_test=1 \
	cache_enabled=1
sh scripts/stress.sh

echo ""
echo "---------------------------------------------------------"
echo "HV COMMAND driver test mode"
echo "run stress test with async mode enabled, cache disabled"
echo ""
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
	async_mode=1 \
	queue_size=16 \
	use_memmap=1 \
	ramdisk=0 \
	ramdisk_start=0x100000000 \
	single_cmd_test=1 \
	cache_enabled=0
sh scripts/stress.sh

echo ""
echo "---------------------------------------------------------"
echo "HV COMMAND driver test mode"
echo "run stress test with async mode enabled, cache enabled"
echo ""
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
	async_mode=1 \
	queue_size=16 \
	use_memmap=1 \
	ramdisk=0 \
	ramdisk_start=0x100000000 \
	single_cmd_test=1 \
	cache_enabled=1
sh scripts/stress.sh

echo ""
echo "---------------------------------------------------------"
echo "BLOCK driver test mode"
echo "run dd test on RAMDISK with async mode disabled, cache disabled"
echo ""
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
	cache_enabled=0
sh scripts/dd.sh

echo ""
echo "---------------------------------------------------------"
echo "BLOCK driver test mode"
echo "run dd test on RAMDISK with async mode disabled, cache enabled"
echo ""
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
sh scripts/dd.sh

echo ""
echo "---------------------------------------------------------"
echo "BLOCK driver test mode"
echo "run dd test on RAMDISK with async mode enabled, cache disabled"
echo ""
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
	async_mode=1 \
	queue_size=16 \
	use_memmap=1 \
	ramdisk=1 \
	ramdisk_start=0x100000000 \
	single_cmd_test=0 \
	cache_enabled=0
sh scripts/dd.sh

echo ""
echo "---------------------------------------------------------"
echo "BLOCK driver test mode"
echo "run dd test on RAMDISK with async mode enabled, cache enabled"
echo ""
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
	async_mode=1 \
	queue_size=16 \
	use_memmap=1 \
	ramdisk=1 \
	ramdisk_start=0x100000000 \
	single_cmd_test=0 \
	cache_enabled=1

sh scripts/dd.sh

echo ""
echo "---------------------------------------------------------"
echo "BLOCK driver test mode"
echo "run dd test on HVDIMM with async mode disabled, cache disabled"
echo ""
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
	ramdisk=0 \
	ramdisk_start=0x100000000 \
	single_cmd_test=0 \
	cache_enabled=0
sh scripts/dd.sh

echo ""
echo "---------------------------------------------------------"
echo "BLOCK driver test mode"
echo "run dd test on HVDIMM with async mode disabled, cache enabled"
echo ""
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
	ramdisk=0 \
	ramdisk_start=0x100000000 \
	single_cmd_test=0 \
	cache_enabled=1
sh scripts/dd.sh

echo ""
echo "---------------------------------------------------------"
echo "BLOCK driver test mode"
echo "run dd test on HVDIMM with async mode enabled, cache enabled"
echo ""
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
	async_mode=1 \
	queue_size=16 \
	use_memmap=1 \
	ramdisk=0 \
	ramdisk_start=0x100000000 \
	single_cmd_test=0 \
	cache_enabled=1
sh scripts/dd.sh

echo ""
echo "---------------------------------------------------------"
echo "BLOCK driver test mode"
echo "run dd test on HVDIMM with async mode enabled, cache disabled"
echo ""

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
	async_mode=1 \
	queue_size=16 \
	use_memmap=1 \
	ramdisk=0 \
	ramdisk_start=0x100000000 \
	single_cmd_test=0 \
	cache_enabled=0
sh scripts/dd.sh

# test char driver on mmls
echo ""
echo "---------------------------------------------------------"
echo "Char driver test mode"
echo ""
echo ""
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
	async_mode=1 \
	queue_size=16 \
	use_memmap=1 \
	ramdisk=1 \
	ramdisk_start=0x100000000 \
	single_cmd_test=0 \
	cache_enabled=0
sudo ./chardrv_regression


