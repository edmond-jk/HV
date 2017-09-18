
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2015 Netlist Inc.                                    *
 *    All rights reserved.                                               *
 *                                                                       *
 *    This program is free software; you can redistribute it and/or      *
 *    modify it under the terms of the GNU General Public License        *
 *    as published by the Free Software Foundation; either version 2     *
 *    of the License, or (at your option) any later version located at   *
 *    <http://www.gnu.org/licenses/                                      *
 *                                                                       *
 *    This program is distributed WITHOUT ANY WARRANTY; without even     *
 *    the implied warranty of MERCHANTABILITY or FITNESS FOR A           *
 *    PARTICULAR PURPOSE.  See the GNU General Public License for        *
 *    more details.                                                      *
 *                                                                       *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#ifndef SIMULATION_TB
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/io.h>
//MK-begin
#include <linux/delay.h>
#include <linux/pci.h>
//MK-end
#include "hv_params.h"
#else
#include <stdio.h>
#include <string.h>
#ifdef USER_SPACE_CMD_DRIVER
#include <errno.h>
#include <sys/mman.h>
#endif
#endif	// SIMULATION_TB

#include "hv_mmio.h"
#include "hv_cmd.h"

#ifdef MMIO_LOGGING
	#ifndef SIMULATION_TB
	#define pr_mmio(fmt, ...) printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
	#else
	#define pr_mmio printf
	#endif
#else
	#define pr_mmio(fmt, ...) do { /* nothing */ } while (0)
#endif

#ifdef NO_CACHE_FLUSH_PERIOD
#undef clflush_cache_range
#define clflush_cache_range(fmt, ...) do { /* nothing */ } while (0)
#endif

//MK-begin
#define  SMB_TIMEOUT    100000000	// 100 ms
//MK-end

/* remaining FPGA buffer before wrap-around for BSM or MMLS 1-way interleaving */
#define remain_sz(index)	((DATA_BUFFER_NUM-index)*DATA_BUFFER_SIZE)

static unsigned char fake_mmls_buf[4096*16]; 	/* 64 KB */
static unsigned char fake_data_buf[4096*16];    /* 64 KB for hv cmd test */
static unsigned char cmd_burst_buf[CMD_BUFFER_SIZE*4];
//MK0307-begin
static unsigned char status_buf[STATUS_BUFFER_SIZE];
//MK0307-end

/* force group ID to 0 since we don't support multiple group */
static unsigned int gid=0;	

/* cache flush control */
#ifndef REV_B_MM
static unsigned int cmd_status_use_cache = 0;
static unsigned int bsm_read_use_cache = 1;
static unsigned int bsm_write_use_cache = 0;
#else  // REV_B_MM
#ifndef SIMULATION_TB
// kernel module cmd driver configures HV command/status/DRAM space cacheable
static unsigned int cmd_status_use_cache = 0;		//MK1103 1
//MK-begin
//static unsigned int cmd_status_use_cache = 0;
//MK-end
#else
// user-space cmd driver configures HV cmd/status/DRAM space non-cacheable 
static unsigned int cmd_status_use_cache = 0;
#endif
#endif  // REV_B_MM
//MKstatic unsigned int fake_read_cache_flush = 0;
//MKstatic unsigned int fake_write_cache_flush = 0;
//MK-begin
static unsigned int fake_read_cache_flush = 0;		//MK1103 1
static unsigned int fake_write_cache_flush = 0;		//MK1103 1
//MK-end

//MK-begin
/*
 * spd
 * dimm id 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
 *
 * Node:   0  0  0  0  0  0  0  0  0  0  0  0  1  1  1  1  1  1  1  1  1  1  1  1 -- hv_training
 * Channel:0  0  0  1  1  1  2  2  2  3  3  3  0  0  0  1  1  1  2  2  2  3  3  3 -- hv_training
 * Channel:0  0  0  1  1  1  4  4  4  5  5  5  2  2  2  3  3  3  6  6  6  7  7  7 -- Linux driver
 * Dimm:   0  1  2  0  1  2  0  1  2  0  1  2  0  1  2  0  1  2  0  1  2  0  1  2
 * PCIdev#:19 19 19 19 19 19 22 22 22 22 22 22 19 19 19 19 19 19 22 22 22 22 22 22
 * PCIfn#: 2  2  2  3  3  3  2  2  2  3  3  3  4  4  4  5  5  5  4  4  4  5  5  5
 */

/* Intel SDP S2600WTT - this table also works for Supermicro, Lenovo servers */
unsigned int SMBsad[24] = {0, 1, 2, 4, 5, 6, 0, 1, 2, 4, 5, 6, 0, 1, 2, 4, 5, 6, 0, 1, 2, 4, 5, 6};
unsigned int SMBctl[24] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
unsigned int SMBdev[24] = {19,19,19,19,19,19,22,22,22,22,22,22,19,19,19,19,19,19,22,22,22,22,22,22};
//MK-end

//MK0209-begin
static unsigned char command_tag=0;
//MK0209-end

//MK0223-begin
/* Debug Feat flags */
static unsigned char df_bcom_ctrl_method=1;						// [0]: 0=MMIO, 1=I2C
//MK0302-begin
static unsigned char df_bcom_toggle_enable=1;					// [1]: 0=no toggle, 1=toggle
//MK0302-end
//MK0307-begin
static unsigned char df_slave_data_cs_enable=1;					// [2]: 0=disable, 1=enable
static unsigned char df_slave_cmd_done_check_enable=1;			// [3]: 0=disable, 1=enable
static unsigned char df_fpga_data_cs_location=0;				// [4]: 0=MMIO, 1=I2C
static unsigned char df_fpga_popcnt_location=0;					// [5]: 0=MMIO, 1=I2C
//MK0307-end
//MK0321-begin
static unsigned char df_fpga_reset_ctrl_method=0;				// [6]: 0=MMIO, 1=I2C
//MK0321-end

/* Debug Feat flags for BSM_WRT */
static unsigned char df_bsm_wrt_cmd_cs_max_retry_count=0;		// [2:0]
static unsigned char df_bsm_wrt_cmd_cs_retry_enable=0;			// [3]
//MK0605static unsigned char df_bsm_wrt_data_cs_max_retry_count=0;		// [6:4]
//MK0605static unsigned char df_bsm_wrt_data_cs_retry_enable=0;			// [7]
//MK0605-begin
static unsigned char df_bsm_wrt_data_cs_max_retry_count=7;		// [6:4]
static unsigned char df_bsm_wrt_data_cs_retry_enable=1;			// [7]
//MK0605-end
static unsigned char df_bsm_wrt_fr_max_retry_count=0;			// [10:8]
static unsigned char df_bsm_wrt_fr_retry_enable=0;				// [11]
//MK0605static unsigned char df_bsm_wrt_qc_max_retry_count=7;			// [14:12]
//MK0605static unsigned char df_bsm_wrt_qc_retry_enable=1;				// [15]
//MK0605-begin
static unsigned char df_bsm_wrt_qc_max_retry_count=0;			// [14:12]
static unsigned char df_bsm_wrt_qc_retry_enable=0;				// [15]
//MK0605-end
static unsigned char df_bsm_wrt_skip_query_command_enable=0;	// [16]
static unsigned char df_bsm_wrt_skip_gws_enable=1;				// [17]
//MK0301static unsigned char df_bsm_wrt_skip_fr_on_lba_enable=0;
//MK0301-begin
//MK0518static unsigned char df_bsm_wrt_skip_termination_enable=0;		// [18]
static unsigned char df_bsm_wrt_skip_termination_enable=1;		// [18]	//MK0518
static unsigned char df_bsm_wrt_send_dummy_command_enable=0;	// [19]
static unsigned char df_bsm_wrt_do_dummy_read_enable=0;			// [20]
//MK0301-end
//MK0307-begin
static unsigned char df_bsm_wrt_popcnt_enable=1;				// [21]
//MK0307-end

/* Debug Feat flags for BSM_RD */
static unsigned char df_bsm_rd_cmd_cs_max_retry_count=0;		// [2:0]
static unsigned char df_bsm_rd_cmd_cs_retry_enable=0;			// [3]
//Checksum control
static unsigned char df_bsm_rd_data_cs_max_retry_count=7;		// [6:4]
static unsigned char df_bsm_rd_data_cs_retry_enable=1;			// [7]
static unsigned char df_bsm_rd_fw_max_retry_count=0;			// [10:8]
static unsigned char df_bsm_rd_fw_retry_enable=0;				// [11]
static unsigned char df_bsm_rd_qc_max_retry_count=7;			// [14:12]
static unsigned char df_bsm_rd_qc_retry_enable=1;				// [15]
static unsigned char df_bsm_rd_skip_query_command_enable=0;		// [16]
static unsigned char df_bsm_rd_skip_grs_enable=1;				// [17]
//MK0301-begin
//MK0518static unsigned char df_bsm_rd_skip_termination_enable=1;		// [18]
static unsigned char df_bsm_rd_skip_termination_enable=0;		// [18]	//MK0518
static unsigned char df_bsm_rd_send_dummy_command_enable=0;		// [19]
static unsigned char df_bsm_rd_do_dummy_read_enable=0;			// [20]
//MK0301-end
//MK0307-begin
static unsigned char df_bsm_rd_popcnt_enable=1;					// [21]
//MK0307-end

/* Misc settings */
//MK0605static unsigned int user_defined_delay_us=1;
//MK0224-begin
static unsigned long df_bsm_wrt_qc_status_delay=1;
static unsigned long df_bsm_rd_qc_status_delay=1000;
//MK0301static unsigned int df_bsm_wrt_skipping_lba=0;
//MK0224-end
//MK0223-end

//MK0301-begin
static unsigned int df_bsm_wrt_dummy_command_lba=0;
static void *df_bsm_wrt_dummy_read_addr;
static unsigned int df_bsm_rd_dummy_command_lba=0;
static void *df_bsm_rd_dummy_read_addr;
static unsigned long df_pattern_mask=DEFAULT_PATTERN_MASK;
//MK0301-end

//MK0405-begin
static unsigned int user_defined_delay_ms[5]={1,1,0,0,0};
//MK0405-end
//MK0605-begin
static unsigned int user_defined_delay_us[5]={1,700,700,0,0};
//MK0605-end

#ifdef MULTI_DIMM
/* blok size of current command */
static unsigned int bsm_blk_size;
static unsigned int mmls_blk_size;
#endif

#ifdef SIMULATION_TB
#define virt_to_phys(x)		x
#define _mm_clflush(addr)\
asm volatile("clflush %0" : "+m" (*(volatile char *)addr));

#define _mm_clwb(addr)\
asm volatile("xsaveopt %0" : "+m" (*(volatile char *)addr));

typedef unsigned long uintptr_t;
#define FLUSH_ALIGN	64
/*
* clflush_cache_range -- flush the CPU cache, using clflush
*/
void clflush_cache_range(void *addr, long size)
{
	uintptr_t uptr;
	/*
	* Loop through cache-line-size (typically 64B) aligned chunks
	* covering the given range.
	*/
	for (uptr = (uintptr_t)addr & ~(FLUSH_ALIGN - 1);
			uptr < (uintptr_t)addr + size; uptr += FLUSH_ALIGN)
		_mm_clflush((char *)uptr);
}

hv_mcpy(void *dst, void *src, long size)
{
	pr_mmio("    hv_mcpy: dst 0x%lx src 0x%lx size %ld\n", (unsigned long)dst, (unsigned long)src, size);
#ifdef USER_SPACE_CMD_DRIVER
	memcpy(dst, src, size);
#endif
}
#else

//MK1103-begin
/*
 * memcpy_64B_movnti: Copy 64 bytes from src to dst
 *		len: must be 64 byte
 *
 * NOTE: MOVNTI stands for "MOVe Non-Temporal Integer" and is one of SSE2
 * Cacheability Control and Ordering Instructions. It performs non-temporal
 * store of a doubleword (32-bit) from a a general-purpose register into memory.
 */
void memcpy_64B_movnti(void *dst, void *src)
{
	register unsigned long t1, t2, t3, t4, t5, t6, t7, t8;

//	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"                                     \n"
		"mov     0*8(%[src]),    %[t1]        \n"
		"mov     1*8(%[src]),    %[t2]        \n"
		"mov     2*8(%[src]),    %[t3]        \n"
		"mov     3*8(%[src]),    %[t4]        \n"
		"mov     4*8(%[src]),    %[t5]        \n"
		"mov     5*8(%[src]),    %[t6]        \n"
		"mov     6*8(%[src]),    %[t7]        \n"
		"mov     7*8(%[src]),    %[t8]        \n"
		"                                     \n"
		"1:                                   \n"
		"movnti  %[t1],         0*8(%[dst])   \n"
		"movnti  %[t2],         1*8(%[dst])   \n"
		"movnti  %[t3],         2*8(%[dst])   \n"
		"movnti  %[t4],         3*8(%[dst])   \n"
		"movnti  %[t5],         4*8(%[dst])   \n"
		"movnti  %[t6],         5*8(%[dst])   \n"
		"movnti  %[t7],         6*8(%[dst])   \n"
		"movnti  %[t8],         7*8(%[dst])   \n"
		"                                     \n"
//		"addq    $64,           %[dst]        \n"
//		"subl    $64,           %[len]        \n"
//		"jnz     1b                           \n"
		"                                     \n"
//		: [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3), [t4]"=&r"(t4),
//		  [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7), [t8]"=&r"(t8),
//		  [src]"+S"(src), [dst]"+D"(dst), [len]"+c"(len)
		: [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3), [t4]"=&r"(t4),
		  [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7), [t8]"=&r"(t8),
		  [src]"+S"(src), [dst]"+D"(dst)
		:
		: "cc"
	);

}

/*
 * memcpy_64B_movnti_2:
 * len: can be multiple of 64 bytes
 */
void memcpy_64B_movnti_2(void *dst, void *src, unsigned int len)
{
	register unsigned long t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"2:                                   \n"
		"mov     0*8(%[src]),    %[t1]        \n"
		"mov     1*8(%[src]),    %[t2]        \n"
		"mov     2*8(%[src]),    %[t3]        \n"
		"mov     3*8(%[src]),    %[t4]        \n"
		"mov     4*8(%[src]),    %[t5]        \n"
		"mov     5*8(%[src]),    %[t6]        \n"
		"mov     6*8(%[src]),    %[t7]        \n"
		"mov     7*8(%[src]),    %[t8]        \n"
		"                                     \n"
		"1:                                   \n"
		"movnti  %[t1],         0*8(%[dst])   \n"
		"movnti  %[t2],         1*8(%[dst])   \n"
		"movnti  %[t3],         2*8(%[dst])   \n"
		"movnti  %[t4],         3*8(%[dst])   \n"
		"movnti  %[t5],         4*8(%[dst])   \n"
		"movnti  %[t6],         5*8(%[dst])   \n"
		"movnti  %[t7],         6*8(%[dst])   \n"
		"movnti  %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"addq    $64,           %[src]        \n"
		"addq    $64,           %[dst]        \n"
		"subl    $64,           %[len]        \n"
		"jnz     2b                           \n"
		"                                     \n"
		: [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3), [t4]"=&r"(t4),
		  [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7), [t8]"=&r"(t8),
		  [src]"+S"(src), [dst]"+D"(dst), [len]"+c"(len)
		:
		: "cc"
	);

}
//MK1103-end

//SJ0313-begin
#define SFENCE 1
void memcpy_64B_movnti_fr(void *dst, void *src, unsigned int len)
{
	register unsigned long t1;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
#if SFENCE
		"pushfq                               \n"
		"cli                                  \n"
#endif
		"2:                                   \n"
		"mov     0*8(%[src]),    %[t1]        \n"
		"                                     \n"
		"1:                                   \n"
#if SFENCE
		"sfence                               \n"
#endif
		"movnti  %[t1],         0*8(%[dst])   \n"
		"                                     \n"
		"addq    $64,           %[src]        \n"
		"addq    $64,           %[dst]        \n"
		"subl    $64,           %[len]        \n"
		"jnz     2b                           \n"
		"                                     \n"
#if SFENCE
		"sti                                  \n"
		"popfq                                \n"
#endif
		: [t1]"=&r"(t1),
		  [src]"+S"(src), [dst]"+D"(dst), [len]"+c"(len)
		:
		: "cc"
	);

}
//SJ0307-end

//MK0106-begin
#define SFENCE 1
void memcpy_64B_movnti_3(void *dst, void *src, unsigned int len)
{
	register unsigned long t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

		__asm__ __volatile__ (
#if SFENCE
			"pushfq                               \n"
			"cli                                  \n"
#endif
			"2:                                   \n"
			"mov     0*8(%[src]),    %[t1]        \n"
			"mov     1*8(%[src]),    %[t2]        \n"
			"mov     2*8(%[src]),    %[t3]        \n"
			"mov     3*8(%[src]),    %[t4]        \n"
			"mov     4*8(%[src]),    %[t5]        \n"
			"mov     5*8(%[src]),    %[t6]        \n"
			"mov     6*8(%[src]),    %[t7]        \n"
			"mov     7*8(%[src]),    %[t8]        \n"
			"                                     \n"
			"1:                                   \n"
#if SFENCE
			"sfence                               \n"
#endif
			"movnti  %[t1],         0*8(%[dst])   \n"
			"movnti  %[t2],         1*8(%[dst])   \n"
			"movnti  %[t3],         2*8(%[dst])   \n"
			"movnti  %[t4],         3*8(%[dst])   \n"
			"movnti  %[t5],         4*8(%[dst])   \n"
			"movnti  %[t6],         5*8(%[dst])   \n"
			"movnti  %[t7],         6*8(%[dst])   \n"
			"movnti  %[t8],         7*8(%[dst])   \n"
			"                                     \n"

			"addq    $64,           %[src]        \n"
			"addq    $64,           %[dst]        \n"
			"subl    $64,           %[len]        \n"
			"jnz     2b                           \n"
			"                                     \n"
#if SFENCE
			"sti                                  \n"
			"popfq                                \n"
#endif
			: [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3), [t4]"=&r"(t4),
			  [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7), [t8]"=&r"(t8),
			  [src]"+S"(src), [dst]"+D"(dst), [len]"+c"(len)
			:
			: "cc"
		);
}
//MK0106-end

//MK0124-begin
/* memcpy in descending order */
void memcpy_64B_movnti_4(void *dst, void *src, unsigned int len)
{
	register unsigned long t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

		__asm__ __volatile__ (
#if SFENCE
			"pushfq                               \n"
			"cli                                  \n"
#endif
			"2:                                   \n"
			"mov     0*8(%[src]),    %[t1]        \n"
			"mov     1*8(%[src]),    %[t2]        \n"
			"mov     2*8(%[src]),    %[t3]        \n"
			"mov     3*8(%[src]),    %[t4]        \n"
			"mov     4*8(%[src]),    %[t5]        \n"
			"mov     5*8(%[src]),    %[t6]        \n"
			"mov     6*8(%[src]),    %[t7]        \n"
			"mov     7*8(%[src]),    %[t8]        \n"
			"                                     \n"
			"1:                                   \n"
#if SFENCE
			"sfence                               \n"
#endif
			"movnti  %[t1],         0*8(%[dst])   \n"
			"movnti  %[t2],         1*8(%[dst])   \n"
			"movnti  %[t3],         2*8(%[dst])   \n"
			"movnti  %[t4],         3*8(%[dst])   \n"
			"movnti  %[t5],         4*8(%[dst])   \n"
			"movnti  %[t6],         5*8(%[dst])   \n"
			"movnti  %[t7],         6*8(%[dst])   \n"
			"movnti  %[t8],         7*8(%[dst])   \n"
			"                                     \n"

			"subq    $64,           %[src]        \n"
			"subq    $64,           %[dst]        \n"
			"subl    $64,           %[len]        \n"
			"jnz     2b                           \n"
			"                                     \n"
#if SFENCE
			"sti                                  \n"
			"popfq                                \n"
#endif
			: [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3), [t4]"=&r"(t4),
			  [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7), [t8]"=&r"(t8),
			  [src]"+S"(src), [dst]"+D"(dst), [len]"+c"(len)
			:
			: "cc"
		);
}
//MK0124-end

void hv_mcpy(void *dst, void *src, long size)
{
//MK	pr_mmio("    hv_mcpy: dst 0x%lx src 0x%lx size %ld\n", (unsigned long)dst, (unsigned long)src, size);
//MK-begin
//	pr_mmio("[%s]: dst=%#.16lx src=%#.16lx size=%ld\n", __func__, (unsigned long)dst, (unsigned long)src, size);
//MK-end
//MK0106
	memcpy(dst, src, size);
//MK0106-begin
//	memcpy_64B_movnti_3(dst, src, size);
//MK0106-end
}
#endif

#ifdef MULTI_DIMM
/*
 * return block size carried on command header
 */
static unsigned int get_blk_size(int type)
{
	return (type==GROUP_BSM) ? bsm_blk_size : mmls_blk_size;
}

/*
 * return the remain buffer size per index
 */
static long intv_remain_sz(int gid, int index, long size)	
{
/* 
 * do not multiply the buffer size by ways since hv_cmd.c
 * doesn't multiple the buffer size by ways either
 */
#if 0
	int ways = interleave_ways(gid);

	if (size == DATA_BUFFER_SIZE)
		return DATA_BUFFER_SIZE;
	else if (size == DATA_BUFFER_SIZE*2) {
		if (ways == 1)
			return (DATA_BUFFER_NUM-index)*DATA_BUFFER_SIZE;
		else
			return DATA_BUFFER_SIZE*2;
	}
	else 
		return (DATA_BUFFER_NUM-index)*DATA_BUFFER_SIZE*ways;
#endif
	return (DATA_BUFFER_NUM-index)*DATA_BUFFER_SIZE;
}

/*
 * return ways of channel interleaving
 */
int interleave_ways (int gid)
{
	return hv_group[gid].intv.chnl_way;
}

/*
 * *hidx : index to emmc[]
 * *llba : local lba on target HVDIMM
 */
static int calc_hidx_llba(int gid, int type, int lba, int ways, int *hidx, int *llba)
{
	int sectors;

	*hidx = 0;
	while (1) {
		if (type == GROUP_BSM) {
#ifdef INTERLEAVE_BSM
			sectors = hv_group[gid].emmc[(*hidx)*ways].b_size*ways;
#else
			ways = 1;
			sectors = hv_group[gid].emmc[(*hidx)*ways].b_size*1;
#endif
		}
		else
			sectors = hv_group[gid].emmc[(*hidx)*ways].m_size*ways;
		if (lba > sectors) {
			lba = lba - sectors;
			*hidx = *hidx + ways;
			if (lba < 0)
				break;
		}
		else {
			/* *hidx is the index to emmc[] and *llba is local lba */
			*llba = lba/ways;
			return 0;
		}
	}
	return -1;
}
#endif

#ifndef REV_B_MM

#ifdef MULTI_DIMM
/* 
 * reformat the command with updated lba
 */
