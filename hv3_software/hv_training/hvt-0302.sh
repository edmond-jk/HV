#!/bin/bash

RED='\033[0;31m'
NC='\033[0m' # No Color

#
# hvt.sh <dimm> <options>
#
SW_VERSION=0302
OPTION='NO'
REG07=0x53

DEBUG_RxDQDQS1="0"

# Check for arguments
if [ $# = 1 ]; then
	DIMM=$1
	if [ $DIMM -gt 23 ]; then
		echo 'DIMM ID Selection ERROR - Max. DIMM ID is 23'
		exit -1
	fi
elif [ $# = 2 ]; then
	DIMM=$1
	OPTION=$2
	if [ $DIMM -gt 23 ]; then
		echo 'DIMM ID Selection ERROR - Max. DIMM ID is 23'
		exit -1
	fi
	if [[ ("$OPTION" != "bcom") && ("$OPTION" != "retest") ]]; then
#		echo 'OPTION '"'$OPTION'"' is not ready !!!'
#		exit -1
		REG07=$2
	fi	
else
    echo "Usage: hvt.sh <dimm#>"
    echo "Usage: hvt.sh <dimm#> bcom   <= Stop training before Enabling BCOM"
    echo "Usage: hvt.sh <dimm#> retest <= Skip pll reset required after reloading FPGA image"
    exit -1
fi

echo "*** Set WriteCombining Cache ******************************************************"

MTRR=$(cat /proc/mtrr | grep -c 0x67fe00000)
if [ "$MTRR" = "0" ]; then
	echo "base=0x67fe00000 size=0x100000 type=write-combining" >| /proc/mtrr
fi
MTRR=$(cat /proc/mtrr | grep -c 0x67ff00000)
if [ "$MTRR" = "0" ]; then
	echo "base=0x67ff00000 size=0x100000 type=write-combining" >| /proc/mtrr
fi
MTRR=$(cat /proc/mtrr | grep -c 0x678f00000)
if [ "$MTRR" = "0" ]; then
	echo "base=0x678f00000 size=0x100000 type=write-combining" >| /proc/mtrr
fi
MTRR=$(cat /proc/mtrr | grep -c 0x679000000)
if [ "$MTRR" = "0" ]; then
	echo "base=0x679000000 size=0x100000 type=write-combining" >| /proc/mtrr
fi

cat /proc/mtrr

echo "*** DIMM[$DIMM] - RESET PLL *******************************************************"
if [ "$OPTION" = "retest" ]; then
	echo " Skip pll reset"
else
./test$SW_VERSION -d $DIMM -pll
fi

echo "*** DIMM[$DIMM] - SET Both FPGA RdWr CLK latency(Reg 0x7) to" $REG07 "*****************"
./test$SW_VERSION -d $DIMM -f 7 $REG07

echo "*** DIMM[$DIMM] - TxDQDQS1 Training ***********************************************"
./test$SW_VERSION -d $DIMM -t 0x400 0 256 8

if [ "$?" = "1" ]; then
	echo $'Failed training \n'

	echo "*** DIMM[$DIMM] - RESET Slave I/O CLK Delay setting 0 ****************************"
	./test$SW_VERSION -d $DIMM -t 0x400 0 256 8 -dly 0
	
	echo "*** DIMM[$DIMM] - Retest with Slave I/O CLK Delay setting 1 **********************"
	./test$SW_VERSION -d $DIMM -t 0x400 0 256 8 -dly 1

	if [ "$?" = "1" ]; then
		echo $'Failed training w delay 1\n'
		echo "*** DIMM[$DIMM] - Retest with Slave I/O CLK Delay setting 2 **********************"
		./test$SW_VERSION -d $DIMM -t 0x400 0 256 8 -dly 2
		./test$SW_VERSION -d $DIMM -t 0x400 0 256 8 -dly 2

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

if [ $DEBUG_RxDQDQS1 = "1" ]; then
	echo "*** DIMM[$DIMM] - RxDQDQS1 Training & Update Result *******************************"
	./test$SW_VERSION -d $DIMM -t 0x800 64 256 4 -u
	if [ "$?" = "1" ]; then
		echo $'Failed training then Stop training !!!\n'
		exit -1
	fi
fi

echo "*** DIMM[$DIMM] - TxDQDQS1 Training & Update Result *******************************"
./test$SW_VERSION -d $DIMM -t 0x400 0 256 4 -u
if [ "$?" = "1" ]; then
	echo $'Failed training then Stop training !!!\n'
	exit -1
fi

if [ $DEBUG_RxDQDQS1 = "0" ]; then
	echo "*** DIMM[$DIMM] - RxDQDQS1 Training & Update Result from TxDQDQS1 *****************"
	./test$SW_VERSION -d $DIMM -u2
fi

echo "*** DIMM[$DIMM] - Send 64B Data to mmioCMD & find dq_mux_clk delay ****************"
./test$SW_VERSION -d $DIMM -T

if [ "$?" = "1" ]; then
	for TARGET in `seq 2 8`;
	do
	echo "*** DIMM[$DIMM] - Find data " $TARGET "in mmioCMD then Set dq_mux_cyc Reg[11:8] "
	./test$SW_VERSION -d $DIMM -M $TARGET $(($TARGET-1)) -u
	if [ "$?" = "0" ]; then
		break
	fi
	done
fi

./test$SW_VERSION -d $DIMM -T

if [ "$OPTION" == "bcom" ]; then
	echo -e '*** Training option '"'${RED}$OPTION${NC}'"' used ***'
	echo -e "*** DIMM[$DIMM] - ${RED}Stop Training${NC} ***"
	echo ""
	echo "*** DIMM[$DIMM] - Set Reg 14 0x80 (Master FPGA only) ******************************"
	./test$SW_VERSION -d $DIMM -5 14 0x80

	echo "*** DIMM[$DIMM] - Set Reg 0x7d to 0x40 (Master FPGA only) *************************"
	./test$SW_VERSION -d $DIMM -f 0x7d 0x40

	echo "*** DIMM[$DIMM] - Reset Reg 0x68 0x80 (Master FPGA only) **************************"
	./test$SW_VERSION -d $DIMM -R1	
else
	echo "*** DIMM[$DIMM] - Enable BCOM Control Block ***************************************"
	./test$SW_VERSION -d $DIMM -5 6 0
	./test$SW_VERSION -d $DIMM -E

	echo "*** DIMM[$DIMM] - TxDQDQS2(FakeWR) training & update H/W **************************"
#	./test$SW_VERSION -d $DIMM -t 16 0 60 2 -D 1 -u
#	./test$SW_VERSION -d $DIMM -t 16 0 132 2 -D 1 -u	# 1/20/2017
#	./test$SW_VERSION -d $DIMM -t 16 20 132 2 -D 1 -u	# 2/14/2017
	./test$SW_VERSION -d $DIMM -t 16 0 128 2 -D 1 -u	# 3/13/2017, data size 1MB from 4KB

	echo "*** DIMM[$DIMM] - Check General Write Status **************************************"
	./test$SW_VERSION -d $DIMM -FW 20

	echo "*** DIMM[$DIMM] - Set Reg 0x7d to 0x40 (Master FPGA only) *************************"
	./test$SW_VERSION -d $DIMM -f 0x7d 0x40

	echo "*** DIMM[$DIMM] - Reset Reg 0x68 0x80 (Master FPGA only) **************************"
	./test$SW_VERSION -d $DIMM -R1

fi

echo "*** DIMM[$DIMM] - Save Training result to SPD(Mfg part) ***************************"
./test$SW_VERSION -d $DIMM -S2

echo $'*** DOME ***\n'

