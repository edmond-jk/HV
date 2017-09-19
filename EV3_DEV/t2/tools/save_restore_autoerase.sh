#!/bin/bash

iterations=${1-2}
num=0
num_org=0
autoerase=${2-0}

function show_pass_fail()
{
	result=$?
	if [ $result == 0 ]
	then 
		echo "PASSED"
	else
	if [ $result == 1 ]
	then 
		echo "FAILED"
	else
	if [ $result == 2 ]
	then 
		echo "COULD NOT EXECUTE"
	else
	if [ $result == 3 ]
	then 
		echo "INVALID DEVICE"
	else
	if [ $result == 4 ]
	then 
		echo "INVALID COMMAND"
	fi
	fi
	fi
	fi
	fi

# Exit if failed
	if [ $result != 0 ]
	then 
		echo "Exiting test script due to failure, code is $result"
		exit $?
	fi

}

if [ $autoerase == 0 ]
then
	evutil /dev/evmap0 noprompt autoerase 0
	show_pass_fail
else
	evutil /dev/evmap0 noprompt autoerase 1
	show_pass_fail
fi

for ((i=1; i<=$iterations; i++ ))
do
	echo "iteration $i of $iterations" 
	if [ $autoerase == 0 ]
	then
		evutil /dev/evmap0 noprompt erase_flash_complete
		show_pass_fail
	fi
	evutil /dev/evmap0 noprompt autoerase $autoerase
	show_pass_fail
	evutil /dev/evmap0 noprompt fill_pattern $num
	show_pass_fail
	evutil /dev/evmap0 noprompt verify_pattern $num
	show_pass_fail
	evutil /dev/evmap0 noprompt force_save_complete
	show_pass_fail
	num_org=$num
	let "num += 1"
	if [ $num == 15 ]
	then
		num=0
	fi
	evutil /dev/evmap0 noprompt verify_pattern $num_org
	show_pass_fail
	evutil /dev/evmap0 noprompt fill_pattern $num
	show_pass_fail
	evutil /dev/evmap0 noprompt verify_pattern $num
	show_pass_fail
	evutil /dev/evmap0 noprompt force_restore_complete
	show_pass_fail
	evutil /dev/evmap0 noprompt verify_pattern $num_org
	show_pass_fail
done
