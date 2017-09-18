#!/bin/bash


REG=$(cat /proc/mtrr | grep 0x678f00000 | awk '{printf $1}' | cut -d ':' -f -1 | cut -d 'g' -f 2)
if [ "$REG" > "0" ]; then
echo "disable REG" $REG
echo "disable=$REG" >| /proc/mtrr
fi

REG=$(cat /proc/mtrr | grep 0x679000000 | awk '{printf $1}' | cut -d ':' -f -1 | cut -d 'g' -f 2)
if [ "$REG" > "0" ]; then
echo "disable REG" $REG
echo "disable=$REG" >| /proc/mtrr
fi

cat /proc/mtrr
