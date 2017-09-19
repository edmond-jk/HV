#!/bin/bash

device_letter=${1-"a"}
num_partitions=${2-128}
partition_size=${3-32M}

device="/dev/ev3mem$1"


let "num_nodes = num_partitions + 1"




echo ""
echo -e -n "Creating "  $num_partitions "Partitions, number of nodes = $num_nodes\n"
echo -e -n "Partition size = $partition_size\n"


sgdisk -Z $device

for ((i=1; i<=$num_partitions; i++))
do
echo "Creating partition $i of $num_partitions"

# There is some overhead so the last partition may be of different size

if [ $i == $num_partitions ]
then
sgdisk -n $i:0:0 -t 0:8300 -c 0:"Linux" $device 
else
sgdisk -n 0:0:+$partition_size	-t 0:8300 -c 0:"Linux" $device 
fi
done

sgdisk -p $device
	
# Inform the OS of the partition table changes
partprobe $device
fdisk -l $device