static void reformat_command(int gid, int type, int hidx, int llba, void *cmd)
{
	struct HV_CMD_BSM_t *bsm_cmd;
	struct HV_CMD_MMLS_t *mmls_cmd;
	unsigned char cmd_type;
	unsigned int sector;

	/* cmd type is the 1st byte */
	cmd_type = *(unsigned char *)cmd;
	if (cmd_type == BSM_READ || cmd_type == BSM_WRITE || cmd_type == MMLS_READ || cmd_type == MMLS_WRITE) {
		if (type == GROUP_BSM) {
			bsm_cmd = (struct HV_CMD_BSM_t *)cmd;
			*(unsigned int *)&bsm_cmd->lba = llba+hv_group[gid].emmc[hidx].b_start;
			bsm_blk_size = (*(unsigned int *)&bsm_cmd->sector)*HV_BLOCK_SIZE;

#ifdef INTERLEAVE_BSM
			/* update bsm_cmd->sector if multi-way interleaving */
			sector = (*(unsigned int *)&bsm_cmd->sector);
#ifndef MIN_1KBLOCK_SUPPORT
			if (sector/interleave_ways(gid) < 8)
				/* force to 8 since min size of read/write a DIMM is 8 sectors
				   code to handle the data will fill unused space w '1' */
				sector = 8;		
			else
#endif
				sector = sector/interleave_ways(gid);
			*(unsigned int *)&bsm_cmd->sector = sector;
#endif
		}
		else {
			mmls_cmd = (struct HV_CMD_MMLS_t *)cmd;
			*(unsigned int *)&mmls_cmd->lba = llba+hv_group[gid].emmc[hidx].m_start;
			mmls_blk_size = (*(unsigned int *)&mmls_cmd->sector)*HV_BLOCK_SIZE;
			
			/* update mmls_cmd->sector if multi-way interleaving */
			sector = (*(unsigned int *)&mmls_cmd->sector);
#ifndef MIN_1KBLOCK_SUPPORT
			if (sector/interleave_ways(gid) < 8)
				/* force to 8 since min size of read/write a DIMM is 8 sectors
				   code to handle the data will fill unused space w '1' */
				sector = 8;		
			else
#endif
				sector = sector/interleave_ways(gid);
			*(unsigned int *)&mmls_cmd->sector = sector;

			/* update mm_addr if HVDIMM is used for the case of '1' filling */
			if (mmls_blk_size < interleave_ways(gid)*DATA_BUFFER_SIZE)
				*(unsigned int *)&mmls_cmd->mm_addr = hv_group[gid].mem[hidx/interleave_ways(gid)].p_dram;
#ifdef RDIMM_POPULATED
			/* update mm_addr if RDIMM is populated */
			*(unsigned int *)&mmls_cmd->mm_addr = hv_group[gid].mem[hidx/interleave_ways(gid)].p_dram;
#endif
		}
	}
}

#ifndef INTERLEAVE_BSM
/*
 * write command to non-interleaved BSM
 */
static void write_command_bsm (int gid, int type, int lba, void *cmd, int cmd_off)
{
	int hidx, llba;
	int ways, mem, idx;
	void *cmd_mmio;

	/* figure out index to emmc[] to its local lba */
	if (calc_hidx_llba(gid, type, lba, 1, &hidx, &llba))
		return;

	/* reformat command with llba */
	reformat_command(gid, type, hidx, llba, cmd);

	/* send command to target HVDIMM */
	ways = interleave_ways(gid);
	mem = hidx/ways;
	idx = hidx%ways;
	if (cmd_off == CMD_OFF)
		cmd_mmio = (void *)(hv_group[gid].mem[mem].v_mmio+(cmd_off*ways+idx*MEM_BURST_SIZE));
	else
		cmd_mmio = (void *)(hv_group[gid].mem[mem].v_other_mmio+(cmd_off*ways+idx*MEM_BURST_SIZE));
	hv_mcpy (cmd_mmio, cmd, CMD_BUFFER_SIZE);
	if (cmd_status_use_cache)
		clflush_cache_range(cmd_mmio, CMD_BUFFER_SIZE);
}
#endif

/*
 * write command to MMLS or interleaved BSM
 */
static void write_command_intv (int gid, int type, int lba, void *cmd, int cmd_off)
{	
	int hidx, llba;
	int i, ways, mem;
	void *cmd_mmio, *cmd_mmio_saved;

	/* figure out index to emmc[] to its local lba */
	ways = interleave_ways(gid);
	if (calc_hidx_llba(gid, type, lba, ways, &hidx, &llba))
		return;

	/* send command to all HVDIMM(s) */
	mem = hidx/ways;
	if (cmd_off == CMD_OFF)
		cmd_mmio_saved = cmd_mmio = (void *)(hv_group[gid].mem[mem].v_mmio+cmd_off*ways);
	else		
		cmd_mmio_saved = cmd_mmio = (void *)(hv_group[gid].mem[mem].v_other_mmio+cmd_off*ways);

	/* reformat command line */
	/* assuming MMLS on all DIMMs starting from the same lba */
	reformat_command(gid, type, mem*ways, llba, cmd);
	for (i=0; i<ways; i++) {
		/* send command */
		hv_mcpy (cmd_mmio, cmd, CMD_BUFFER_SIZE);
		cmd_mmio = cmd_mmio + MEM_BURST_SIZE;
	}
	if (cmd_status_use_cache)
		clflush_cache_range(cmd_mmio_saved, CMD_BUFFER_SIZE*ways);
}

#ifndef INTERLEAVE_BSM
/*
 * 
 */
static void proc_bsm_nowrap (int gid, void *data_mmio, int ways, void *data, long size, int type)
{
	int i;
	void *save_data_mmio = data_mmio;

	if (ways ==1) {
		if (type == HV_WRITE) {
			hv_mcpy(data_mmio, data, size);
			if (bsm_write_use_cache)
				clflush_cache_range(data_mmio, size);
		}
		else {
			if (bsm_read_use_cache)
				clflush_cache_range(data_mmio, size);
			hv_mcpy(data, data_mmio, size);
		}
	}
	else {
		if (type == HV_READ) {
			if (bsm_read_use_cache)
				clflush_cache_range(save_data_mmio, size*ways);
		}

		for (i = 0; i < size/MEM_BURST_SIZE; i++) {
			if (type == HV_WRITE) {
				hv_mcpy(data_mmio, data, MEM_BURST_SIZE);
			}
			else {
				hv_mcpy(data, data_mmio, MEM_BURST_SIZE);
			}
			data_mmio += MEM_BURST_SIZE*ways;
			data += MEM_BURST_SIZE;
		}
		if (type == HV_WRITE) {
			if (bsm_write_use_cache)
				clflush_cache_range(save_data_mmio, size*ways);
		}
	}
}

static void proc_bsm_data (int gid, long lba, int index, void *data, long size, int type)
{
	int hidx, llba;
	int ways, mem, idx;
	void *data_mmio;
	long bsm_buf_off;
	long remain_size;

	/* figure out hidx to emmc[] to its local lba */
	if (calc_hidx_llba(gid, GROUP_BSM, lba, 1, &hidx, &llba))
		return;

	/* figure out data buffer MMIO address */
	ways = interleave_ways(gid);
	mem = hidx/ways;	/* idx to interleave group */
	idx = hidx%ways;	/* idx inside the group */
	bsm_buf_off = (type == HV_WRITE) ? (BSM_WRITE_OFF+index*DATA_BUFFER_SIZE)*ways
					 : (BSM_READ_OFF+index*DATA_BUFFER_SIZE)*ways;
	if (type == HV_WRITE)
		data_mmio = (void *)(hv_group[gid].mem[mem].v_bsm_w_mmio + (bsm_buf_off+idx*MEM_BURST_SIZE));
	else
		data_mmio = (void *)(hv_group[gid].mem[mem].v_bsm_r_mmio + (bsm_buf_off+idx*MEM_BURST_SIZE));

	/* transfer data to and from BSM */
	if ((remain_size=remain_sz(index)) >= size)  {
		proc_bsm_nowrap (gid, data_mmio, ways, data, size, type);
	}
	else {
		proc_bsm_nowrap (gid, data_mmio, ways, data, remain_size, type);

		/* handle the case that index needs to wrap around */
		index = 0;
		bsm_buf_off = (type == HV_WRITE) ? (BSM_WRITE_OFF+index*DATA_BUFFER_SIZE)*ways
						 : (BSM_READ_OFF+index*DATA_BUFFER_SIZE)*ways;
		if (type == HV_WRITE)
			data_mmio = (void *)(hv_group[gid].mem[mem].v_bsm_w_mmio + (bsm_buf_off+idx*MEM_BURST_SIZE));
		else
			data_mmio = (void *)(hv_group[gid].mem[mem].v_bsm_r_mmio + (bsm_buf_off+idx*MEM_BURST_SIZE));
		proc_bsm_nowrap (gid, data_mmio, ways, data+remain_size, size-remain_size, type);
	}
}
#endif

