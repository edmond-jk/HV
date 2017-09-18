#!/bin/bash
# -----------------------------------------------------------------------------
# hdtest.sh
#
# -----------------------------------------------------------------------------
SCRIPT_VERSION="05-25-2017"

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
# Escape sequence codes for echo
UNDERSCORE='\033[4m'
INVERSE='\033[7m'
BOLD_RED='\033[1;31m'
LIGHT_RED='\033[0;31m'
LIGHT_GREEN='\033[1;32m'
LIGHT_YELLOW='\033[1;33m'
LIGHT_BLUE='\033[1;34m'
LIGHT_CYAN='\033[1;36m'
NC='\033[0m'				# No Color
UP_ONE_LINE='\033[1A'
DOWN_ONE_LINE='\033[1B'
MOVE_FWD_34_COL='\033[34C'
MOVE_FWD_2_COL='\033[2C'
CLEAR_LINE='\033[2K'

# Escape sequence codes for printf
P_UNDERSCORE=$'\e[4m'
P_INVERSE=$'\e[7m'
P_BOLD_RED=$'\e[1;31m'
P_LIGHT_RED=$'\e[0;31m'
P_LIGHT_GREEN=$'\e[1;32m'
P_LIGHT_YELLOW=$'\e[1;33m'
P_LIGHT_BLUE=$'\e[1;34m'
P_LIGHT_CYAN=$'\e[1;36m'
P_LIGHT_GRAY=$'\e[0;30m'
P_BOLD_GRAY=$'\e[1;30m'
P_NC=$'\e[0m'				# No Color
P_UP_ONE_LINE=$'\e[1A'
P_DOWN_ONE_LINE=$'\e[1B'
P_MOVE_FWD_34_COL=$'\e[34C'
P_MOVE_FWD_2_COL=$'\e[2C'
P_CLEAR_LINE=$'\e[2K'

# Equate definitions
PMEM="0"
SW_VERSION=0405			# HybriDIMM training SW version number
DIMM_ID=3
MASTER_FPGA_ONLY="0"
MAX_FAIL_COUNT=100
#HH0515-begin
# Defaults for emmc options
((addr_inc=512))
#HH0515-end
#HH0519-begin
fl_flag=0				# indicates the user provided the first LBA
((first_lba=0))			# start test from LBA 0 by default
#HH0519-end
#HH0523-begin
ll_flag=0				# indicates the user provided the last LBA
((max_lba=0))			# LBA 0 by default
ai_flag=0

aii_flag=0
((ai_inc=0))
bs_flag=0
((bpc=1))

((page_count=0))
s_flag=0
#HH0523-end

if [[ "$MASTER_FPGA_ONLY" = "0" ]];then
	WR_CMP_64_BITS_MASK=0xFFFFFFFFFFFFFFFF
else
	WR_CMP_64_BITS_MASK=0x00000000FFFFFFFF
fi

#FAKE_READ_BUFFER_PA=0x678F00000
FAKE_READ_BUFFER_PA=0x639F00000
MAX_BUFF_SIZE=0x20000000	# 512MB

#
# >> DEBUG_FEAT_BSM_WR & DEBUG_FEAT_BSM_RD <<
#
# [21]    - BSM_WRT/RD Pop Count Enable (only for BSM_RD)
# [20]    - BSM_WRT/RD Do Dummy 64-Byte Read Enable
# [19]    - BSM_WRT/RD Send Dummy Command Enable
# [18]    - BSM_WRT/RD Skip Termination Enable
# [17]    - BSM_WRT/RD Skip G.W.S./G.R.S. Check
# [16]    - BSM_WRT/RD Skip Query Enable
#
# [15]    - BSM_WRT/RD Query Command Retry Enable
# [14:12] - BSM_WRT/RD Query Command Max Retry Count
#
# [11]    - BSM_WRT/RD Fake-Rd Retry Enable
# [10:8]  - BSM_WRT/RD Fake-Rd Max Retry Count
#
# [7]     - BSM_WRT/RD Data Checksum Enable
# [6:4]   - BSM_WRT/RD Data Checksum Max Retry Count
#
# [3]     - BSM_WRT/RD Cmd Checksum Enable
# [2:0]   - BSM_WRT/RD Cmd Checksum Max Retry Count
	
DEBUG_FEAT_BSM_WR=0x2f000
DEBUG_FEAT_BSM_RD=0x2f0f0

LA_ECC_TABLE_0_PA=0x67ff20000	# BG=1, BA=3, X=0xFFF8, Y=0
LA_ECC_TABLE_1_PA=0x67ff20040	# BG=0, BA=3, X=0xFFF8, Y=0
LA_ECC_TABLE_2_PA=0x67ff22000	# BG=1, BA=3, X=0xFFF8, Y=200

send_LA_trigger_dummy_rd="NO"


Check_TBM_result="1"

df_bcom_toggle_enable="1"
df_bcom_ctrl_method="1"
df_fpga_reset_ctrl_method="1"

df_bsm_wr_skip_termination_enable="1"	# if skipped, 2us delay @hv_fake_operation() default=0
df_bsm_rd_skip_termination_enable="0"	# if skipped, 2us delay @hv_fake_operation() default=1
df_bsm_rd_popcnt_enable="1"				# bit[21]
send_dummy_command_enable="0"
send_dummy_read_bsm_wr_LA="0"	# LA_ECC_TABLE_0_PA
send_dummy_read_bsm_rd_LA="0"	# LA_ECC_TABLE_2_PA
send_fpga_reset_reg_0x68="1"

if [[ "$df_bsm_wr_skip_termination_enable" = "1" ]];then
	((DEBUG_FEAT_BSM_WR|=0x40000))	
fi

if [[ "$df_bsm_rd_skip_termination_enable" = "1" ]];then
	((DEBUG_FEAT_BSM_RD|=0x40000))	
fi

if [[ "$df_bsm_rd_popcnt_enable" = "1" ]];then
	((DEBUG_FEAT_BSM_RD|=0x200000))	
fi

if [[ "$send_dummy_command_enable" = "1" ]];then
	((DEBUG_FEAT_BSM_WR|=0x80000))	
fi

if [[ "$send_dummy_read_bsm_wr_LA" = "1" ]];then
	((DEBUG_FEAT_BSM_WR|=0x100000))
fi

if [[ "$send_dummy_read_bsm_rd_LA" = "1" ]];then
	((DEBUG_FEAT_BSM_RD|=0x100000))
fi

pass_count=0
fail_count=0
loop_pass_count=0
loop_fail_count=0
Max_error_to_display=10
testfile_4k=""
max_retry_count=5

user_defined_u_delay=1  # enable_bcom()	& disable_bcom():  100ns - Enable - tRFC -
                        # read_status()
bsm_wr_qc_n_delay=1
bsm_rd_qc_n_delay=1000

max_count=1
do_fr_test=1
do_fw_test=1
#HH0424-begin
do_load_frb_flag=0		# test data will be loaded in fake-read buffer only if set to 1
#HH0424-end
#HH0502-begin
page_save_flag=0		# test data will be saved in files if set to 1
#HH0502-end
FAILSTOP="NO"
TEST_4KB_PAGE_MODE="0"		#SJ0403

LA_trigger_LA_ECC_TABLE_0_PA ()
{
	/home/lab/work/hv_training/test$SW_VERSION -d $DIMM_ID -r32 $LA_ECC_TABLE_0_PA 64 >&-	
}		

reset_fpga_reg_0x68 ()
{
#	/home/lab/work/hv_training/test$SW_VERSION -d $DIMM_ID -f 0x68 0xe0 >&-
#	sleep 0.01
#	/home/lab/work/hv_training/test$SW_VERSION -d $DIMM_ID -f 0x68 0 >&-
#	sleep 0.01		
	echo 1 > /sys/kernel/debug/$DEBUG_FS/hv_reset
}		

