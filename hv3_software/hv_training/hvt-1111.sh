#!/bin/bash

#
# hvt.sh <dimm> <options>
#
SW_VERSION=1111

# Check for arguments
if [ $# -gt 0 ]; then
#    echo "Your command line contains $# arguments"
    ARGUMENT="$#"
    if [ $# = 1 ]; then
		DIMM=$1
		if [ $DIMM -gt 23 ]; then
			echo 'DIMM ID Selection ERROR - Max. DIMM ID is 23'
			exit -1
	#	else
	#		echo 'DIMM = ' $OPTION
		fi
    fi
else
    echo "Usage: hvt.sh <dimm#>"
    exit -1
fi

echo "*** DIMM[$DIMM] - RESET PLL *********************************************"
test$SW_VERSION -d $DIMM -pll

echo "*** DIMM[$DIMM] - SET FPGA1 RdWr CLK latency(Reg 0x7) to 0x43"
test$SW_VERSION -d $DIMM -5 7 0x43

echo "*** DIMM[$DIMM] - TxDQDQS1 Training *************************************"
test$SW_VERSION -d $DIMM -t 0x400 0 256 8

# echo $? # exit(-1) ???

if [ "$?" = "1" ]; then
	echo $'Failed training \n'

	echo "*** DIMM[$DIMM] - Retest TxDQDQS1 Training with Slave I/O CLK Delay setting 0"
	test$SW_VERSION -d $DIMM -t 0x400 0 256 8 -dly 0
	
	echo "*** DIMM[$DIMM] - Retest TxDQDQS1 Training with Slave I/O CLK Delay setting 1"
	test$SW_VERSION -d $DIMM -t 0x400 0 256 8 -dly 1

	if [ "$?" = "1" ]; then
		echo $'Failed training w delay 1\n'
		echo "*** DIMM[$DIMM] - Retest TxDQDQS1 Training with Slave I/O CLK Delay setting 2"
		test$SW_VERSION -d $DIMM -t 0x400 0 256 8 -dly 2

		if [ "$?" = "1" ]; then
			echo $'Failed training w delay 2 then Stop training !!!\n'
			exit -1
		else
			echo $'Pass!!\n'
		fi
	else
		echo $'Pass!!\n'
	fi
else
	echo $'Pass!!\n\n'
fi
 
echo "*** DIMM[$DIMM] - RxDQDQS1 Training w Update Result *********************"
test$SW_VERSION -d $DIMM -t 0x800 0 200 4 -u
if [ "$?" = "1" ]; then
	echo $'Failed training then Stop training !!!\n'
	exit -1
fi

echo "*** DIMM[$DIMM] - TxDQDQS1 Training w Update Result *********************"
test$SW_VERSION -d $DIMM -t 0x400 0 256 4 -u
if [ "$?" = "1" ]; then
	echo $'Failed training then Stop training !!!\n'
	exit -1
fi

echo "*** DIMM[$DIMM] - Send 64B Data to mmioCMD window ***********************"
test$SW_VERSION -d $DIMM -T

if [ "$?" = "1" ]; then
	for TARGET in 2 3 4 5 6 7 8
	do
	echo "*** DIMM[$DIMM] - Find data " $TARGET "in mmioCMD then Set dq_mux_cyc Reg[11:8] "
	test$SW_VERSION -d $DIMM -M $TARGET $(($TARGET-1)) -u
	if [ "$?" = "0" ]; then
		break
	fi
	done
fi

echo "*** DIMM[$DIMM] - Enable BCOM Control Block *****************************"
test$SW_VERSION -d $DIMM -E

echo "*** DIMM[$DIMM] - TxDQDQS2(FakeWR) training *****************************"
test$SW_VERSION -d $DIMM -t 16 0 60 2 -D 2 -u

echo "*** DIMM[$DIMM] - Check General Write Status ****************************"
test$SW_VERSION -d $DIMM -FW 20

echo "*** DIMM[$DIMM] - Set Reg 0x7d to 0x40 (Master FPGA only) ***************"
test$SW_VERSION -d $DIMM -5 0x7d 0x40

echo "*** DIMM[$DIMM] - Set Reg 0x68 0x80 (Master FPGA only) ******************"
test$SW_VERSION -d $DIMM -5 0x68 0x80

echo "*** DIMM[$DIMM] - Set Reg 0x68 0x00 (Master FPGA only) ******************"
test$SW_VERSION -d $DIMM -5 0x68 0x0

