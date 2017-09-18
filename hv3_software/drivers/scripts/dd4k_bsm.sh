# prepare input file to dd
echo $1 | xxd -r -p > dd_infile
for i in {1..511}
do
    echo $1 | xxd -r -p >> dd_infile
done
od -v -t x8 -w8 -An dd_infile > infile
#xxd -g 8 -c 8 -p -E dd_infile > infile

bsm=/dev/hv_bsm0
mmls=/dev/hv_mmls0
for ((i=1; i<=1; i++));
do
	if [ -e $bsm ]
	then
		echo "BSM Write"
		sudo dd if=./dd_infile of=$bsm bs=4k count=1
		echo "BSM Read"
		sudo dd of=./dd_outfile if=$bsm bs=4k count=1
	else
		echo "No BSM block device found!!!"
	fi
done

od -v -t x8 -w8 -An dd_outfile > outfile
#xxd -g 8 -c 8 -p -E dd_outfile > outfile
meld outfile infile