reset_bcom_control ()
{
	/home/lab/work/hv_training/test$SW_VERSION -d $DIMM_ID -5 0x6 0x80 >&-
	sleep 0.01
	/home/lab/work/hv_training/test$SW_VERSION -d $DIMM_ID -B >&-
	sleep 0.01		
}		

do_bsm_write ()
{
	# Send command to the command driver

	echo $fake_read_buffer_pa > /sys/kernel/debug/$DEBUG_FS/fake_read_buff_pa
#HH0522	echo $ms > /sys/kernel/debug/$DEBUG_FS/LBA
#HH0522-begin
	echo $ta > /sys/kernel/debug/$DEBUG_FS/LBA
#HH0522-end
	echo 1 > /sys/kernel/debug/$DEBUG_FS/bsm_write
}

do_bsm_read ()
{
	# Send commands to the command driver
	echo $fake_read_buffer_pa > /sys/kernel/debug/$DEBUG_FS/fake_read_buff_pa
	echo $fake_write_buffer_pa > /sys/kernel/debug/$DEBUG_FS/fake_write_buff_pa
#HH0522	echo $ms > /sys/kernel/debug/$DEBUG_FS/LBA
#HH0522-begin
	echo $ta > /sys/kernel/debug/$DEBUG_FS/LBA
#HH0522-end
	echo 1 > /sys/kernel/debug/$DEBUG_FS/bsm_read
#	echo 1 > /sys/kernel/debug/$DEBUG_FS/terminate_cmd

}

#HH0424-begin
do_load_frb ()
{
	# Tell the driver the fake-read buffer physical address and LBA
	echo $fake_read_buffer_pa > /sys/kernel/debug/$DEBUG_FS/fake_read_buff_pa
#HH0522	echo $ms > /sys/kernel/debug/$DEBUG_FS/LBA
#HH0522-begin
	echo $ta > /sys/kernel/debug/$DEBUG_FS/LBA
#HH0522-end

	# Load test data in the fake-read buffer.
	# In case of ext data, the driver will load the test data selected outside
	# of this function. If internal data was selected before calling this
	# function, this operation will have the driver generate the test data
	# before loading it to the fake-read buffer.
	echo 1 > /sys/kernel/debug/$DEBUG_FS/load_frb
}
#HH0424-end

get_tbm_result ()
{
  if [[ "$Check_TBM_result" = "0" ]];then
	printf "${P_BOLD_GRAY}Skip${P_NC}\n"
  else

	if [[ "${pat[$i-1]}" = "i1" ]];then
		echo -en 'a' > /dev/ttyUSB0	# menu a) TBM - address pattern verification
		echo -en '80000000' > /dev/ttyUSB0
		sleep 0.1
	else
		echo -en '9 80000000 1\xd' > /dev/ttyUSB0
	fi
		sleep 0.1
	if [[ "${pat[$i-1]}" = "5" ]];then
		echo -en '55555555' > /dev/ttyUSB0
	elif [[ "${pat[$i-1]}" = "a" ]];then
		echo -en 'aaaaaaaa' > /dev/ttyUSB0	
	elif [[ "${pat[$i-1]}" = "0" ]];then
		echo -en '00000000' > /dev/ttyUSB0	
	elif [[ "${pat[$i-1]}" = "f" ]];then
		echo -en 'ffffffff' > /dev/ttyUSB0	
	fi
	sleep 0.1
	
	echo -en "$tbm\xd" > /dev/ttyUSB0
	echo -en "$Max_error_to_display\xd" > /dev/ttyUSB0
		
	while read  -r consoleOut < /dev/ttyUSB0;
	do
       	if [[ $consoleOut == *Passed!* ]]; then
           	printf "${P_LIGHT_GREEN}PASS${P_NC}\n"
           	break
       	fi
       	if [[ $consoleOut == *Failed* ]]; then
            printf "\n${P_LIGHT_RED}   $consoleOut${P_NC}"
       	fi
       	if [[ $consoleOut == *words* ]]; then
		echo -e "\n"
           	break
     	fi		
	done
  fi
}

get_tbm_result_4KB_page ()
{
	if [[ "${pat[$i-1]}" = "i1" ]];then
		echo -en 'a' > /dev/ttyUSB0	# menu a) TBM - address pattern verification
		echo -en $tbm_s > /dev/ttyUSB0
		sleep 0.1
	else
	    echo -en '9' > /dev/ttyUSB0 	# menu 9) TBM -
    	echo -en $tbm_s > /dev/ttyUSB0
		echo -en '1\xd' > /dev/ttyUSB0
	fi
	sleep 0.1
	if [[ "${pat[$i-1]}" = "5" ]];then
		echo -en '55555555' > /dev/ttyUSB0
	elif [[ "${pat[$i-1]}" = "a" ]];then
		echo -en 'aaaaaaaa' > /dev/ttyUSB0	
	elif [[ "${pat[$i-1]}" = "0" ]];then
		echo -en '00000000' > /dev/ttyUSB0	
	elif [[ "${pat[$i-1]}" = "f" ]];then
		echo -en 'ffffffff' > /dev/ttyUSB0	
	fi
	sleep 0.1
	
	echo -en "fff\xd" > /dev/ttyUSB0
	echo -en "$Max_error_to_display\xd" > /dev/ttyUSB0
		
	while read  -r consoleOut < /dev/ttyUSB0;
	do
       	if [[ $consoleOut == *Passed!* ]]; then
           	printf "${P_LIGHT_GREEN}TBM-PASS${P_NC}\n"
           	break
       	fi
       	if [[ $consoleOut == *Failed* ]]; then
           	printf "\n${P_LIGHT_RED}   $consoleOut${P_NC}"
       	fi
       	if [[ $consoleOut == *words* ]]; then
		echo -e "\n"
           	break
     	fi		
	done		
}

