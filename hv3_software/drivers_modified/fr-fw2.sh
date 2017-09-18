#!/bin/bash
# -----------------------------------------------------------------------------
# fr-fw.sh
# arg #1 - TBM memory size (must be multiple of 4KB)
#			TBM addr increments by 4KB while host fake addr increments by 32KB.
# arg #2 - k for KB, m for MB, g for GB
# arg #3 - repeat count
#
# 02/03/2017	rev 1.0
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
# Escape sequence codes for echo
UNDERSCORE='\033[4m'
INVERSE='\033[7m'
BOLD_RED='\033[1;31m'
LIGHT_RED='\033[0;31m'
LIGHT_GREEN='\033[1;32m'
LIGHT_YELLOW='\033[1;33m'
LIGHT_BLUE='\033[1;34m'
LIGHT_CYAN='\033[1;36m'
NC='\033[0m' # No Color
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
P_NC=$'\e[0m' # No Color
P_UP_ONE_LINE=$'\e[1A'
P_DOWN_ONE_LINE=$'\e[1B'
P_MOVE_FWD_34_COL=$'\e[34C'
P_MOVE_FWD_2_COL=$'\e[2C'
P_CLEAR_LINE=$'\e[2K'

#echo "arg count = $#"

DIMM_ID=3
MASTER_FPGA_ONLY="1"

if [[ "$MASTER_FPGA_ONLY" = "1" ]];then
	WR_CMP_64_BITS_MASK=0x00000000FFFFFFFF
else
	WR_CMP_64_BITS_MASK=0xFFFFFFFFFFFFFFFF
fi

#FAKE_READ_BUFFER_PA=0x678F00000
FAKE_READ_BUFFER_PA=0x639F00000
MAX_BUFF_SIZE=0x20000000	# 512MB

#
# >> DEBUG_FEAT_BSM_WR & DEBUG_FEAT_BSM_RD <<
#
# [20]    - BSM_WRT/DO Do Dummy 64-Byte Read Enable
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

Check_TBM_result="1"
df_bcom_toggle_enable="1"
df_bsm_wr_skip_termination_enable="0"	# if skipped, 2us delay @hv_fake_operation() default=0
df_bsm_rd_skip_termination_enable="1"	# if skipped, 2us delay @hv_fake_operation() default=1
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

bsm_wr_wait_u_delay=1
bsm_wr_qc_n_delay=1
bsm_rd_qc_n_delay=1000

max_count=1
do_fr_test=1
do_fw_test=1
FAILSTOP="NO"

LA_trigger_act_0000_rd ()
{
	/home/lab/work/hv_training/test0302 -d $DIMM_ID -r32 0x480000000 64 >&-	
}		

reset_fpga_reg_0x68 ()
{
	/home/lab/work/hv_training/test0302 -d $DIMM_ID -f 0x68 0xe0 >&-
	sleep 0.01
	/home/lab/work/hv_training/test0302 -d $DIMM_ID -f 0x68 0 >&-
	sleep 0.01		
###	echo 1 > /sys/kernel/debug/hvcmd_test/hv_reset
}		

reset_bcom_control ()
{
	/home/lab/work/hv_training/test0302 -d $DIMM_ID -5 0x6 0x80 >&-
	sleep 0.01
	/home/lab/work/hv_training/test0302 -d $DIMM_ID -B >&-
	sleep 0.01		
}		

do_bsm_write ()
{
	# Send command to the command driver

	echo $fake_read_buffer_pa > /sys/kernel/debug/hvcmd_test/fake_read_buff_pa
	echo $ms > /sys/kernel/debug/hvcmd_test/LBA
	echo 1 > /sys/kernel/debug/hvcmd_test/bsm_write
}

do_bsm_read ()
{
	# Send commands to the command driver
	echo $fake_read_buffer_pa > /sys/kernel/debug/hvcmd_test/fake_read_buff_pa
	echo $fake_write_buffer_pa > /sys/kernel/debug/hvcmd_test/fake_write_buff_pa
	echo $ms > /sys/kernel/debug/hvcmd_test/LBA
	echo 1 > /sys/kernel/debug/hvcmd_test/bsm_read
#	echo 1 > /sys/kernel/debug/hvcmd_test/terminate_cmd

}

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

