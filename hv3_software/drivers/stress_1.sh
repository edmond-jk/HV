#!/bin/bash
# -----------------------------------------------------------------------------
# stress_1.sh (stress test)
# arg #1 - repeat count in decimal
# arg #2 - address increment value in decimal
#			(TBM block size equivalent to 4KB used by the host)
#
# 01/12/2017	rev 1.0		- initial rev
#
# This script performs the following
# 1. BSM_WRITE
# 2. BSM_READ
# 3. compare fake-write buffer with fake-read buffer
#
# for repeat count
#    for each test data pattern file from the list
#       for each 4KB block in TBM (128MB: 0 ~ 0x7FF_F000)
#          for each 4KB block of fake-read buffer (0x678F00000 ~ 0x679EFE000)
#
# -----------------------------------------------------------------------------

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


# Size of a page - 4KB
DEFAULT_PAGE_SIZE=0x1000

# Size of TBM we want to test
#DEFAULT_TBM_SIZE_TO_TEST=0x8000000	#128MB
DEFAULT_TBM_SIZE_TO_TEST=0x1000000	#16MB

# LBA increment value equivalent to 0x1000 (4KB) for TBM
DEFAULT_LBA_INC_VALUE=0x8000

# Host increments by 0x8000 (32KB) = FPGA increments by 0x1000 (4KB)
# TBM addr  = 0 ~ 0x800_0000 (4KB base = 0 ~ 0x7FF_0000)
# Host addr = 0 ~ 0x4000_8000 (32KB base = 0 ~ 0x3FFF_8000)
#   -. TBM size to test / 4096 = page count
#   -. page count * 0x8000 = size of fake buffer in host
#   -. Last base addr of fake read buffer = size of fake buffer in host - 0x8000
MAX_TBM_BASE_ADDR=DEFAULT_TBM_SIZE_TO_TEST-DEFAULT_PAGE_SIZE
#MAX_HOST_TBM_BASE_ADDR=((DEFAULT_TBM_SIZE_TO_TEST/DEFAULT_PAGE_SIZE)*DEFAULT_LBA_INC_VALUE)-DEFAULT_LBA_INC_VALUE
MAX_HOST_TBM_BASE_ADDR=0x10000			#----------devel only----------

# fake_write buffer will be located at 4096 bytes higher than fake_read buffer
FAKE_READ_BUFFER_PA=0x678F00000

# 0x6_79F0_0000 - (4096*2)
#MAX_READ_FAKE_BUFFER_PA=0x679EFE000
MAX_READ_FAKE_BUFFER_PA=0x678F03000		#----------devel only----------


# Name of text file that has a list of data pattern files
data_file_list="data_file_list.txt"
pass_count=0
fail_count=0
lba_inc_value=DEFAULT_LBA_INC_VALUE

usage ()
{
	echo -e "${LIGHT_GREEN}"
	echo -e "Usage: $0     [repeat_count lba_inc_value]"
	echo -e "Options:"
	echo -e "  repeat_count   test repeat count. The default value is 1 if not given."
	echo -e "                 It should be 1 or higher if specified."
	echo -e "  lba_inc_value  TBM block size equivalent to 4KB used by the host"
	echo
	echo -e " This script performs the following"
	echo -e "   1. BSM_WRITE"
	echo -e "   2. BSM_READ"
	echo -e "   3. compare fake-write buffer with fake-read buffer"
	echo
	echo -e "  for repeat count"
	echo -e "    for each test data pattern file from the list"
	echo -e "      for each 4KB block in TBM (128MB: 0 ~ 0x7FF_F000)"
	echo -e "        for each 4KB block of fake-read buffer (0x678F00000 ~ 0x679EFE000)"
	echo
	echo -e " This script requires a text file, called data_file_list.txt,"
	echo -e " that has a list of data pattern files. If the list file does"
	echo -e " not exist it will not run."
	echo -e "${NC}"
	exit 1
}


# The file that has a list of data files is required for this script
if [ ! -f "./$data_file_list" ]; then
	echo
	echo -e "${LIGHT_RED}$data_file_list is not found.${NC}"
	usage
fi


