#!/bin/bash

iterations=${1-1}
echo ""
echo -e -n "Num_Vec" "Vec_Size" "\t" "Write(msec)" "\t" "Read(msec)"

./test.sh $iterations 32 4
./test.sh $iterations 64 4
./test.sh $iterations 128 4
./test.sh $iterations 256 4
./test.sh $iterations 384 4
./test.sh $iterations 512 4
./test.sh $iterations 640 4
./test.sh $iterations 768 4
./test.sh $iterations 896 4
./test.sh $iterations 1024 4
./test.sh $iterations 32 1
./test.sh $iterations 64 1
./test.sh $iterations 128 1
./test.sh $iterations 256 1
./test.sh $iterations 384 1
./test.sh $iterations 512 1
./test.sh $iterations 640 1
./test.sh $iterations 768 1
./test.sh $iterations 896 1
./test.sh $iterations 1024 1
./test.sh $iterations 32 64
./test.sh $iterations 64 64

echo ""
