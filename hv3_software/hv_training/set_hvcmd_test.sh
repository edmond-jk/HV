#!/bin/bash

# 0x10 - MMLS_RD - eMMC to DRAM
# 0x20 - MMLS_WR - DRAM to eMMC
# 0x80 - TBM_WR - DRAM to TBM
# 0x90 - TBM_RD - TBM to DRAM

if [ "$1" -eq 10 ]; then
echo 0 > /sys/kernel/debug/hvcmd_test/tag
echo 8 > /sys/kernel/debug/hvcmd_test/sector 
echo 0 > /sys/kernel/debug/hvcmd_test/LBA
echo 1 > /sys/kernel/debug/hvcmd_test/bsm_read
fi
if [ "$1" -eq 20 ]; then
echo 1 > /sys/kernel/debug/hvcmd_test/tag
echo 8 > /sys/kernel/debug/hvcmd_test/sector 
echo 16 > /sys/kernel/debug/hvcmd_test/LBA
echo 1 > /sys/kernel/debug/hvcmd_test/bsm_write
fi
if [ "$1" -eq 70 ]; then
echo 1 > /sys/kernel/debug/hvcmd_test/mmls_read
fi