print_failed_wr_cmp ()
{
	cp /sys/kernel/debug/$DEBUG_FS/wr_cmp fail2.txt
	((max_fail_=4))
	((line_index=0))
	((qw_offset=0))
	while read line;
	do
		if [ ${#line} -eq 135 ]; then

			((column_offset=0))
			for ((qw_idx=0; qw_idx<8; qw_idx=$qw_idx+1))
			do
				qword=${line:$column_offset:16}
				((column_offset=$column_offset+17))

				if [ $qword != 0000000000000000 ] && [ $max_fail_ -gt 0 ]; then
					printf "%s%.3d:%s %s" $P_LIGHT_RED $qw_offset $qword $P_NC
					((max_fail_--))
				fi

				((qw_offset++))
			done
		fi

		((line_index++))
	done < ./fail2.txt
	printf "\n\n"
}

usage ()
{
#HH0424	echo "Usage: fr-fw2.sh"
	echo "Usage: $0"	##HH0424
    echo "   -p <5|i0|i1|i2> [-p1 <>] [-p2 <>] -l <loop> -s <size> <k|m|g>"
#HH0502-begin
	echo "      [-ps]                - Same as -p except that page data is saved in a file."
#HH0502-end
	echo "      [-bc 0|1]            - Bcom control Method (0=MMIO, 1=I2C)"
	echo "      [-rc 0|1]            - Reset control Method (0=MMIO, 1=I2C)"
	echo "      [-fs]                - Fail Stop mode"
#HH0515-begin
	echo "      [-fc <count>]        - Max fail count"
	echo "      [-ct 0|1]            - Check TBM result Off/On"
	echo "      [-pmem]              - Use pmem driver debug FD files"
#HH0515-end
	echo "      [-dly <user>]"
	echo "      [-dbg <bsm-wr> <bsm-rd>]"
	echo "      [-fr|fw]             - FakeRD or FakeWR only"
#HH0424-begin
	echo "      [-lfrb]              - Load data in fake-read buffer only. It will"
	echo "                             not send data to FPGA."
#HH0424-end
	echo "      [-pp]                - Per Page BSM-WR & BSM-RD mode"
	echo "      [-lp]                - do dummy read for LA trigger"
#HH0523-begin
	echo "      [-emmc]              - Run test against eMMC instead of TBM"
	echo "      [-fl <size> <k|m|g>] - Specifies the first LBA to start the test from"
	echo "      [-ll <size> <k|m|g>] - Specifies the max LBA, which is exclusive"
	echo "      [-ai <size> <k|m|g>] - Specifies LBA increment value to the next segment"
	echo "      [-aii <size> <k|m>]  - Specifies LBA increment value within each segment"
	echo "      [-bs <size> <k|m>]   - Specifies block size in each segment to test"
#HH0523-end
	echo
	exit 1
}

function ProgressBar {
# Process data
    let _progress=(${1}*100/${2}*100)/100
    let _done=(${_progress}*4)/10
    let _left=40-$_done
# Build progressbar string lengths
    _fill=$(printf "%${_done}s")
    _empty=$(printf "%${_left}s")

# 1.2.1.1 Progress : [########################################] 100%
printf "\r %6d pages: [${_fill// /#}${_empty// /-}] ${_progress}%%" $2

}

run_bsm_Write ()
{
	fr_ts0=$(date +%s%N | cut -b1-13)
	printf "\n"

	if [ "$send_fpga_reset_reg_0x68" = "1" ]; then
		reset_fpga_reg_0x68
	fi
	
	page=1;	pass_count=0; fail_count=0

	#printf "   Page  Src Buffer     TBM Address(LBA)\n\n"	
#HH0424	for ((ms=0; ms<$max_lba; ms=$ms+512))		#for_0x200
#HH0424-begin
#HH0515	for ((ms=0; ms<$max_lba; ms=$ms+8))			#for emmc LBA
#HH05150-begin
#HH0519	for ((ms=0; ms<$max_lba; ms=$ms+$addr_inc))
#HH0519-begin
	((loop_index=0))
	for ((ms=$first_lba; ms<$((addr_inc*page_count)); ms=$ms+$addr_inc))
#HH0519-end
#HH0515-end
#HH0424-end
	do

#HH0522-begin
		((temp=$ms+$ai_inc*$loop_index))
		for ((bc=0; bc<$bpc; bc++))
		do
			((ta=$temp+$bc*8))
#HH0529-end

#HH0523		((tbm_a=0x80000000+ms*8))
#HH0523-begin
		((tbm_a=0x80000000+ta*8))
#HH0523-end

#HH0424		do_bsm_write
#HH0424-begin
		# Check if we want to load data in fake-read buffer only
		# The test data must exist in data files.
		if [ "$do_load_frb_flag" = "1" ]; then
#HH0502-begin
			# Check if we want to save test data in files
			if [ "$page_save_flag" = "1" ]; then
				# Create a data file name with the current page number
				printf -v page_data_file "page_%08d.txt" $page
				# The current data file is expected to be present and the data
				# in the file will be provided to the driver.
				if [ -e $page_data_file ]; then
					echo 1 > /sys/kernel/debug/$DEBUG_FS/extdat
					cat $page_data_file > /sys/kernel/debug/$DEBUG_FS/4k_file_out
				else
					printf "${P_LIGHT_RED}run_bsm_Write(): %s not found${P_NC}\n" $page_data_file
					exit 1
				fi
			fi
#HH0502-end
			# Load data in fake-read buffer only
			do_load_frb
		else
			# Normal BSM_WRITE operation is performed
			do_bsm_write
#HH0502-begin
			# Need to save test data in files
			if [ "$page_save_flag" = "1" ]; then
				# Create a file name with the current page number
				printf -v page_data_file "page_%08d.txt" $page
#				printf "%s\n" $page_data_file
				# Get test data just written to FPGA back & save it in file
				cat /sys/kernel/debug/$DEBUG_FS/load_frb > $page_data_file
			fi
#HH0502-end

		fi
#HH0424-end

#HH0522		ProgressBar ${page} ${page_count} 
#HH0522-begin
		((ttt=$page_count*$bpc))
		ProgressBar ${page} ${ttt} 
#HH0522-end
			
		((page++))
		((fake_read_buffer_pa+=0x1000))

#HH0522-begin
		done
		((loop_index++))
#HH0529-end

	done
		
	printf "\n   Finished BSM Write(FakeRD) & Checking TBM Result   ... " 

	if [[ ("${pat[$i-1]}" = "5") || ("${pat[$i-1]}" = "a") || ("${pat[$i-1]}" = "0") || \
		  ("${pat[$i-1]}" = "f") || ("${pat[$i-1]}" = "i1") ]];then
		ts0=$(date +%s%N | cut -b1-13)
		get_tbm_result
		ts1=$(date +%s%N | cut -b1-13)
		laptime_tbm=$((ts1-ts0))
		echo "   TBM Checking Time: $laptime_tbm ms"
	else
		printf "${P_BOLD_GRAY}Skip${P_NC}\n"
	fi
	fr_ts1=$(date +%s%N | cut -b1-13)
}		

run_bsm_Read ()
{
	fw_ts0=$(date +%s%N | cut -b1-13)

	if [ "$send_fpga_reset_reg_0x68" = "1" ]; then
		reset_fpga_reg_0x68
	fi
	# Init buffer address
	((fake_read_buffer_pa=FAKE_READ_BUFFER_PA))
	((fake_write_buffer_pa=FAKE_READ_BUFFER_PA+MAX_BUFF_SIZE))
	page=1;	pass_count=0; fail_count=0

	printf "\n   Page  Buffer(Dst)    TBM Address(LBA)     Buffer(Src)  RESULT\n\n"
#HH0424	for ((ms=0; ms<$max_lba; ms=$ms+512))		#for_0x200
#HH0424-begin
#HH0515	for ((ms=0; ms<$max_lba; ms=$ms+8))			#for emmc LBA
#HH0515-begin
#HH0519	for ((ms=0; ms<$max_lba; ms=$ms+$addr_inc))
#HH0519-begin
	((loop_index=0))
	for ((ms=$first_lba; ms<$((addr_inc*page_count)); ms=$ms+$addr_inc))
#HH0519-end
#HH0515-end
#HH0424-end
	do

#HH0522-begin
		((temp=$ms+$ai_inc*$loop_index))
		for ((bc=0; bc<$bpc; bc++))
		do
			((ta=$temp+$bc*8))
#HH0529-end

	#	if [ $((page%4096)) -eq 1 ]; then
	#		reset_fpga_reg_0x68	
	#	fi
#HH0523		((tbm_a=0x80000000+ms*8))
#HH0523-begin
		((tbm_a=0x80000000+ta*8))
#HH0523-end

		do_bsm_read

		result_str=$( cat /sys/kernel/debug/$DEBUG_FS/wr_cmp )
		result=${result_str:0:4}
		error_count=${result_str:5:3}

	if [ $result = "PASS" ]; then
#HH0523		printf "%s %6d  0x%08x <- 0x%08x(%08x) 0x%08x  %s%s %s\n" \
#HH0523				$P_UP_ONE_LINE $page $fake_write_buffer_pa $tbm_a $ms \
#HH0523				$fake_read_buffer_pa $P_LIGHT_GREEN $result $P_NC
#HH0523-begin
		printf "%s %6d  0x%08x <- 0x%08x(%08x) 0x%08x  %s%s %s\n" \
				$P_UP_ONE_LINE $page $fake_write_buffer_pa $tbm_a $ta \
				$fake_read_buffer_pa $P_LIGHT_GREEN $result $P_NC
#HH0523-end
		((pass_count++))
	elif [ $result = "FAIL" ]; then
#HH0523		printf "%s%s %6d  0x%08x <- 0x%08x(%08x) 0x%08x  #%s   %s" \
#HH0523				$P_UP_ONE_LINE $P_LIGHT_RED $page $fake_write_buffer_pa $tbm_a $ms \
#HH0523				$fake_read_buffer_pa $error_count $P_NC
#HH0523-begin
		printf "%s%s %6d  0x%08x <- 0x%08x(%08x) 0x%08x  #%s   %s" \
				$P_UP_ONE_LINE $P_LIGHT_RED $page $fake_write_buffer_pa $tbm_a $ta \
				$fake_read_buffer_pa $error_count $P_NC
#HH0523-end
		print_failed_wr_cmp 
		((fail_count++))
		if [ "$FAILSTOP" = "YES" ] && [ $fail_count = $MAX_FAIL_COUNT ];then
			if [ $send_LA_trigger_dummy_rd = "YES" ]; then	
				LA_trigger_LA_ECC_TABLE_0_PA
			fi	
#HH0424			exit 1
#HH0424-begin
			exit 2
#HH0424-end
		fi

	fi
		((page++))
		((fake_write_buffer_pa+=0x1000))
		((fake_read_buffer_pa+=0x1000))

#HH0522-begin
		done
		((loop_index++))
#HH0529-end

	done

	if [ $fail_count -eq 0 ]; then
		((loop_pass_count++))
	else
		((loop_fail_count++))
	fi

	fw_ts1=$(date +%s%N | cut -b1-13)
}

run_bsm_Write_Read_per_Page ()
{
	if [ "$send_fpga_reset_reg_0x68" = "1" ]; then
		reset_fpga_reg_0x68
	fi
	printf "   Page  Buffer(Src)    TBM Address(LBA)        Buffer(Dst)\n\n"

#HH0522	for ((page=1; page<=$page_count; page++))		#for_0x200
#HH0522	do
#HH0522-begin
#HH0523	for ((page=1; page<=$((page_count*bpc)); page++))
#HH0523	do
#HH0522-end
#HH0523-begin
	((loop_index=0))
	((page=1))
	for ((ms=$first_lba; ms<$((addr_inc*page_count)); ms=$ms+$addr_inc))
	do

		((temp=$ms+$ai_inc*$loop_index))
		for ((bc=0; bc<$bpc; bc++))
		do
			((ta=$temp+$bc*8))
#HH0523-end

#HH0523		((tbm_a=0x80000000+ms*8))
#HH0523-begin
		((tbm_a=0x80000000+ta*8))
#HH0523-end
		tbm_s=`echo "obase=16; $tbm_a" | bc` 
				
#HH0424		do_bsm_write
#HH0424-begin
		# Check if we want to load data in fake-read buffer only
		# The test data must exist in data files.
		if [ "$do_load_frb_flag" = "1" ]; then
#HH0502-begin
			# Check if we want to save test data in files
			if [ "$page_save_flag" = "1" ]; then
				# Create a data file name with the current page number
				printf -v page_data_file "page_%08d.txt" $page
				# The current data file is expected to be present and the data
				# in the file will be provided to the driver.
				if [ -e $page_data_file ]; then
					echo 1 > /sys/kernel/debug/$DEBUG_FS/extdat
					cat $page_data_file > /sys/kernel/debug/$DEBUG_FS/4k_file_out
				else
					printf "${P_LIGHT_RED}run_bsm_Write_Read_per_Page(): %s not found${P_NC}\n" $page_data_file
					exit 1
				fi
			fi
#HH0502-end
			# Load data in fake-read buffer only
			do_load_frb
		else
			# Normal BSM_WRITE operation is performed
			do_bsm_write
#HH0502-begin
			# Need to save test data in files
			if [ "$page_save_flag" = "1" ]; then
				# Create a file name with the current page number
				printf -v page_data_file "page_%08d.txt" $page
#				printf "%s\n" $page_data_file
				# Get test data just written to FPGA back & save it in file
				cat /sys/kernel/debug/$DEBUG_FS/load_frb > $page_data_file
			fi
#HH0502-end
		fi
#HH0424-end

		if [[ "$Check_TBM_result" = "1" ]];then
			if [[ ("${pat[$i-1]}" = "5") || ("${pat[$i-1]}" = "a") || ("${pat[$i-1]}" = "0") || \
		  		("${pat[$i-1]}" = "f") || ("${pat[$i-1]}" = "i1") ]];then
#HH0523				printf "$P_UP_ONE_LINE %6d  0x%08x -> 0x%08x(%08x)    $P_NC" \
#HH0523					      $page $fake_read_buffer_pa $tbm_a $ms
#HH0523-begin
				printf "$P_UP_ONE_LINE %6d  0x%08x -> 0x%08x(%08x)    $P_NC" \
					      $page $fake_read_buffer_pa $tbm_a $ta
#HH0523-end
				get_tbm_result_4KB_page
			fi
		fi

		if [ "$send_fpga_reset_reg_0x68" = "1" ]; then
			reset_fpga_reg_0x68
		fi

		do_bsm_read

		result_str=$( cat /sys/kernel/debug/$DEBUG_FS/wr_cmp )
		result=${result_str:0:4}
		error_count=${result_str:5:3}

		if [ $result = "PASS" ]; then
#HH0523			printf "$P_UP_ONE_LINE %6d  0x%08x -> 0x%08x(%08x) -> 0x%08x   $P_LIGHT_GREEN%s  $P_NC\n" \
#HH0523					 $page $fake_read_buffer_pa $tbm_a $ms \
#HH0523					 $fake_write_buffer_pa  $result 
#HH0523-begin
			printf "$P_UP_ONE_LINE %6d  0x%08x -> 0x%08x(%08x) -> 0x%08x   $P_LIGHT_GREEN%s  $P_NC\n" \
					 $page $fake_read_buffer_pa $tbm_a $ta \
					 $fake_write_buffer_pa  $result 
#HH0523-end
			((pass_count++))
		elif [ $result = "FAIL" ]; then
#HH0523			printf "$P_LIGHT_RED %6d  0x%08x -> 0x%08x(%08x) -> 0x%08x   %s  $P_NC" \
#HH0523					 $page $fake_read_buffer_pa $tbm_a $ms \
#HH0523					 $fake_write_buffer_pa $error_count
#HH0523-begin
			printf "$P_LIGHT_RED %6d  0x%08x -> 0x%08x(%08x) -> 0x%08x   %s  $P_NC" \
					 $page $fake_read_buffer_pa $tbm_a $ta \
					 $fake_write_buffer_pa $error_count
#HH0523-end
			 print_failed_wr_cmp
			((fail_count++))
			if [ "$FAILSTOP" = "YES" ];then
				if [ $send_LA_trigger_dummy_rd = "YES" ]; then	
					LA_trigger_LA_ECC_TABLE_0_PA
				fi					
#HH0424				exit 1
#HH0424-begin
				exit 2
#HH0424-end
			fi
		fi
#HH0424		((ms+=0x200))
#HH0424-begin
#HH0515		((ms+=8))
#HH0515-begin
#HH0523		((ms+=addr_inc))
#HH0515-end
#HH0424-end
	#	((fake_write_buffer_pa-=0x1000))
		((fake_write_buffer_pa+=0x1000))
		((fake_read_buffer_pa+=0x1000))

#HH0523-begin
			((page++))
		done
		((loop_index++))
#HH0523-end

	done

	if [ $fail_count -eq 0 ]; then
		((loop_pass_count++))
	else
		((loop_fail_count++))
	fi
}


################################################################################
# 							M A I N   P R O G R A M
################################################################################
while [[ $# -gt 0 ]]
do
	key="$1"
	case $key in
    	-p|--pattern)
	    	pattern="$2"; shift;;
    	-p1|--pattern1)
	    	pattern1="$2"; shift;;
    	-p2|--pattern2)
	    	pattern2="$2"; shift;;
    	-l|--loop)
    		max_count="$2"; shift ;;
    	-dbg|--debug)
			DEBUG_FEAT_BSM_WR="$2"
			DEBUG_FEAT_BSM_RD="$3"; shift ;;
    	-dly|--delay)
    		user_defined_u_delay="$2"; shift ;;
    	-s|--size)