print_failed_wr_cmp ()
{
	cp /sys/kernel/debug/hvcmd_test/wr_cmp fail2.txt
	((max_fail_count=4))
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

				if [ $qword != 0000000000000000 ] && [ $max_fail_count -gt 0 ]; then
					printf "%s%.3d:%s %s" $P_LIGHT_RED $qw_offset $qword $P_NC
					((max_fail_count--))
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
	echo "Usage: fr-fw2.sh"
    echo "       -p <5|i0|i1|i2> {-p1 <>} {-p2 <>} -l <loop> -s <size> <k|m|g> -fr|fw -fs -dly <> -dbg <>"
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
    		bsm_wr_wait_u_delay="$2"; shift ;;
    	-s|--size)
    		size="$2"
    		unit="$3"; shift;;
    	-fr|--fakerd)
    		do_fw_test=0;;
    	-fw|--fakewr)
    		do_fr_test=0;;			
    	-fs|--failstop)
    		FAILSTOP="YES";;
    	-b|--bcom)
    		df_bcom_toggle_enable="$2";;
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
# [5]     - FPGA Data popcnt location(1=i2c, 0=mmio)
# [4]     - FPGA Data Checksum location(1=i2c, 0=mmio)
# [3]     - Slave CMD_DONE check enable
# [2]     - Slave Data Checksum enable
# [1]     - BCOM Toggle Enable
# [0]     - BCOM Ctrl Method (0=MMIO, 1=I2C)

DEBUG_FEAT=0x1

df_fpga_data_cs_location="0"
df_slave_cmd_done_check_enable="0"
df_slave_data_cs_enable="0"

if [[ "$MASTER_FPGA_ONLY" = "1" ]];then
	df_slave_cmd_done_check_enable="0"
	df_slave_data_cs_enable="0"
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
	/home/lab/work/hv_training/test0302 -d $DIMM_ID -5 6 0x80 >&-
	sleep 0.01		
else
	/home/lab/work/hv_training/test0302 -d $DIMM_ID -B >&-
	sleep 0.01		
fi

