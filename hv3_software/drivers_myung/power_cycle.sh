#!/bin/bash
# -----------------------------------------------------------------------------
# power_cycle.sh
#
# 04/06/2017	rev 1.0
#
# -----------------------------------------------------------------------------
SCRIPT_VERSION="04-06-2017"

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
hd_stress_test_script="hdtest.sh"
result_file="count.log"

usage ()
{
	printf "Usage: power_cycle.sh\n"
    printf "   This script requires the following scripts and executabiles.\n"
	printf "      * %s (training program)\n" $hd_training_prog
	printf "      * %s (training script)\n" $hd_training_script
	printf "      * %s (stress script)\n" $hd_stress_test_script
	printf "\n"
	exit 1
}

# Check if scripts and files required for this test exist in this directory
if [ ! -e $hd_training_prog ]; then
	printf "%s%s not found%s\n" $P_LIGHT_RED $hd_training_prog $P_NC
	usage
fi

if [ ! -e $hd_training_script ]; then
	printf "%s%s not found%s\n" $P_LIGHT_RED $hd_training_script $P_NC
	usage
fi

if [ ! -e $hd_stress_test_script ]; then
	printf "%s%s not found%s\n" $P_LIGHT_RED $hd_stress_test_script $P_NC
	usage
fi

# Cerate result file with counter value, zero
if [ ! -e $result_file ]; then
	echo 0 > $result_file
fi

# Read the pass count from the result file
typeset -i pass_count=$( cat $result_file )

# Run HybriDIMM training SW
$hd_training_script 3 reload
##printf "%s is running\n" $hd_training_script

# Run stress test
$hd_stress_test_script -p i2 -fs -s 4 k -l 1 -pmem
##printf "%s is running\n" $hd_stress_test_script
##./hdtest.sh
rc=$?

# Check the result from the stress test
if [ $rc -eq 0 ]; then
	# Bump up pass count
	((pass_count++))
	echo $pass_count > $result_file
#	reboot
	printf "rebooting...\n"
elif [ $rc -eq 2 ]; then
	# Error!!
	printf "%s%s failed%s\n" $P_LIGHT_RED $hd_stress_test_script $P_NC
	printf "%spass_count = %d%s\n" $P_LIGHT_RED $pass_count $P_NC
else
	printf "invalid error code from %s\n" $hd_stress_test_script
	exit 1
fi

