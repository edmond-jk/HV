
# BSM size in sectors
echo 0x80000 > /sys/kernel/debug/hvcmd_test/bsm_stress_size

# MMLS size in sectors
echo 0x0 > /sys/kernel/debug/hvcmd_test/mmls_stress_size

# 0 : both sequential and random test  
# 1 : sequential test only 
echo 1 > /sys/kernel/debug/hvcmd_test/stress_type

# block size in byte
echo 16000 > /sys/kernel/debug/hvcmd_test/stress_block_size

# perform data integrity check
# 0 : no check, for measuring throughput
# 1 : check
echo 0 > /sys/kernel/debug/hvcmd_test/stress_data_compare

# start the test
echo 1 > /sys/kernel/debug/hvcmd_test/hv_stress

# BSM size in sectors
echo 0x0 > /sys/kernel/debug/hvcmd_test/bsm_stress_size

# MMLS size in sectors
echo 0x80000 > /sys/kernel/debug/hvcmd_test/mmls_stress_size

# start the test
echo 1 > /sys/kernel/debug/hvcmd_test/hv_stress

dmesg | tail -n 20 | grep sequential
dmesg | tail -n 20 | grep random
dmesg | tail -n 20 | grep passed
dmesg | tail -n 20 | grep failed
