#!/bin/bash
echo 0x0C > /sys/kernel/debug/hvcmd_test/debug_feat
echo 0x2e0000 > /sys/kernel/debug/hvcmd_test/debug_feat_bsm_wrt
echo 0x2a0000 > /sys/kernel/debug/hvcmd_test/debug_feat_bsm_rd
echo 700 > /sys/kernel/debug/hvcmd_test/bsm_wrt_delay_before_qc
echo 700 > /sys/kernel/debug/hvcmd_test/bsm_rd_delay_before_qc
echo 1 > /sys/kernel/debug/hvcmd_test/debug_feat_enable                
