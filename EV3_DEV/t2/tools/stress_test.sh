#!/bin/bash

loop=${1-1}

for ((i=1; i<=$loop;i++))
do

	echo iteration : $i
	echo ""
	echo -e -n "Num_Vec" "Vec_Size" "\t" "Write(msec)" "\t" "Read(msec)"

	./test_patterns.sh 32 512
	./test_patterns.sh 32 1024
	./test_patterns.sh 32 2048
	./test_patterns.sh 32 4096
	./test_patterns.sh 64 512
	./test_patterns.sh 64 1024
	./test_patterns.sh 64 2048
	./test_patterns.sh 64 4096
	./test_patterns.sh 128 512
	./test_patterns.sh 128 1024
	./test_patterns.sh 128 2048
	./test_patterns.sh 128 4096
	./test_patterns.sh 256 512
	./test_patterns.sh 256 1024
	./test_patterns.sh 256 2048
	./test_patterns.sh 256 4096
	./test_patterns.sh 384 512
	./test_patterns.sh 384 1024
	./test_patterns.sh 384 2048
	./test_patterns.sh 384 4096
	./test_patterns.sh 512 512
	./test_patterns.sh 512 1024
	./test_patterns.sh 512 2048
	./test_patterns.sh 512 4096
	./test_patterns.sh 640 512
	./test_patterns.sh 640 1024
	./test_patterns.sh 640 2048
	./test_patterns.sh 640 4096
	./test_patterns.sh 768 512
	./test_patterns.sh 768 1024
	./test_patterns.sh 768 2048
	./test_patterns.sh 768 4096
	./test_patterns.sh 896 512
	./test_patterns.sh 896 1024
	./test_patterns.sh 896 2048
	./test_patterns.sh 896 4096
	./test_patterns.sh 1024 512
	./test_patterns.sh 1024 1024
	./test_patterns.sh 1024 2048
	./test_patterns.sh 1024 4096

	echo ""
done