#HH0523-begin
			s_flag=1
#HH0523-end
    		size="$2"
    		unit="$3"; shift;;
    	-fr|--fakerd)
    		do_fw_test=0;;
    	-fw|--fakewr)
    		do_fr_test=0;;			
    	-fs|--failstop)
			if [[ ($2 == -*) || ($2 == "") ]]; then	## SJ0403
				MAX_FAIL_COUNT=1		## SJ0403
			else
				MAX_FAIL_COUNT="$2"		## SJ0403
			fi					
			FAILSTOP="YES";;
    	-ct|--CheckTbm)		
    		Check_TBM_result="$2";;
    	-fc|--failcount)
    		MAX_FAIL_COUNT="$2";;
    	-b|--bcom)
    		df_bcom_toggle_enable="$2";;
    	-bc|--bcomCtrl)
    		df_bcom_ctrl_method="$2";;
    	-rc|--RestCtrl)
    		df_fpga_reset_ctrl_method="$2";;
    	-pp|--PerPage)
    		TEST_4KB_PAGE_MODE="1";;
    	-la|--LA)
    		send_LA_trigger_dummy_rd="YES";;
    	-pmem|--pmem)
			DEBUG_FS="hdcmd_test"	
    		PMEM="1";;
#HH0515-begin
    	-emmc|--emmctest)
			emmc_flag=1
    		((addr_inc=8));;