pat=("$pattern" "$pattern1" "$pattern2")
pat_length=${#pat[@]}
patname=("Byte_Inc" "TBM_Address" "Random" "All '0'" "All 'f'" "All '5'" "All 'a'")

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

((max_lba=$page_count*512))		#for_0x200
max_lba_hex=`echo "obase=16; $max_lba" | bc`
((TBM_Size=$page_count*4096-1))

if [[ $page_count -gt 524288 ]]; then
	echo -e "\n ERROR ... MAX TBM Size is 512MB \n"
	exit 1
fi	

tbm=`echo "obase=16; $TBM_Size" | bc`
BCOM=$(/home/lab/work/hv_training/test0302 -d $DIMM_ID -5 6 | awk '{print $8}')
BCOM=${BCOM:4:1}

printf "\n$LIGHT_BLUE Memory Size= $size $_unit (0x$tbm), $page_count page(s)"
printf ", user_us_dly= $bsm_wr_wait_u_delay, bsm_qc_wr/rd_ns_dly= $bsm_wr_qc_n_delay, $bsm_rd_qc_n_delay, FAILSTOP= $FAILSTOP"
printf "\n debug_feat= %1x debug_feat_bsm_wr= %06x, debug_feat_bsm_rd= %06x" $DEBUG_FEAT $DEBUG_FEAT_BSM_WR $DEBUG_FEAT_BSM_RD
printf "\n BCOM_Control= $BCOM (1:Toggle,0:FPGA), BIT_MASK= $WR_CMP_64_BITS_MASK"
printf ", Checksum_location= $df_fpga_data_cs_location(0:mmio,1:i2C)"
printf "\n bsm_rd_popcnt_enable= $df_bsm_rd_popcnt_enable $P_NC\n"


echo $bsm_wr_wait_u_delay > /sys/kernel/debug/hvcmd_test/user_defined_delay
echo $bsm_wr_qc_n_delay > /sys/kernel/debug/hvcmd_test/debug_feat_bsm_wrt_qc_status_delay
echo $bsm_rd_qc_n_delay > /sys/kernel/debug/hvcmd_test/debug_feat_bsm_rd_qc_status_delay
echo $DEBUG_FEAT > /sys/kernel/debug/hvcmd_test/debug_feat
echo $LA_ECC_TABLE_0_PA > /sys/kernel/debug/hvcmd_test/debug_feat_bsm_wrt_dummy_read_addr
echo $LA_ECC_TABLE_2_PA > /sys/kernel/debug/hvcmd_test/debug_feat_bsm_rd_dummy_read_addr
if [[ "$send_dummy_command_enable" = "1" ]];then
	echo 0 > /sys/kernel/debug/hvcmd_test/debug_feat_bsm_wrt_dummy_command_lba
fi
echo $DEBUG_FEAT_BSM_WR > /sys/kernel/debug/hvcmd_test/debug_feat_bsm_wrt
echo $DEBUG_FEAT_BSM_RD > /sys/kernel/debug/hvcmd_test/debug_feat_bsm_rd
echo $WR_CMP_64_BITS_MASK > /sys/kernel/debug/hvcmd_test/wr_cmp
echo 1 > /sys/kernel/debug/hvcmd_test/debug_feat_enable

echo $max_retry_count > /sys/kernel/debug/hvcmd_test/max_retry_count
echo 0 > /sys/kernel/debug/hvcmd_test/tag
echo 8 > /sys/kernel/debug/hvcmd_test/sector

script_ts0=$(date +%s%N | cut -b1-13)
for ((c=1; c<=$max_count; c++))
do		
	loop_ts0=$(date +%s%N | cut -b1-13)
	for (( i=1; i<${pat_length}+1; i++ ));
	do		
		if [ "${pat[$i-1]}" = "i0" ];then
			testpattern=${patname[0]}  
			echo 0 > /sys/kernel/debug/hvcmd_test/extdat
			echo 0 > /sys/kernel/debug/hvcmd_test/intdat_idx
		elif [ "${pat[$i-1]}" = "i1" ];then
			testpattern=${patname[1]}   
			echo 0 > /sys/kernel/debug/hvcmd_test/extdat
			echo 1 > /sys/kernel/debug/hvcmd_test/intdat_idx
		elif [ "${pat[$i-1]}" = "i2" ];then
			testpattern=${patname[2]}  
			echo 0 > /sys/kernel/debug/hvcmd_test/extdat
			echo 2 > /sys/kernel/debug/hvcmd_test/intdat_idx
		elif [ "${pat[$i-1]}" = "0" ];then
			testpattern=${patname[3]}
			echo 1 > /sys/kernel/debug/hvcmd_test/extdat
			cat testfile_4k_all_0.txt > /sys/kernel/debug/hvcmd_test/4k_file_out
		elif [ "${pat[$i-1]}" = "f" ];then
			testpattern=${patname[4]}
			echo 1 > /sys/kernel/debug/hvcmd_test/extdat
			cat testfile_4k_all_f.txt > /sys/kernel/debug/hvcmd_test/4k_file_out
		elif [ "${pat[$i-1]}" = "5" ];then
			testpattern=${patname[5]}
			echo 1 > /sys/kernel/debug/hvcmd_test/extdat
			cat testfile_4k_all_5.txt > /sys/kernel/debug/hvcmd_test/4k_file_out
		elif [ "${pat[$i-1]}" = "a" ];then
			testpattern=${patname[6]}
			echo 1 > /sys/kernel/debug/hvcmd_test/extdat
			cat testfile_4k_all_a.txt > /sys/kernel/debug/hvcmd_test/4k_file_out
		else
		#	printf "\n Undefined Pattern"
			break
		fi

		# Init buffer address
		((fake_read_buffer_pa=FAKE_READ_BUFFER_PA))
		((fake_write_buffer_pa=FAKE_READ_BUFFER_PA+MAX_BUFF_SIZE))

		printf "\n%s loop=$c, Pattern: $testpattern, FakeRD: 0x%X, FakeWR: 0x%X %s\n" \
				$P_BOLD_GRAY $fake_read_buffer_pa $fake_write_buffer_pa $P_NC

	  if [ "$do_fr_test" = 1 ];then
		printf "\n"

		if [ "$send_fpga_reset_reg_0x68" = "1" ]; then
			reset_fpga_reg_0x68
		fi
	
		page=1;	pass_count=0; fail_count=0

		#printf "   Page  Src Buffer     TBM Address(LBA)\n\n"	
		for ((ms=0; ms<$max_lba; ms=$ms+512))		#for_0x200
		do

			((tbm_a=0x80000000+ms*8))
			do_bsm_write

			ProgressBar ${page} ${page_count} 
			
			((page++))
			((fake_read_buffer_pa+=0x1000))

		done
		
		printf "\n   Finished BSM Write(FakeRD) & Checking TBM Result   ... " 

		if [[ ("${pat[$i-1]}" = "5") || ("${pat[$i-1]}" = "a") || ("${pat[$i-1]}" = "0") || \
			  ("${pat[$i-1]}" = "f") || ("${pat[$i-1]}" = "i1") ]];then
			ts0=$(date +%s%N | cut -b1-13)
			get_tbm_result
			ts1=$(date +%s%N | cut -b1-13)
			echo "   TBM Checking Time: $((ts1-ts0)) ms"
		else
			printf "${P_BOLD_GRAY}Skip${P_NC}\n"
		fi
	  fi
	  if [ "$do_fw_test" = 1 ];then

		if [ "$send_fpga_reset_reg_0x68" = "1" ]; then
			reset_fpga_reg_0x68
		fi
		# Init buffer address
		((fake_read_buffer_pa=FAKE_READ_BUFFER_PA))
		((fake_write_buffer_pa=FAKE_READ_BUFFER_PA+MAX_BUFF_SIZE))
		page=1;	pass_count=0; fail_count=0

		printf "\n   Page  Buffer(Dst)    TBM Address(LBA)     Buffer(Src)  RESULT\n\n"
		for ((ms=0; ms<$max_lba; ms=$ms+512))		#for_0x200
		do
		#	if [ $((page%4096)) -eq 1 ]; then
		#		reset_fpga_reg_0x68	
		#	fi
			((tbm_a=0x80000000+ms*8))
			do_bsm_read

			result_str=$( cat /sys/kernel/debug/hvcmd_test/wr_cmp )
			result=${result_str:0:4}
			error_count=${result_str:5:3}

		if [ $result = "PASS" ]; then
			printf "%s %6d  0x%08x <- 0x%08x(%08x) 0x%08x  %s%s %s\n" \
					$P_UP_ONE_LINE $page $fake_write_buffer_pa $tbm_a $ms \
					$fake_read_buffer_pa $P_LIGHT_GREEN $result $P_NC
			((pass_count++))
		elif [ $result = "FAIL" ]; then
			printf "%s%s %6d  0x%08x <- 0x%08x(%08x) 0x%08x  #%s   %s" \
					$P_UP_ONE_LINE $P_LIGHT_RED $page $fake_write_buffer_pa $tbm_a $ms \
					$fake_read_buffer_pa $error_count $P_NC
			print_failed_wr_cmp 
			((fail_count++))
			if [ "$FAILSTOP" = "YES" ];then
		#		LA_trigger_act_0000_rd	
				exit 1
			fi
		fi
			((page++))
			((fake_write_buffer_pa+=0x1000))
			((fake_read_buffer_pa+=0x1000))
		done

		if [ $fail_count -eq 0 ]; then
			((loop_pass_count++))
		else
			((loop_fail_count++))
		fi

	#	printf "\n   Finished BSM Read(FakeWR) %d Pages \n" $page
		printf "\n$P_LIGHT_BLUE   # of pass = %6d, # of loop_pass = %6d$P_NC" $pass_count $loop_pass_count
		printf "\n$P_LIGHT_BLUE   # of fail = %6d, # of loop_fail = %6d$P_NC" $fail_count $loop_fail_count
	#	printf "\n$P_LIGHT_BLUE   # of runs = %6d$P_NC" $((pass_count+fail_count))
	  fi

	done
	loop_ts1=$(date +%s%N | cut -b1-13)
	printf "\n   Loop Test Time: $((loop_ts1-loop_ts0)) ms, excepts TBM: $((loop_ts1-loop_ts0-(ts1-ts0)))ms"
	printf ", Total Test time:  $((loop_ts1-script_ts0)) ms\n"
#	echo
#	read -n 1 -s -p "  Press any key to continue"	
done

echo


