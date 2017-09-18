#!/bin/bash
echo "disable=$1" >| /proc/mtrr
cat /proc/mtrr
