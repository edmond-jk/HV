#!/bin/bash

RED='\033[0;31m'
NC='\033[0m' # No Color

#
# hvt.sh <dimm> <options>
#
SW_VERSION=0321
OPTION='NO'

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
else
    echo "Usage: hvt.sh <dimm#>"
    exit -1
fi

echo
echo "*** DIMM[$DIMM] - RESET PLL *******************************************************"
if [ "$OPTION" = "reload" ]; then
	echo " Skip pll reset"
else
./test$SW_VERSION -d $DIMM -pll
#sleep 0.1
echo "*** DIMM[$DIMM] - Enable BCOM Control Block ********************************"
./test$SW_VERSION -d $DIMM -5 6 0
./test$SW_VERSION -d $DIMM -E
#sleep 0.1

echo "*** DIMM[$DIMM] - Set Reg 0x7d to 0x40 (Both FPGA) *************************"
./test$SW_VERSION -d $DIMM -f 0x7d 0x40
#sleep 0.1

fi

echo "*** DIMM[$DIMM] - Load Training data from SPD ******************************"
./test$SW_VERSION -d $DIMM -L2

#echo "*** DIMM[$DIMM] - Reset Reg 0x68 0x80 (Both FPGA) **************************"
#./test$SW_VERSION -d $DIMM -R1

echo $'*** DOME ***\n'