#HH0515-end
#HH0424-begin
    	-lfrb|--loadfrb)
    		do_load_frb_flag=1;;
#HH0424-end
#HH0502-begin
    	-ps|--patsav)
			page_save_flag=1;
	    	pattern="$2"; shift;;
#HH0502-end
#HH0519-begin
    	-fl|--firstlba)
			fl_flag=1
    		fl_size="$2"
    		fl_unit="$3"; shift;;
#HH0519-end
#HH0523-begin
    	-ll|--lastlba)
			ll_flag=1
    		ll_size="$2"
    		ll_unit="$3"; shift;;
#HH0523-end
#HH0522-begin
    	-ai|--addrinc)
			ai_flag=1
    		ai_size="$2"
    		ai_unit="$3"; shift;;
    	-aii|--addrincinc)
			aii_flag=1
    		aii_size="$2"
    		aii_unit="$3"; shift;;
    	-bs|--blocksize)
			bs_flag=1
    		bs_size="$2"
    		bs_unit="$3"; shift;;
#HH0522-end
		--default)
    		DEFAULT=YES;;
    	*)
        # unknown option
    	;;
	esac
	shift # past argument or value
done

#
# >> DEBUG_FEAT <<
#
# [6] - FPGA Reset Ctrl MEthod(1=i2c, 0=mmio)       - df_fpga_reset_ctrl_method
# [5] - FPGA Data popcnt location(1=i2c, 0=mmio)    - df_fpga_popcnt_location
# [4] - FPGA Data Checksum location(1=i2c, 0=mmio)  - df_fpga_data_cs_location
# [3] - Slave CMD_DONE check enable                 - df_slave_cmd_done_check_enable
# [2] - Slave Data Checksum enable                  - df_slave_data_cs_enable
# [1] - BCOM Toggle Enable                          - df_bcom_toggle_enable
# [0] - BCOM Ctrl Method (0=MMIO, 1=I2C)            - df_bcom_ctrl_method

DEBUG_FEAT=0x0

df_fpga_popcnt_location="0"
df_fpga_data_cs_location="0"
df_slave_cmd_done_check_enable="1"
df_slave_data_cs_enable="1"

if [[ "$MASTER_FPGA_ONLY" = "1" ]];then
	df_slave_cmd_done_check_enable="0"
	df_slave_data_cs_enable="0"
fi

if [[ "$df_fpga_popcnt_location" = "1" ]];then
	((DEBUG_FEAT|=0x20))	
fi

if [[ "$df_fpga_data_cs_location" = "1" ]];then
	((DEBUG_FEAT|=0x10))	
fi

if [[ "$df_slave_cmd_done_check_enable" = "1" ]];then
	((DEBUG_FEAT|=0x8))	
fi

if [[ "$df_slave_data_cs_enable" = "1" ]];then
	((DEBUG_FEAT|=0x4))	
fi

if [[ "$df_bcom_toggle_enable" = "1" ]];then
	((DEBUG_FEAT|=0x2))
fi

if [[ "$df_bcom_ctrl_method" = "1" ]];then
	((DEBUG_FEAT|=0x1))	
fi

if [[ "$df_fpga_reset_ctrl_method" = "1" ]];then
	((DEBUG_FEAT|=0x40))	
fi


if [[ "$PMEM" = "0" ]];then
	DEBUG_FS="hvcmd_test"
else
	DEBUG_FS="hdcmd_test"
fi


pat=("$pattern" "$pattern1" "$pattern2")
pat_length=${#pat[@]}
#HH0424 patname=("Byte_Inc" "TBM_Address" "Random" "All '0'" "All 'f'" "All '5'" "All 'a'")
#HH0424-begin
patname=("Byte_Inc" "TBM_Address" "Random" "All '0'" "All 'f'" "All '5'" "All 'a'" "E000_0000")
#HH0424-end

#----------------------------------------------------------------------
# -s option handler => page_count
#----------------------------------------------------------------------
if [ "$s_flag" = "1" ]; then	#HH0523
case "$unit" in
k)
	((rem=$size%4))
	if [ $size = 0 ] || [ $rem != 0 ]; then
		echo " The memory size must be multiple of 4KB"
		usage
	else
		_unit="KB"
		((page_count=$size/4))
	fi
	;;
m)
	_unit="MB"
	((page_count=size*256))
	;;
g)
	_unit="GB"
	((page_count=size*262144))
	;;
*)
	echo " Unknown option $unit"
	usage
	;;
esac
fi	#HH0523

#HH0519-begin
#----------------------------------------------------------------------
# -fl option handler => first_lba
#   o. max_lba/last_lba will be updated based on first_lba
#----------------------------------------------------------------------
if [ "$emmc_flag" = "1" ] && [ "$fl_flag" = "1" ]; then
	case "$fl_unit" in
	k)
		((rem=$fl_size%4))
		if [ $rem -eq 0 ]; then
			# 268435456 KB = 256 GB
			if [ $fl_size -lt 268435456 ]; then
				# Convert it to LBA
				((first_lba=$fl_size*2))
			else
				printf "${P_LIGHT_RED}[ERROR]${P_NC} -fl <size>: should be multiple of 4 within 0 ~ 268435452\n"
				exit 1
			fi
		else
			printf "${P_LIGHT_RED}[ERROR]${P_NC} -fl <size>: should be multiple of 4\n"
			exit 1
		fi
		;;
	m)
		# 262144 MB = 256 GB
		if [ $fl_size -lt 262144 ]; then
			# Convert it to LBA
			((first_lba=256*$fl_size*8))
		else
			printf "${P_LIGHT_RED}[ERROR]${P_NC} -fl <size>: should be in 0 ~ 262143\n"
			exit 1
		fi
		;;
	g)
		if [ $fl_size -lt 256 ]; then
			# Convert it to LBA
			((first_lba=262144*$fl_size*8))
		else
			printf "${P_LIGHT_RED}[ERROR]${P_NC} -fl <size>: should be in 0 ~ 255\n"
			exit 1
		fi
		;;
	*)
		printf "${P_LIGHT_RED}[ERROR]${P_NC} -fl <unit> invalid: $fl_unit\n"
		usage
		;;
	esac

	# Change default according to first_lba
	((max_lba=first_lba+8))
	((last_lba=max_lba-8))
	((page_count=1))
fi
#HH0519-end