# Set default repeat count to 1 if it is not given
if [ $# = 0 ]; then
	repeat_count=1
	lba_inc_value=DEFAULT_LBA_INC_VALUE
elif [ $# = 1 ] || [ $# = 2 ]; then
	if [ $1 -gt 0 ]; then
		repeat_count="$1"
		lba_inc_value=DEFAULT_LBA_INC_VALUE
	elif [ $1 -lt 1 ]; then
		usage
	fi
else
	usage
fi

# Take 2nd argument
if [ $# = 2 ]; then
	if [ $2 -gt 0 ]; then
		lba_inc_value=$2
	else
		usage
	fi
elif [ $# -gt 2 ]; then
	usage
fi


if [ $repeat_count -gt 0 ]; then

	# Set max TBM base address
	((max_lba=MAX_HOST_TBM_BASE_ADDR))
	max_lba_hex=`echo "obase=16; $max_lba" | bc`

	echo

	# Repeat count for the stress test. This comes from the user.
	for ((c=1; c<=$repeat_count; c++))
	do

		# For each data pattern file, which comes from another file.
		while read filename; do

			if [ ! -f "./$filename" ]; then
				echo -e "${LIGHT_RED}$filename is not found.${NC}"
				echo
				continue	# go back and get the next file from the list
			fi

			# ta = TMB Address
			# For each 4KB in TBM - host needs to increment the address by 32KB
			# (=0x8000) to make FPGA increment by 4KB (=0x1000). This address will
			# be xferred to FPGA as LBA.
			for ((ta=0; ta<=$max_lba; ta=$ta+$lba_inc_value))
			do

				# fa = Fake buffer Address
				# For each fake buffer address
				for ((fa=FAKE_READ_BUFFER_PA; fa<=MAX_READ_FAKE_BUFFER_PA; fa=$fa+0x1000))
				do
					# Get a new set of fake-read/write buffer address
					# Fake-read buffer will increment by 4KB and fake-write buffer will
					# always be 4KB higher than the fake-read buffer.
					((fake_read_buffer_pa=$fa))
					((fake_write_buffer_pa=$fake_read_buffer_pa+0x1000))

#
# the body of the actual test comes here ........
#
					# >>-- Do BSM_WRITE --<<

					# FPGA expects sector count to be multiple of 8
#--					echo 0 > /sys/kernel/debug/hvcmd_test/tag
#--					echo 8 > /sys/kernel/debug/hvcmd_test/sector
#--					echo $ta > /sys/kernel/debug/hvcmd_test/LBA

					# Use test data from external file
#--					echo 1 > /sys/kernel/debug/hvcmd_test/extdat
#--					cat $filename > /sys/kernel/debug/hvcmd_test/4k_file_out

#--					echo $fake_read_buffer_pa > /sys/kernel/debug/hvcmd_test/fake_read_buff_pa
#--					echo 1 > /sys/kernel/debug/hvcmd_test/bsm_write
#					/home/lab/work/devmem2c/devmem2h $fake_read_buffer_pa w 4096 0x123456789abcdef0  >&-	#----------devel only----------
#--					echo 1 > /sys/kernel/debug/hvcmd_test/mmls_write

					# >>-- Do BSM_READ --<<

					# We want to read the data from the same TBM address ($ta)
#--					echo 0 > /sys/kernel/debug/hvcmd_test/tag
#--					echo 8 > /sys/kernel/debug/hvcmd_test/sector
#--					echo $ta > /sys/kernel/debug/hvcmd_test/LBA

#--					echo $fake_write_buffer_pa > /sys/kernel/debug/hvcmd_test/fake_write_buff_pa
#--					echo 1 > /sys/kernel/debug/hvcmd_test/bsm_read
#--					echo 1 > /sys/kernel/debug/hvcmd_test/mmls_read
#					/home/lab/work/devmem2c/devmem2h $fake_write_buffer_pa w 4096 0x123456789abcdef1 >&-	#----------devel only----------

					# Set mask for 64-bit comparison (for now testing the master FPGA only)
#--					echo 0x00000000FFFFFFFF > /sys/kernel/debug/hvcmd_test/wr_cmp

					# Compare fake_read buffer with fake_write buffer and get
					# the result from wr_cmp file
#--					result_str=$( cat /sys/kernel/debug/hvcmd_test/wr_cmp )

					# Extract the first four characters, which will be PASS or FAIL
#--					result=${result_str:0:4}
#--					error_count=${result_str:5:3}

					# Convert decimal value to hex value for display purpose
					s1=`echo "obase=16; $fake_read_buffer_pa" | bc`
					s2=`echo "obase=16; $fake_write_buffer_pa" | bc`
					ta_hex=`echo "obase=16; $ta" | bc`	#debug
					echo -e "${LIGHT_GREEN}$c:${NC} $filename - lba=0x$ta_hex - ${MOVE_FWD_2_COL}${LIGHT_CYAN}$s1 - $s2${NC}"	#----------devel only----------

					# Show the result of the fake rd/wrt buffer comparison
#--					if [ $result = "PASS" ]; then
##						echo -e "${LIGHT_GREEN}$c: ${MOVE_FWD_2_COL}${LIGHT_CYAN}$s1 - $s2${NC}"
##						echo -e "${UP_ONE_LINE}${MOVE_FWD_34_COL}${LIGHT_GREEN}pass $error_count${NC}"
#--						((pass_count++))
#--					elif [ $result = "FAIL" ]; then
#--						echo -e "${LIGHT_GREEN}$c: ${MOVE_FWD_2_COL}${LIGHT_RED}$s1 - $s2${NC}"
#--						echo -e "${UNDERSCORE}${UP_ONE_LINE}${MOVE_FWD_34_COL}${LIGHT_RED}fail $error_count${NC} >>> $filename - lba=0x$ta_hex"
#--						((fail_count++))
#--					else
#--						echo -e "${LIGHT_GREEN}$c: ${LIGHT_RED}unexpected error found in /sys/kernel/debug/hvcmd_test/wr_cmp${NC}"
#--						exit 2
#--					fi

				done	# end of for ((fa=FAKE_READ_BUFFER_PA;...
				echo

			done	# end of for ((ta=0;...
			echo

		done < $data_file_list
		echo

	done	#for ((c=1;...

else

	usage

fi