static void proc_intv_nowrap (int gid, long lba, int index, void *data, long size, int type, int group)
{
	int hidx, llba;
	int i, j;
	int ways, mem;
	void *hv_dram, *tmp_dram, *tmp_data;
	int intv_size;
	void *hv_mmio=0;
	long bsm_buf_off;

	/* figure out index to emmc[] to its local lba */
	ways = interleave_ways(gid);
	if (calc_hidx_llba(gid, group, lba, ways, &hidx, &llba))
		return;

	/* convert to virtual addr */
	mem = hidx/ways;
	if (group == GROUP_MMLS)
		hv_dram = (void *)hv_group[gid].mem[mem].v_dram+index*DATA_BUFFER_SIZE;
	else {
		bsm_buf_off = (type == HV_WRITE) ? (BSM_WRITE_OFF*ways+index*DATA_BUFFER_SIZE)
						 : (BSM_READ_OFF*ways+index*DATA_BUFFER_SIZE);
		if (type == HV_WRITE)
			hv_mmio = (void *)(hv_group[gid].mem[mem].v_bsm_w_mmio + bsm_buf_off);
		else
			hv_mmio = (void *)(hv_group[gid].mem[mem].v_bsm_r_mmio + bsm_buf_off);
		hv_dram = (void *)hv_group[gid].mem[mem].v_dram+index*DATA_BUFFER_SIZE;
	}

	/* handle data on all HVDIMM(s) */
	if (type == HV_WRITE) {
#ifdef MIN_1KBLOCK_SUPPORT
		if (get_blk_size(group) < 0) {
#else
		if (get_blk_size(group) < ways*DATA_BUFFER_SIZE) {
#endif
			memset (hv_dram, 1, DATA_BUFFER_SIZE*ways);	/* fill w 1 */
			intv_size = size/ways;
			size = ways*DATA_BUFFER_SIZE;

			for (i=0; i<ways; i++) {
				tmp_dram = hv_dram+MEM_BURST_SIZE*i;
				tmp_data = data+intv_size*i;
				for (j=0; j<intv_size/MEM_BURST_SIZE; j++) {
					hv_mcpy (tmp_dram, tmp_data, MEM_BURST_SIZE);
					tmp_dram += ways*MEM_BURST_SIZE;
					tmp_data += MEM_BURST_SIZE;
				}
			}

			/* fake read from hv_dram */
			if (group == GROUP_MMLS) {
				if (fake_read_cache_flush)
					clflush_cache_range(hv_dram, size);
				hv_mcpy (fake_mmls_buf, hv_dram, size);
			}
			else {
				hv_mcpy(hv_mmio, hv_dram, size);
				if (bsm_write_use_cache)
					clflush_cache_range(hv_mmio, size);
			}
		}
		else {
			if (group == GROUP_MMLS) {
#ifdef RDIMM_POPULATED
				hv_mcpy (hv_dram, data, size);
				/* fake read from hv_dram */
				if (fake_read_cache_flush)
					clflush_cache_range(hv_dram, size);
				hv_mcpy (fake_mmls_buf, hv_dram, size);
#else
				/* fake read from hv_dram */
				if (fake_read_cache_flush)
					clflush_cache_range(data, size);
				hv_mcpy (fake_mmls_buf, data, size);
#endif
			}
			else {
				hv_mcpy(hv_mmio, data, size);
				if (bsm_write_use_cache)
					clflush_cache_range(hv_mmio, size);
			}
		}
	}
	else {
#ifdef MIN_1KBLOCK_SUPPORT
		if (get_blk_size(group) < 0) {
#else
		if (get_blk_size(group) < ways*DATA_BUFFER_SIZE) {
#endif
			intv_size = size/ways;
			size = ways*DATA_BUFFER_SIZE;

			if (group == GROUP_MMLS) {
				/* fake write to hv_dram */
				hv_mcpy (hv_dram, fake_mmls_buf, size);
				if (fake_write_cache_flush)
					clflush_cache_range(hv_dram, size);
			}
			else {
				if (bsm_read_use_cache)
					clflush_cache_range(hv_mmio, size);
				hv_mcpy(hv_dram, hv_mmio, size);
			}

			for (i=0; i<ways; i++) {
				tmp_dram = hv_dram+MEM_BURST_SIZE*i;
				tmp_data = data+intv_size*i;
				for (j=0; j<intv_size/MEM_BURST_SIZE; j++) {
					hv_mcpy (tmp_data, tmp_dram, MEM_BURST_SIZE);
					tmp_dram += ways*MEM_BURST_SIZE;
					tmp_data += MEM_BURST_SIZE;
				}
			}
		}
		else {
			if (group == GROUP_MMLS) {
#ifdef RDIMM_POPULATED
				/* fake write to hv_dram */
				hv_mcpy (hv_dram, fake_mmls_buf, size);
				if (fake_write_cache_flush)
					clflush_cache_range(hv_dram, size);
				hv_mcpy (data, hv_dram, size);
#else
				/* fake write to hv_dram */
				hv_mcpy (data, fake_mmls_buf, size);
				if (fake_write_cache_flush)
					clflush_cache_range(data, size);
#endif
			}
			else {
				if (bsm_read_use_cache)
					clflush_cache_range(hv_mmio, size);
				hv_mcpy(data, hv_mmio, size);
			}
		}
	}	
}

static void proc_intv_data (int gid, long lba, int index, void *data, long size, int type, int group)
{
	long remain_size;

	if ((remain_size=intv_remain_sz(gid, index, size)) >= size)  {
		proc_intv_nowrap (gid, lba, index, data, size, type, group);
	}
	else {
		/* handle the case that index needs to wrap around */
		proc_intv_nowrap (gid, lba, index, data, remain_size, type, group);
		proc_intv_nowrap (gid, lba, 0, data+remain_size, size-remain_size, type, group);
	}
}

void hv_write_command (int gid, int type, int lba, void *cmd)
{
	pr_mmio("%s entered.\n", __func__);
	if (type == GROUP_BSM)
#ifdef INTERLEAVE_BSM
		write_command_intv(gid, type, lba, cmd, CMD_OFF);
#else
		write_command_bsm(gid, type, lba, cmd, CMD_OFF);
#endif
	else
		write_command_intv(gid, type, lba, cmd, CMD_OFF);
}

void hv_write_termination (int gid, int type, int lba, void *cmd)
{
	pr_mmio("%s entered.\n", __func__);
	if (!cmd)
		cmd = cmd_burst_buf;

	if (type == GROUP_BSM)
#ifdef INTERLEAVE_BSM
		write_command_intv(gid, type, lba, cmd, TERM_OFF);
#else
		write_command_bsm(gid, type, lba, cmd, TERM_OFF);
#endif
	else
		write_command_intv(gid, type, lba, cmd, TERM_OFF);
}

void hv_write_ecc (int gid, int lba, void *cmd)
{
}

/*
 *
 * size can be 4, 8, 12 or 16KB
 *
 */
void hv_write_bsm_data (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
#ifdef INTERLEAVE_BSM
	proc_intv_data (gid, lba, index, data, size, HV_WRITE, GROUP_BSM);
#else
	proc_bsm_data (gid, lba, index, data, size, HV_WRITE);
#endif
}

void hv_read_bsm_data (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
#ifdef INTERLEAVE_BSM
	proc_intv_data (gid, lba, index, data, size, HV_READ, GROUP_BSM);
#else
	proc_bsm_data (gid, lba, index, data, size, HV_READ);
#endif
}

/*
 *
 * command driver uses
 *	size = min(block_size, ways*buffer_cnt*4KB)
 *
 * when calling
 *	hv_mmls_fake_read (int gid, long lba, int index, void *data, long size)
 *	hv_mmls_fake_write (int gid, long lba, int index, void *data, long size)
 * 
 * then it updates index as follows (index should cycle between 0-3)
 * 	if size is 4KB
 * 		increment index by 1
 * 	else if size is 8KB, 
 *		if 1-way interleaving
 *			increment index by 2
 *		else
 *			increment index by 1
 *	else
 *		increment index by size/(4KB*ways)
 *
 */
void hv_mmls_fake_read (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
	proc_intv_data (gid, lba, index, data, size, HV_WRITE, GROUP_MMLS);
}

void hv_mmls_fake_write (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
	proc_intv_data (gid, lba, index, data, size, HV_READ, GROUP_MMLS);
}

static unsigned char hv_read_status (int gid, int type, long lba, long offset)
{
	int hidx, llba;
	int i=0, ways, mem;
#ifndef INTERLEAVE_BSM
	int idx;
#endif
	void *status_mmio;
	unsigned char more_status, status;

	/* figure out index to emmc[] to its local lba */
	ways=interleave_ways(gid);
	if (calc_hidx_llba(gid, type, lba, ways, &hidx, &llba))
		return -1;

	/* read status of all HVDIMM(s) */
	mem = hidx/ways;
#ifndef INTERLEAVE_BSM
	if (type == GROUP_BSM) {
		idx = hidx%ways;
		status_mmio = (void *)(hv_group[gid].mem[mem].v_other_mmio+offset*ways+idx*MEM_BURST_SIZE);
		if (cmd_status_use_cache)
			clflush_cache_range(status_mmio, MEM_BURST_SIZE);
		hv_mcpy (&status, status_mmio, sizeof(unsigned char));
		return status;	
	}
	else {
#endif
		status_mmio = (void *)(hv_group[gid].mem[mem].v_other_mmio+offset*ways);		
		/* read 1st status */
		if (cmd_status_use_cache)
			clflush_cache_range(status_mmio, MEM_BURST_SIZE);
		hv_mcpy (&status, status_mmio, sizeof(unsigned char));
		status_mmio = status_mmio + MEM_BURST_SIZE;
		for (i=1; i<ways; i++) {
			/* read subsequent status */
			if (cmd_status_use_cache)
				clflush_cache_range(status_mmio, MEM_BURST_SIZE);
			hv_mcpy (&more_status, status_mmio, sizeof(unsigned char));
			if (more_status != status)
				return 0;
			status_mmio = status_mmio + MEM_BURST_SIZE;
		}
		return status;
#ifndef INTERLEAVE_BSM
	}
#endif
}

unsigned char hv_query_status (int gid, int type, long lba, unsigned char status)
{
	int hidx, llba;
	int i=0, ways, mem;
#ifndef INTERLEAVE_BSM
	int idx;
#endif
	void *status_mmio;
	unsigned char q_status;
	unsigned char ettc_status;
	int ettc_val=0;

	pr_mmio("%s: entered\n", __func__);

	/* figure out index to emmc[] to its local lba */
	ways=interleave_ways(gid);
	if (calc_hidx_llba(gid, type, lba, ways, &hidx, &llba))
		return -1;

	/* read status of all HVDIMM(s) */
	mem = hidx/ways;
#ifndef INTERLEAVE_BSM
	if (type == GROUP_BSM) {
		idx = hidx%ways;
		status_mmio = (void *)(hv_group[gid].mem[mem].v_other_mmio+QUERY_STATUS_OFF*ways+idx*MEM_BURST_SIZE);
		if (cmd_status_use_cache)
			clflush_cache_range(status_mmio, MEM_BURST_SIZE);
		hv_mcpy (&q_status, status_mmio, sizeof(unsigned char));
		return q_status;	
	}
	else {
#endif
		status_mmio = (void *)(hv_group[gid].mem[mem].v_other_mmio+QUERY_STATUS_OFF*ways);
		for (i=0; i<ways; i++) {
			/* read status */
			if (cmd_status_use_cache)
				clflush_cache_range(status_mmio, MEM_BURST_SIZE);
			hv_mcpy (&q_status, status_mmio, sizeof(unsigned char));
			if ((q_status & status) != status) {
				if (((q_status & 0xF0) >> 4) > ettc_val) {
					/*  cmd not done and ETtC is larger */
					ettc_val = (q_status & 0xF0) >> 4;
					ettc_status = q_status;
				}
				if (ettc_val > 0)
					return ettc_status;
			}
			status_mmio = status_mmio + MEM_BURST_SIZE;
		}
		return status;
#ifndef INTERLEAVE_BSM
	}
#endif
}

unsigned char hv_read_buf_status (int gid, int type, long lba)
{
	pr_mmio("%s: entered\n", __func__);
	return hv_read_status(gid, type, lba, READ_STATUS_OFF);
}

unsigned char hv_write_buf_status (int gid, int type, long lba)
{
	pr_mmio("%s: entered\n", __func__);
	return hv_read_status(gid, type, lba, WRITE_STATUS_OFF);
}

#else	/* MULTI_DIMM */

int interleave_ways (int gid)
{
	return 1;
}

void hv_write_command (int gid, int type, int lba, void *cmd)
{
	pr_mmio("%s: entered\n", __func__);
	hv_mcpy(CMD_OFF, cmd, CMD_BUFFER_SIZE);
	if (cmd_status_use_cache)
		clflush_cache_range(CMD_OFF, CMD_BUFFER_SIZE);
}

void hv_write_termination (int gid, int type, int lba, void *cmd)
{
	pr_mmio("%s: entered\n", __func__);
	if (cmd)
		hv_mcpy(TERM_OFF, cmd, CMD_BUFFER_SIZE);
	else
		hv_mcpy(TERM_OFF, cmd_burst_buf, CMD_BUFFER_SIZE);
	if (cmd_status_use_cache)
		clflush_cache_range(TERM_OFF, CMD_BUFFER_SIZE);
}

void hv_write_ecc (int gid, int lba, void *cmd)
{
	pr_mmio("%s: entered\n", __func__);
	hv_mcpy(ECC_OFF, cmd, CMD_BUFFER_SIZE);
	if (cmd_status_use_cache)
		clflush_cache_range(ECC_OFF, CMD_BUFFER_SIZE);
}

#define bsm_write_off(index)	(BSM_WRITE_OFF+index*DATA_BUFFER_SIZE)
#define bsm_read_off(index)	(BSM_READ_OFF+index*DATA_BUFFER_SIZE)
#define mmls_dram_off(index)	(MMLS_DRAM_OFF+index*DATA_BUFFER_SIZE)

void hv_write_bsm_data (int gid, long lba, int index, void *data, long size)
{
	long remain_size;

	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
	if ((remain_size=remain_sz(index)) >= size)  {
		hv_mcpy(bsm_write_off(index), data, size);
		if (bsm_write_use_cache)
			clflush_cache_range(bsm_write_off(index), size);
	}
	else {
		hv_mcpy(bsm_write_off(index), data, remain_size);
		if (bsm_write_use_cache)
			clflush_cache_range(bsm_write_off(index), remain_size);
		hv_mcpy(bsm_write_off(0), data+remain_size, size-remain_size);
		if (bsm_write_use_cache)
			clflush_cache_range(bsm_write_off(0), size-remain_size);
	}
}

void hv_read_bsm_data (int gid, long lba, int index, void *data, long size)
{
	long remain_size;

	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
	if ((remain_size=remain_sz(index)) >= size)  {
		if (bsm_read_use_cache)
			clflush_cache_range(bsm_read_off(index), size);
		hv_mcpy(data, bsm_read_off(index), size);
	}
	else {
		if (bsm_read_use_cache)
			clflush_cache_range(bsm_read_off(index), remain_size);
		hv_mcpy(data, bsm_read_off(index), remain_size);
		if (bsm_read_use_cache)
			clflush_cache_range(bsm_read_off(0), size-remain_size);
		hv_mcpy(data+remain_size, bsm_read_off(0), size-remain_size);
	}
}

void hv_mmls_fake_read (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
#ifdef RDIMM_POPULATED
	hv_mcpy(mmls_dram_off(index), data, size);
	if (fake_read_cache_flush)
		clflush_cache_range(mmls_dram_off(index), size);
	hv_mcpy(fake_mmls_buf, mmls_dram_off(index), size);		/* fake read */
#else
	if (fake_read_cache_flush)
		clflush_cache_range(data, size);
	hv_mcpy(fake_mmls_buf, data, size);				/* fake read */
#endif

}

void hv_mmls_fake_write (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
#ifdef RDIMM_POPULATED
	hv_mcpy(mmls_dram_off(index), fake_mmls_buf, size);		/* fake write */
	if (fake_write_cache_flush)
		clflush_cache_range(mmls_dram_off(index), size);
	hv_mcpy((void *)data, mmls_dram_off(index), size);
#else
	hv_mcpy(data, fake_mmls_buf, size);				/* fake write */
	if (fake_write_cache_flush)
		clflush_cache_range(data, size);
#endif
}

unsigned char hv_query_status (int gid, int type, long lba, unsigned char status)
{
	unsigned char q_status;

	pr_mmio("%s: entered\n", __func__);
	if (cmd_status_use_cache)
		clflush_cache_range(QUERY_STATUS_OFF, MEM_BURST_SIZE);
	hv_mcpy(&q_status, QUERY_STATUS_OFF, sizeof(unsigned char));
	return (unsigned int) q_status;
}

unsigned char hv_read_buf_status (int gid, int type, long lba)
{
	unsigned char q_status;

	pr_mmio("%s: entered\n", __func__);
	if (cmd_status_use_cache)
		clflush_cache_range(READ_STATUS_OFF, MEM_BURST_SIZE);
	hv_mcpy(&q_status, READ_STATUS_OFF, sizeof(unsigned char));
	return (unsigned int) q_status;
}

unsigned char hv_write_buf_status (int gid, int type, long lba)
{
	unsigned char q_status;

	pr_mmio("%s: entered\n", __func__);
	if (cmd_status_use_cache)
		clflush_cache_range(WRITE_STATUS_OFF, MEM_BURST_SIZE);
	hv_mcpy(&q_status, WRITE_STATUS_OFF, sizeof(unsigned char));
	return (unsigned int) q_status;
}

#endif	/* MULTI_DIMM */

static void calc_emmc_alloc(int gid)
{
	unsigned long b_size, m_size;
	int i;

	b_size = hv_group[gid].bsm_size/hv_group[gid].num_hv;
	m_size = hv_group[gid].mmls_size/hv_group[gid].num_hv;
	for (i=0; i<hv_group[gid].num_hv; i++) {
		hv_group[gid].emmc[i].b_start = 0;
		hv_group[gid].emmc[i].b_size = b_size;
		hv_group[gid].emmc[i].m_start = b_size;
		hv_group[gid].emmc[i].m_size = m_size;	
	}
}

#ifndef SIMULATION_TB
static long ramdisk_size;
static int io_init_done=0;
static int io_release_done=0;
static void hv_request_mem(unsigned long phys, unsigned long *virt_p, unsigned long size, int cache, char *name)
{
	if (request_mem_region(phys, size, name) == NULL) {
		pr_warn("hv: unable to request %s IO space starting 0x%lx size(%lx)\n", name, phys, size);
		return;
	}
	if (cache)
		*virt_p = (unsigned long)ioremap_cache(phys, size);
	else
		*virt_p = (unsigned long)ioremap_wc(phys, size);
}

int hv_io_init(void)
{
#ifdef MULTI_DIMM
	int ways = interleave_ways(gid);
	int num_mem = hv_group[gid].num_hv/ways;
#else
	int ways = 1;
	int num_mem = 1;
#endif
	int i;

	if (io_init_done)
		return 0;

	calc_emmc_alloc(gid);

	for (i=0; i<num_mem; i++) {
		/*
		 * Request IO space
		 */
		if (get_ramdisk()) {
			if (!get_use_memmap())
				hv_group[gid].mem[i].v_mmio = phys_to_virt(get_ramdisk_start());
			else {
				if (bsm_start()+bsm_size() > mmls_start()+mmls_size())
					ramdisk_size = bsm_start()+bsm_size();
				else
					ramdisk_size = mmls_start()+mmls_size();
				hv_request_mem(	get_ramdisk_start(), 
						(unsigned long *) &hv_group[gid].mem[i].v_mmio, 
						ramdisk_size, 
						1, 
						"ramdisk");				
			}
		} else {
			/* reserve 1G HV DRAM at top of DRAM space */
			hv_group[gid].mem[i].p_dram = hv_group[gid].mem[i].p_mmio - HV_DRAM_SIZE;
			if (!get_use_memmap()) {
				hv_group[gid].mem[i].v_mmio = phys_to_virt(hv_group[gid].mem[i].p_mmio+MMIO_CMD_OFF*ways);
				hv_group[gid].mem[i].v_bsm_w_mmio = phys_to_virt(hv_group[gid].mem[i].p_mmio+MMIO_BSM_W_OFF*ways);
				hv_group[gid].mem[i].v_bsm_r_mmio = phys_to_virt(hv_group[gid].mem[i].p_mmio+MMIO_BSM_R_OFF*ways);
				hv_group[gid].mem[i].v_other_mmio = phys_to_virt(hv_group[gid].mem[i].p_mmio+MMIO_OTHER_OFF*ways);
				hv_group[gid].mem[i].v_dram = phys_to_virt(hv_group[gid].mem[i].p_dram);
				cmd_status_use_cache = 1;
				bsm_write_use_cache = 1;			
				bsm_read_use_cache = 1;
			}
			else {
				hv_request_mem(	hv_group[gid].mem[i].p_mmio+MMIO_CMD_OFF*ways, 
						(unsigned long *) &hv_group[gid].mem[i].v_mmio, 
						MMIO_CMD_SIZE*ways, 
						cmd_status_use_cache, 
						"cmd");				
				hv_request_mem(	hv_group[gid].mem[i].p_mmio+MMIO_BSM_W_OFF*ways, 
						(unsigned long *) &hv_group[gid].mem[i].v_bsm_w_mmio, 
						MMIO_BSM_W_SIZE*ways, 
						bsm_write_use_cache, 
						"bsm_w");				
				hv_request_mem(	hv_group[gid].mem[i].p_mmio+MMIO_BSM_R_OFF*ways, 
						(unsigned long *) &hv_group[gid].mem[i].v_bsm_r_mmio, 
						MMIO_BSM_R_SIZE*ways, 
						bsm_read_use_cache, 
						"bsm_r");				
				hv_request_mem(	hv_group[gid].mem[i].p_mmio+MMIO_OTHER_OFF*ways, 
						(unsigned long *) &hv_group[gid].mem[i].v_other_mmio, 
						MMIO_OTHER_SIZE*ways, 
						cmd_status_use_cache, 
						"other");				
				hv_request_mem(	hv_group[gid].mem[i].p_dram, 
						(unsigned long *) &hv_group[gid].mem[i].v_dram, 
						HV_DRAM_SIZE, 
						1, 
						"dram");				
			}
		}
	}

	io_init_done = 1;
	return 0;
}

static void hv_release_mem(unsigned long phys, void *virt, unsigned long size)
{
	iounmap(virt);
	release_mem_region(phys, size);
}

void hv_io_release(void)
{
#ifdef MULTI_DIMM
	int ways = interleave_ways(gid);
	int num_mem = hv_group[gid].num_hv/ways;
#else
	int ways = 1;
	int num_mem = 1;
#endif
	int i;

	if (io_release_done)
		return;

	for (i=0; i<num_mem; i++) {
		if (!get_ramdisk() && get_use_memmap()) {
			hv_release_mem(	hv_group[gid].mem[i].p_mmio+MMIO_CMD_OFF*ways, 
					hv_group[gid].mem[i].v_mmio, MMIO_CMD_SIZE*ways);
			hv_release_mem(	hv_group[gid].mem[i].p_mmio+MMIO_BSM_W_OFF*ways, 
					hv_group[gid].mem[i].v_bsm_w_mmio, MMIO_BSM_W_SIZE*ways);
			hv_release_mem(	hv_group[gid].mem[i].p_mmio+MMIO_BSM_R_OFF*ways, 
					hv_group[gid].mem[i].v_bsm_r_mmio, MMIO_BSM_R_SIZE*ways);
			hv_release_mem(	hv_group[gid].mem[i].p_mmio+MMIO_OTHER_OFF*ways, 
					hv_group[gid].mem[i].v_other_mmio, MMIO_OTHER_SIZE);
			hv_release_mem(	hv_group[gid].mem[i].p_dram, 
					hv_group[gid].mem[i].v_dram, HV_DRAM_SIZE);
		}

		if (get_ramdisk() && get_use_memmap()) {
			hv_release_mem(	get_ramdisk_start(), 
					hv_group[gid].mem[i].v_mmio, ramdisk_size);
		}
	}

	io_release_done = 1;
}
#endif

#ifdef SIMULATION_TB
static int get_ramdisk() { return  0; }
static int get_ramdisk_start() { return  0x0; }
#endif

void get_bsm_iodata(struct HV_BSM_IO_t *p_bio_data)
{
	p_bio_data->b_size = bsm_size();
	p_bio_data->b_iomem = (void *)hv_group[gid].mem[0].v_mmio+bsm_start();
	if (get_ramdisk())
		p_bio_data->phys_start = (unsigned long) (get_ramdisk_start()+
			bsm_start());
	else
		; /* phys_start is used by char device on RAMDISK */
}

void get_mmls_iodata(struct HV_MMLS_IO_t *p_mio_data)
{
	p_mio_data->m_size = mmls_size();
	p_mio_data->m_iomem = (void *)hv_group[gid].mem[0].v_mmio+mmls_start();
	if (get_ramdisk())
		p_mio_data->phys_start = (unsigned long) (get_ramdisk_start()+
			mmls_start());
	else
		; /* phys_start is used by char device on RAMDISK */
}

#ifdef USER_SPACE_CMD_DRIVER
extern int get_fd();

static void hv_mmap_mem(unsigned long phys, unsigned long *virt_p, unsigned long size)
{
	*virt_p = (unsigned long)mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, get_fd(), phys);
	if(*virt_p == -1)
		printf("hv_mmap_mem failed phys(0x%lx), size(0x%lx) errno(%d)\n", phys, size, errno);
}

static void hv_mumap_mem(void *virt_p, unsigned long size)
{
	if(munmap(virt_p, size) == -1) 
		printf("hv_mumap_mem failed virt_p(0x%lx), size(0x%lx) errno(%d)\n", (unsigned long)virt_p, size, errno);
}

int hv_io_init() 
{
#ifdef MULTI_DIMM
	int ways = interleave_ways(gid);
	int num_mem = hv_group[gid].num_hv/ways;
#else
	int ways = 1;
	int num_mem = 1;
#endif
	int i;

	calc_emmc_alloc(gid);

	for (i=0; i<num_mem; i++) {
		hv_group[gid].mem[i].p_dram = hv_group[gid].mem[i].p_mmio - HV_DRAM_SIZE;
		hv_mmap_mem(hv_group[gid].mem[i].p_mmio+MMIO_CMD_OFF*ways, 
				(unsigned long *) &hv_group[gid].mem[i].v_mmio, 
				MMIO_CMD_SIZE*ways);			
		hv_mmap_mem(hv_group[gid].mem[i].p_mmio+MMIO_BSM_W_OFF*ways, 
				(unsigned long *) &hv_group[gid].mem[i].v_bsm_w_mmio, 
				MMIO_BSM_W_SIZE*ways);				
		hv_mmap_mem(hv_group[gid].mem[i].p_mmio+MMIO_BSM_R_OFF*ways, 
				(unsigned long *) &hv_group[gid].mem[i].v_bsm_r_mmio, 
				MMIO_BSM_R_SIZE*ways);				
		hv_mmap_mem(hv_group[gid].mem[i].p_mmio+MMIO_OTHER_OFF*ways, 
				(unsigned long *) &hv_group[gid].mem[i].v_other_mmio, 
				MMIO_OTHER_SIZE);				
		hv_mmap_mem(hv_group[gid].mem[i].p_dram, 
				(unsigned long *) &hv_group[gid].mem[i].v_dram, 
				HV_DRAM_SIZE);
	}
	return 0;
}

void hv_io_release(void)
{
#ifdef MULTI_DIMM
	int ways = interleave_ways(gid);
	int num_mem = hv_group[gid].num_hv/ways;
#else
	int ways = 1;
	int num_mem = 1;
#endif
	int i;

	for (i=0; i<num_mem; i++) {
		hv_mumap_mem(hv_group[gid].mem[i].v_mmio, MMIO_CMD_SIZE*ways);			
		hv_mumap_mem(hv_group[gid].mem[i].v_bsm_w_mmio, MMIO_BSM_W_SIZE*ways);				
		hv_mumap_mem(hv_group[gid].mem[i].v_bsm_r_mmio, MMIO_BSM_R_SIZE*ways);				
		hv_mumap_mem(hv_group[gid].mem[i].v_other_mmio, MMIO_OTHER_SIZE*ways);				
		hv_mumap_mem(hv_group[gid].mem[i].v_dram, HV_DRAM_SIZE);
	}
}
#endif	// USER_SPACE_CMD_DRIVER

#else	// REV_B_MM

#ifdef MULTI_DIMM

/* 
 * reformat the command with updated lba
 */
static void reformat_command(int gid, int type, int hidx, int llba, void *cmd)
{
	struct HV_CMD_BSM_t *bsm_cmd;
	struct HV_CMD_MMLS_t *mmls_cmd;
	unsigned char cmd_type;
	unsigned int sector;

	/* cmd type is the 1st byte */
	cmd_type = *(unsigned char *)cmd;
	if (cmd_type == BSM_READ || cmd_type == BSM_WRITE || cmd_type == MMLS_READ || cmd_type == MMLS_WRITE) {
		if (type == GROUP_BSM) {
			bsm_cmd = (struct HV_CMD_BSM_t *)cmd;
			*(unsigned int *)&bsm_cmd->lba = llba+hv_group[gid].emmc[hidx].b_start;
			bsm_blk_size = (*(unsigned int *)&bsm_cmd->sector)*HV_BLOCK_SIZE;

			/* update bsm_cmd->sector if multi-way interleaving */
			sector = (*(unsigned int *)&bsm_cmd->sector);
#ifndef MIN_1KBLOCK_SUPPORT
			if (sector/interleave_ways(gid) < 8)
				/* force to 8 since min size of read/write a DIMM is 8 sectors
				   code to handle the data will fill unused space w '1' */
				sector = 8; 	
			else
#endif
				sector = sector/interleave_ways(gid);
			*(unsigned int *)&bsm_cmd->sector = sector;

			/* update mm_addr if HVDIMM is used for the case of '1' filling */
			if (bsm_blk_size < interleave_ways(gid)*DATA_BUFFER_SIZE)
				*(unsigned int *)&bsm_cmd->mm_addr = hv_group[gid].mem[hidx/interleave_ways(gid)].p_dram + BSM_DRAM_OFF;
#ifdef RDIMM_POPULATED
			/* update mm_addr if RDIMM is populated */
			*(unsigned int *)&bsm_cmd->mm_addr = hv_group[gid].mem[hidx/interleave_ways(gid)].p_dram + BSM_DRAM_OFF;
#endif

		}
		else {
			mmls_cmd = (struct HV_CMD_MMLS_t *)cmd;
			*(unsigned int *)&mmls_cmd->lba = llba+hv_group[gid].emmc[hidx].m_start;
			mmls_blk_size = (*(unsigned int *)&mmls_cmd->sector)*HV_BLOCK_SIZE;
			
			/* update mmls_cmd->sector if multi-way interleaving */
			sector = (*(unsigned int *)&mmls_cmd->sector);
#ifndef MIN_1KBLOCK_SUPPORT
			if (sector/interleave_ways(gid) < 8)
				/* force to 8 since min size of read/write a DIMM is 8 sectors
				   code to handle the data will fill unused space w '1' */
				sector = 8; 	
			else
#endif
				sector = sector/interleave_ways(gid);
			*(unsigned int *)&mmls_cmd->sector = sector;

			/* update mm_addr if HVDIMM is used for the case of '1' filling */
			if (mmls_blk_size < interleave_ways(gid)*DATA_BUFFER_SIZE)
				*(unsigned int *)&mmls_cmd->mm_addr = hv_group[gid].mem[hidx/interleave_ways(gid)].p_dram + MMLS_DRAM_OFF;
#ifdef RDIMM_POPULATED
			/* update mm_addr if RDIMM is populated */
			*(unsigned int *)&mmls_cmd->mm_addr = hv_group[gid].mem[hidx/interleave_ways(gid)].p_dram + MMLS_DRAM_OFF;
#endif
		}
	}
}


/*
 * write command to MMLS or interleaved BSM
 */
void hv_write_cmd (int gid, int type, long lba, void *cmd, long cmd_off)
{	
	int hidx, llba;
	int i, ways, mem;
	void *cmd_mmio, *cmd_mmio_saved;

	/* figure out index to emmc[] to its local lba */
	ways = interleave_ways(gid);
	if (calc_hidx_llba(gid, type, lba, ways, &hidx, &llba))
		return;
	if(!cmd)
		return;
	/* send command to all HVDIMM(s) */
	mem = hidx/ways;
	cmd_mmio_saved = cmd_mmio = (void *)(hv_group[gid].mem[mem].v_mmio+cmd_off*ways);

	/* reformat command line */
	/* assuming MMLS on all DIMMs starting from the same lba */
	reformat_command(gid, type, mem*ways, llba, cmd);
	for (i=0; i<ways; i++) {
		/* send command */
		// copy data to the memory location
		hv_mcpy(cmd_mmio, cmd, CMD_BUFFER_SIZE);
		// fake read to write data to FPGA
		if (cmd_status_use_cache)
			clflush_cache_range(cmd_mmio, CMD_BUFFER_SIZE);
		hv_mcpy(fake_mmls_buf, cmd_mmio, CMD_BUFFER_SIZE); 

		cmd_mmio = cmd_mmio + MEM_BURST_SIZE;
	}

}

static void proc_intv_nowrap (int gid, long lba, int index, void *data, long size, int type, int group)
{
	int hidx, llba;
	int i, j;
	int ways, mem;
	void *hv_dram, *tmp_dram, *tmp_data;
	int intv_size;

	/* figure out index to emmc[] to its local lba */
	ways = interleave_ways(gid);
	if (calc_hidx_llba(gid, group, lba, ways, &hidx, &llba))
		return;

	/* convert to virtual addr */
	mem = hidx/ways;
#ifndef MMLS_16K_ALIGNMENT
		if (group == GROUP_MMLS)
			hv_dram = (void *)hv_group[gid].mem[mem].v_dram + MMLS_DRAM_OFF + index*DATA_BUFFER_SIZE;
		else
			hv_dram = (void *)hv_group[gid].mem[mem].v_dram + BSM_DRAM_OFF + index*DATA_BUFFER_SIZE;
#else
		if (group == GROUP_MMLS)
			hv_dram = (void *)hv_group[gid].mem[mem].v_dram + MMLS_DRAM_OFF + index*MMLS_ALIGNMENT_SIZE;
		else
			hv_dram = (void *)hv_group[gid].mem[mem].v_dram + BSM_DRAM_OFF + index*MMLS_ALIGNMENT_SIZE;
#endif

	/* handle data on all HVDIMM(s) */
	if (type == HV_WRITE) {
#ifdef MIN_1KBLOCK_SUPPORT
		if (get_blk_size(group) < 0) 
#else
		if (get_blk_size(group) < ways*DATA_BUFFER_SIZE) 
#endif
		{
			memset (hv_dram, 1, DATA_BUFFER_SIZE*ways);	/* fill w 1 */
			intv_size = size/ways;
			size = ways*DATA_BUFFER_SIZE;

			for (i=0; i<ways; i++) {
				tmp_dram = hv_dram+MEM_BURST_SIZE*i;
				tmp_data = data+intv_size*i;
				for (j=0; j<intv_size/MEM_BURST_SIZE; j++) {
					hv_mcpy (tmp_dram, tmp_data, MEM_BURST_SIZE);
					tmp_dram += ways*MEM_BURST_SIZE;
					tmp_data += MEM_BURST_SIZE;
				}
			}

			/* fake read from hv_dram */
			if (fake_read_cache_flush)
				clflush_cache_range(hv_dram, size);
			hv_mcpy (fake_mmls_buf, hv_dram, size);

		}
		else {
#if defined (RDIMM_POPULATED) || defined (MMLS_16K_ALIGNMENT)
			hv_mcpy (hv_dram, data, size);
			/* fake read from hv_dram */
			if (fake_read_cache_flush)
				clflush_cache_range(hv_dram, size);
			hv_mcpy (fake_mmls_buf, hv_dram, size);
#else
			/* fake read from hv_dram */
			if (fake_read_cache_flush)
				clflush_cache_range(data, size);
			hv_mcpy (fake_mmls_buf, data, size);
#endif

		}
	}
	else {
#ifdef MIN_1KBLOCK_SUPPORT
		if (get_blk_size(group) < 0) 
#else
		if (get_blk_size(group) < ways*DATA_BUFFER_SIZE) 
#endif
		{
			intv_size = size/ways;
			size = ways*DATA_BUFFER_SIZE;

			/* fake write to hv_dram */
			hv_mcpy (hv_dram, fake_mmls_buf, size);
			if (fake_write_cache_flush)
				clflush_cache_range(hv_dram, size);

			for (i=0; i<ways; i++) {
				tmp_dram = hv_dram+MEM_BURST_SIZE*i;
				tmp_data = data+intv_size*i;
				for (j=0; j<intv_size/MEM_BURST_SIZE; j++) {
					hv_mcpy (tmp_data, tmp_dram, MEM_BURST_SIZE);
					tmp_dram += ways*MEM_BURST_SIZE;
					tmp_data += MEM_BURST_SIZE;
				}
			}
		}
		else {

#if defined (RDIMM_POPULATED) || defined (MMLS_16K_ALIGNMENT)
			/* fake write to hv_dram */
			hv_mcpy (hv_dram, fake_mmls_buf, size);
			if (fake_write_cache_flush)
				clflush_cache_range(hv_dram, size);
			hv_mcpy (data, hv_dram, size);
#else
			/* fake write to hv_dram */
			hv_mcpy (data, fake_mmls_buf, size);
			if (fake_write_cache_flush)
				clflush_cache_range(data, size);
#endif
		}
	}	
}


static void proc_intv_data (int gid, long lba, int index, void *data, long size, int type, int group)
{
	long remain_size;

	if ((remain_size=intv_remain_sz(gid, index, size)) >= size)  {
		proc_intv_nowrap (gid, lba, index, data, size, type, group);
	}
	else {
		/* handle the case that index needs to wrap around */
		proc_intv_nowrap (gid, lba, index, data, remain_size, type, group);
		proc_intv_nowrap (gid, lba, 0, data+remain_size, size-remain_size, type, group);
	}
}

void hv_write_ecc (int gid, int lba, void *cmd)
{
}

/*
 *
 * size can be 4, 8, 12 or 16KB
 *
 */
void hv_write_bsm_data (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
	proc_intv_data (gid, lba, index, data, size, HV_WRITE, GROUP_BSM);

}

void hv_read_bsm_data (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
	proc_intv_data (gid, lba, index, data, size, HV_READ, GROUP_BSM);

}


/*
 *
 * command driver uses
 *	size = min(block_size, ways*buffer_cnt*4KB)
 *
 * when calling
 *	hv_mmls_fake_read (int gid, long lba, int index, void *data, long size)
 *	hv_mmls_fake_write (int gid, long lba, int index, void *data, long size)
 * 
 * then it updates index as follows (index should cycle between 0-3)
 * 	if size is 4KB
 * 		increment index by 1
 * 	else if size is 8KB, 
 *		if 1-way interleaving
 *			increment index by 2
 *		else
 *			increment index by 1
 *	else
 *		increment index by size/(4KB*ways)
 *
 */
void hv_mmls_fake_read (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
	proc_intv_data (gid, lba, index, data, size, HV_WRITE, GROUP_MMLS);
}

void hv_mmls_fake_write (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
	proc_intv_data (gid, lba, index, data, size, HV_READ, GROUP_MMLS);
}

unsigned char hv_read_status (int gid, int type, long lba, long offset)
{
	int hidx, llba;
	int i=0, ways, mem;

	void *status_mmio;
	unsigned char more_status, status;

	/* figure out index to emmc[] to its local lba */
	ways=interleave_ways(gid);
	if (calc_hidx_llba(gid, type, lba, ways, &hidx, &llba))
		return -1;

	/* read status of all HVDIMM(s) */
	mem = hidx/ways;

	status_mmio = (void *)(hv_group[gid].mem[mem].v_mmio + offset*ways);		
	/* read 1st status */
	// fake write 
	hv_mcpy(status_mmio, fake_mmls_buf, MEM_BURST_SIZE);
	if (cmd_status_use_cache)
		clflush_cache_range(status_mmio, MEM_BURST_SIZE);
	//read status data
	hv_mcpy (&status, status_mmio, sizeof(unsigned char));
	status_mmio = status_mmio + MEM_BURST_SIZE;
	for (i=1; i<ways; i++) {
		/* read subsequent status */
		// fake write 
		hv_mcpy(status_mmio, fake_mmls_buf, MEM_BURST_SIZE);
		if (cmd_status_use_cache)
			clflush_cache_range(status_mmio, MEM_BURST_SIZE);
		//read status data
		hv_mcpy (&more_status, status_mmio, sizeof(unsigned char));
		if (more_status != status)
			return 0;
		status_mmio = status_mmio + MEM_BURST_SIZE;
	}
	return status;

}


unsigned char hv_query_status (int gid, int type, long lba, unsigned char status)
{
	int hidx, llba;
	int i=0, ways, mem;
	void *status_mmio;
	unsigned char q_status;
	unsigned char ettc_status;
	int ettc_val=0;

	pr_mmio("%s: entered\n", __func__);

	/* figure out index to emmc[] to its local lba */
	ways=interleave_ways(gid);
	if (calc_hidx_llba(gid, type, lba, ways, &hidx, &llba))
		return -1;

	/* read status of all HVDIMM(s) */
	mem = hidx/ways;

	status_mmio = (void *)(hv_group[gid].mem[mem].v_mmio + QUERY_STATUS_OFF*ways);
	for (i=0; i<ways; i++) {
		/* read status */
		// fake write 
		hv_mcpy(status_mmio, fake_mmls_buf, MEM_BURST_SIZE);
		if (cmd_status_use_cache)
			clflush_cache_range(status_mmio, MEM_BURST_SIZE);
		//read status data
		hv_mcpy (&q_status, status_mmio, sizeof(unsigned char));
		if ((q_status & status) != status) {
			if (((q_status & 0xF0) >> 4) > ettc_val) {
				/*  cmd not done and ETtC is larger */
				ettc_val = (q_status & 0xF0) >> 4;
				ettc_status = q_status;
			}
			if (ettc_val > 0)
				return ettc_status;
		}
		status_mmio = status_mmio + MEM_BURST_SIZE;
	}
	return status;

}

unsigned long hv_get_dram_addr (int type, void *addr)
{
	// Need SW to convert physical address to dram address in multi DIMM 
	return 0;	
}

#else	/* MULTI_DIMM */

int interleave_ways (int gid)
{
	return 1;
}

void hv_write_cmd (int gid, int type, long lba, void *cmd, void *addr)
{
//MK1102-begin for debugging only
#if 0	//MK1110 temporarily removed
	unsigned char i2c_reg[32], master_cmd_buff[32], *pbyte=(unsigned char *)cmd;
	unsigned int i, j;

	for (i=0; i < 8; i++)
	{
		for (j=0; j < 4; j++)
		{
			master_cmd_buff[i*4+j] = *(pbyte+(i*8)+j);
		}
		pr_mmio("cmdbuff %02u: %.2x %.2x %.2x %.2x\n",
				i*8, master_cmd_buff[i*4], master_cmd_buff[(i*4)+1],
				master_cmd_buff[(i*4)+2], master_cmd_buff[(i*4)+3]);
	}
	pr_mmio("%s: target addr = 0x%.16lx\n", __func__, (unsigned long)addr);
#endif	//MK1110
//MK1102-end
//MK	pr_mmio("%s: entered cmd off 0x%lx\n", __func__, (unsigned long) addr);
//MK-begin
//MK1013	pr_mmio("[%s]: writing command (%#.2x) to %#.16lx\n", __func__, *(unsigned char *)cmd, (unsigned long) addr);
//MK-end
	if(!cmd)
		cmd = cmd_burst_buf;

//MK1103	// copy data to the memory location
//MK1103	hv_mcpy(addr, cmd, CMD_BUFFER_SIZE);
//MK1103-begin
	/* Write a 64-byte command descriptor to MMIO Command Region */
	memcpy_64B_movnti(addr, cmd);
//MK1103-end

	// fake read to write data to FPGA
	if (cmd_status_use_cache)
		clflush_cache_range(addr, CMD_BUFFER_SIZE);
//MK	hv_mcpy(fake_mmls_buf, addr, CMD_BUFFER_SIZE);

//MK1102-begin for debugging only
#if 0	//MK1110 temporarily removed
	/* Read 32 bytes of command desc for master FPGA from I2C registers */
	write_smbus(3, 5, 0xA0, 1);		// Set to page 1
	write_smbus(3, 7, 0xA0, 1);		// Set to page 1
	udelay(10000);					// 10ms delay
	for (i=0; i < 8; i++)
	{
		for (j=0; j < 4; j++)
		{
			i2c_reg[i*4+j] = (unsigned char) read_smbus(3, 5, (i*0x10)+j);
		}
		pr_mmio("0x%.3x: %.2x %.2x %.2x %.2x\n",
				(i*0x10)+0x100, i2c_reg[i*4], i2c_reg[(i*4)+1], i2c_reg[(i*4)+2], i2c_reg[(i*4)+3]);
	}
	write_smbus(3, 5, 0xA0, 0);		// Back to page 0
	write_smbus(3, 7, 0xA0, 0);		// Back to page 0
	udelay(10000);					// 10ms delay
#endif	//MK1110
//MK1102-end
}

#ifndef MMLS_16K_ALIGNMENT 
#define mmls_dram_off(index)	(MMLS_DRAM_OFF+index*DATA_BUFFER_SIZE)
#define bsm_dram_off(index)		(BSM_DRAM_OFF+index*DATA_BUFFER_SIZE)
#else
#define mmls_dram_off(index)	(MMLS_DRAM_OFF+index*MMLS_ALIGNMENT_SIZE)
#define bsm_dram_off(index)		(BSM_DRAM_OFF+index*MMLS_ALIGNMENT_SIZE)
#endif

void hv_write_bsm_data (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
#if defined (RDIMM_POPULATED) || defined (MMLS_16K_ALIGNMENT)
	hv_mcpy(bsm_dram_off(index), data, size);
	if (fake_read_cache_flush)
		clflush_cache_range(bsm_dram_off(index), size);
	hv_mcpy(fake_data_buf, bsm_dram_off(index), size); 	/* fake read */
#else
	if (fake_read_cache_flush)
		clflush_cache_range(data, size);
	hv_mcpy(fake_data_buf, data, size); 			/* fake read */
#endif

}

void hv_read_bsm_data (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
#if defined (RDIMM_POPULATED) || defined (MMLS_16K_ALIGNMENT)
	hv_mcpy(bsm_dram_off(index), fake_data_buf, size); 	/* fake write */
	if (fake_write_cache_flush)
		clflush_cache_range(bsm_dram_off(index), size);
	hv_mcpy((void *)data, bsm_dram_off(index), size);
#else
	hv_mcpy(data, fake_data_buf, size); 			/* fake write */
	if (fake_write_cache_flush)
		clflush_cache_range(data, size);
#endif

}

void hv_mmls_fake_read (int gid, long lba, int index, void *data, long size)
{
//MK	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
//MK0818-begin
//	pr_mmio("[%s]: target address=%#.16lx index=%d size=%ld\n", __func__, (unsigned long)data, index, size);
//MK0818-end
#if defined (RDIMM_POPULATED) || defined (MMLS_16K_ALIGNMENT)
	hv_mcpy(mmls_dram_off(index), data, size);
	if (fake_read_cache_flush)
		clflush_cache_range(mmls_dram_off(index), size);
	hv_mcpy(fake_data_buf, mmls_dram_off(index), size);		/* fake read */
#else
	if (fake_read_cache_flush)
		clflush_cache_range(data, size);
	hv_mcpy(fake_data_buf, data, size);				/* fake read */
#endif

//MK0518-begin
	/* Send a termination command to indicate the end of fake-read operation
	 * to FPGA.
	 */
	if ( bsm_wrt_skip_termination_enabled() ) {
		if ( bcom_toggle_enabled() ) {
		} else {
			udelay(2);
			pr_debug("[%s]: User Delay 2us \n", __func__);
		}
		pr_debug("[%s]: skipping termination command\n", __func__);
	} else {
		hv_write_termination(0, 0, lba, NULL);
	}
//MK0518-end
}

//MK0728-begin
void hv_mmls_fake_read_2(int gid, long lba, int index, void *pdata, long size)
{
	unsigned long *pbuff=(unsigned long *) pdata;
	unsigned long *pfakebuff=(unsigned long *) fake_data_buf;
	unsigned long i, burst_index, total_burst_count;


	total_burst_count = 4096 >> 6;	// 4KB / 64 bytes

	pr_mmio("[%s]: target address=%#.16lx total burst count=%ld size=%ld\n", __func__, (unsigned long)pdata, total_burst_count, size);

	for (burst_index=0; burst_index < total_burst_count; burst_index++) {

		/* Since we are reading 64 bytes at a time, flush as much as we read. */
		clflush_cache_range((void *)pbuff, 64);

		/* Fake-read eight 64-bit data (= 64bytes) */
		for (i=0; i < 8; i++)
		{
			/* Fake-read 64-bit at a time */
			*pfakebuff++ = *pbuff++;
		}

		/*
		 * We just completed reading 64 bytes of data from one 128KB block
		 * in HVDIMM. Advance the pointer to the beginning of the next 128KB
		 * block. Note: we are not going to read from this 128KB right away.
		 */
		pbuff += 8;

		/* Advance ptr to a block with the next burst index */
		if (burst_index % 2 == 0)
			/*
			 * The next 128KB block is (8KB-128bytes) away. "1024" means 1024
			 * 64-bit words (qword) and "16" means 16 qwords, which is
			 * 128 bytes.
			 */
			pbuff = pbuff + 1024 - 16;
		else
			/*
			 * The next 128KB block is (8KB) away. "1024" means 1024 64-bit word
			 * (qword) backward.
			 */
			pbuff = pbuff - 1024;
	}

}

void hv_mmls_fake_read_3(int gid, long lba, int index, void *pdata, long size)
{
	unsigned long *pbuff=(unsigned long *) pdata;
	unsigned long *pfakebuff=(unsigned long *) fake_data_buf;
	unsigned long i, burst_index, total_burst_count;


	total_burst_count = 4096 >> 6;	// 4KB / 64 bytes

	pr_mmio("[%s]: target address=%#.16lx total burst count=%ld size=%ld\n", __func__, (unsigned long)pdata, total_burst_count, size);

	for (burst_index=0; burst_index < total_burst_count; burst_index++) {

		/* Since we are reading 64 bytes at a time, flush as much as we read. */
		clflush_cache_range((void *)pbuff, 64);

		/* Fake-read eight 64-bit data (= 64bytes) */
		for (i=0; i < 8; i++)
		{
			/* Fake-read 64-bit at a time */
			*pfakebuff++ = *pbuff++;
		}

		/*
		 * We just completed reading 64 bytes of data in HVDIMM DRAM. Skip
		 * the next 64 bytes of space, which belongs to another bank because
		 * our FPGA doesn't handle this case yet.
		 */
		pbuff += 8;
	}

}
//MK0728-end

void hv_mmls_fake_write (int gid, long lba, int index, void *data, long size)
{
//MK	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
//MK0818-begin
//	pr_mmio("[%s]: target address = %#.16lx index=%d size=%ld\n", __func__, (unsigned long)data, index, size);
//MK0818-end

//MK0105-begin
//MK0222#if (DEBUG_FEAT_BCOM_ONOFF == 1)
//MK0222	hv_enable_bcom();
//MK0222#endif
//MK0105-end

//MK0116-begin for devel only
	/* This fake buffer is written with data stored in DRAM during the fake-read
	 * operation and then it is read during this fake-write operation.
	 * If fake-write operation is not working correctly the content
	 * of this fake buffer may be written to the target DRAM location during
	 * the fake-write operation and we may end up getting unexpected passes
	 * during the buffer comparison steps. So, we want to clear this buffer
	 * before we do a fake-write operation. If the target DRAM location got
	 * zeros that means normal write operation was done instead of a fake-write
	 * operation. */
//MK0519	memset(fake_data_buf, 0x5A, size);
//MK0116-end

//MK0302//MK0222-begin
//MK0302#if (DEBUG_FEAT_BCOM_ONOFF == 1)
//MK0302	hv_enable_bcom();
//MK0302#endif
//MK0302//MK0222-end
//MK0302-begin
	/* If BCOM toggle is enabled, enable BCOM now */
	if ( bcom_toggle_enabled() ) {
		hv_enable_bcom();
	}
//MK0302-end

#if defined (RDIMM_POPULATED) || defined (MMLS_16K_ALIGNMENT)
	hv_mcpy(mmls_dram_off(index), fake_data_buf, size);		/* fake write */
	if (fake_write_cache_flush)
		clflush_cache_range(mmls_dram_off(index), size);
	hv_mcpy((void *)data, mmls_dram_off(index), size);
#else
//MK0109	hv_mcpy(data, fake_data_buf, size);				/* fake write */
//MK0109-begin
//MK0519	memcpy_64B_movnti_3(data, fake_data_buf, size);			/* fake write */
//MK0519-begin
	memcpy_64B_movnti_3(data, (void *)fake_mmls_buf, size);	/* fake write */
//MK0519-end
//MK0124-begin
	/* memcpy in descending order */
//	memcpy_64B_movnti_4(data+size-64, fake_data_buf+size-64, size);			/* fake write */
//MK0124-end
//MK0109-end
	if (fake_write_cache_flush)
		clflush_cache_range(data, size);
#endif

//MK0518-begin
	/* Send a termination command before disabling BCOM indicating the end of
	 * fake-write operation to FPGA.
	 */
	if ( bsm_rd_skip_termination_enabled() ) {
		if ( bcom_toggle_enabled() ) {
		} else {
			udelay(2);
			pr_debug("[%s]: User Delay 2us \n", __func__);
		}
		pr_debug("[%s]: skipping termination command\n", __func__);
	} else {
		hv_write_termination(0, 0, lba, NULL);
	}
//MK0518-end

//MK0302//MK0105-begin
//MK0302#if (DEBUG_FEAT_BCOM_ONOFF == 1)
//MK0302	hv_disable_bcom();
//MK0302#endif
//MK0302//MK0105-end
//MK0302-begin
	/* If BCOM toggle is enabled, disable BCOM now */
	if ( bcom_toggle_enabled() ) {
		hv_disable_bcom();
	}
//MK0302-end

}

unsigned char hv_read_status (int gid, int type, long lba, void *addr)
{
//MK0224	unsigned char q_status=0;
//MK1024-begin
//#define DEBUG_STATUS_BYTE_OFFSET		0
//	unsigned char *pByte=(unsigned char *)addr;
//	unsigned long *pQword=(unsigned long *)addr;
//MK1024-end

//MK	pr_mmio("%s: entered cmd off 0x%lx\n", __func__, (unsigned long) addr);
//MK1024-begin
//	pr_mmio("[%s]: Addr of Status Byte = 0x%lx\n", __func__, (unsigned long) (pByte+DEBUG_STATUS_BYTE_OFFSET));
//MK0127	pr_mmio("[%s]: Addr of Status Byte = 0x%lx\n", __func__, (unsigned long) addr);
//MK1024-end

//MK0302//MK0105-begin
//MK0302#if (DEBUG_FEAT_BCOM_ONOFF == 1)
//MK0302	hv_enable_bcom();
//MK0302#endif
//MK0302//MK0105-end
//SJ0313//MK0302-begin
//SJ0313	/* If BCOM toggle is enabled, enable BCOM now */
//SJ0313	if ( bcom_toggle_enabled() ) {
//SJ0313		hv_enable_bcom();
//SJ0313	}
//SJ0313//MK0302-end
//SJ0313-begin
	/* If BCOM toggle is enabled, enable BCOM now */
	if ( bcom_toggle_enabled() ) {
		hv_enable_bcom();
	} else {
//MK0605		hv_delay_us(get_user_defined_delay());
//MK0605-begin
		hv_delay_us(get_user_defined_delay_us(0));	// delay index 0
//MK0605-end
	}
//SJ0313-end

	// fake write 
//MK1103	hv_mcpy(addr, fake_mmls_buf, MEM_BURST_SIZE);
//MK1103-begin
	/* Presight-write 64 bytes of dummy data to the selected MMIO status */
//MK0222	memcpy_64B_movnti(addr, (void*)fake_mmls_buf);
	memcpy_64B_movnti_3(addr, (void*)fake_mmls_buf, 64);	//MK0222
//MK1103-end
	if (cmd_status_use_cache)
		clflush_cache_range(addr, MEM_BURST_SIZE);

//MK0302//MK0105-begin
//MK0302#if (DEBUG_FEAT_BCOM_ONOFF == 1)
//MK0302	hv_disable_bcom();
//MK0302#endif
//MK0302//MK0105-end
//SJ0313//MK0302-begin
//SJ0313	/* If BCOM toggle is enabled, disable BCOM now */
//SJ0313	if ( bcom_toggle_enabled() ) {
//SJ0313		hv_disable_bcom();
//SJ0313	}
//SJ0313//MK0302-end
//SJ0313-begin
	/* If BCOM toggle is enabled, disable BCOM now */
	if ( bcom_toggle_enabled() ) {
		hv_disable_bcom();
	} else {
//MK0605		hv_delay_us(get_user_defined_delay());
//MK0605-begin
		hv_delay_us(get_user_defined_delay_us(0));	// delay index 0
//MK0605-end
	}
//SJ0313-end

	/* Read status byte */
//MK0224	hv_mcpy(&q_status, addr, sizeof(unsigned char));
//MK0224-begin
	/* Let's use status_buf to read status */
	clflush_cache_range((void *)cmd_burst_buf, CMD_BUFFER_SIZE);	// SJ0227
	hv_mcpy((void *)cmd_burst_buf, (void *)addr, CMD_BUFFER_SIZE);
//MK0307	return cmd_burst_buf[0];
//MK0307-begin
	if (addr == QUERY_STATUS_OFF) {
		if ( slave_cmd_done_check_enabled() ) {
//MK0616			return( (cmd_burst_buf[4] & 0x0C) | ((cmd_burst_buf[0] & 0x0C) >> 2) );
			return( ((cmd_burst_buf[4] & 0x0E) << 2) | ((cmd_burst_buf[0] & 0x0E) >> 1) );	//MK0616
		} else {
			return cmd_burst_buf[0];
		}
	} else {
		return cmd_burst_buf[0];
	}
//MK0307-end

//MK0224-end

//MK1024-begin
//	hv_mcpy(&q_status, (void *)(pByte+DEBUG_STATUS_BYTE_OFFSET), sizeof(unsigned char));;
//	pr_mmio("[%s]: Q.C.S. = 0x%.16lx - 0x%.16lx - 0x%.16lx - 0x%.16lx\n",
//			__func__, *pQword, *(pQword+1), *(pQword+2), *(pQword+3));
//	pr_mmio("[%s]:          0x%.16lx - 0x%.16lx - 0x%.16lx - 0x%.16lx\n",
//			__func__, *(pQword+4), *(pQword+5), *(pQword+6), *(pQword+7) );
//MK1024-end

//MK0224	return q_status;
}

//MK0120-begin
void hv_read_fake_buffer_checksum(unsigned char fake_rw_flag, struct block_checksum_t *pcs)
{
//	unsigned char checksum=0xEE;


#if 0	//MK0126
//	unsigned short signature=0;
//	unsigned int retry_count=3;

	pr_mmio("[%s]: MMIO region addr for checksum = 0x%lx\n", __func__, (unsigned long) addr);

	/* We are writing zeros to the fake target */
	memset(fake_mmls_buf, 0, 64);

//	while ( (signature != 0xDEAD) && (retry_count > 0) )
//	{

#if (DEBUG_FEAT_BCOM_ONOFF == 1)
		hv_enable_bcom();
#endif

		/* Presight-write 64 bytes of dummy data to the selected MMIO status */
		memcpy_64B_movnti_3(addr, (void*)fake_mmls_buf, 64);

		if (cmd_status_use_cache)
			clflush_cache_range(addr, MEM_BURST_SIZE);

#if (DEBUG_FEAT_BCOM_ONOFF == 1)
		hv_disable_bcom();
#endif

		/* Assume invalid checksum */
//		checksum = 0xEE;

		/* Check for a 16-bit signature word */
//		hv_mcpy(&signature, addr+2, sizeof(unsigned short));
//		if (signature == 0xDEAD) {
			/* Read checksum byte */
			hv_mcpy(&checksum, addr+1, sizeof(unsigned char));
//			break;
//		}

		/*
		 * In case that we are running this code on an FPGA image that doesn't
		 * have the signature feature, we still need to return checksum.
		 */
//		hv_mcpy(&checksum, addr+1, sizeof(unsigned char));
//		retry_count--;
//	} // end of while

//	pr_mmio("[%s]: retry_count=%d, signature=0x%.4X, checksum=0x%.2X\n", __func__, retry_count, signature, checksum);
#endif	//MK0126

//MK0126-begin
	unsigned int i2c_addr_offset=0;
//MK0307-begin
	unsigned int data_cs=0;
	struct hv_query_cmd_status *pqcs=(struct hv_query_cmd_status *)status_buf;
	unsigned char idx=(fake_rw_flag & 0x03);
	unsigned int fpga_id=FPGA1_ID;

	/*
	 * [1]: 0=master FPGA, 1=slave FPGA
	 * [0]: 0=fake-write, 1=fake-read
	 */
	if (idx == 0) {
		/* Fake-write / master */
		fpga_id = FPGA1_ID;
		i2c_addr_offset = 0;
//MK0425		data_cs = pqcs->master_checksum1;
		data_cs = pqcs->fw_m_cs1;	//MK0425
	} else if (idx == 1) {
		/* Fake-read / master */
		fpga_id = FPGA1_ID;
		i2c_addr_offset = 4;
//MK0425		data_cs = pqcs->master_checksum1;
		data_cs = pqcs->fr_m_cs1;	//MK0425
	} else if (idx == 2) {
		/* Fake-write / slave */
		fpga_id = FPGA2_ID;
		i2c_addr_offset = 0;
//MK0425		data_cs = pqcs->slave_checksum1;
		data_cs = pqcs->fw_s_cs1;	//MK0425
	} else {
		/* Fake-read / slave */
		fpga_id = FPGA2_ID;
		i2c_addr_offset = 4;
//MK0425		data_cs = pqcs->slave_checksum1;
		data_cs = pqcs->fr_s_cs1;	//MK0425
	}
//MK0307-end

#if 0	//MK0307
	write_smbus(3, 5, 0xA0, 2);		// Set to page 2
	udelay(1000);

	if (fake_rw_flag != 0) {
		/* Offset adjustment for fake-read checksum addr */
		i2c_addr_offset = 4;
	}

	pcs->sub_cs[0] = (unsigned char) read_smbus(3, FPGA1_ID, 0+i2c_addr_offset);
	udelay(100);
	pcs->sub_cs[1] = (unsigned char) read_smbus(3, FPGA1_ID, 1+i2c_addr_offset);
	udelay(100);
	pcs->sub_cs[2] = (unsigned char) read_smbus(3, FPGA1_ID, 2+i2c_addr_offset);
	udelay(100);
	pcs->sub_cs[3] = (unsigned char) read_smbus(3, FPGA1_ID, 3+i2c_addr_offset);
	udelay(100);

	write_smbus(3, 5, 0xA0, 0);		// Set to page 0
#endif	//MK0307

//MK0307-begin
	/*
	 * Always get data checksum from I2C if it is for fake-read. Otherwise,
	 * check data CS location flag.
	 */
//MK0425	if ( i2c_addr_offset == 4 || get_data_cs_location() != 0 ) {
	if ( get_data_cs_location() == 1 ) {	//MK0425
		/* Get data checksum from I2C registers */
		write_smbus(3, fpga_id, 0xA0, 2);	// Set to page 2
		udelay(1000);

		pcs->sub_cs[0] = (unsigned char) read_smbus(3, fpga_id, 0+i2c_addr_offset);
		udelay(100);
		pcs->sub_cs[1] = (unsigned char) read_smbus(3, fpga_id, 1+i2c_addr_offset);
		udelay(100);
		pcs->sub_cs[2] = (unsigned char) read_smbus(3, fpga_id, 2+i2c_addr_offset);
		udelay(100);
		pcs->sub_cs[3] = (unsigned char) read_smbus(3, fpga_id, 3+i2c_addr_offset);
		udelay(100);

		write_smbus(3, fpga_id, 0xA0, 0);	// Set to page 0
	} else {
		/* Get data checksum from the query command status burst saved before */
		*(unsigned int *)&pcs->sub_cs[0] = data_cs;
	}
//MK0307-end

	return;
//MK0126-end

//MK0126	return checksum;
}
//MK0120-end

//MK0214-begin
void hv_read_fake_buffer_checksum_2(unsigned char fake_rw_flag, struct block_checksum_t *pcs)
{
	unsigned int i2c_addr_offset=0;
//MK0307-begin
	unsigned int data_cs=0;
	struct hv_query_cmd_status *pqcs=(struct hv_query_cmd_status *)status_buf;
	unsigned char idx=(fake_rw_flag & 0x03);
	unsigned int fpga_id=FPGA1_ID;

	/*
	 * [1]: 0=master FPGA, 1=slave FPGA
	 * [0]: 0=fake-write, 1=fake-read
	 */
	if (idx == 0) {
		/* Fake-write / master */
		fpga_id = FPGA1_ID;
		i2c_addr_offset = 0;
//MK0425		data_cs = pqcs->master_checksum2;
		data_cs = pqcs->fw_m_cs2;	//MK0425
	} else if (idx == 1) {
		/* Fake-read / master */
		fpga_id = FPGA1_ID;
		i2c_addr_offset = 4;
//MK0425		data_cs = pqcs->master_checksum2;
//MK0425		data_cs = pqcs->fr_m_cs2;	//MK0425
		data_cs = 0;	//MK0425 - cs2 not supported for fake-read
	} else if (idx == 2) {
		/* Fake-write / slave */
		fpga_id = FPGA2_ID;
		i2c_addr_offset = 0;
//MK0425		data_cs = pqcs->slave_checksum2;
		data_cs = pqcs->fw_s_cs2;	//MK0425
	} else {
		/* Fake-read / slave */
		fpga_id = FPGA2_ID;
		i2c_addr_offset = 4;
//MK0425		data_cs = pqcs->slave_checksum2;
//MK0425		data_cs = pqcs->fr_s_cs2;	//MK0425
		data_cs = 0;	//MK0425 - cs2 not supported for fake-read
	}
//MK0307-end

#if 0	//MK0307
	write_smbus(3, 5, 0xA0, 2);		// Set to page 2
	udelay(1000);

	if (fake_rw_flag != 0) {
		/* Offset adjustment for fake-read checksum addr */
		i2c_addr_offset = 4;
	}

	pcs->sub_cs[0] = (unsigned char) read_smbus(3, FPGA1_ID, 8+i2c_addr_offset);
	udelay(100);
	pcs->sub_cs[1] = (unsigned char) read_smbus(3, FPGA1_ID, 9+i2c_addr_offset);
	udelay(100);
	pcs->sub_cs[2] = (unsigned char) read_smbus(3, FPGA1_ID, 10+i2c_addr_offset);
	udelay(100);
	pcs->sub_cs[3] = (unsigned char) read_smbus(3, FPGA1_ID, 11+i2c_addr_offset);
	udelay(100);

	write_smbus(3, 5, 0xA0, 0);		// Set to page 0
#endif	//MK0307

//MK0307-begin
	/*
	 * Always get data checksum from I2C if it is for fake-read. Otherwise,
	 * check data CS location flag.
	 */
//MK0425	if ( i2c_addr_offset == 4 || get_data_cs_location() != 0 ) {
	if ( i2c_addr_offset == 0 && get_data_cs_location() == 1 ) {	//MK0425
		/* Get data checksum from I2C registers */
		write_smbus(3, fpga_id, 0xA0, 2);	// Set to page 2
		udelay(1000);

		pcs->sub_cs[0] = (unsigned char) read_smbus(3, fpga_id, 8+i2c_addr_offset);
		udelay(100);
		pcs->sub_cs[1] = (unsigned char) read_smbus(3, fpga_id, 9+i2c_addr_offset);
		udelay(100);
		pcs->sub_cs[2] = (unsigned char) read_smbus(3, fpga_id, 10+i2c_addr_offset);
		udelay(100);
		pcs->sub_cs[3] = (unsigned char) read_smbus(3, fpga_id, 11+i2c_addr_offset);
		udelay(100);

		write_smbus(3, fpga_id, 0xA0, 0);	// Set to page 0
	} else {
		/* Get data checksum from the query command status burst saved before */
		*(unsigned int *)&pcs->sub_cs[0] = data_cs;
	}
//MK0307-end

	return;
}
//MK0214-end

//MK0207-begin
void hv_read_mmio_command_checksum(struct fpga_debug_info_t *pdebuginfo)
{
	unsigned int dword=0;

	write_smbus(3, 5, 0xA0, 2);		// Set to page 2
	udelay(1000);

	/* Get LBA most recently used by FPGA */
	dword = (unsigned int)read_smbus(3, FPGA1_ID, 0x5B) & 0x000000FF;
	dword <<= 8;
	udelay(100);

	dword |= ((unsigned int)read_smbus(3, FPGA1_ID, 0x5A) & 0x000000FF);
	dword <<= 8;
	udelay(100);

	dword |= ((unsigned int)read_smbus(3, FPGA1_ID, 0x59) & 0x000000FF);
	dword <<= 8;
	udelay(100);

	dword |= ((unsigned int)read_smbus(3, FPGA1_ID, 0x58) & 0x000000FF);
	udelay(100);

	pdebuginfo->mmio_cmd_checksum = dword;

	write_smbus(3, 5, 0xA0, 0);		// Set to page 0
	return;
}

void hv_display_mmio_command_slaveio(void)
{
	unsigned char byte_data=0;
	int i, j;
	unsigned int dword[8]={0,0,0,0,0,0,0,0};

	write_smbus(3, 5, 0xA0, 1);		// Set to page 1
	udelay(1000);

	for (i=0; i < 8; i++)
	{
		for (j=3; j >= 0; j--)
		{
			byte_data = (unsigned char)read_smbus(3, FPGA1_ID, i*0x10+j);
			udelay(100);
			dword[i] |= ((unsigned int)byte_data) << j*8;
		}
		pr_mmio("[0x%.2X]: %.2X %.2X %.2X %.2X\n", i*0x10,
				(dword[i]) & 0x000000FF,
				(dword[i] >> 8) & 0x000000FF,
				(dword[i] >> 16) & 0x000000FF,
				(dword[i] >> 24) & 0x000000FF);
	}

	write_smbus(3, 5, 0xA0, 0);		// Set to page 0
	return;
}

void hv_reset_internal_state_machine(void)
{
	pr_mmio("[%s] FPGA internal state machine reset\n", __func__);

//MK0321-begin
	if (get_fpga_reset_ctrl_method() ) {
//MK0321-end
	write_smbus(3, 5, 0xA0, 0);
	udelay(1000);
	write_smbus(3, 7, 0xA0, 0);		//SJ0303
	udelay(1000);

	write_smbus(3, 5, 0x68, 0xE0);
	udelay(1000);
	write_smbus(3, 7, 0x68, 0xE0);	//SJ0303
	udelay(1000);

	write_smbus(3, 5, 0x68, 0);
	udelay(1000);
	write_smbus(3, 7, 0x68, 0);		//SJ0303
//MK0321-begin
	} else {
		/* Do dummy write to RESET_OFF in MMIO region */
//MK0519		hv_mcpy(RESET_OFF, cmd_burst_buf, CMD_BUFFER_SIZE);
//MK0519-begin
		memcpy_64B_movnti_3(RESET_OFF, (void*)fake_mmls_buf, CMD_BUFFER_SIZE);
//MK0519-end
		udelay(1000);
	}
//MK0321-end
	return;
}
//MK0207-end

//SJ0313-begin
void hv_reset_bcom_control(void)
{
	pr_mmio("[%s] FPGA1 bcom control reset\n", __func__);

	write_smbus(3, 5, 0x6, 0x80);
	mdelay(10);

	hv_enable_bcom();
	mdelay(10);

}
//SJ0313-end

//MK0202-begin
void hv_read_fpga_debug_info(struct fpga_debug_info_t *pdebuginfo)
{
	unsigned int dword=0;

//MK0209-begin
//MK0215	udelay(10000);
//MK0209-end

	write_smbus(3, 5, 0xA0, 2);		// Set to page 2
	udelay(1000);

	/* This is the LBA that FPGA received through MMIO command packet */
	dword = (unsigned int)read_smbus(3, FPGA1_ID, 0x53) & 0x000000FF;
	dword <<= 8;
	udelay(100);

	dword |= ((unsigned int)read_smbus(3, FPGA1_ID, 0x52) & 0x000000FF);
	dword <<= 8;
	udelay(100);

	dword |= ((unsigned int)read_smbus(3, FPGA1_ID, 0x51) & 0x000000FF);
	dword <<= 8;
	udelay(100);

	dword |= ((unsigned int)read_smbus(3, FPGA1_ID, 0x50) & 0x000000FF);

	pdebuginfo->lba = dword;

	/* Get fake-read buffer address most recently used by FPGA */
	dword = (unsigned int)read_smbus(3, FPGA1_ID, 0x57) & 0x000000FF;
	dword <<= 8;
	udelay(100);

	dword |= ((unsigned int)read_smbus(3, FPGA1_ID, 0x56) & 0x000000FF);
	dword <<= 8;
	udelay(100);

	dword |= ((unsigned int)read_smbus(3, FPGA1_ID, 0x55) & 0x000000FF);
	dword <<= 8;
	udelay(100);

	dword |= ((unsigned int)read_smbus(3, FPGA1_ID, 0x54) & 0x000000FF);

	pdebuginfo->fr_buff_addr = dword;

//MK0209-begin
	/* This is the LBA that FPGA used to write data to TBM  */
	dword = (unsigned int)read_smbus(3, FPGA1_ID, 0x63) & 0x000000FF;
	dword <<= 8;
	udelay(100);

	dword |= ((unsigned int)read_smbus(3, FPGA1_ID, 0x62) & 0x000000FF);
	dword <<= 8;
	udelay(100);

	dword |= ((unsigned int)read_smbus(3, FPGA1_ID, 0x61) & 0x000000FF);
	dword <<= 8;
	udelay(100);

	dword |= ((unsigned int)read_smbus(3, FPGA1_ID, 0x60) & 0x000000FF);

	pdebuginfo->lba2 = dword;
//MK0209-end

	write_smbus(3, 5, 0xA0, 0);		// Set to page 0
	return;
}
//MK0202-end

unsigned long hv_get_dram_addr (int type, void *addr)
{

	/* locate physical address for mmls-write command  for singel DIMM */
#if defined (RDIMM_POPULATED) || defined (MMLS_16K_ALIGNMENT)
	if (type == GROUP_BSM)
		return (unsigned long) (hv_group[0].mem[0].p_dram+BSM_DRAM_OFF);
	else
		return (unsigned long) (hv_group[0].mem[0].p_dram+MMLS_DRAM_OFF);
#else
	return (unsigned long)virt_to_phys(addr);
#endif

}

#endif	/* MULTI_DIMM */

/**
 * calc_emmc_alloc - Initialize flash data for all HVDIMMs in the given group
 * @gid: Group ID
 *
 * The calc_emmc_alloc() function initializes flash information of each HVDIMM
 * for the selected group. Each group may have more than one HVDIMM.
 * This routine assumes that all HVDIMMs in each group have the identical size
 * of flash and DRAM and each
 */
static void calc_emmc_alloc(int gid)
{
	unsigned long b_size, m_size;
	int i;

	/*
	 * The total size of flash from one or more HVDIMMs and the number of
	 * HVDIMMs in this group are pre-determined. Calculate the size of
	 * flash for each HVDIMM in the group.
	 */
	b_size = hv_group[gid].bsm_size/hv_group[gid].num_hv;
	m_size = hv_group[gid].mmls_size/hv_group[gid].num_hv;

	/*
	 * Based on the info above, calculate start sector number and the size of
	 * flash in sectors for each HVDIMM in the group.
	 */
	for (i=0; i<hv_group[gid].num_hv; i++) {
		hv_group[gid].emmc[i].b_start = 0;
		hv_group[gid].emmc[i].b_size = b_size;
		hv_group[gid].emmc[i].m_start = b_size;
		hv_group[gid].emmc[i].m_size = m_size;	
	}
}

#ifndef SIMULATION_TB
static long ramdisk_size;
static int io_init_done=0;
static int io_release_done=0;
static void hv_request_mem(unsigned long phys, unsigned long *virt_p, unsigned long size, int cache, char *name)
{
//MK-begin
	/**
	 * NOTE: request_mem_region() tells the kernel that the command driver wants
	 * to use this range of I/O addresses so the region is reserved and other
	 * drivers can't ask for any part of the region using the same API.
	 * This mechanism doesn't do any kind of mapping, it's a pure reservation
	 * mechanism, which relies on the fact that all kernel device drivers must
	 * be nice, and they must call request_mem_region, check the return value,
	 * and behave properly in case of error. So it is completely logical that
	 * the driver code works without request_mem_region, it's just that it
	 * doesn't comply with the kernel coding rules.
	 */
//MK-end
	if (request_mem_region(phys, size, name) == NULL) {
		pr_warn("hv: unable to request %s IO space starting 0x%lx size(%lx)\n", name, phys, size);

		/**
		 * The following return stmt is temporarily removed for the following
		 * reason.
		 * The test server had one LRDIMM (16GB) and one HybriDIMM (8GB). Since
		 * we do not support "interleaving" as of today, in order to have the
		 * system BIOS disable "memory interleaving" in this memory
		 * configuration, SPD in HybriDIMM had to be programmed as "NV" and this
		 * makes request_mem_region() fail. Since request_mem_region() is
		 * a courtesy call, we can ignore the error and call ioremap_xyz().
		 */
//MK0818		return;
	}
//MK0209	if (cache)
//MK0209		*virt_p = (unsigned long)ioremap_cache(phys, size);
//MK0209	else
//MK0209		*virt_p = (unsigned long)ioremap_wc(phys, size);
//MK0209-begin
	if (cache == 1)
		*virt_p = (unsigned long)ioremap_cache(phys, size);
	else if (cache == 2)
		*virt_p = (unsigned long)ioremap_nocache(phys, size);
	else
		*virt_p = (unsigned long)ioremap_wc(phys, size);
//MK0209-end
}

int hv_io_init(void)
{
#ifdef MULTI_DIMM
	int ways = interleave_ways(gid);
	int num_mem = hv_group[gid].num_hv/ways;
#else
	int ways = 1;
	int num_mem = 1;
#endif
	int i;

	if (io_init_done)
		return 0;

//MK-begin
	pr_mmio("[%s]: starting...\n", __func__);
//MK-end

	calc_emmc_alloc(gid);

	for (i=0; i<num_mem; i++) {
		/*
		 * Request IO space
		 */
		if (get_ramdisk()) {
			hv_group[gid].mem[i].p_dram = hv_group[gid].mem[i].p_mmio*ways - HV_MMLS_DRAM_SIZE;
			if (!get_use_memmap()) {
				hv_group[gid].mem[i].v_mmio = phys_to_virt(get_ramdisk_start());
				hv_group[gid].mem[i].v_dram = phys_to_virt(hv_group[gid].mem[i].p_dram);
			}
			else {
				if (bsm_start()+bsm_size() > mmls_start()+mmls_size())
					ramdisk_size = bsm_start()+bsm_size();
				else
					ramdisk_size = mmls_start()+mmls_size();
				hv_request_mem(	get_ramdisk_start(), 
						(unsigned long *) &hv_group[gid].mem[i].v_mmio, 
						ramdisk_size, 
						1, 
						"ramdisk");				
				hv_request_mem(	hv_group[gid].mem[i].p_dram, 
						(unsigned long *) &hv_group[gid].mem[i].v_dram, 
						HV_MMLS_DRAM_SIZE, 
						1, 
						"dram");
//MK-begin
				pr_mmio("[%s]: ramdisk_size = %#.16lx bytes = %ld sectors\n", __func__, ramdisk_size, ramdisk_size >> 9);
				pr_mmio("[%s]: hv_group[%u].mem[%u].p_dram = %#.16lx\n", __func__, gid, i, (unsigned long)hv_group[gid].mem[i].p_dram);
				pr_mmio("[%s]: hv_group[%u].mem[%u].v_dram = %#.16lx\n", __func__, gid, i, (unsigned long)hv_group[gid].mem[i].v_dram);
				pr_mmio("[%s]: hv_group[%u].mem[%u].p_mmio = %#.16lx\n", __func__, gid, i, (unsigned long)hv_group[gid].mem[i].p_mmio);
				pr_mmio("[%s]: hv_group[%u].mem[%u].v_mmio = %#.16lx\n", __func__, gid, i, (unsigned long)hv_group[gid].mem[i].v_mmio);
//MK-end
			}
		} else {
			/* Reserve 96MB HV DRAM at top of DRAM space */
			hv_group[gid].mem[i].p_dram = hv_group[gid].mem[i].p_mmio*ways - HV_MMLS_DRAM_SIZE;
			if (!get_use_memmap()) {
				hv_group[gid].mem[i].v_mmio = phys_to_virt(hv_group[gid].mem[i].p_mmio*ways);
				hv_group[gid].mem[i].v_dram = phys_to_virt(hv_group[gid].mem[i].p_dram);
			}
			else {
//MK1103				hv_request_mem(	hv_group[gid].mem[i].p_mmio*ways,
//MK1103						(unsigned long *) &hv_group[gid].mem[i].v_mmio,
//MK1103						HV_MMIO_SIZE*ways,
//MK1103						1,
//MK1103						"cmd");
//MK1103				hv_request_mem(	hv_group[gid].mem[i].p_dram,
//MK1103						(unsigned long *) &hv_group[gid].mem[i].v_dram,
//MK1103						HV_MMLS_DRAM_SIZE,
//MK1103						1,
//MK1103						"dram");
//MK1103-begin
				/* Request write-combining */
				hv_request_mem(	hv_group[gid].mem[i].p_mmio*ways,
						(unsigned long *) &hv_group[gid].mem[i].v_mmio,
						HV_MMIO_SIZE*ways,
						0,
						"cmd");
//MK0205				hv_request_mem(	hv_group[gid].mem[i].p_dram,
//MK0205						(unsigned long *) &hv_group[gid].mem[i].v_dram,
//MK0205						(HV_MMLS_DRAM_SIZE+HV_BUFFER_SIZE),
//MK0205						0,
//MK0205						"dram");
//MK0205-begin
				hv_request_mem(	hv_group[gid].mem[i].p_dram,
						(unsigned long *) &hv_group[gid].mem[i].v_dram,
						(HV_MMLS_DRAM_SIZE),
						0,
						"dram");
//MK0205-end
//MK1103-end
			}
//MK-begin
			pr_mmio("[%s]: hv_group[%u].mem[%u].p_mmio = %#.16lx\n",
					__func__, gid, i, (unsigned long)hv_group[gid].mem[i].p_mmio);
			pr_mmio("[%s]: hv_group[%u].mem[%u].v_mmio = %#.16lx\n",
					__func__, gid, i, (unsigned long)hv_group[gid].mem[i].v_mmio);
			pr_mmio("[%s]: hv_group[%u].mem[%u].p_dram = %#.16lx\n",
					__func__, gid, i, (unsigned long)hv_group[gid].mem[i].p_dram);
			pr_mmio("[%s]: hv_group[%u].mem[%u].v_dram = %#.16lx\n",
					__func__, gid, i, (unsigned long)hv_group[gid].mem[i].v_dram);
//MK-end
		}
	}

//MK0209-begin
	clear_command_tag();
//MK0209-end

	io_init_done = 1;
	return 0;
}

static void hv_release_mem(unsigned long phys, void *virt, unsigned long size)
{
	iounmap(virt);
	release_mem_region(phys, size);
}

void hv_io_release(void)
{
#ifdef MULTI_DIMM
	int ways = interleave_ways(gid);
	int num_mem = hv_group[gid].num_hv/ways;
#else
	int ways = 1;
	int num_mem = 1;
#endif
	int i;

	if (io_release_done)
		return;

	for (i=0; i<num_mem; i++) {
		if (!get_ramdisk() && get_use_memmap()) {
			hv_release_mem( hv_group[gid].mem[i].p_mmio*ways, 
					hv_group[gid].mem[i].v_mmio, HV_MMIO_SIZE*ways);
			hv_release_mem(	hv_group[gid].mem[i].p_dram,
					hv_group[gid].mem[i].v_dram, HV_MMLS_DRAM_SIZE);
		}

		if (get_ramdisk() && get_use_memmap()) {
			hv_release_mem(	get_ramdisk_start(), 
					hv_group[gid].mem[i].v_mmio, ramdisk_size);
			hv_release_mem(	hv_group[gid].mem[i].p_dram, 
					hv_group[gid].mem[i].v_dram, HV_MMLS_DRAM_SIZE);
		}
	}

	io_release_done = 1;
}
#endif

//MK-begin
/**
 * hv_open_sesame - Enables the communication channel with HVDIMM
 *
 * This dummy write operation switches BCOM to FPGA from RCD. This operation
 * is needed only once per each power-up.
 * Current implementation supports only one HVDIMM.
 **/
void hv_open_sesame(void)
{
//MK1102-begin
	unsigned int i, j, k;
//MK1102-end

//MK1201-begin
	struct hd_vid_t vid_desc;
	vid_desc.spd_dimm_id = 3;
	(void) get_vid_command((void *)&vid_desc);
	pr_mmio("[%s]: VID = 0x%.4X\n", __func__, vid_desc.vid);
//MK1201-end

	pr_mmio("[%s]: Open sesame!!\n", __func__);

//MK0217-begin
	/* Initialize BCOM ctrl method based on I2C reg setting */
	init_bcom_ctrl_method();

//MK0302#if (DEBUG_FEAT_BCOM_ONOFF == 0)
//MK0302	hv_enable_bcom();
//MK0302#endif
//MK0217-end

//MK0321-begin
	/* Initialize FPGA reset ctrl method based on the current I2C reg setting */
	init_fpga_reset_ctrl_method();
//MK0321-end

	show_pci_config_regs(0, 5, 0);
//MK1107-begin
	show_fpga_i2c_reg(3, FPGA1_ID, 0, 0x7F);
	show_dimm_spd(3);
	pr_mmio("Temperature of DRAM on DIMM_ID #%d = %dC\n", 3, get_dimm_temp(3));
//MK1107-end

	// Dummy write to BCOM offset to wake up the FPGA
//MK1221	hv_mcpy(BCOM_OFF, cmd_burst_buf, CMD_BUFFER_SIZE);

	// Give it about 1us to finish the task
	udelay(1);

//MK1101-begin
	for (i = 0; i < MAX_NPS; i++ ) {
		pr_mmio("[%s]: hd_desc[%d].tolm_addr=0x%.16lx\n", __func__, i, hd_desc[i].tolm_addr);
		pr_mmio("[%s]: hd_desc[%d].tohm_addr=0x%.16lx\n", __func__, i, hd_desc[i].tohm_addr);
		pr_mmio("[%s]: hd_desc[%d].sys_rsvd_mem_size=%ld GB\n", __func__, i, hd_desc[i].sys_rsvd_mem_size);
		pr_mmio("[%s]: hd_desc[%d].sys_mem_size=%ld GB\n", __func__, i, hd_desc[i].sys_mem_size);
		pr_mmio("[%s]: hd_desc[%d].spd_dimm_mask=0x%.8x\n", __func__, i, hd_desc[i].spd_dimm_mask);
		pr_mmio("[%s]: hd_desc[%d].sys_dimm_count=%d\n", __func__, i, hd_desc[i].sys_dimm_count);
		pr_mmio("[%s]: hd_desc[%d].sys_hdimm_count=%d\n", __func__, i, hd_desc[i].sys_hdimm_count);
		pr_mmio("[%s]: hd_desc[%d].a7_mode=%d\n", __func__, i, hd_desc[i].a7_mode);
		pr_mmio("[%s]: hd_desc[%d].sock_way=%d\n", __func__, i, hd_desc[i].sock_way);
		pr_mmio("[%s]: hd_desc[%d].chan_way=%d\n", __func__, i, hd_desc[i].chan_way);
		pr_mmio("[%s]: hd_desc[%d].rank_way=%d\n\n", __func__, i, hd_desc[i].rank_way);
		for (j=0; j < MAX_CPN; j++) {
			/* If the current channel has any dimm, display its info */
			if ( hd_desc[i].channel[j].ch_dimms != 0 ) {
				pr_mmio("[%s]: hd_desc[%d].channel[%d].ch_dimms=%d\n", __func__, i, j, hd_desc[i].channel[j].ch_dimms);
				pr_mmio("[%s]: hd_desc[%d].channel[%d].ch_dimm_mask=0x%.2X\n", __func__, i, j, hd_desc[i].channel[j].ch_dimm_mask);
				for (k=0; k < MAX_DPC; k++) {
					/* If there is a dimm in the current dimm socket, display its info */
					if ( hd_desc[i].channel[j].ch_dimm_mask & (unsigned char)(1 << k) ) {
						pr_mmio("[%s]: hd_desc[%d].channel[%d].dimm[%d].dram_size=%d GB\n", __func__, i, j, k, hd_desc[i].channel[j].dimm[k].dram_size);
						pr_mmio("[%s]: hd_desc[%d].channel[%d].dimm[%d].nv_size=%d GB\n", __func__, i, j, k, hd_desc[i].channel[j].dimm[k].nv_size);
						pr_mmio("[%s]: hd_desc[%d].channel[%d].dimm[%d].node_num=%d\n", __func__, i, j, k, hd_desc[i].channel[j].dimm[k].node_num);
						pr_mmio("[%s]: hd_desc[%d].channel[%d].dimm[%d].chan_num=%d\n", __func__, i, j, k, hd_desc[i].channel[j].dimm[k].chan_num);
						pr_mmio("[%s]: hd_desc[%d].channel[%d].dimm[%d].slot_num=%d\n", __func__, i, j, k, hd_desc[i].channel[j].dimm[k].slot_num);
						pr_mmio("[%s]: hd_desc[%d].channel[%d].dimm[%d].banks=%d\n", __func__, i, j, k, hd_desc[i].channel[j].dimm[k].banks);
						pr_mmio("[%s]: hd_desc[%d].channel[%d].dimm[%d].ranks=%d\n", __func__, i, j, k, hd_desc[i].channel[j].dimm[k].ranks);
						pr_mmio("[%s]: hd_desc[%d].channel[%d].dimm[%d].iowidth=%d\n", __func__, i, j, k, hd_desc[i].channel[j].dimm[k].iowidth);
						pr_mmio("[%s]: hd_desc[%d].channel[%d].dimm[%d].row=0x%x\n", __func__, i, j, k, hd_desc[i].channel[j].dimm[k].row);
						pr_mmio("[%s]: hd_desc[%d].channel[%d].dimm[%d].col=0x%x\n", __func__, i, j, k, hd_desc[i].channel[j].dimm[k].col);
						pr_mmio("[%s]: hd_desc[%d].channel[%d].dimm[%d].spd_dimm_id=%d\n", __func__, i, j, k, hd_desc[i].channel[j].dimm[k].spd_dimm_id);
					}
				}
			}
		}
	}
//MK1101-end

//MK0519-begin
	/* Initialize fake_mmls_buf */
	clear_fake_mmls_buf();
//MK0519-end
}

/**
 * hv_termination - Send termination command to HVDIMM
 *
 * The termination command informs HVDIMM that the current command session
 * is completed.
 **/
void hv_write_termination(int gid, int type, int lba, void *cmd)
{
//SJ0313-begin
	if ( bcom_toggle_enabled() ) {
	} else {
//MK0605		hv_delay_us(get_user_defined_delay());
//MK0605-begin
		hv_delay_us(get_user_defined_delay_us(0));	// delay index 0
//MK0605-end
	}
//SJ0313-end

	pr_mmio("[%s]: Current command session id completed\n", __func__);

	/*
	 * Dummy write to TERMINATION offset is good enough since HVDIMM
	 * just needs to detect the address. HVDIMM doesn't care about
	 * the content of the command buffer. Also, we may not even need to
	 * write the entire 64 bytes of the command buffer. We will deal with it
	 * later.
	 */
//MK0727-begin - test only
//MK0519	memset(cmd_burst_buf, 0x77, CMD_BUFFER_SIZE);
//MK0727-end

//MK0519	hv_mcpy(TERM_OFF, cmd_burst_buf, CMD_BUFFER_SIZE);
//MK0519-begin
		memcpy_64B_movnti_3(TERM_OFF, (void*)fake_mmls_buf, CMD_BUFFER_SIZE);
//MK0519-end

//MK0724-begin
	if (cmd_status_use_cache)
		clflush_cache_range(TERM_OFF, CMD_BUFFER_SIZE);
//MK0724-end
}

/**
 * hv_train_ecc_table - Xfers the predefined ECC table to HVDIMM
 *
 **/
int hv_train_ecc_table(void)
{
	unsigned long i, j, k;
	unsigned long *ecc_table, *temp;

	pr_mmio("[%s]: # of HVDIMMs = %d, size of ECC table = %d bytes\n",
			__func__, hv_group[gid].num_hv,
			ECC_TABLE_ENTRY_SIZE*ECC_TABLE_TOTAL_ENTRY*hv_group[gid].num_hv);

	// Allocate memory to setup ECC table
    temp = (unsigned long *)kzalloc(ECC_TABLE_ENTRY_SIZE*ECC_TABLE_TOTAL_ENTRY*hv_group[gid].num_hv, GFP_KERNEL);
    if ( temp == NULL ) {
    	pr_mmio("[%s]: ECC table allocation failed\n", __func__);
    	return -1;
    }

    // Build the ECC table with the predefined ECC bits
    ecc_table = temp;
 	for (i=0; i < ECC_TABLE_TOTAL_ENTRY; i++)
   	{
   		for (j=0; j < hv_group[gid].num_hv; j++)
   		{
   			for (k=0; k < 8; k++)
   			{
   	   			*ecc_table = i;
   	   			ecc_table++;
   			}
   		}
   	}

 	// Send the ECC table to HVDIMM
//MK1103	hv_mcpy(ECC_OFF, temp, ECC_TABLE_ENTRY_SIZE*ECC_TABLE_TOTAL_ENTRY*hv_group[gid].num_hv);
//MK1103-begin
	/* Write ECC table to MMIO ECC table region */
	memcpy_64B_movnti_2((void*)ECC_OFF, (void*)temp, (unsigned int)CMD_BUFFER_SIZE);
	if (cmd_status_use_cache)
		clflush_cache_range(ECC_OFF, ECC_TABLE_ENTRY_SIZE*ECC_TABLE_TOTAL_ENTRY*hv_group[gid].num_hv);
//MK1103-end

	// Free up the ECC table
	kfree(temp);
	return 0;
}

void show_pci_config_regs(int bus, int dev, int func)
{
	int i, j;
	struct pci_dev *pdev = NULL;
	unsigned char regb[16];
	unsigned int reg_dword;


	/* Finding the device by Vendor/Device ID Pair */
	/* Intel Corporation Xeon E7 v4/Xeon E5 v4/Xeon E3 v4/Xeon D Memory Controller 0 - Target Address/Thermal/RAS (FF:13:00) */
	pdev = pci_get_device(0x8086, 0x6FA8, pdev);
	if (pdev == NULL) {
		pr_mmio("[%s]: 0x8086-0x6FA8 not found\n", __func__);
	} else {

		pr_mmio("[%s]: Intel PCI Device (0xFF, 0x13, 0) Config Space\n", __func__);
		for (i=0; i < 16; i++)
		{
			for (j=0; j < 16; j++)
			{
				pci_read_config_byte(pdev, i*16+j, &regb[j]);
			}
			pr_mmio("0x%.2x: %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x\n",
					i*16, regb[0], regb[1], regb[2], regb[3], regb[4], regb[5], regb[6], regb[7],
					regb[8], regb[9], regb[10], regb[11], regb[12], regb[13], regb[14], regb[15]);
		}

		pci_read_config_dword(pdev, 0x40, &reg_dword);
		pr_mmio("pxpcap (0x40) = 0x%.8x\n", reg_dword);
		pci_read_config_dword(pdev, 0x10C, &reg_dword);
		pr_mmio("mh_sense_500ns_cfg (0x10C) = 0x%.8x\n", reg_dword);
		pci_read_config_dword(pdev, 0x180, &reg_dword);
		pr_mmio("smb_stat_0 (0x180) = 0x%.8x\n", reg_dword);
		pci_read_config_dword(pdev, 0x188, &reg_dword);
		pr_mmio("smbcntl_0 (0x188) = 0x%.8x\n", reg_dword);
	}
	pr_mmio("\n");
}

//MK1107-begin
void show_fpga_i2c_reg(unsigned int spd_dimm_id, unsigned int fpga_id,
						unsigned int start_addr, unsigned int end_addr)
{
	unsigned int i, j;
	unsigned char buff[16];

	if ( hd_desc[0].spd_dimm_mask & (1 << spd_dimm_id) ) {
		pr_mmio("[I2C Registers: DIMM_ID #%d - FPGA_ID #%d: 0x%.02x ~ 0x%.02x]\n",
				spd_dimm_id, fpga_id, start_addr, end_addr);
		for (i=0; i < 8; i++)
		{
			for (j=0; j < 16; j++)
			{
				buff[j] = (unsigned char) read_smbus(spd_dimm_id, fpga_id, i*16+j);
			}
			pr_mmio("0x%.3x: %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x\n",
					i*16, buff[0], buff[1], buff[2], buff[3], buff[4], buff[5], buff[6], buff[7],
					buff[8], buff[9], buff[10], buff[11], buff[12], buff[13], buff[14], buff[15]);
		}
		pr_mmio("\n");
	} else {
		pr_mmio("DIMM with SPD ID (%d) not found\n", spd_dimm_id);
	}
}

void show_dimm_spd(unsigned int spd_dimm_id)
{
	unsigned int i, j;
	unsigned char buff[16];

	if ( hd_desc[0].spd_dimm_mask & (1 << spd_dimm_id) ) {
		pr_mmio("[SPD Content (512 bytes): DIMM_ID #%d]\n", spd_dimm_id);
		pr_mmio("       00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
		set_i2c_page_address(spd_dimm_id, 0);
		for (i=0; i < 16; i++)
		{
			for (j=0; j < 16; j++)
			{
				buff[j] = (unsigned char) read_smbus(spd_dimm_id, SPD_ID, i*16+j);
			}
			pr_mmio("0x%.3x: %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x  %c %c %c %c %c %c %c %c %c %c %c %c %c %c %c %c\n",
					i*16, buff[0], buff[1], buff[2], buff[3], buff[4], buff[5], buff[6], buff[7],
					buff[8], buff[9], buff[10], buff[11], buff[12], buff[13], buff[14], buff[15],
					(buff[0]<127)&&(buff[0]>31)?buff[0]:'.', (buff[1]<127)&&(buff[1]>31)?buff[1]:'.',
					(buff[2]<127)&&(buff[2]>31)?buff[2]:'.', (buff[3]<127)&&(buff[3]>31)?buff[3]:'.',
					(buff[4]<127)&&(buff[4]>31)?buff[4]:'.', (buff[5]<127)&&(buff[5]>31)?buff[5]:'.',
					(buff[6]<127)&&(buff[6]>31)?buff[6]:'.', (buff[7]<127)&&(buff[7]>31)?buff[7]:'.',
					(buff[8]<127)&&(buff[8]>31)?buff[8]:'.', (buff[9]<127)&&(buff[9]>31)?buff[9]:'.',
					(buff[10]<127)&&(buff[10]>31)?buff[10]:'.', (buff[11]<127)&&(buff[11]>31)?buff[11]:'.',
					(buff[12]<127)&&(buff[12]>31)?buff[12]:'.', (buff[13]<127)&&(buff[13]>31)?buff[13]:'.',
					(buff[14]<127)&&(buff[14]>31)?buff[14]:'.', (buff[15]<127)&&(buff[15]>31)?buff[15]:'.');
		}
		pr_mmio("\n");

		set_i2c_page_address(spd_dimm_id, 1);
		for (i=0; i < 16; i++)
		{
			for (j=0; j < 16; j++)
			{
				buff[j] = (unsigned char) read_smbus(spd_dimm_id, SPD_ID, i*16+j);
			}
			pr_mmio("0x%.3x: %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x  %c %c %c %c %c %c %c %c %c %c %c %c %c %c %c %c\n",
					0x100+i*16, buff[0], buff[1], buff[2], buff[3], buff[4], buff[5], buff[6], buff[7],
					buff[8], buff[9], buff[10], buff[11], buff[12], buff[13], buff[14], buff[15],
					(buff[0]<127)&&(buff[0]>31)?buff[0]:'.', (buff[1]<127)&&(buff[1]>31)?buff[1]:'.',
					(buff[2]<127)&&(buff[2]>31)?buff[2]:'.', (buff[3]<127)&&(buff[3]>31)?buff[3]:'.',
					(buff[4]<127)&&(buff[4]>31)?buff[4]:'.', (buff[5]<127)&&(buff[5]>31)?buff[5]:'.',
					(buff[6]<127)&&(buff[6]>31)?buff[6]:'.', (buff[7]<127)&&(buff[7]>31)?buff[7]:'.',
					(buff[8]<127)&&(buff[8]>31)?buff[8]:'.', (buff[9]<127)&&(buff[9]>31)?buff[9]:'.',
					(buff[10]<127)&&(buff[10]>31)?buff[10]:'.', (buff[11]<127)&&(buff[11]>31)?buff[11]:'.',
					(buff[12]<127)&&(buff[12]>31)?buff[12]:'.', (buff[13]<127)&&(buff[13]>31)?buff[13]:'.',
					(buff[14]<127)&&(buff[14]>31)?buff[14]:'.', (buff[15]<127)&&(buff[15]>31)?buff[15]:'.');
		}
		pr_mmio("\n");

		set_i2c_page_address(spd_dimm_id, 0);
	} else {
		pr_mmio("DIMM with SPD ID (%d) not found\n", spd_dimm_id);
	}
}

int get_dimm_temp(unsigned int spd_dimm_id)
{
	short int temp;

	if ( hd_desc[0].spd_dimm_mask & (1 << spd_dimm_id) ) {
		temp = (short int)(read_smbus(spd_dimm_id, TSOD_ID, 5) & 0x0FFF);
		return(temp/16);
	} else {
		pr_mmio("DIMM with SPD ID (%d) not found\n", spd_dimm_id);
		return(-1);
	}
}

int set_i2c_page_address(unsigned int dimm, int page)
{
//	int Data, startCount=0;
//	int N;
	struct pci_dev *pdev = NULL;
	unsigned int reg_val;
	unsigned int smbCmd, smbCfg, smbStat;
	unsigned int smbA, smbC, smbD;
	unsigned long ts;

	/* Search for Main IMC (Integrated Memory Controller) */
	pdev = pci_get_device(0x8086, 0x6FA8, pdev);
	if (pdev == NULL) {
		pr_debug("[%s]: IMC, 0x8086-0x6FA8, not found\n", __func__);
		return(-1);
	}

	smbA = page + 6;	// 6: Set Page 0, 7: Set Page 1
	smbC = SMBctl[dimm];
	smbD = SMBdev[dimm];

	smbCfg = (PAGE_ID << 28) | 0x08000000;
	smbCmd = 0x80000000 | (smbA << 24);

//	if (d<(12)) N=0;
//	else		N=1;

//	MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x188+smbC,smbCfg);
//	Data = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x188+smbC);
	pci_write_config_dword(pdev, 0x188+smbC, smbCfg);
	pci_read_config_dword(pdev, 0x188+smbC, &reg_val);

//	if (Data&0x100) {								//
//		usleep(10*1000);
//		MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x188+smbC,smbCfg);
//		Data = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x188+smbC);
//	}
//	if (Data&0x100) return (-1);
	if (reg_val & 0x00000100) {
		udelay(10*1000);
		pci_write_config_dword(pdev, 0x188+smbC, smbCfg);
		pci_read_config_dword(pdev, 0x188+smbC, &reg_val);
	}

	if (reg_val & 0x00000100) {
		pr_debug("[%s]: SPDCMD access not enabled\n", __func__);
		return(-1);
	}

//	startCount = getTickCount();
	ts = hv_nstimeofday();

	do {
//		usleep(1);
//		smbStat = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x180+smbC);
		udelay(1);
		pci_read_config_dword(pdev, 0x180+smbC, &smbStat);

		if (!(smbStat & 0x10000000))
			break;					// bail out if busy
//	} while(elapsedTime(startCount) < SMB_TIMEOUT);
	} while ((hv_nstimeofday() - ts) < SMB_TIMEOUT);

//	MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x184+smbC,smbCmd);
	pci_write_config_dword(pdev, 0x184+smbC, smbCmd);

//	startCount = getTickCount();
	ts = hv_nstimeofday();
	do {
//		usleep(1000);
//		smbStat = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x180+smbC);
		udelay(1000);
		pci_read_config_dword(pdev, 0x180+smbC, &smbStat);

		if (!(smbStat & 0x10000000))
			break;					// bail out if busy
//	} while(elapsedTime(startCount) < SMB_TIMEOUT);
	} while ((hv_nstimeofday() - ts) < SMB_TIMEOUT);

	return 0;
}
//MK1107-end

int read_smbus(unsigned int dimm, unsigned int smb_dti, unsigned int off)
{
//MK	int Data, startCount=0;
//MK	int N;
	struct pci_dev *pdev = NULL;
	unsigned int reg_val;
	unsigned int smbCmd, smbCfg, smbStat;
	unsigned int smbA, smbC, smbD;
	unsigned long ts;

	/* Search for Main IMC (Integrated Memory Controller) */
	pdev = pci_get_device(0x8086, 0x6FA8, pdev);
	if (pdev == NULL) {
		pr_debug("[%s]: IMC, 0x8086-0x6FA8, not found\n", __func__);
		return(-1);
	}

	smbA = SMBsad[dimm];	// I2C device ID for SPD on the selected DIMM
	smbC = SMBctl[dimm];	//
	smbD = SMBdev[dimm];	// PCI device ID where the selected DIMM belongs to

	smbCfg = (smb_dti << 28) | 0x08000000;
//	smbCmd = 0x80000000 | (smbA << 24) | (off << 16);
	if (smb_dti == TSOD_ID)
		smbCmd  = 0xA0000000 | (smbA << 24) | (off << 16);
	else
		smbCmd  = 0x80000000 | (smbA << 24) | (off << 16);


//MK	if (dimm < 12)
//MK		N=0;
//MK	else
//MK		N=1;

//MK	MMioWriteDword(Bus+N*(Bus+1), smbD, 0, 0x188+smbC, smbCfg);
//MK	Data = MMioReadDword(Bus+N*(Bus+1), smbD, 0, 0x188+smbC);
	pci_write_config_dword(pdev, 0x188+smbC, smbCfg);
	pci_read_config_dword(pdev, 0x188+smbC, &reg_val);

	if (reg_val & 0x00000100) {
//MK		usleep(10*1000);
//MK		MMioWriteDword(Bus+N*(Bus+1), smbD, 0, 0x188+smbC, smbCfg);
//MK		Data = MMioReadDword(Bus+N*(Bus+1), smbD, 0, 0x188+smbC);
		udelay(10*1000);
		pci_write_config_dword(pdev, 0x188+smbC, smbCfg);
		pci_read_config_dword(pdev, 0x188+smbC, &reg_val);
	}

	if (reg_val & 0x00000100) {
		pr_debug("[%s]: SPDCMD access not enabled\n", __func__);
		return(-1);
	}

//MK	startCount = getTickCount();
	ts = hv_nstimeofday();
	do {
//MK		usleep(1);
//MK		smbStat = MMioReadDword(Bus+N*(Bus+1), smbD, 0, 0x180+smbC);
		udelay(1);
		pci_read_config_dword(pdev, 0x180+smbC, &smbStat);
		if (!(smbStat & 0x10000000))
			break;			// bail out if not busy
	} while ((hv_nstimeofday() - ts) < SMB_TIMEOUT);

//MK	MMioWriteDword(Bus+N*(Bus+1), smbD, 0, 0x184+smbC, smbCmd);
	pci_write_config_dword(pdev, 0x184+smbC, smbCmd);

//MK	startCount = getTickCount();
	ts = hv_nstimeofday();
	do {
//MK		usleep(1);
//MK		smbStat = MMioReadDword(Bus+N*(Bus+1), smbD, 0, 0x180+smbC);
		udelay(1);
		pci_read_config_dword(pdev, 0x180+smbC, &smbStat);
		if (!(smbStat & 0x10000000))
			break;			// bail out if not busy
	} while ((hv_nstimeofday() - ts) < SMB_TIMEOUT);

	while (!(smbStat & 0xA0000000))	// Read Data Valid & No SMBus error
	{
//MK		smbStat = MMioReadDword(Bus+N*(Bus+1), smbD, 0, 0x180+smbC);
		pci_read_config_dword(pdev, 0x180+smbC, &smbStat);
	}

	if (smb_dti == TSOD_ID)
		return((int)(smbStat & 0x0000FFFF));
	else
		return((int)(smbStat & 0x000000FF));
}

int write_smbus(unsigned int dimm, unsigned int smb_dti, unsigned int off, unsigned int data)
{
//MK	int startCount;
//MK	int N;
	struct pci_dev *pdev = NULL;
	unsigned int reg_val;
	unsigned int smbCmd, smbCfg, smbStat;
	unsigned int smbA, smbC, smbD;
	unsigned long ts;

	/* Search for Main IMC (Integrated Memory Controller) */
	pdev = pci_get_device(0x8086, 0x6FA8, pdev);
	if (pdev == NULL) {
		pr_debug("[%s]: IMC, 0x8086-0x6FA8, not found\n", __func__);
		return(-1);
	}

	smbA = SMBsad[dimm];	// I2C device ID for SPD on the selected DIMM
	smbC = SMBctl[dimm];
	smbD = SMBdev[dimm];	// PCI device ID where the selected DIMM belongs to

	smbCfg = (smb_dti << 28) | 0x08000000;
	smbCmd = 0x88000000 | (smbA << 24) | (off << 16) | data;

//MK	if (d<(12)) N=0;
//MK	else		N=1;

//MK	MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x188+smbC,smbCfg);
//MK	Data = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x188+smbC);
	pci_write_config_dword(pdev, 0x188+smbC, smbCfg);
	pci_read_config_dword(pdev, 0x188+smbC, &reg_val);

	if (reg_val & 0x00000100) {
//MK		usleep(10*1000);
//MK		MMioWriteDword(Bus+N*(Bus+1), smbD, 0, 0x188+smbC, smbCfg);
//MK		Data = MMioReadDword(Bus+N*(Bus+1), smbD, 0, 0x188+smbC);
		udelay(10*1000);
		pci_write_config_dword(pdev, 0x188+smbC, smbCfg);
		pci_read_config_dword(pdev, 0x188+smbC, &reg_val);
	}

	if (reg_val & 0x00000100) {
		pr_debug("[%s]: SPDCMD access not enabled\n", __func__);
		return(-1);
	}

//MK	startCount = getTickCount();
	ts = hv_nstimeofday();
	do {
//MK		usleep(1);
//MK		smbStat = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x180+smbC);
		udelay(1);
		pci_read_config_dword(pdev, 0x180+smbC, &smbStat);
		if (!(smbStat & 0x10000000))
			break;			// bail out if not busy
	} while ((hv_nstimeofday() - ts) < SMB_TIMEOUT);

//MK	MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x184+smbC,smbCmd);
	pci_write_config_dword(pdev, 0x184+smbC, smbCmd);

//MK	startCount = getTickCount();
	ts = hv_nstimeofday();
	do {
//MK		usleep(1);
//MK		smbStat = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x180+smbC);
		udelay(1);
		pci_read_config_dword(pdev, 0x180+smbC, &smbStat);
		if (!(smbStat & 0x60000000))
			break;			// Busy?
	} while ((hv_nstimeofday() - ts) < SMB_TIMEOUT);

	return(0);
}
//MK-end
//MK1019-begin
void clear_fake_mmls_buf(void)
{
//MK0519	memset(fake_mmls_buf, 0, CMD_BUFFER_SIZE);
//MK0519-begin
	memset((void *)fake_mmls_buf, 0xFF, sizeof(fake_mmls_buf));
//MK0519-end
}
//MK1019-end

//MK1215-begin
void display_buffer(unsigned long *pbuff, unsigned long qwcount, unsigned long qwmask)
{
	unsigned int i;
	unsigned long *pdat=pbuff;
#if 0
	/* Display 8 qwords per line */
	for (i=0; i < qwcount/8; i++)
	{
		pr_mmio("0x%.16lx 0x%.16lx 0x%.16lx 0x%.16lx 0x%.16lx 0x%.16lx 0x%.16lx 0x%.16lx \n",
				*pdat & qwmask, *(pdat+1) & qwmask, *(pdat+2) & qwmask, *(pdat+3) & qwmask,
				*(pdat+4) & qwmask, *(pdat+5) & qwmask, *(pdat+6) & qwmask, *(pdat+7) & qwmask);
		pdat+=8;
	}
#endif
}
//MK1215-end

//MK0105-begin
void hv_enable_bcom(void)
{
//	unsigned int reg;

//MK0127	pr_debug("[%s]: entered\n", __func__);

//MK0217-begin
	if (get_bcom_ctrl_method() == 1) {
		/* HDIMM #3, master FPGA, I2C reg 0x06, then give 100us delay */
//		reg = (unsigned int) read_smbus(3, 5, 0x06);
//		write_smbus(3, 5, 0x06, reg & 0xFFFFFF7F);
		write_smbus(3, 5, 0x06, 0x0);
		udelay(100);
	}

	/* Do dummy write to BCOM_SWITCH offset in MMIO region */
//MK0519	hv_mcpy(BCOM_OFF, cmd_burst_buf, CMD_BUFFER_SIZE);
//MK0519-begin
		memcpy_64B_movnti_3(BCOM_OFF, (void*)fake_mmls_buf, CMD_BUFFER_SIZE);
//MK0519-end
	udelay(100);
//MK0217-end
}

//MK0302#if (DEBUG_FEAT_BCOM_ONOFF == 1)
#define BCOM_DISABLE_CHECK_TIMEOUT	200000	/* 200 us */
void hv_disable_bcom(void)
{
//MK0217-begin
	unsigned int reg;
	unsigned long ts, elapsed_time=0;

//MK0127	pr_debug("[%s]: entered\n", __func__);

	if (get_bcom_ctrl_method() == 1) {
		/* HDIMM #3, master FPGA, I2C reg 0x06, then give 1us delay */
//		reg = (unsigned int) read_smbus(3, 5, 0x06);
//		write_smbus(3, 5, 0x06, reg | 0x00000080);
		write_smbus(3, 5, 0x06, 0x80);
//		udelay(10);

		/* Check status bit, [0], to make sure it is disabled */
		ts = hv_nstimeofday();
		while(elapsed_time < BCOM_DISABLE_CHECK_TIMEOUT) {
			reg = (unsigned int) read_smbus(3, 5, 0x06);
			if (reg & 0x00000001) {
				break;
			}
			/* Elapsed time since the first query command */
			elapsed_time = hv_nstimeofday() - ts;
		} // end of while
	} else {
		/* Do dummy write to BCOM_DISABLE offset in MMIO region to disable BCOM */
//MK0519		hv_mcpy(BCOM_DIS_OFF, cmd_burst_buf, CMD_BUFFER_SIZE);
//MK0519-begin
		memcpy_64B_movnti_3(BCOM_DIS_OFF, (void*)fake_mmls_buf, CMD_BUFFER_SIZE);
//MK0519-end
		udelay(100);
	}

//MK0127	pr_debug("[%s]: Disabling BCOM Mux %s\n", __func__, (reg & 0x00000001) ? "passed" : "failed");
//MK0217-end
}
//MK0302#endif

//MK0217-begin
void hv_config_bcom_ctrl(unsigned char sw)
{
	unsigned char reg;

	pr_debug("[%s]: BCOM  will be enabled or disabled using %s\n",
			__func__, (sw == 1) ? "I2C" : "MMIO");

	reg = ((unsigned char) read_smbus(3, 5, 0x70)) & 0x7F;
	reg |= (sw << 7);
	write_smbus(3, 5, 0x70, reg);
	udelay(100);
}
//MK0217-end

//MK0105-end

//MK0126-begin
void calculate_checksum_64bytes(unsigned char *p64byteBuff, unsigned int index,
		unsigned char *pcs0, unsigned char *pcs1, unsigned char *pcs2, unsigned char *pcs3)
{
	unsigned char *pbuff=p64byteBuff;
	unsigned int i, j;


//	pr_debug("[%s]: pbuff = 0x%.16lX\n", __func__, (unsigned long)pbuff);
	/* 1. Compute checksum for byte lane 0~2 burst 0~6 */
	for (i=0; i<7; i++)
	{
		for (j=0; j<3; j++)
		{
			*pcs0 ^= *pbuff++;
		}
		/* Advance the pointer to the next 64-byte */
		pbuff = p64byteBuff + ((i+1)*8);
	}

//	pr_debug("[%s]: pbuff = 0x%.16lX\n", __func__, (unsigned long)pbuff);
	/* Now, pbuff is pointing to the last burst of a 64-byte block */
	/* 2. Compute checksum for byte lane 0~2 burst 7 only */
	for (j=0; j<3; j++)
//	for (j=0; j<4; j++)
	{
		*pcs1 ^= *pbuff++;
	}

	/* 3. Compute checksum for byte lane 3 burst 0~6 */
	pbuff = p64byteBuff + 3;
//	pr_debug("[%s]: pbuff = 0x%.16lX\n", __func__, (unsigned long)pbuff);
	for (i=0; i<7; i++)
	{
		*pcs2 ^= *pbuff;
		/* Advance the pointer to the next 64-byte */
		pbuff += 8;
	}

//	pr_debug("[%s]: pbuff = 0x%.16lX\n", __func__, (unsigned long)pbuff);
	/* 4. Compute checksum for byte lane 3 burst 7 */
	*pcs3 = *pbuff;

	return;
}

void calculate_checksum(void *ba, struct block_checksum_t *pcs)
{
	unsigned char cs[4]={0,0,0,0}, *pbuff=(unsigned char*)ba;
	unsigned int i;

	memset((void*)pcs, 0, sizeof(struct block_checksum_t));

//	pr_debug("[%s]: cs[3:0] = 0x%.8X, size of (cs) = %ld\n", __func__, (unsigned int)*(unsigned int *)cs, sizeof(cs));
	for (i=0; i<64; i++)
	{
		memset((void*)cs, 0, sizeof(cs));
		calculate_checksum_64bytes(&pbuff[i*64], i, &cs[0], &cs[1], &cs[2], &cs[3]);
		pcs->sub_cs[0] ^= cs[0];
		pcs->sub_cs[1] ^= cs[1];
		pcs->sub_cs[2] ^= cs[2];
		pcs->sub_cs[3] ^= cs[3];
//MK0131-begin
		/* If it is 1st burst, do cs[0] one more time */
		if (i == 0) {
			pcs->sub_cs[0] ^= cs[0];
		}

		/* If it is 2nd burst, do cs[1] one more time */
		if (i == 1) {
			pcs->sub_cs[1] ^= cs[1];
		}

		/* If it is 3rd burst, do cs[2] one more time */
		if (i == 2) {
			pcs->sub_cs[2] ^= cs[2];
		}

		/* If it is 4th burst, do cs[3] one more time */
		if (i == 3) {
			pcs->sub_cs[3] ^= cs[3];
		}
//MK0131-end
	}

	return;
}
//MK0126-end

//MK0214-begin
void calculate_checksum_64bytes_2(unsigned char *p64byteBuff, unsigned int index,
		unsigned char *pcs0, unsigned char *pcs1, unsigned char *pcs2, unsigned char *pcs3)
{
	unsigned long *pbuff=(unsigned long *)p64byteBuff;
	unsigned int i;


	for (i=0; i<8; i++)
	{
		*pcs0 += (unsigned char)*pbuff;
		*pcs1 += (unsigned char)(*pbuff >> 8);
		*pcs2 += (unsigned char)(*pbuff >> 16);
		*pcs3 += (unsigned char)(*pbuff >> 24);
		pbuff++;
	}
	return;
}

/* For each burst (64-byte block), each byte-lane will be added */
/* Sum of byte-lane from each burst will be XORed. */
void calculate_checksum_2(void *ba, struct block_checksum_t *pcs)
{
	unsigned char cs[4]={0,0,0,0}, *pbuff=(unsigned char*)ba;
	unsigned int i;

	memset((void*)pcs, 0, sizeof(struct block_checksum_t));

	for (i=0; i<64; i++)
	{
		memset((void*)cs, 0, sizeof(cs));
		calculate_checksum_64bytes_2(&pbuff[i*64], i, &cs[0], &cs[1], &cs[2], &cs[3]);
		pcs->sub_cs[0] ^= cs[0];
		pcs->sub_cs[1] ^= cs[1];
		pcs->sub_cs[2] ^= cs[2];
		pcs->sub_cs[3] ^= cs[3];
	}

	return;
}
//MK0214-end

//MK0307-begin
unsigned char data_cs_comp(struct block_checksum_t *pcs1, struct block_checksum_t *pcs2)
{
	if ( (pcs1->sub_cs[0] == pcs2->sub_cs[0]) && (pcs1->sub_cs[1] == pcs2->sub_cs[1]) &&
			(pcs1->sub_cs[2] == pcs2->sub_cs[2]) && (pcs1->sub_cs[3] == pcs2->sub_cs[3]) ) {
		pr_debug("[%s]: checksum is the same\n", __func__);
		return 0;
	}

	pr_debug("[%s]: checksum is different\n", __func__);
	return 1;
}
//MK0307-end

//MK0209-begin
void clear_command_tag(void)
{
	command_tag = 0;
}

void inc_command_tag(void)
{
	/* it is 4-bit ID */
	command_tag++;
	if (command_tag == 32) {
		clear_command_tag();
	}
}

unsigned char get_command_tag(void)
{
	return command_tag;
}
//MK0209-end

//MK0223-begin
void set_debug_feat_flags(unsigned int flag)
{
	/* Config BCOM ctrl method */
	df_bcom_ctrl_method = (unsigned char)((flag & 0x00000001));
	hv_config_bcom_ctrl(df_bcom_ctrl_method);

//MK0302-begin
	/* Config BCOM toggle enable */
	df_bcom_toggle_enable = (unsigned char)((flag & 0x00000002) >> 1);
	if ( bcom_toggle_enabled() ) {
		hv_disable_bcom();	// Set RCD high
	} else {
		hv_enable_bcom();	// Set RCD low
	}
//MK0302-end

//MK0307-begin
	/* Config slave data CS enable */
	df_slave_data_cs_enable = (unsigned char)((flag & 0x00000004) >> 2);

	/* Config slave CMD_DONE check enable */
	df_slave_cmd_done_check_enable = (unsigned char)((flag & 0x00000008) >> 3);

	/* Config FPGA data CS location */
	df_fpga_data_cs_location = (unsigned char)((flag & 0x00000010) >> 4);

	/* Config FPGA popcnt location */
	df_fpga_popcnt_location = (unsigned char)((flag & 0x00000020) >> 5);
//MK0307-end

//MK0321-begin
	/* Config FPGA Reset ctrl method */
	df_fpga_reset_ctrl_method = (unsigned char)((flag & 0x00000040) >> 6);
	hv_config_fpga_reset_ctrl(df_fpga_reset_ctrl_method);
//MK0321-end

	pr_debug("[%s]: df_bcom_ctrl_method = %d (%s)\n", __func__,
			df_bcom_ctrl_method, (df_bcom_ctrl_method == 1) ? "I2C" : "MMIO");
//MK0302-begin
	pr_debug("[%s]: df_bcom_toggle_enable = %d (%s)\n", __func__,
			df_bcom_toggle_enable, (df_bcom_toggle_enable == 1) ? "enabled" : "disabled");
//MK0302-end
//MK0307-begin
	pr_debug("[%s]: df_slave_data_cs_enable = %d (%s)\n", __func__,
			df_slave_data_cs_enable, (df_slave_data_cs_enable == 1) ? "enabled" : "disabled");
	pr_debug("[%s]: df_slave_cmd_done_check_enable = %d (%s)\n", __func__,
			df_slave_cmd_done_check_enable, (df_slave_cmd_done_check_enable == 1) ? "enabled" : "disabled");
	pr_debug("[%s]: df_fpga_data_cs_location = %d (%s)\n", __func__,
			df_fpga_data_cs_location, (df_fpga_data_cs_location == 1) ? "I2C" : "MMIO");
	pr_debug("[%s]: df_fpga_popcnt_location = %d (%s)\n", __func__,
			df_fpga_popcnt_location, (df_fpga_popcnt_location == 1) ? "I2C" : "MMIO");
//MK0307-end
//MK0321-begin
	pr_debug("[%s]: df_fpga_reset_ctrl_method = %d (%s)\n", __func__,
			df_fpga_reset_ctrl_method, (df_fpga_reset_ctrl_method == 1) ? "I2C" : "MMIO");
//MK0321-end
}

void set_debug_feat_bsm_wrt_flags(unsigned int flag)
{
	/* Config BSM_WRITE Command Checksum Verification */
	df_bsm_wrt_cmd_cs_retry_enable = (unsigned char)((flag & 0x00000008) >> 3);
	if (df_bsm_wrt_cmd_cs_retry_enable) {
		df_bsm_wrt_cmd_cs_max_retry_count = (unsigned char)(flag & 0x00000007);
	} else {
		df_bsm_wrt_cmd_cs_max_retry_count = 0;
	}

	/* Config BSM_WRITE Data Checksum Verification */
	df_bsm_wrt_data_cs_retry_enable = (unsigned char)((flag & 0x00000080) >> 7);
	if (df_bsm_wrt_data_cs_retry_enable) {
		df_bsm_wrt_data_cs_max_retry_count = (unsigned char)((flag & 0x00000070) >> 4);
	} else {
		df_bsm_wrt_data_cs_max_retry_count = 0;
	}

	/* Config BSM_WRITE Command Fake-Rd Retry settings */
	df_bsm_wrt_fr_retry_enable = (unsigned char)((flag & 0x00000800) >> 11);
	if (df_bsm_wrt_fr_retry_enable) {
		df_bsm_wrt_fr_max_retry_count = (unsigned char)((flag & 0x00000700) >> 8);
	} else {
		df_bsm_wrt_fr_max_retry_count = 0;
	}

	/* Config BSM_WRITE Query Command Retry settings */
	df_bsm_wrt_qc_retry_enable = (unsigned char)((flag & 0x00008000) >> 15);
	if (df_bsm_wrt_qc_retry_enable) {
		df_bsm_wrt_qc_max_retry_count = (unsigned char)((flag & 0x00007000) >> 12);
	} else {
		df_bsm_wrt_qc_max_retry_count = 0;
	}

	/* Config BSM_WRITE Skip Query Enable */
	df_bsm_wrt_skip_query_command_enable = (unsigned char)((flag & 0x00010000) >> 16);

	/* Config BSM_WRITE skip G.W.S. reg check */
	df_bsm_wrt_skip_gws_enable = (unsigned char)((flag & 0x00020000) >> 17);

//MK0301-begin
	/* Config BSM_WRITE skip termination enable */
	df_bsm_wrt_skip_termination_enable = (unsigned char)((flag & 0x00040000) >> 18);

	/* Config BSM_WRITE send a dummy command for LBA enable */
	df_bsm_wrt_send_dummy_command_enable = (unsigned char)((flag & 0x00080000) >> 19);

	/* Config BSM_WRITE do a dummy read enable */
	df_bsm_wrt_do_dummy_read_enable = (unsigned char)((flag & 0x00100000) >> 20);
//MK0301-end

//MK0307-begin
	/* Config BSM_WRITE pop count enable */
	df_bsm_wrt_popcnt_enable = (unsigned char)((flag & 0x00200000) >> 21);
//MK0307-end

	pr_debug("[%s]: df_bsm_wrt_cmd_cs_retry_enable = %d  df_bsm_wrt_cmd_cs_max_retry_count = %d\n",
			__func__, df_bsm_wrt_cmd_cs_retry_enable, df_bsm_wrt_cmd_cs_max_retry_count);
	pr_debug("[%s]: df_bsm_wrt_data_cs_retry_enable = %d  df_bsm_wrt_data_cs_max_retry_count = %d\n",
			__func__, df_bsm_wrt_data_cs_retry_enable, df_bsm_wrt_data_cs_max_retry_count);
	pr_debug("[%s]: df_bsm_wrt_fr_retry_enable = %d  df_bsm_wrt_fr_max_retry_count = %d\n",
			__func__, df_bsm_wrt_fr_retry_enable, df_bsm_wrt_fr_max_retry_count);
	pr_debug("[%s]: df_bsm_wrt_qc_retry_enable = %d  df_bsm_wrt_qc_max_retry_count = %d\n",
			__func__, df_bsm_wrt_qc_retry_enable, df_bsm_wrt_qc_max_retry_count);
	pr_debug("[%s]: df_bsm_wrt_skip_query_command_enable = %d\n", __func__, df_bsm_wrt_skip_query_command_enable);
	pr_debug("[%s]: df_bsm_wrt_skip_gws_enable = %d\n",	__func__, df_bsm_wrt_skip_gws_enable);
//MK0301-begin
	pr_debug("[%s]: df_bsm_wrt_skip_termination_enable = %d\n",	__func__, df_bsm_wrt_skip_termination_enable);
	pr_debug("[%s]: df_bsm_wrt_send_dummy_command_enable = %d  LBA = 0x%.8X\n",
			__func__, df_bsm_wrt_send_dummy_command_enable, df_bsm_wrt_dummy_command_lba);
	pr_debug("[%s]: df_bsm_wrt_do_dummy_read_enable = %d  ADDR = 0x%.16lX\n",
			__func__, df_bsm_wrt_do_dummy_read_enable, (unsigned long)df_bsm_wrt_dummy_read_addr);
//MK0301-end

//MK0307-begin
	pr_debug("[%s]: df_bsm_wrt_popcnt_enable = %d\n", __func__, df_bsm_wrt_popcnt_enable);
//MK0307-end
}

void set_debug_feat_bsm_rd_flags(unsigned int flag)
{
	/* Config BSM_READ Command Checksum Verification */
	df_bsm_rd_cmd_cs_retry_enable = (unsigned char)((flag & 0x00000008) >> 3);
	if (df_bsm_rd_cmd_cs_retry_enable) {
		df_bsm_rd_cmd_cs_max_retry_count = (unsigned char)(flag & 0x00000007);
	} else {
		df_bsm_rd_cmd_cs_max_retry_count = 0;
	}

	/* Config BSM_READ Data Checksum Verification */
	df_bsm_rd_data_cs_retry_enable = (unsigned char)((flag & 0x00000080) >> 7);
	if (df_bsm_rd_data_cs_retry_enable) {
		df_bsm_rd_data_cs_max_retry_count = (unsigned char)((flag & 0x00000070) >> 4);
	} else {
		df_bsm_rd_data_cs_max_retry_count = 0;
	}

	/* Config BSM_READ Command Fake-Wrt Retry settings */
	df_bsm_rd_fw_retry_enable = (unsigned char)((flag & 0x00000800) >> 11);
	if (df_bsm_rd_fw_retry_enable) {
		df_bsm_rd_fw_max_retry_count = (unsigned char)((flag & 0x00000700) >> 8);
	} else {
		df_bsm_rd_fw_max_retry_count = 0;
	}

	/* Config BSM_READ Query Command Retry settings */
	df_bsm_rd_qc_retry_enable = (unsigned char)((flag & 0x00008000) >> 15);
	if (df_bsm_rd_qc_retry_enable) {
		df_bsm_rd_qc_max_retry_count = (unsigned char)((flag & 0x00007000) >> 12);
	} else {
		df_bsm_rd_qc_max_retry_count = 0;
	}

	/* Config BSM_READ Skip Query Enable */
	df_bsm_rd_skip_query_command_enable = (unsigned char)((flag & 0x00010000) >> 16);

	/* Config BSM_READ skip G.R.S. reg check */
	df_bsm_rd_skip_grs_enable = (unsigned char)((flag & 0x00020000) >> 17);

//MK0301-begin
	/* Config BSM_READ skip termination enable */
	df_bsm_rd_skip_termination_enable = (unsigned char)((flag & 0x00040000) >> 18);

	/* Config BSM_READ send a dummy command for LBA enable */
	df_bsm_rd_send_dummy_command_enable = (unsigned char)((flag & 0x00080000) >> 19);

	/* Config BSM_READ do a dummy read enable */
	df_bsm_rd_do_dummy_read_enable = (unsigned char)((flag & 0x00100000) >> 20);
//MK0301-end

//MK0307-begin
	/* Config BSM_READ pop count enable */
	df_bsm_rd_popcnt_enable = (unsigned char)((flag & 0x00200000) >> 21);
//MK0307-end

	pr_debug("[%s]: df_bsm_rd_cmd_cs_retry_enable = %d  df_bsm_rd_cmd_cs_max_retry_count = %d\n",
			__func__, df_bsm_rd_cmd_cs_retry_enable, df_bsm_rd_cmd_cs_max_retry_count);
	pr_debug("[%s]: df_bsm_rd_data_cs_retry_enable = %d  df_bsm_rd_data_cs_max_retry_count = %d\n",
			__func__, df_bsm_rd_data_cs_retry_enable, df_bsm_rd_data_cs_max_retry_count);
	pr_debug("[%s]: df_bsm_rd_fw_retry_enable = %d  df_bsm_rd_fw_max_retry_count = %d\n",
			__func__, df_bsm_rd_fw_retry_enable, df_bsm_rd_fw_max_retry_count);
	pr_debug("[%s]: df_bsm_rd_qc_retry_enable = %d  df_bsm_rd_qc_max_retry_count = %d\n",
			__func__, df_bsm_rd_qc_retry_enable, df_bsm_rd_qc_max_retry_count);
	pr_debug("[%s]: df_bsm_rd_skip_query_command_enable = %d\n", __func__, df_bsm_rd_skip_query_command_enable);
	pr_debug("[%s]: df_bsm_rd_skip_grs_enable = %d\n",	__func__, df_bsm_rd_skip_grs_enable);
//MK0301-begin
	pr_debug("[%s]: df_bsm_rd_skip_termination_enable = %d\n",	__func__, df_bsm_rd_skip_termination_enable);
	pr_debug("[%s]: df_bsm_rd_send_dummy_command_enable = %d  LBA = 0x%.8X\n",
			__func__, df_bsm_rd_send_dummy_command_enable, df_bsm_rd_dummy_command_lba);
	pr_debug("[%s]: df_bsm_rd_do_dummy_read_enable = %d  ADDR = 0x%.16lX\n",
			__func__, df_bsm_rd_do_dummy_read_enable, (unsigned long)df_bsm_rd_dummy_read_addr);
//MK0301-end

//MK0307-begin
	pr_debug("[%s]: df_bsm_rd_popcnt_enable = %d\n", __func__, df_bsm_rd_popcnt_enable);
//MK0307-end
}


void init_bcom_ctrl_method(void)
{
	unsigned char reg;

	reg = ((unsigned char) read_smbus(3, 5, 0x70)) & 0x80;
	df_bcom_ctrl_method = (reg >>= 7);
}

unsigned char get_bcom_ctrl_method(void)
{
	return(df_bcom_ctrl_method);
}

//MK0302-begin
unsigned char bcom_toggle_enabled(void)
{
	return(df_bcom_toggle_enable);
}
//MK0302-end

//MK0307-begin
unsigned char slave_data_cs_enabled(void)
{
	return(df_slave_data_cs_enable);
}

unsigned char slave_cmd_done_check_enabled(void)
{
	return(df_slave_cmd_done_check_enable);
}

unsigned char get_data_cs_location(void)
{
	return(df_fpga_data_cs_location);
}

unsigned char get_popcnt_location(void)
{
	return(df_fpga_popcnt_location);
}
//MK0307-end

//MK0321-begin
void hv_config_fpga_reset_ctrl(unsigned char sw)
{
	unsigned char reg;

	pr_debug("[%s]: FPGA Reset will be enabled or disabled using %s\n",
			__func__, (sw == 1) ? "I2C" : "MMIO");

	reg = ((unsigned char) read_smbus(3, 5, 0x70)) & 0xF7;
	reg |= ((sw & 0x01) << 3);
	write_smbus(3, 5, 0x70, reg);
	udelay(100);
    write_smbus(3, 7, 0x70, reg);
    udelay(100);
}

void init_fpga_reset_ctrl_method(void)
{
	unsigned char reg;

	reg = ((unsigned char) read_smbus(3, 5, 0x70)) & 0x08;
	df_fpga_reset_ctrl_method = (reg >>= 3);
}

unsigned char get_fpga_reset_ctrl_method(void)
{
	return(df_fpga_reset_ctrl_method);
}
//MK0321-end


unsigned char bsm_wrt_cmd_checksum_verification_enabled(void)
{
	return(df_bsm_wrt_cmd_cs_retry_enable);
}

unsigned char get_bsm_wrt_cmd_checksum_max_retry_count(void)
{
	return(df_bsm_wrt_cmd_cs_max_retry_count);
}

unsigned char bsm_wrt_data_checksum_verification_enabled(void)
{
	return(df_bsm_wrt_data_cs_retry_enable);
}

unsigned char get_bsm_wrt_data_checksum_max_retry_count(void)
{
	return(df_bsm_wrt_data_cs_max_retry_count);
}

unsigned char bsm_wrt_fr_retry_enabled(void)
{
	return(df_bsm_wrt_fr_retry_enable);
}

unsigned char get_bsm_wrt_fr_max_retry_count(void)
{
	return(df_bsm_wrt_fr_max_retry_count);
}

unsigned char bsm_wrt_qc_retry_enabled(void)
{
	return(df_bsm_wrt_qc_retry_enable);
}

unsigned char get_bsm_wrt_qc_max_retry_count(void)
{
	return(df_bsm_wrt_qc_max_retry_count);
}

unsigned char bsm_wrt_skip_query_command_enabled(void)
{
	return(df_bsm_wrt_skip_query_command_enable);
}

unsigned char bsm_wrt_skip_gws_enabled(void)
{
	return(df_bsm_wrt_skip_gws_enable);
}

//MK0301//MK0224-begin
//MK0301unsigned char bsm_wrt_skip_fr_on_lba_enabled(void)
//MK0301{
//MK0301	return(df_bsm_wrt_skip_fr_on_lba_enable);
//MK0301}

//MK0301void set_bsm_wrt_skipping_lba(unsigned int lba)
//MK0301{
//MK0301	df_bsm_wrt_skipping_lba = lba;
//MK0301}

//MK0301unsigned int get_bsm_wrt_skipping_lba(void)
//MK0301{
//MK0301	return(df_bsm_wrt_skipping_lba);
//MK0301}
//MK0301//MK0224-end

//MK0301-begin
unsigned char bsm_wrt_skip_termination_enabled(void)
{
	return(df_bsm_wrt_skip_termination_enable);
}

unsigned char bsm_wrt_send_dummy_command_enabled(void)
{
	return(df_bsm_wrt_send_dummy_command_enable);
}

unsigned char bsm_wrt_do_dummy_read_enabled(void)
{
	return(df_bsm_wrt_do_dummy_read_enable);
}

void set_bsm_wrt_dummy_command_lba(unsigned int lba)
{
	df_bsm_wrt_dummy_command_lba = lba;
}

unsigned int get_bsm_wrt_dummy_command_lba(void)
{
	return(df_bsm_wrt_dummy_command_lba);
}

void set_bsm_wrt_dummy_read_addr(unsigned long va)
{
	df_bsm_wrt_dummy_read_addr = (void *)va;
}

unsigned long *get_bsm_wrt_dummy_read_addr(void)
{
	return((unsigned long *)df_bsm_wrt_dummy_read_addr);
}
//MK0301-end
//MK0307-begin
unsigned char bsm_wrt_popcnt_enabled(void)
{
	return(df_bsm_wrt_popcnt_enable);
}
//MK0307-end


unsigned char bsm_rd_cmd_checksum_verification_enabled(void)
{
	return(df_bsm_rd_cmd_cs_retry_enable);
}

unsigned char get_bsm_rd_cmd_checksum_max_retry_count(void)
{
	return(df_bsm_rd_cmd_cs_max_retry_count);
}

unsigned char bsm_rd_data_checksum_verification_enabled(void)
{
	return(df_bsm_rd_data_cs_retry_enable);
}

unsigned char get_bsm_rd_data_checksum_max_retry_count(void)
{
	return(df_bsm_rd_data_cs_max_retry_count);
}

unsigned char bsm_rd_fw_retry_enabled(void)
{
	return(df_bsm_rd_fw_retry_enable);
}

unsigned char get_bsm_rd_fw_max_retry_count(void)
{
	return(df_bsm_rd_fw_max_retry_count);
}

unsigned char bsm_rd_qc_retry_enabled(void)
{
	return(df_bsm_rd_qc_retry_enable);
}

unsigned char get_bsm_rd_qc_max_retry_count(void)
{
	return(df_bsm_rd_qc_max_retry_count);
}

unsigned char bsm_rd_skip_query_command_enabled(void)
{
	return(df_bsm_rd_skip_query_command_enable);
}

unsigned char bsm_rd_skip_grs_enabled(void)
{
	return(df_bsm_rd_skip_grs_enable);
}

//MK0301-begin
unsigned char bsm_rd_skip_termination_enabled(void)
{
	return(df_bsm_rd_skip_termination_enable);
}

unsigned char bsm_rd_send_dummy_command_enabled(void)
{
	return(df_bsm_rd_send_dummy_command_enable);
}

unsigned char bsm_rd_do_dummy_read_enabled(void)
{
	return(df_bsm_rd_do_dummy_read_enable);
}

void set_bsm_rd_dummy_command_lba(unsigned int lba)
{
	df_bsm_rd_dummy_command_lba = lba;
}

unsigned int get_bsm_rd_dummy_command_lba(void)
{
	return(df_bsm_rd_dummy_command_lba);
}

void set_bsm_rd_dummy_read_addr(unsigned long va)
{
	df_bsm_rd_dummy_read_addr = (void *)va;
}

unsigned long *get_bsm_rd_dummy_read_addr(void)
{
	return((unsigned long *)df_bsm_rd_dummy_read_addr);
}

void set_pattern_mask(unsigned long mask)
{
	df_pattern_mask = mask;
}

unsigned long get_pattern_mask(void)
{
	return(df_pattern_mask);
}
//MK0301-end
//MK0307-begin
unsigned char bsm_rd_popcnt_enabled(void)
{
	return(df_bsm_rd_popcnt_enable);
}
//MK0307-end


//MK0605void set_user_defined_delay(unsigned int usdelay)
//MK0605{
//MK0605	user_defined_delay_us = usdelay;
//MK0605}

//MK0605unsigned int get_user_defined_delay(void)
//MK0605{
//MK0605	return(user_defined_delay_us);
//MK0605}

//MK0224-begin
void set_bsm_wrt_qc_status_delay(unsigned long delay)
{
	df_bsm_wrt_qc_status_delay = delay;	// in ns
}

unsigned long get_bsm_wrt_qc_status_delay(void)
{
	return(df_bsm_wrt_qc_status_delay);
}

void set_bsm_rd_qc_status_delay(unsigned long delay)
{
	df_bsm_rd_qc_status_delay = delay;	// in ns
}

unsigned long get_bsm_rd_qc_status_delay(void)
{
	return(df_bsm_rd_qc_status_delay);
}
//MK0224-end

//MK0405-begin
void set_user_defined_delay_ms(unsigned char idx, unsigned int msdelay)
{
	if (idx < 5) {
		user_defined_delay_ms[idx] = msdelay;
	}
}

unsigned int get_user_defined_delay_ms(unsigned char idx)
{
	if (idx < 5) {
		return(user_defined_delay_ms[idx]);
	} else {
		return(0);
	}
}
//MK0405-end

//MK0605-begin
void set_user_defined_delay_us(unsigned char idx, unsigned int usdelay)
{
	if (idx < 5) {
		user_defined_delay_us[idx] = usdelay;
	}
}

unsigned int get_user_defined_delay_us(unsigned char idx)
{
	if (idx < 5) {
		return(user_defined_delay_us[idx]);
	} else {
		return(0);
	}
}
//MK0605-end

void hv_delay_us(unsigned int delay_us)
{
	unsigned int i;
	unsigned long t1, t2;


	t1 = hv_nstimeofday();
	if (delay_us > 1000) {
		for (i=0; i < (delay_us/1000); i++)
		{
			udelay(1000);
		}
		udelay(delay_us%1000);
	} else  if (delay_us > 0) {
		udelay(delay_us);
	}
	t2 = hv_nstimeofday();

	pr_debug("[%s]: user_defined_delay_us = %d us, elapsed_time = %ld ns\n", __func__, delay_us, t2-t1);
}
//MK0223-end

//MK0307-begin
void save_query_status(void)
{
	memcpy((void *)status_buf, (void *)cmd_burst_buf, STATUS_BUFFER_SIZE);
	pr_debug("[%s]: cmd_burst_buf\n", __func__);
	display_buffer((unsigned long *)cmd_burst_buf, 8, 0xffffffffFFFFFFFF);
	pr_debug("[%s]: status_buf\n", __func__);
	display_buffer((unsigned long *)status_buf, 8, 0xffffffffFFFFFFFF);
}

int sw_popcnt_32(int i)
{
     // Java: use >>> instead of >>
     // C or C++: use uint32_t
     i = i - ((i >> 1) & 0x55555555);
    i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
     return (((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;

}

int sw_popcnt_64(long i)
{
     i = i - ((i >> 1) & 0x5555555555555555);
     i = (i & 0x3333333333333333) + ((i >> 2) & 0x3333333333333333);
     return (((i + (i >> 4)) & 0x0F0F0F0F0F0F0F0F) * 0x0101010101010101) >> 56;
}

int calculate_popcnt(void *pbuff, unsigned long bytecnt, unsigned char type)
{
	int i, popcnt=0;
	unsigned int *pdword=(unsigned int *)pbuff;
	unsigned long *pqword=(unsigned long *)pbuff;

	if (type == 0) {
		/* 32-bit word at even locations */
		for (i=0; i<bytecnt/4; i+=2)
		{
//			popcnt += __builtin_popcount(pdword[i]);	// 32-bit GCC intrinsic
			popcnt += sw_popcnt_32(pdword[i]);				// 32-bit SW algorithm
		}
	} else if (type == 1) {
		/* 32-bit word at odd locations */
		for (i=1; i<bytecnt/4; i+=2)
		{
//			popcnt += __builtin_popcount(pdword[i]);	// 32-bit GCC intrinsic
			popcnt += sw_popcnt_32(pdword[i]);				// 32-bit SW algorithm
		}
	} else {
		for (i=0; i<bytecnt/8; i++)
		{
//			popcnt += __builtin_popcountl(pqword[i]);	// 64-bit GCC intrinsic
			popcnt += sw_popcnt_64(pqword[i]);			// 64-bit SW algorithm
		}
	}

	return popcnt;
}

//MK0418int hv_read_fake_buffer_popcnt(unsigned char fake_rw_flag)
short int hv_read_fake_buffer_popcnt(unsigned char fake_rw_flag)	//MK0418
{
	struct hv_query_cmd_status *pqcs=(struct hv_query_cmd_status *)status_buf;
	unsigned char idx=(fake_rw_flag & 0x03), bytedata=0;
	unsigned int fpga_id=FPGA1_ID;
	unsigned int i2c_addr_offset=0, popcnt=0;

	/*
	 * [1]: 0=master FPGA, 1=slave FPGA
	 * [0]: 0=fake-write, 1=fake-read
	 *
	 * NOTE: We support idx=0 & 2 cases only at this point (3/9/2017)
	 */
	if (idx == 0) {
		/* Fake-write / master */
		fpga_id = FPGA1_ID;
//MK0418		i2c_addr_offset = 0;
		i2c_addr_offset = 0x0C;	//MK0418
//MK0425		popcnt = pqcs->master_popcount;
		popcnt = pqcs->fw_m_pc;	//MK0425
	} else if (idx == 1) {
		/* Fake-read / master */
		fpga_id = FPGA1_ID;
//MK0418		i2c_addr_offset = 4;
		i2c_addr_offset = 0x0E;	//MK0418
//MK0425		popcnt = pqcs->master_popcount;
		popcnt = pqcs->fr_m_pc;	//MK0425
	} else if (idx == 2) {
		/* Fake-write / slave */
		fpga_id = FPGA2_ID;
//MK0418		i2c_addr_offset = 0;
		i2c_addr_offset = 0x0C;	//MK0418
//MK0425		popcnt = pqcs->slave_popcount;
		popcnt = pqcs->fw_s_pc;	//MK0425
	} else {
		/* Fake-read / slave */
		fpga_id = FPGA2_ID;
//MK0418		i2c_addr_offset = 4;
		i2c_addr_offset = 0x0E;	//MK0418
//MK0425		popcnt = pqcs->slave_popcount;
		popcnt = pqcs->fr_s_pc;	//MK0425
	}

	/*
	 * Always get pop count from I2C if it is for fake-read. Otherwise,
	 * check data CS location flag.
	 */
	if ( get_popcnt_location() == 1 ) {
		/* Get data pop count from I2C registers */
		write_smbus(3, fpga_id, 0xA0, 2);	// Set to page 2
		udelay(1000);

//MK0418		bytedata = (unsigned char) read_smbus(3, fpga_id, 0x0F + i2c_addr_offset);
		bytedata = (unsigned char) read_smbus(3, fpga_id, i2c_addr_offset+1);	//MK0418
		udelay(100);
		popcnt = (unsigned int)bytedata;
////		pr_debug("[%s]: fpga_id = %d, i2c_addr_offset = 0x%.2x, bytedata = 0x%.2X\n", __func__, fpga_id, i2c_addr_offset+1, bytedata);

//MK0418		bytedata = (unsigned char) read_smbus(3, fpga_id, 0x0E + i2c_addr_offset);
		bytedata = (unsigned char) read_smbus(3, fpga_id, i2c_addr_offset);	//MK0418
		udelay(100);
		popcnt = (popcnt << 8) | (unsigned int)bytedata;
////		pr_debug("[%s]: fpga_id = %d, i2c_addr_offset = 0x%.2x, bytedata = 0x%.2X\n", __func__, fpga_id, i2c_addr_offset, bytedata);
////		pr_debug("[%s]: fpga popcnt = 0x%.4X\n", __func__, popcnt);

//MK0418		bytedata = (unsigned char) read_smbus(3, fpga_id, 0x0D + i2c_addr_offset);
//MK0418		udelay(100);
//MK0418		popcnt = (popcnt << 8) | (unsigned int)bytedata;

//MK0418		bytedata = (unsigned char) read_smbus(3, fpga_id, 0x0C + i2c_addr_offset);
//MK0418		udelay(100);
//MK0418		popcnt = (popcnt << 8) | (unsigned int)bytedata;

		write_smbus(3, fpga_id, 0xA0, 0);	// Set to page 0
	}

//MK0418	return (int)popcnt;
	return (short int)popcnt;		//MK0418
}
//MK0307-end

#ifdef SIMULATION_TB
static int get_ramdisk() { return  0; }
static int get_ramdisk_start() { return  0x0; }
#endif

void get_bsm_iodata(struct HV_BSM_IO_t *p_bio_data)
{
	p_bio_data->b_size = bsm_size();
	p_bio_data->b_iomem = (void *)hv_group[gid].mem[0].v_mmio+bsm_start();
	if (get_ramdisk())
		p_bio_data->phys_start = (unsigned long) (get_ramdisk_start()+
			bsm_start());
	else
		; /* phys_start is used by char device on RAMDISK */
}

void get_mmls_iodata(struct HV_MMLS_IO_t *p_mio_data)
{
	p_mio_data->m_size = mmls_size();
	p_mio_data->m_iomem = (void *)hv_group[gid].mem[0].v_mmio+mmls_start();
	if (get_ramdisk())
		p_mio_data->phys_start = (unsigned long) (get_ramdisk_start()+
			mmls_start());
	else
		; /* phys_start is used by char device on RAMDISK */
}

#ifdef USER_SPACE_CMD_DRIVER
extern int get_fd();

static void hv_mmap_mem(unsigned long phys, unsigned long *virt_p, unsigned long size)
{
	*virt_p = (unsigned long)mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, get_fd(), phys);
	if(*virt_p == -1)
		printf("hv_mmap_mem failed phys(0x%lx), size(0x%lx) errno(%d)\n", phys, size, errno);
}

static void hv_mumap_mem(void *virt_p, unsigned long size)
{
	if(munmap(virt_p, size) == -1) 
		printf("hv_mumap_mem failed virt_p(0x%lx), size(0x%lx) errno(%d)\n", (unsigned long)virt_p, size, errno);
}

int hv_io_init() 
{
#ifdef MULTI_DIMM
	int ways = interleave_ways(gid);
	int num_mem = hv_group[gid].num_hv/ways;
#else
	int ways = 1;
	int num_mem = 1;
#endif
	int i;

	calc_emmc_alloc(gid);

	for (i=0; i<num_mem; i++) {
		hv_group[gid].mem[i].p_dram = hv_group[gid].mem[i].p_mmio*ways - HV_MMLS_DRAM_SIZE;
		hv_mmap_mem(hv_group[gid].mem[i].p_mmio*ways, 
				(unsigned long *) &hv_group[gid].mem[i].v_mmio, 
				HV_MMIO_SIZE*ways);			
		hv_mmap_mem(hv_group[gid].mem[i].p_dram, 
				(unsigned long *) &hv_group[gid].mem[i].v_dram, 
				HV_MMLS_DRAM_SIZE);
	}
	return 0;
}

void hv_io_release(void)
{
#ifdef MULTI_DIMM
	int ways = interleave_ways(gid);
	int num_mem = hv_group[gid].num_hv/ways;
#else
	int ways = 1;
	int num_mem = 1;
#endif
	int i;

	for (i=0; i<num_mem; i++) {
		hv_mumap_mem(hv_group[gid].mem[i].v_mmio, HV_MMIO_SIZE*ways);			
		hv_mumap_mem(hv_group[gid].mem[i].v_dram, HV_MMLS_DRAM_SIZE);
	}
}
#endif	// USER_SPACE_CMD_DRIVER


#endif // REV_B_MM

long bsm_start(void) { return 0; }
long bsm_size(void) { return get_bsm_default_size();  }
long mmls_start(void) { return bsm_start()+bsm_size(); }
long mmls_size(void) { return get_pmem_default_size();  }
long mmls_cdev(void) { return hv_group[gid].mmls_device==DEV_CHAR ? 1 : 0; }

long bsm_cdev(void) { return hv_group[gid].bsm_device==DEV_CHAR ? 1 : 0; }