#HH0523-begin
#----------------------------------------------------------------------
# -ll option handler => max_lba
#   o. max_lba will be overriden here
#   o. page_count/last_lba will be updated based the new max_lba
#----------------------------------------------------------------------
if [ "$emmc_flag" = "1" ]; then
	if [ "$ll_flag" = "1" ]; then
		case "$ll_unit" in
		k)
			((rem=$ll_size%4))
			if [ $rem -eq 0 ]; then
				# 268435456 KB = 256 GB
				if [ $ll_size -le 268435456 ]; then
					# Convert it to LBA
					((max_lba=$ll_size*2))
				else
					printf "${P_LIGHT_RED}[ERROR]${P_NC} -ll <size>: should be multiple of 4 within 0 ~ 268435456\n"
					exit 1
				fi
			else
				printf "${P_LIGHT_RED}[ERROR]${P_NC} -ll <size>: should be multiple of 4\n"
				exit 1
			fi
			;;
		m)
			# 262144 MB = 256 GB
			if [ $ll_size -le 262144 ]; then
				# Convert it to LBA
				((max_lba=256*$ll_size*8))
			else
				printf "${P_LIGHT_RED}[ERROR]${P_NC} -ll <size>: should be in 0 ~ 262144\n"
				exit 1
			fi
			;;
		g)
			if [ $ll_size -le 256 ]; then
				# Convert it to LBA
				((max_lba=262144*$ll_size*8))
			else
				printf "${P_LIGHT_RED}[ERROR]${P_NC} -ll <size>: should be in 0 ~ 256\n"
				exit 1
			fi
			;;
		*)
			printf "${P_LIGHT_RED}[ERROR]${P_NC} -ll <unit> invalid: $ll_unit\n"
			usage
			;;
		esac

		((page_count=(max_lba-first_lba)/8))
	else
		if [ "$fl_flag" = "1" ]; then
			# when only -fl was given
			((max_lba=first_lba+8))
			((page_count=(max_lba-first_lba)/8))
		else
			# when both -fl & -ll were NOT given but -emmc was there and
			# assuming -s was there as well
			((max_lba=$first_lba+$page_count*8))		#for eMMC test
		fi
	fi

	((last_lba=max_lba-8))
else
	# -emmc was not there so do it for TBM test
	((max_lba=$first_lba+$page_count*512))		#for TBM test
fi
#HH0523-end


#HH0424 ((max_lba=$page_count*512))		#for_0x200
#HH0424-begin
#HH0523if [ "$emmc_flag" = "1" ]; then
#HH0523	((max_lba=$first_lba+$page_count*8))		#for eMMC test
#HH0523else
#HH0523	((max_lba=$first_lba+$page_count*512))		#for TBM test
#HH0523fi
#HH0424-end

#HH0523max_lba_hex=`echo "obase=16; $max_lba" | bc`


#HH0522-begin
#----------------------------------------------------------------------
# -ai option handler => addr_inc
#   o. -ai will override addr_inc value set by -emmc option
#   o. -ai option is only for emmc for now
#   o. page_count/max_lba/last_lba will be updated based on the new
#      addr_inc
#   o. page_count in this handler assumes bs (or bpc) = 1
#----------------------------------------------------------------------
if [ "$emmc_flag" = "1" ] && [ "$ai_flag" = "1" ]; then
	case "$ai_unit" in
	k)
		((rem=$ai_size%4))
		if [ $rem -eq 0 ]; then
			# 268435456 KB = 256 GB
			if [ $ai_size -gt 0 ] && [ $ai_size -lt 268435456 ]; then
				# Convert it to LBA
				((addr_inc=$ai_size*2))
			else
				printf "${P_LIGHT_RED}[ERROR]${P_NC} -ai <size>: should be multiple of 4 within 4 ~ 268435452\n"
				exit 1
			fi
		else
			printf "${P_LIGHT_RED}[ERROR]${P_NC} -ai <size>: should be multiple of 4\n"
			exit 1
		fi
		;;
	m)
		# 262144 MB = 256 GB
		if [ $ai_size -gt 0 ] && [ $ai_size -lt 262144 ]; then
			# Convert it to LBA
			((addr_inc=256*$ai_size*8))
		else
			printf "${P_LIGHT_RED}[ERROR]${P_NC} -ai <size>: should be in 1 ~ 262143\n"
			exit 1
		fi
		;;
	g)
		if [ $ai_size -gt 0 ] && [ $ai_size -lt 256 ]; then
			# Convert it to LBA
			((addr_inc=262144*$ai_size*8))
		else
			printf "${P_LIGHT_RED}[ERROR]${P_NC} -ai <size>: should be in 1 ~ 255\n"
			exit 1
		fi
		;;
	*)
		printf "${P_LIGHT_RED}[ERROR]${P_NC} -ai <unit> invalid: $ai_unit\n"
		usage
		;;
	esac

	# last LBA is updated based on the value from the -ai option
	((page_count=$max_lba/$addr_inc))
	((last_lba=$first_lba+($addr_inc*($page_count-1))))
	((max_lba=last_lba+8))
fi

#------------------------------------------------------------
# -aii option handler => ai_inc
#   o. -aii option is only for emmc for now
#   o. last_lba/max_lba will be updated based on ai_inc
#------------------------------------------------------------
if [ "$emmc_flag" = "1" ] && [ "$aii_flag" = "1" ]; then
	case "$aii_unit" in
	k)
		((rem=$aii_size%4))
		if [ $rem -eq 0 ]; then
			# 268435456 KB = 256 GB
			if [ $aii_size -lt 268435456 ]; then
				# Convert it to LBA
				((ai_inc=$aii_size*2))
			else
				printf "${P_LIGHT_RED}[ERROR]${P_NC} -aii <size>: should be multiple of 4 within 0 ~ 268435452\n"
				exit 1
			fi
		else
			printf "${P_LIGHT_RED}[ERROR]${P_NC} -aii <size>: should be multiple of 4\n"
			exit 1
		fi
		;;
	m)
		# 262144 MB = 256 GB
		if [ $aii_size -lt 262144 ]; then
			# Convert it to LBA
			((ai_inc=256*$aii_size*8))
		else
			printf "${P_LIGHT_RED}[ERROR]${P_NC} -aii <size>: should be in 0 ~ 262143\n"
			exit 1
		fi
		;;
	*)
		printf "${P_LIGHT_RED}[ERROR]${P_NC} -aii <unit> invalid: $aii_unit\n"
		usage
		;;
	esac

	# last LBA is updated based on the value from the -aii option
	((last_lba=$first_lba+($addr_inc*($page_count-1))+($ai_inc*($page_count-1))))
	((max_lba=last_lba+8))
fi

if [ $ai_inc -gt $addr_inc ]; then
	printf "${P_LIGHT_RED}[ERROR]${P_NC} -aii <size> should be less than -ai <size>\n"
	exit 1
fi

#------------------------------------------------------------
# -bs option handler => bpc (Block Page Count)
#   o. -bs option is only for emmc for now
#   o. last_lba/max_lba will be updated based on bpc
#------------------------------------------------------------
if [ "$emmc_flag" = "1" ] && [ "$bs_flag" = "1" ]; then
	case "$bs_unit" in
	k)
		((rem=$bs_size%4))
		if [ $rem -eq 0 ]; then
			# 268435456 KB = 256 GB
			if [ $bs_size -lt 268435456 ]; then
				# Convert it to page count
				((bpc=$bs_size/4))
			else
				printf "${P_LIGHT_RED}[ERROR]${P_NC} -bs <size> should be multiple of 4 within 0 ~ 268435452\n"
				exit 1
			fi
		else
			printf "${P_LIGHT_RED}[ERROR]${P_NC} -bs <size> should be multiple of 4\n"
			exit 1
		fi
		;;
	m)
		# 262144 MB = 256 GB
		if [ $bs_size -lt 262144 ]; then
			# Convert it to page count
			((bpc=256*$bs_size))
		else
			printf "${P_LIGHT_RED}[ERROR]${P_NC} -bs <size>: should be in 0 ~ 262143\n"
			exit 1
		fi
		;;
	*)
		printf "${P_LIGHT_RED}[ERROR]${P_NC} -bs <unit> invalid: $aii_unit\n"
		usage
		;;
	esac

	# last LBA is updated based on the value from the -bs option
	((last_lba=$first_lba+($addr_inc*($page_count-1))+($ai_inc*($page_count-1))+(($bpc-1)*8)))
	((max_lba=last_lba+8))
fi

