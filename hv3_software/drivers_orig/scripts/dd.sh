bsm=/dev/hv_bsm0
mmls=/dev/hv_mmls0
for ((i=1; i<=1; i++));
do
	echo "Test Loop" $i
	if [ -e $bsm ]
	then
		echo "BSM Write"
		sudo dd if=/dev/zero of=$bsm bs=64k count=20k
		echo "BSM Read"
		sudo dd of=/dev/null if=$bsm bs=64k count=20k
	else
		echo "No BSM block device found!!!"
	fi
	if [ -e $mmls ]
	then
		echo "MMLS Write"
		sudo dd if=/dev/zero of=$mmls bs=64k count=20k
		echo "MMLS Read"
		sudo dd of=/dev/null if=$mmls bs=64k count=20k
	else
		echo "No MMLS block device found!!!"
	fi
done

