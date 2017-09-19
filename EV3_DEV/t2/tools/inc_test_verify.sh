#!/bin/bash

loop=${1-1}

for ((i=1; i<=$loop; i++))
do

	echo iteration : $i

	ev3util /dev/ev3map0 noprompt fill_inc_pattern 0 0x40000000 32 1024
	ev3util /dev/ev3map0 noprompt verify_inc_pattern 0 0x40000000 32 1024
	ev3util /dev/ev3map0 noprompt fill_inc_pattern 0 0x40000000 128 1024
	ev3util /dev/ev3map0 noprompt verify_inc_pattern 0 0x40000000 128 1024
	ev3util /dev/ev3map0 noprompt fill_inc_pattern 0 0x40000000 256 1024
	ev3util /dev/ev3map0 noprompt verify_inc_pattern 0 0x40000000 256 1024
	ev3util /dev/ev3map0 noprompt fill_inc_pattern 0 0x40000000 512 1024
	ev3util /dev/ev3map0 noprompt verify_inc_pattern 0 0x40000000 512 1024
	ev3util /dev/ev3map0 noprompt fill_inc_pattern 0 0x40000000 1024 1024
	ev3util /dev/ev3map0 noprompt verify_inc_pattern 0 0x40000000 1024 1024
	
	echo ""
done
