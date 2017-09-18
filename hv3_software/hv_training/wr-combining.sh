#!/bin/bash
echo "base=$1 size=0x100000 type=write-combining" >| /proc/mtrr
cat /proc/mtrr