if [ $bpc -gt $((addr_inc/8)) ]; then
	printf "${P_LIGHT_RED}[ERROR]${P_NC} -bs <size> should be less than -ai <size>\n"
	exit 1
fi
#HH0522-end


# This is valid only for TBM test
((TBM_Size=$page_count*4096-1))

#HH0519if [[ $page_count -gt 524288 ]]; then
#HH0519	echo -e "\n ERROR ... MAX TBM Size is 512MB \n"
#HH0519	exit 1
#HH0519fi	
#HH0519-begin

# -. first_lba is the LBA where the test starts from.
# -. last_lba may not be the last LBA of the test depending on ai_inc & bpc.
#    It will be the last LBA when ai_inc=0 & bpc=1.
last_lba_hex=`echo "obase=16; $last_lba" | bc`
first_lba_hex=`echo "obase=16; $first_lba" | bc`
addr_inc_hex=`echo "obase=16; $addr_inc" | bc`
ai_inc_hex=`echo "obase=16; $ai_inc" | bc`
#HH0519-end
max_lba_hex=`echo "obase=16; $max_lba" | bc`	#HH0523

tbm=`echo "obase=16; $TBM_Size" | bc`
#BCOM=$(/home/lab/work/hv_training/test$SW_VERSION -d $DIMM_ID -5 6 | awk '{print $8}')
#BCOM=${BCOM:4:1}

printf "$P_LIGHT_BLUE"
printf "\n Script Update: $SCRIPT_VERSION"
printf "\n Debug FileSys: $DEBUG_FS"
#HH0519printf "\n Memory Size  : $size $_unit (0x$tbm), $page_count page(s)"
#HH0519-begin
if [ "$emmc_flag" = "1" ]; then
	printf "\n Memory Size  : $((page_count*bpc)) page(s), First LBA ($first_lba, 0x$first_lba_hex), Last LBA ($last_lba, 0x$last_lba_hex), max_lba ($max_lba, 0x$max_lba_hex)"
else
	printf "\n Memory Size  : $size $_unit (0x$tbm), $page_count page(s)"
fi
#HH0519-end
printf "\n Delay times  : (user) $user_defined_u_delay us, (bsm_wr_qc) $bsm_wr_qc_n_delay ns, (bsm_rd_qc) $bsm_rd_qc_n_delay ns"
printf "$P_LIGHT_CYAN"
printf "\n Debug Feature                : 0x%02X" $DEBUG_FEAT 
printf "$P_LIGHT_BLUE"
printf "\n  [06] fpga_reset_ctrl_method - $df_fpga_reset_ctrl_method (1:i2c,0:mmio), $send_fpga_reset_reg_0x68 (1:Run,0:Skip)"
printf "\n  [05] fpga_popcnt_location   - $df_fpga_popcnt_location"
printf "\n  [04] fpga_data_cs_location  - $df_fpga_data_cs_location"
printf "\n  [03] slave_cmd_done_check_en- $df_slave_cmd_done_check_enable"
printf "\n  [02] slave_data_cs_enable   - $df_slave_data_cs_enable"
printf "\n  [01] bcom_toggle_enable     - $df_bcom_toggle_enable (1:Toggle,0:FPGA)"
printf "\n  [00] bcom_ctrl_method       - $df_bcom_ctrl_method (1:i2c,0:mmio)"
printf "$P_LIGHT_CYAN"
printf "\n Debug Feature bsm_wr, bsm_rd : 0x%06X , 0x%06X" $DEBUG_FEAT_BSM_WR $DEBUG_FEAT_BSM_RD
printf "$P_LIGHT_BLUE"
printf "\n  [21] popcnt_enable          - $(((DEBUG_FEAT_BSM_WR&0x200000)>>21)), $(((DEBUG_FEAT_BSM_RD&0x200000)>>21))"
printf "\n  [20] do_dummy_read_enable   - $(((DEBUG_FEAT_BSM_WR&0x100000)>>20)), $(((DEBUG_FEAT_BSM_RD&0x100000)>>20))"
printf "\n  [19] send_dummy_cmd_enable  - $(((DEBUG_FEAT_BSM_WR&0x080000)>>19)), $(((DEBUG_FEAT_BSM_RD&0x080000)>>19))"
printf "\n  [18] skip_termination_enable- $(((DEBUG_FEAT_BSM_WR&0x040000)>>18)), $(((DEBUG_FEAT_BSM_RD&0x040000)>>18))"
printf "\n  [17] skip_g[r|w]s_enable    - $(((DEBUG_FEAT_BSM_WR&0x020000)>>17)), $(((DEBUG_FEAT_BSM_RD&0x020000)>>17))"
printf "\n  [16] skip_query_enable      - $(((DEBUG_FEAT_BSM_WR&0x010000)>>16)), $(((DEBUG_FEAT_BSM_RD&0x010000)>>16))"
printf "\n  [15] query_chk_retry_enable - $(((DEBUG_FEAT_BSM_WR&0x008000)>>15)), $(((DEBUG_FEAT_BSM_RD&0x008000)>>15))"
printf "    [14:12] query_chk_max_retry - $(((DEBUG_FEAT_BSM_WR&0x007000)>>12)), $(((DEBUG_FEAT_BSM_RD&0x007000)>>12))"
printf "\n  [11] f_[r|w]_cs_retry_enable- $(((DEBUG_FEAT_BSM_WR&0x000800)>>11)), $(((DEBUG_FEAT_BSM_RD&0x000800)>>11))"
printf "    [10:08] f_[r|w]_cs_max_retry- $(((DEBUG_FEAT_BSM_WR&0x000700)>> 8)), $(((DEBUG_FEAT_BSM_RD&0x000700)>> 8))"
printf "\n  [07] data_cs_retry_enable   - $(((DEBUG_FEAT_BSM_WR&0x000080)>> 7)), $(((DEBUG_FEAT_BSM_RD&0x000080)>> 7))"
printf "    [06:04] data_cs_max_retry   - $(((DEBUG_FEAT_BSM_WR&0x000070)>> 4)), $(((DEBUG_FEAT_BSM_RD&0x000070)>> 4))"
printf "\n  [03] cmd_cs_retry_enable    - $(((DEBUG_FEAT_BSM_WR&0x000008)>> 3)), $(((DEBUG_FEAT_BSM_RD&0x000008)>> 3))"
printf "    [02:00] cmd_cs_max_retry    - $(((DEBUG_FEAT_BSM_WR&0x000007)>> 0)), $(((DEBUG_FEAT_BSM_RD&0x000007)>> 0))"
printf "\n 64 BIT MASK  : $WR_CMP_64_BITS_MASK"
printf "\n FAILSTOP MODE: $FAILSTOP"
if [ "$FAILSTOP" = "YES" ];then
printf ", Max Count: $MAX_FAIL_COUNT"
fi		
printf ", Per_PAGE_MODE: $TEST_4KB_PAGE_MODE  Check_TBM: $Check_TBM_result"

#HH0516-begin
if [ "$emmc_flag" = "1" ];then
	printf ", Target: eMMC"
	printf "\n first_lba = $first_lba (0x$first_lba_hex), last_lba = $last_lba (0x$last_lba_hex), max_lba = $max_lba (0x$max_lba_hex)\n"
	printf " addr_inc = $addr_inc (0x$addr_inc_hex), ai_inc = $ai_inc (0x$ai_inc_hex), bpc = $bpc, page_count = $page_count, total page count ($page_count * $bpc) = $((page_count*bpc))"
else		
	printf ", Target: TBM"
fi
#HH0516-end

#HH0519-begin
# Only for eMMC test case
if [ "$emmc_flag" = "1" ]; then
	if [ $max_lba -gt 536870912 ]; then
		printf "${P_LIGHT_RED}[ERROR]${P_NC} max_lba out of range: should be less than equal to ~ 536870912 (0x2000_0000)\n"
		exit 1
	fi
else
	if [[ $page_count -gt 524288 ]]; then
		printf "${P_LIGHT_RED}[ERROR]${P_NC} MAX TBM Size is 512MB\n"
		exit 1
	fi	
