#!/bin/bash

# ----------------------------------------
# Escape Sequence - Color codes
# ----------------------------------------
# Black       0;30     Dark Gray     1;30
# Blue        0;34     Light Blue    1;34
# Green       0;32     Light Green   1;32
# Cyan        0;36     Light Cyan    1;36
# Red         0;31     Light Red     1;31
# Purple      0;35     Light Purple  1;35
# Brown       0;33     Yellow        1;33
# Light Gray  0;37     White         1;37
# ----------------------------------------
UNDERSCORE='\033[4m'
INVERSE='\033[7m'
LIGHT_RED='\033[1;31m'
LIGHT_GREEN='\033[1;32m'
LIGHT_YELLOW='\033[1;33m'
LIGHT_BLUE='\033[1;34m'
LIGHT_CYAN='\033[1;36m'
NC='\033[0m' # No Color
UP_ONE_LINE='\033[1A'
DOWN_ONE_LINE='\033[1B'
MOVE_FWD_34_COL='\033[34C'
MOVE_FWD_2_COL='\033[2C'

#echo "arg count = $#"

# fake_write buffer will be located at 4096 bytes higher than fake_read buffer
FAKE_READ_BUFFER_PA=0x678F00000

# 0x6_79F0_0000 - (4096*2)
MAX_READ_FAKE_BUFFER_PA=0x679EFE000

pass_count=0
fail_count=0
pass_file_name="pass.txt"
fail_file_name="fail.txt"

if [ $# = 1 ]; then

	max_count="$1"
#	echo "arg = $1"
	echo

	# Init buffer address
	((fake_read_buffer_pa=FAKE_READ_BUFFER_PA))
	((fake_write_buffer_pa=FAKE_READ_BUFFER_PA+0x1000))

	for ((c=1; c<=$max_count; c++))
	do

		# Convert decimal value to hex value
		s1=`echo "obase=16; $fake_read_buffer_pa" |bc`
		s2=`echo "obase=16; $fake_write_buffer_pa" |bc`

		# Do BSM_WRITE
		echo 0 > /sys/kernel/debug/hvcmd_test/tag
		echo 8 > /sys/kernel/debug/hvcmd_test/sector
		echo 0 > /sys/kernel/debug/hvcmd_test/LBA

#		echo 1 > /sys/kernel/debug/hvcmd_test/extdat
#		cat testfile_4k_0.txt > /sys/kernel/debug/hvcmd_test/4k_file_out

		echo $fake_read_buffer_pa > /sys/kernel/debug/hvcmd_test/fake_read_buff_pa
		echo 1 > /sys/kernel/debug/hvcmd_test/bsm_write
		/home/lab/work/devmem2c/devmem2h $fake_read_buffer_pa w 4096 0x123456789abcdef0  >&-	# for devel only
#		echo 1 > /sys/kernel/debug/hvcmd_test/mmls_write

		# Do BSM_READ
		echo 0 > /sys/kernel/debug/hvcmd_test/tag
		echo 8 > /sys/kernel/debug/hvcmd_test/sector
		echo 0 > /sys/kernel/debug/hvcmd_test/LBA

		echo $fake_write_buffer_pa > /sys/kernel/debug/hvcmd_test/fake_write_buff_pa
		echo 1 > /sys/kernel/debug/hvcmd_test/bsm_read
#		echo 1 > /sys/kernel/debug/hvcmd_test/mmls_read
		/home/lab/work/devmem2c/devmem2h $fake_write_buffer_pa w 4096 0x123456789abcdef4 >&-	# for devel only

		# Set the mask for 64-bit comparison
#		echo 0x00000000FFFFFFFF > /sys/kernel/debug/hvcmd_test/wr_cmp
		echo 0xFFFFFFFFFFFFFFFF > /sys/kernel/debug/hvcmd_test/wr_cmp

		# Compare fake_read buffer with fake_write buffer and get the result
		# from wr_cmp file
		result_str=$( cat /sys/kernel/debug/hvcmd_test/wr_cmp )

		# Extract the first four characters, which will be PASS or FAIL
		result=${result_str:0:4}
		error_count=${result_str:5:3}

		if [ $result = "PASS" ]; then
			echo -e "${LIGHT_GREEN}$c: ${MOVE_FWD_2_COL}${LIGHT_CYAN}$s1 - $s2${NC}"
			echo -e "${UP_ONE_LINE}${MOVE_FWD_34_COL}${LIGHT_GREEN}pass $error_count${NC}"
			((pass_count++))
		elif [ $result = "FAIL" ]; then
			echo -e "${LIGHT_GREEN}$c: ${MOVE_FWD_2_COL}${LIGHT_RED}$s1 - $s2${NC}"
			echo -e "${UNDERSCORE}${UP_ONE_LINE}${MOVE_FWD_34_COL}${LIGHT_RED}fail $error_count${NC}"
			((fail_count++))
		else
			echo -e "${LIGHT_GREEN}$c: ${LIGHT_RED}unexpected error found in /sys/kernel/debug/hvcmd_test/wr_cmp${NC}"
		fi

		# Get the next fake_read/write buffer address. Advance by 4KB
		((fake_read_buffer_pa+=0x1000))
		((fake_write_buffer_pa+=0x1000))

		# If we reached the max address, let's go back to the beginning
		if [[ $fake_read_buffer_pa -gt MAX_READ_FAKE_BUFFER_PA ]]; then
			((fake_read_buffer_pa=FAKE_READ_BUFFER_PA))
			((fake_write_buffer_pa=FAKE_READ_BUFFER_PA+0x1000))
		fi

	done

else

	echo "This script requires one argument: loop count"

fi

echo
echo -e "${LIGHT_BLUE} # of pass = $pass_count${NC}"
echo -e "${LIGHT_BLUE} # of fail = $fail_count${NC}"
echo -e "${LIGHT_YELLOW}total runs = $((pass_count+fail_count))${NC}"
echo
