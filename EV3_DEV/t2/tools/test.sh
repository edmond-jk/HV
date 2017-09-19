#!/bin/bash

iterations=${1-1}
num_vec=${2-32}
size_in1k=${3-4}
pattern=${4-1}

for ((i=1; i<=$iterations; i++ ))
do
	echo ""
	echo -n -e $num_vec "\t" $size_in1k"K" "\t"
	ev3util /dev/ev3map0 noprompt fill_pattern $pattern $num_vec $size_in1k
	ev3util /dev/ev3map0 noprompt verify_pattern $pattern $num_vec $size_in1k
	let "num += 1"
	if [ $num == 15 ]
	then
		num=0
	fi
done
