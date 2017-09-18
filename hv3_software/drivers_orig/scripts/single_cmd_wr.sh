# sh single_cmd_wr.sh arg1
# arg1: 0 => no bsm data comparison
#	1 => compare bsm w/r data

sh insmod.sh
echo 1 >/sys/kernel/debug/hvcmd_test/tag
echo 8 >/sys/kernel/debug/hvcmd_test/sector
echo 4 >/sys/kernel/debug/hvcmd_test/LBA
echo 0 >/sys/kernel/debug/hvcmd_test/latency_mode

#declare -i yy

echo "Test bsm: size = 4k ..."
echo 8 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_write
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
#	printf "cmp_result=%d\n" $yy
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test bsm: size = 8k ..."
echo 16 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_write
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test bsm: size = 16k ..."
echo 32 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_write
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test bsm: size = 32k ..."
echo 64 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_write
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test bsm: size = 1M ..."
echo 2000 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_write
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test bsm: size = 2M ..."
echo 4000 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_write
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test bsm: size = 4M ..."
echo 8000 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_write
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test bsm: size = 8M ..."
echo 16000 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_write
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test bsm: size = 16M ..."
echo 32000 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_write
echo 1 >/sys/kernel/debug/hvcmd_test/bsm_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

#
# Test MMLS ...
#
echo
echo Test MMLS ...
echo 
#sh insmod.sh
echo 1 >/sys/kernel/debug/hvcmd_test/tag
echo 4 >/sys/kernel/debug/hvcmd_test/LBA
echo 0 >/sys/kernel/debug/hvcmd_test/latency_mode

echo "Test mmls: size = 4k ..."
echo 8 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_write
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	echo "compare result"
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	printf "cmp_result=%d\n" $yy
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test mmls: size = 8k ..."
echo 16 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_write
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test mmls: size = 16k ..."
echo 32 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_write
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test mmls: size = 32k ..."
echo 64 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_write
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test mmls: size = 1M ..."
echo 2000 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_write
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test mmls: size = 2M ..."
echo 4000 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_write
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test mmls: size = 4M ..."
echo 8000 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_write
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test mmls: size = 8M ..."
echo 16000 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_write
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi

echo "Test mmls: size = 16M ..."
echo 32000 >/sys/kernel/debug/hvcmd_test/sector
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_write
echo 1 >/sys/kernel/debug/hvcmd_test/mmls_read
echo 1 >/sys/kernel/debug/hvcmd_test/wr_cmp

if [ "$1" = 1 ]
then
	yy=$(cat /sys/kernel/debug/hvcmd_test/cmp_result )
	if [ "$yy" = "1" ] ; then 
		echo Same...
	else
		echo Mismatch!!!
	fi
else
echo "... "
fi