fi
#HH0519-end

rc=$(/home/lab/work/hd_training_v2/hd-training-v2 -d $DIMM_ID -sn)
printf "\n $rc Test start @ $(date)"
printf "$P_NC\n"

#HH0424 echo $user_defined_u_delay > /sys/kernel/debug/$DEBUG_FS/user_defined_delay
echo $bsm_wr_qc_n_delay > /sys/kernel/debug/$DEBUG_FS/debug_feat_bsm_wrt_qc_status_delay
echo $bsm_rd_qc_n_delay > /sys/kernel/debug/$DEBUG_FS/debug_feat_bsm_rd_qc_status_delay
echo $DEBUG_FEAT > /sys/kernel/debug/$DEBUG_FS/debug_feat
echo $LA_ECC_TABLE_0_PA > /sys/kernel/debug/$DEBUG_FS/debug_feat_bsm_wrt_dummy_read_addr
echo $LA_ECC_TABLE_2_PA > /sys/kernel/debug/$DEBUG_FS/debug_feat_bsm_rd_dummy_read_addr
if [[ "$send_dummy_command_enable" = "1" ]];then
	echo 0 > /sys/kernel/debug/$DEBUG_FS/debug_feat_bsm_wrt_dummy_command_lba
fi
echo $DEBUG_FEAT_BSM_WR > /sys/kernel/debug/$DEBUG_FS/debug_feat_bsm_wrt
echo $DEBUG_FEAT_BSM_RD > /sys/kernel/debug/$DEBUG_FS/debug_feat_bsm_rd
echo $WR_CMP_64_BITS_MASK > /sys/kernel/debug/$DEBUG_FS/wr_cmp
#HH0424-begin
echo 1 > /sys/kernel/debug/$DEBUG_FS/bsm_wrt_delay_before_qc	# for eMMC operation
echo 1 > /sys/kernel/debug/$DEBUG_FS/bsm_rd_delay_before_qc		# for eMMC operation
#HH0424-end
echo 1 > /sys/kernel/debug/$DEBUG_FS/debug_feat_enable

echo $max_retry_count > /sys/kernel/debug/$DEBUG_FS/max_retry_count
echo 0 > /sys/kernel/debug/$DEBUG_FS/tag
echo 8 > /sys/kernel/debug/$DEBUG_FS/sector

total_ts0=$(date +%s%N | cut -b1-13)
laptime_tbm=0

for ((c=1; c<=$max_count; c++))
do		
	loop_ts0=$(date +%s%N | cut -b1-13)
	for (( i=1; i<${pat_length}+1; i++ ));
	do		
		if [ "${pat[$i-1]}" = "i0" ];then
			testpattern=${patname[0]}  
			echo 0 > /sys/kernel/debug/$DEBUG_FS/extdat
			echo 0 > /sys/kernel/debug/$DEBUG_FS/intdat_idx
		elif [ "${pat[$i-1]}" = "i1" ];then
			testpattern=${patname[1]}   
			echo 0 > /sys/kernel/debug/$DEBUG_FS/extdat
			echo 1 > /sys/kernel/debug/$DEBUG_FS/intdat_idx
		elif [ "${pat[$i-1]}" = "i2" ];then
			testpattern=${patname[2]}  
			echo 0 > /sys/kernel/debug/$DEBUG_FS/extdat
			echo 2 > /sys/kernel/debug/$DEBUG_FS/intdat_idx
		elif [ "${pat[$i-1]}" = "0" ];then
			testpattern=${patname[3]}
			echo 1 > /sys/kernel/debug/$DEBUG_FS/extdat
			cat testfile_4k_all_0.txt > /sys/kernel/debug/$DEBUG_FS/4k_file_out
		elif [ "${pat[$i-1]}" = "f" ];then
			testpattern=${patname[4]}
			echo 1 > /sys/kernel/debug/$DEBUG_FS/extdat
			cat testfile_4k_all_f.txt > /sys/kernel/debug/$DEBUG_FS/4k_file_out
		elif [ "${pat[$i-1]}" = "5" ];then
			testpattern=${patname[5]}
			echo 1 > /sys/kernel/debug/$DEBUG_FS/extdat
			cat testfile_4k_all_5.txt > /sys/kernel/debug/$DEBUG_FS/4k_file_out
		elif [ "${pat[$i-1]}" = "a" ];then
			testpattern=${patname[6]}
			echo 1 > /sys/kernel/debug/$DEBUG_FS/extdat
			cat testfile_4k_all_a.txt > /sys/kernel/debug/$DEBUG_FS/4k_file_out
#HH0424-begin
		elif [ "${pat[$i-1]}" = "i3" ];then
			testpattern=${patname[7]}   
			echo 0 > /sys/kernel/debug/$DEBUG_FS/extdat
			echo 3 > /sys/kernel/debug/$DEBUG_FS/intdat_idx
#HH0424-end
		else
		#	printf "\n Undefined Pattern"
			break
		fi

		# Init buffer address
		((fake_read_buffer_pa=FAKE_READ_BUFFER_PA))
		((fake_write_buffer_pa=FAKE_READ_BUFFER_PA+MAX_BUFF_SIZE))
	#	((fake_write_buffer_pa=FAKE_READ_BUFFER_PA))
		if [ "$TEST_4KB_PAGE_MODE" = 1 ];then	
#HH0522			((fake_write_buffer_pa+=(page_count-1)*0x1000))
#HH0522-begin
			((fake_write_buffer_pa+=(page_count*bpc-1)*0x1000))
#HH0522-end
			printf "\n%s loop=$c, $testpattern, FakeRD: 0x%X, FakeWR: 0x%X %s\n\n" \
					$P_BOLD_GRAY $fake_read_buffer_pa $fake_write_buffer_pa $P_NC
		else
			printf "\n%s loop=$c, Pattern: $testpattern, FakeRD: 0x%X, FakeWR: 0x%X %s\n" \
				$P_BOLD_GRAY $fake_read_buffer_pa $fake_write_buffer_pa $P_NC
		fi

		if [ "$TEST_4KB_PAGE_MODE" = 1 ];then	
			run_bsm_Write_Read_per_Page	
		else
			if [ "$do_fr_test" = 1 ];then
				run_bsm_Write
	  		fi

			if [ "$do_fw_test" = 1 ];then
				run_bsm_Read
	  		fi
		fi 

	#	printf "\n   Finished BSM Read(FakeWR) %d Pages \n" $page
		printf "\n$P_LIGHT_BLUE   # of pass = %6d, # of loop_pass = %6d$P_NC" $pass_count $loop_pass_count
		printf "\n$P_LIGHT_BLUE   # of fail = %6d, # of loop_fail = %6d$P_NC" $fail_count $loop_fail_count
	#	printf "\n$P_LIGHT_BLUE   # of runs = %6d$P_NC" $((pass_count+fail_count))

	done
	loop_ts1=$(date +%s%N | cut -b1-13)
	lap_loop=$((loop_ts1-loop_ts0))
	lap_fr=$((fr_ts1-fr_ts0))
	lap_fw=$((fw_ts1-fw_ts0))

	if [ "$TEST_4KB_PAGE_MODE" = 1 ];then
		printf "\n  Loop time: $lap_loop ms "
	else
		printf "\n  Loop time: $lap_loop ms = $lap_fr + $laptime_tbm + $lap_fw"
	fi
	printf ", Total time: $((loop_ts1-total_ts0)) ms\n"
#	echo
#	read -n 1 -s -p "  Press any key to continue"	
done

echo

# -----------------------------------------------------------------------------
# [[ CHANGE HISTORY ]]
# hdtest.sh
#
# 05/25/2017	rev 1.2
#				-. Added new options for eMMC test: -lfrb, -fl, -ll, -ai, -aii,
#                  -bs
# 03/21/2017	rev 1.1
# 02/03/2017	rev 1.0
#
# -----------------------------------------------------------------------------
