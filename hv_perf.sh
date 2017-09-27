#!/bin/bash

signal=KILL

FILE_NAME="/home/lab/Tools/vdbench/run_vdbench"
NUM_THREADS=1
READ_RATIO=100
ELAPSED_TIME=120

pushd /home/lab/Tools/hv3_software/drivers
sh scripts/insmod.sh
popd

pushd /home/lab/Tools/hv3_software/hv_perf_multi
rmmod hvperf.ko
./ignore.sh
insmod hvperf.ko
popd

pushd /home/lab/work/IntelPerformanceCounterMonitor
gnome-terminal -e "pcm-memory.x 1 -csv=../mem_traffic.txt" --title "INTEL Performance Counter Monitor" --geometry=80x24+100+100
popd

pushd /home/lab/Tools/vdbench

rm test_log.txt
gnome-terminal --title "vdbench" --geometry=80x24+100+100 -x bash -c "vdbench -m 2 -f read_traffic >> test_log.txt"   
popd

gnome-terminal -e "python perf_viewer.py" --title "Netlist Performance Viewer" --geometry 100x24+910+0

pushd /sys/kernel/debug
echo 1 > start_HV_IO
popd

pushd /home/lab/Tools/vdbench 
while : 
do 
	read -p "Enter # of threads used by vdbench:" th_number
	NUM_THREADS=$th_number
	echo "NUM_TREADS is: " $NUM_THREADS
       
       	SD="sd=sd1,lun=/dev/pmem0,openflags=o_direct"
       	WD="wd=wd1,sd=sd1,xfersize=128k,rdpct=$READ_RATIO,seekpct=random"
       	RD="rd=run1,wd=wd1,iorate=max,elapsed=$ELAPSED_TIME,interval=1,threads=$NUM_THREADS"

	echo $SD > $FILE_NAME
	echo $WD >> $FILE_NAME
	echo $RD >> $FILE_NAME
	
	ps -ef | grep java | grep -v grep | awk '{print $2}' | xargs kill -9
       	gnome-terminal --title "vdbench" --geometry=80x24+100+100 -x bash -c "vdbench -m 2 -f run_vdbench >> test_log.txt"   
done
popd 
