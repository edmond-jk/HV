#!/bin/bash

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

