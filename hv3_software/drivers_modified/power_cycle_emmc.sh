#!/bin/bash
# -----------------------------------------------------------------------------
# power_cycle_emmc.sh
# CHANGE HISTORY is located at the bottom of this file.
# -----------------------------------------------------------------------------
SCRIPT_VERSION="05-02-2017"

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


# Name of required files for this script
hd_training_prog="test0405"
hd_training_script="hvt-0405.sh"
hd_stress_test_script="hdtest_sj_emmc.sh"
max_loop_log="max_loop.log"
total_loop_log="total.log"
pass_log="pass.log"
hdtest_log="hdtest.log"

usage ()
{
	printf "Usage: $0\n"
    printf "   This script requires the following scripts and executables.\n"
	printf "      * %s (training program)\n" $hd_training_prog
	printf "      * %s (training script)\n" $hd_training_script
	printf "      * %s (stress script)\n" $hd_stress_test_script
	printf "\n"
	exit 1
}


# Check if required scripts and files for this test exist in the current directory
if [ ! -e $hd_training_prog ]; then
	printf "$P_LIGHT_RED%s not found$P_NC\n" $hd_training_prog
	usage
fi

if [ ! -e $hd_training_script ]; then
	printf "$P_LIGHT_RED%s not found$P_NC\n" $hd_training_script
	usage
fi

if [ ! -e $hd_stress_test_script ]; then
	printf "$P_LIGHT_RED%s not found$P_NC\n" $hd_stress_test_script
	usage
fi

# Cerate a file with the number of total loops and initialize it to zero
if [ ! -e $total_loop_log ]; then
	echo 0 > $total_loop_log
fi

# Cerate a file with the number of passes and initialize it to zero
if [ ! -e $pass_log ]; then
	echo 0 > $pass_log
fi

# This file should exist before running this script. Otherwise, set to 1
if [ ! -e $max_loop_log ]; then
	typeset -i max_loop_count=1
else
	typeset -i max_loop_count=$( cat $max_loop_log )
fi

# Get counter values from files
typeset -i total_loop_count=$( cat $total_loop_log )
typeset -i pass_count=$( cat $pass_log )

# In any case, the mex count should not be zero
if [ $max_loop_count -eq 0 ]; then
	printf "$P_LIGHT_RED[ERROR] max_loop_count = %d$P_NC\n" $max_loop_count
	exit 1
fi

# Run HybriDIMM training SW
./$hd_training_script 3 reload
./$hd_training_prog -d 3 -f 0x0e 0
##printf "%s is running\n" $hd_training_script

# Run stress test
if [ $total_loop_count -eq 0 ];then
	# Just fill up eMMC with test data (BSM_WRITE) and save the data in files
	$hd_stress_test_script -ps i2 -fs -fc 1 -rc 0 -s 128 m -dbg 0x2200f0 0x2600f0 -pp -fr

	# Get the return code from the test script
	rc=$?
else
	# Load fake-read buffer from data files and do BSM_READ
	$hd_stress_test_script -ps i2 -fs -fc 1 -rc 0 -s 128 m -dbg 0x2200f0 0x2600f0 -pp -lfrb
	rc=$?
	if [ $rc -eq 0 ]; then
		# Fill up eMMC with new test data (BSM_WRITE) and save the new data in files
		$hd_stress_test_script -ps i2 -fs -fc 1 -rc 0 -s 128 m -dbg 0x2200f0 0x2600f0 -pp -fr

		# Get the return code from the test script
		rc=$?
	fi
fi

# Check the result from the stress test
if [ $rc -eq 0 ]; then
	if [ $total_loop_count -gt 0 ];then
		# Bump up pass count
		((pass_count++))
		echo $pass_count > $pass_log
		printf "$P_LIGHT_CYANpass_count = %d$P_NC\n" $pass_count
	fi

	# Log time stamp and total_loop_count
	echo "$(date) - pass_count = %d, $total_loop_count, $max_loop_count" >> $hdtest_log
##	cat $hdtest_log

	if [ $total_loop_count -lt $max_loop_count ]; then
		# Bump up the total reboots
		((total_loop_count++))
		echo $total_loop_count > $total_loop_log
		printf "$P_LIGHT_CYANtotal_loop_count = %d$P_NC\n" $total_loop_count

		printf "System will reboot after 3 sec, max. loop= %d\n" $max_loop_count
		sleep 3
		reboot
	else
		printf "[TEST DONE]\n"
		printf "$P_LIGHT_CYANmax_loop_count = %d, total_loop_count = %d, pass_count = %d$P_NC\n" $max_loop_count $total_loop_count $pass_count
	fi
elif [ $rc -eq 2 ]; then
	# Error!!
	printf "$P_LIGHT_RED[TEST FAILED (%d)]$P_NC\n" $rc
	printf "$P_LIGHT_REDmax_loop_count = %d, total_loop_count = %d, pass_count = %d$P_NC\n" $max_loop_count $total_loop_count $pass_count
else
	printf "unexpected error code (%d) from %s\n" $rc $hd_stress_test_script
	exit 1
fi

# -----------------------------------------------------------------------------
# [[ CHANGE HISTORY ]]
# power_cycle_emmc.sh
#
# 05/02/2017	rev 1.0
#
# -----------------------------------------------------------------------------