/*
 *
 *  HVDIMM header file for BSM/MMLS.
 *
 *  (C) 2015 Netlist, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef SIMULATION_TB
#include <linux/init.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/delay.h>

#include "hvdimm.h"
#include "hv_params.h"
#include "hv_cache.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#endif

#include "hv_mmio.h"
#include "hv_cmd.h"

#define SYNC		0
#define ASYNC		1

#define SEQUENTIAL_TEST	1	
#define RANDOM_TEST	2	

#define KER_BUF_LEN	64

#ifdef SIMULATION_TB
#define pr_debug printf
#define pr_info printf
#define pr_notice printf
#define u64 unsigned long
#define __iomem 
#define hv_log
#endif

static struct dentry *file_r;
static struct dentry *u64int;
static int file_stress;
u64 bsm_stress_size=0x40000;
u64 mmls_stress_size=0x40000;
u64 stress_type=0;
u64 stress_block_size=4096;
u64 stress_data_compare=0;

static char ker_buf_stress[KER_BUF_LEN];

static char stress_src_buf[16*1024*1024];  
static char dst_buf[16*1024*1024]; 
static char untouched_buf[16*1024*1024]; 

static void __iomem *bsm_io;
static void __iomem *mmls_io;

static int q_used = 0;
int async = 0;

#ifndef SIMULATION_TB
void hv_cmd_stress_test(void);

static spinlock_t lock;
static spinlock_t cb_lock;
static unsigned long flags;
#else
void hv_cmd_stress_test(int type);

static unsigned int lock;
static unsigned int cb_lock;
static unsigned long flags;
static void spin_lock_init(unsigned int *lock) {}
static spin_lock_irqsave(unsigned int *lock, unsigned long flags) {}
static spin_unlock_irqrestore(unsigned int *lock, unsigned long flags) {}

static int get_ramdisk() { return  0; }
static int get_cache_enabled() { return 0; }
static int get_queue_size() { return 16; }
static int get_async_mode() { return async; }

static void hv_memcpy_toio(void __iomem *dst, void *src, int count, int async, void (*callback)(int tag, int err), int tag) {}
static void hv_memcpy_fromio(void *dst, void __iomem *src, int count, int async, void (*callback)(int tag, int err), int tag) {}

/* cache type */
#define	CACHE_BSM		0
#define CACHE_MMLS		1
#define CACHE_TYPE_NUM		2
static int cache_write(unsigned int type, unsigned long lba, int sectors, void *data, int tag, unsigned int async, void (*callback)(int tag, int err)) {}
static int cache_read(unsigned int type, unsigned long lba, int sectors, void *data, int tag, unsigned int async, void (*callback)(int tag, int err)) {}

static void udelay(unsigned long delay) 
{ 
	usleep(delay);
}

static void get_random_bytes(void *val, int size) 
{
	unsigned int r, i;
	unsigned char *c, *rp;

	r = rand();
	if (size == 1)
		r = r%256;
	else if (size == 2)
		r = r%(256*256);

	c = (unsigned char *)val;
	rp = (unsigned char *)&r;
	for (i=0; i<size; i++)
		c[i] = rp[i];
}
#endif

static unsigned long b, a;
static unsigned long latency;

/* supported block size */
#define NUM_BLOCK_SIZE	9
static unsigned long hv_block_size[]= 
{
	4*1024,		/* 4KB */
	8*1024,		/* 8KB */
	16*1024,	/* 16KB */
	32*1024,	/* 32KB */
	1024*1024,	/* 1MB */
	2*1024*1024,	/* 2MB */
	4*1024*1024,	/* 4MB */
	8*1024*1024,	/* 8MB */
	16*1024*1024	/* 16MB */
};
static unsigned long block_in_byte;
static unsigned long block_in_sector;

extern void hv_memcpy_toio(void __iomem *dst, void *src, int count, int async, void (*callback)(int tag, int err), int tag);
extern void hv_memcpy_fromio(void *dst, void __iomem *src, int count, int async, void (*callback)(int tag, int err), int tag);

int mmls_read_cache_command(unsigned int tag,
				unsigned int sector,
				unsigned int lba,
				unsigned long mm_addr,
				unsigned char async,
				void *callback_func)
{
	if (get_cache_enabled())
		cache_read(CACHE_MMLS, lba, sector, (void *)mm_addr, tag, async, callback_func);
	else
		mmls_read_command(tag, sector, lba, mm_addr, async, callback_func);
	return 0;
}

int mmls_write_cache_command(unsigned int tag,
				unsigned int sector,
				unsigned int lba,
				unsigned long mm_addr,
				unsigned char async,
				void *callback_func)
{
	if (get_cache_enabled())
		cache_write(CACHE_MMLS, lba, sector, (void *)mm_addr, tag, async, callback_func);
	else
		mmls_write_command(tag, sector, lba, mm_addr, async, callback_func);
	return 0;
}

int bsm_read_cache_command(unsigned int tag,
					unsigned int sector,
					unsigned int lba,
					unsigned char *buf,
					unsigned char async,
					void *call_back)
{
	if (get_cache_enabled())
		cache_read(CACHE_BSM, lba, sector, buf, tag, async, call_back);
	else
//MK0207		bsm_read_command(tag, sector, lba, buf, async, call_back);
//MK0207-begin
		bsm_read_command(tag, sector, lba, buf, async, call_back, 0);
//MK0207-end
	return 0;
}

int bsm_write_cache_command(unsigned int tag,
					unsigned int sector,
					unsigned int lba,
					unsigned char *buf,
					unsigned char async,
					void *call_back)
{
	if (get_cache_enabled())
		cache_write(CACHE_BSM, lba, sector, buf, tag, async, call_back);
	else
		bsm_write_command(tag, sector, lba, buf, async, call_back);
	return 0;
}

static void hv_memcpy_cache_toio(void __iomem *dst, void *src, int count, int async, void (*callback)(int tag, int err), int tag)
{
	if (get_cache_enabled()) {
		if ((unsigned long)dst < (unsigned long)mmls_io)
			cache_write(CACHE_BSM, (dst-bsm_io)/HV_BLOCK_SIZE, count/HV_BLOCK_SIZE, src, tag, async, callback);
		else
			cache_write(CACHE_MMLS, (dst-mmls_io)/HV_BLOCK_SIZE, count/HV_BLOCK_SIZE, src, tag, async, callback);
	}
	else
		hv_memcpy_toio(dst, src, count, async, callback, tag);
}

static void hv_memcpy_cache_fromio(void *dst, void __iomem *src, int count, int async, void (*callback)(int tag, int err), int tag)
{
	if (get_cache_enabled()) {
		if ((unsigned long)src < (unsigned long)mmls_io)
			cache_read(CACHE_BSM, (src-bsm_io)/HV_BLOCK_SIZE, count/HV_BLOCK_SIZE, dst, tag, async, callback);
		else
			cache_read(CACHE_MMLS, (src-mmls_io)/HV_BLOCK_SIZE, count/HV_BLOCK_SIZE, dst, tag, async, callback);
	}
	else
		hv_memcpy_fromio(dst, src, count, async, callback, tag);
}

#ifndef SIMULATION_TB
/* view stress test operation */
static ssize_t view_hv_stress(struct file *fp, char __user *user_buffer,
				size_t count, loff_t *position)
{
	pr_debug("%s ...\n", __func__);

	return simple_read_from_buffer(user_buffer, count, position,
		ker_buf_stress,	KER_BUF_LEN);
}

/* write stress test operation */
static ssize_t hv_stress(struct file *fp, const char __user *user_buffer,
				size_t count, loff_t *position)
{
	hv_cmd_stress_test();
	return simple_write_to_buffer(ker_buf_stress, KER_BUF_LEN, position,
		user_buffer, count);
}

static const struct file_operations fops_hv_stress = {
	.read = view_hv_stress,
	.write = hv_stress,
};

int hv_cmd_stress_init(struct dentry *dirret)
{
	/* This requires hv_stresss file operations */
	file_r = debugfs_create_file("hv_stress", 0644,dirret,&file_stress,
	&fops_hv_stress);
	if (!file_r) {
		/* Abort module loading */
		pr_err("init_debug: failed to create file for hv_stresss");
		return (-ENODEV);
	}

	/* create input variable bsm_stress_size */
	u64int = debugfs_create_u64("bsm_stress_size", 0644, dirret, &bsm_stress_size);
	if (!u64int) {
		pr_err("error in creating bsm_stress_size file");
		return (-ENODEV);
	}

	/* create input variable mmls_stress_size */
	u64int = debugfs_create_u64("mmls_stress_size", 0644, dirret, &mmls_stress_size);
	if (!u64int) {
		pr_err("error in creating mmls_stress_size file");
		return (-ENODEV);
	}

	/* create input variable stress_type */
	u64int = debugfs_create_u64("stress_type", 0644, dirret, &stress_type);
	if (!u64int) {
		pr_err("error in creating stress_type file");
		return (-ENODEV);
	}

	/* create input variable stress_type */
	u64int = debugfs_create_u64("stress_block_size", 0644, dirret, &stress_block_size);
	if (!u64int) {
		pr_err("error in creating stress_block_size file");
		return (-ENODEV);
	}

	/* create input variable stress_type */
	u64int = debugfs_create_u64("stress_data_compare", 0644, dirret, &stress_data_compare);
	if (!u64int) {
		pr_err("error in creating stress_data_compare file");
		return (-ENODEV);
	}

	return 0;
}
#endif

static unsigned char rand_char(void)
{
	unsigned char i;

	get_random_bytes(&i, sizeof(i));
	return i;
}

static unsigned short rand_short(void)
{
	unsigned short i;

	get_random_bytes(&i, sizeof(i));
	return i;
}

static unsigned int rand_int(void)
{
	unsigned int i;

	get_random_bytes(&i, sizeof(i));
	return i;
}

static void calc_block_size(unsigned long size)
{
	int i;

	for (i=0; i<NUM_BLOCK_SIZE; i++) {
		if (size <= hv_block_size[i]) {
			block_in_byte = hv_block_size[i];
			block_in_sector = block_in_byte/HV_BLOCK_SIZE;
			return;
		}
	}
	block_in_byte = hv_block_size[NUM_BLOCK_SIZE-1];
	block_in_sector = block_in_byte/HV_BLOCK_SIZE;
	return;
}

static int get_q(void)
{
	int loop_cnt;

	/* wait if q is full */
	if (async) {
		loop_cnt = 0;
		while (q_used >= get_queue_size()) {
			udelay(100);
			if(loop_cnt++ < 10) 
				hv_log("wait for queue flushed, loop_cnt %d\n", loop_cnt);
		}
	}
	return 0;
}

static void inc_q(void)
{
	if (async) {
		q_used ++;
		hv_log("inc_q: (q_used %d)\n", q_used);
	}
}

static void stress_callback(int tag, int err)
{
	unsigned long flags;

	spin_lock_irqsave(&cb_lock, flags);
	q_used --;
	hv_log("stress_callback: (q_used %d)\n", q_used);
	spin_unlock_irqrestore(&cb_lock, flags);
}

static void wait_for_queue_flushed(void)
{
	int loop_cnt;

	if (async) {
		loop_cnt = 0;
		while (q_used > 0) {
			udelay(100);
			if(loop_cnt++ < 10)
				hv_log("read_test:  wait for queue flushed q_used(%d)\n", q_used);
		}
	}
}

static void init_stress_src_buf(unsigned char c)
{
	unsigned long i;

	for (i=0; i<sizeof(stress_src_buf); i++) {
		stress_src_buf[i] = c;
		c++;
		if ((i%4096) == 0)
			c = c+7;
	}
}

static int read_test(unsigned long bstart, unsigned long bsecs, unsigned long  mstart, unsigned long msecs, int flag)
{
	unsigned long bsec, msec;
	unsigned int tag;
 	int async_saved=0;
	unsigned char *src_buf, *ut_buf;

	/* use sync mode for read verification */
	if (stress_data_compare) {
		async_saved = async;
		async = 0;
	}

	/* read BSM and MMLS */
	hv_log("hv: read_test, read BSM and MMLS\n");
	src_buf = stress_src_buf;
	ut_buf = untouched_buf;
	bsec = bstart;
	msec = mstart;
	tag = 0;
	while (1)
	{
		if (bsec < bsecs) {
			if (stress_data_compare)
				memset(dst_buf, 0, block_in_byte);
			if (get_q()) {
				pr_notice("FAILURE: queue full\n");
				goto err;
			}
			spin_lock_irqsave(&lock, flags);
			if (get_ramdisk()) {
				hv_memcpy_cache_fromio(dst_buf, bsm_io+bsec*HV_BLOCK_SIZE, block_in_byte, async, stress_callback, tag);
				inc_q();
				tag ++;
			}
			else {
				if (!bsm_read_cache_command(tag, block_in_sector, bsec, dst_buf,
					  async, stress_callback)) {
					inc_q();
					tag ++;
				}
			}
			spin_unlock_irqrestore(&lock, flags);
			if (tag == get_queue_size())
				tag = 0;

			if (stress_data_compare) {
				/* compare the data */
				if ((flag == SEQUENTIAL_TEST &&
		                     memcmp(src_buf, dst_buf, block_in_byte) != 0) ||
				    (flag == RANDOM_TEST && 
				     memcmp(src_buf, dst_buf, block_in_byte) != 0 &&
				     memcmp(ut_buf, dst_buf, block_in_byte) != 0)) {
					pr_notice("FAILURE: BSM read test failed (bsec 0x%lx)\n", bsec);
					goto err;
				}
				src_buf = src_buf + block_in_byte;
				if (((unsigned long)src_buf - (unsigned long)stress_src_buf) >= sizeof(stress_src_buf))
					src_buf = stress_src_buf;
				ut_buf = ut_buf + block_in_byte;
				if (((unsigned long)ut_buf - (unsigned long)untouched_buf) >= sizeof(untouched_buf))
					ut_buf = untouched_buf;
			}
			bsec = bsec + block_in_sector;
		}
		
		if (msec < msecs) {
			if (stress_data_compare)
				memset(dst_buf, 0, block_in_byte);
			if (get_q()) {
				pr_notice("FAILURE: queue full\n");
				goto err;
			}
			spin_lock_irqsave(&lock, flags);
			if (get_ramdisk()) {
				hv_memcpy_cache_fromio(dst_buf, mmls_io+msec*HV_BLOCK_SIZE, block_in_byte, async, stress_callback, tag);
				inc_q();
				tag ++;
			}
			else {
				if (!mmls_read_cache_command(tag, block_in_sector, msec, (unsigned long)dst_buf,
					  async, stress_callback)) {
					inc_q();
					tag ++;
				}
			}
			spin_unlock_irqrestore(&lock, flags);
			if (tag == get_queue_size())
				tag = 0;

			if (stress_data_compare) {
				/* compare the data */
				if ((flag == SEQUENTIAL_TEST &&
		                     memcmp(src_buf, dst_buf, block_in_byte) != 0) ||
				    (flag == RANDOM_TEST && 
				     memcmp(src_buf, dst_buf, block_in_byte) != 0 &&
				     memcmp(ut_buf, dst_buf, block_in_byte) != 0)) {
					pr_notice("FAILURE: MMLS read test failed (msec 0x%lx)\n", msec);
					goto err;
				}
				src_buf = src_buf + block_in_byte;
				if (((unsigned long)src_buf - (unsigned long)stress_src_buf) >= sizeof(stress_src_buf))
					src_buf = stress_src_buf;
				ut_buf = ut_buf + block_in_byte;
				if (((unsigned long)ut_buf - (unsigned long)untouched_buf) >= sizeof(untouched_buf))
					ut_buf = untouched_buf;
			}
			msec = msec + block_in_sector;
		}

		if (bsec >= bsecs && msec >= msecs)
			break;
	}

	if (stress_data_compare)
		async = async_saved;
	return 0;

err:
	if (stress_data_compare)
		async = async_saved;
	return -1;
}

static int sequential_test(unsigned long bstart, unsigned long bsecs, unsigned long  mstart, unsigned long msecs)
{
	unsigned long bsec, msec;
	unsigned int tag;
	unsigned int ret;
	unsigned long total_bytes;
	unsigned char *src_buf;

	if ((bsecs-bstart) == 0 && (msecs-mstart) == 0) {
		pr_notice("selected BSM/MMLS size is 0. no test performed\n");
		return 0;
	}

	if (bsecs-bstart != 0)
		pr_notice("sequential test (BSM start/size in sector 0x%lx/0x%lx)\n", \
				bstart, bsecs-bstart);
	if (msecs-mstart != 0)
		pr_notice("sequential test (MMLS start/size in sector 0x%lx/0x%lx)\n", \
				mstart, msecs-mstart);

	total_bytes = ((bsecs-bstart)+(msecs-mstart))*HV_BLOCK_SIZE;

	b = hv_nstimeofday();

	/* write BSM and MMLS */
	src_buf = stress_src_buf;
	bsec = bstart;
	msec = mstart;
	tag = 0;
	while (1)
	{
		if (bsec < bsecs) {
			if (get_q()) {
				pr_notice("FAILURE: queue full\n");
				return -1;
			}
			spin_lock_irqsave(&lock, flags);
			if (get_ramdisk()) {
				hv_memcpy_cache_toio(bsm_io+bsec*HV_BLOCK_SIZE, src_buf, block_in_byte, async, stress_callback, tag);
				inc_q();
				tag ++;
			}
			else {
				if (!bsm_write_cache_command(tag, block_in_sector, bsec, src_buf,
					  async, stress_callback)) {
					inc_q();
					tag ++;
				}
			}
			src_buf = src_buf + block_in_byte;
			if (((unsigned long)src_buf - (unsigned long)stress_src_buf) >= sizeof(stress_src_buf))
				src_buf = stress_src_buf;
			spin_unlock_irqrestore(&lock, flags);
			if (tag == get_queue_size())
				tag = 0;
			bsec = bsec + block_in_sector;
		}

		if (msec < msecs) {
			if (get_q()) {
				pr_notice("FAILURE: queue full\n");
				return -1;
			}
			spin_lock_irqsave(&lock, flags);
			if (get_ramdisk()) {
				hv_memcpy_cache_toio(mmls_io+msec*HV_BLOCK_SIZE, src_buf, block_in_byte, async, stress_callback, tag);
				inc_q();
				tag ++;
			}
			else {
				if (!mmls_write_cache_command(tag, block_in_sector, msec, 
					(unsigned long)src_buf, async, stress_callback)) {
					inc_q();
					tag ++;
				}
			}
			src_buf = src_buf + block_in_byte;
			if (((unsigned long)src_buf - (unsigned long)stress_src_buf) >= sizeof(stress_src_buf))
				src_buf = stress_src_buf;
			spin_unlock_irqrestore(&lock, flags);
			if (tag == get_queue_size())
				tag = 0;
			msec = msec + block_in_sector;
		}
		
		if (bsec >= bsecs && msec >= msecs)
			break;
	}

	a = hv_nstimeofday();
	latency = a - b;
	pr_info("sequential test write 0x%lx blocks total latency is %ldns\n", total_bytes/block_in_byte, latency);
	pr_info("sequential test write throughput is %ldMB\n", (total_bytes*1000)/latency);
	pr_info("sequential test write latency per %ldKB block is %ldns\n", block_in_byte/1024, latency/(total_bytes/block_in_byte));

	wait_for_queue_flushed();

	b = hv_nstimeofday();
	/* read back the data and verify */
	ret = read_test(bstart, bsecs, mstart, msecs, SEQUENTIAL_TEST);
	a = hv_nstimeofday();
	latency = a - b;
	pr_info("sequential test read 0x%lx blocks total latency is %ldns\n", total_bytes/block_in_byte, latency);
	pr_info("sequential test read throughput is %ldMB\n", (total_bytes*1000)/latency);
	pr_info("sequential test read latency per %ldKB block is %ldns\n", block_in_byte/1024, latency/(total_bytes/block_in_byte));

	wait_for_queue_flushed();

	return ret;
}

#define RANDOM_SECTORS	(rand_char()/32+1)*block_in_sector	/* max 8 blocks */

static int random_test(unsigned long bstart, unsigned long bsecs, unsigned long mstart, unsigned long msecs)
{
	unsigned long bsec, msec;
	unsigned int tag;
	unsigned long i, j;
	unsigned long secs;
	int ret;
	unsigned char *src_buf;
	unsigned long src_offset;

	if ((bsecs-bstart) == 0 && (msecs-mstart) == 0) {
		pr_notice("selected BSM/MMLS size is 0. no test performed\n");
		return 0;
	}

	if (bsecs-bstart != 0)
		pr_notice("random test (BSM start/size in sector 0x%lx/0x%lx)\n", \
				bstart, bsecs-bstart);
	if (msecs-mstart != 0)
		pr_notice("random test (MMLS start/size in sector 0x%lx/0x%lx)\n", \
				mstart, msecs-mstart);

	b = hv_nstimeofday();

	/* write BSM and MMLS */
	tag = 0;
	i = rand_char()/2+1;
	while (i--)
	{
		if (bstart < bsecs) {
			secs = RANDOM_SECTORS;
			bsec = bstart+rand_short()*block_in_sector;
			src_buf = stress_src_buf;
			if (bsec > bsecs)
				bsec = bstart;
			else {
				src_offset = (bsec-bstart)*HV_BLOCK_SIZE;
				if (src_offset >= sizeof(stress_src_buf))
					src_offset = src_offset%sizeof(stress_src_buf);
				src_buf = stress_src_buf + src_offset;
			}
			if (bsec+secs >= bsecs) 
				secs = bsecs-bsec;			
			hv_log("random test (i 0x%lx bsec 0x%lx secs 0x%lx)\n", i,bsec,secs);
			for (j=0; j<secs; j=j+block_in_sector) {
				if (get_q()) {
					pr_notice("FAILURE: queue full\n");
					return -1;
				}
				spin_lock_irqsave(&lock, flags);
				if (get_ramdisk()) {
					hv_memcpy_cache_toio(bsm_io+(bsec+j)*HV_BLOCK_SIZE, src_buf, block_in_byte, async, stress_callback, tag);
					inc_q();
					tag ++;
				}
				else {
					if (!bsm_write_cache_command(tag, block_in_sector, (bsec+j), src_buf,
					  async, stress_callback)) {
						inc_q();
						tag ++;
					}
				}
				src_buf = src_buf + block_in_byte;
				if (((unsigned long)src_buf - (unsigned long)stress_src_buf) >= sizeof(stress_src_buf))
					src_buf = stress_src_buf;
				spin_unlock_irqrestore(&lock, flags);
				if (tag == get_queue_size())
					tag = 0;
			}

			secs = RANDOM_SECTORS;
			bsec = bstart+rand_short()*block_in_sector;
			src_buf = stress_src_buf;
			if (bsec > bsecs)
				bsec = bstart;
			else {
				src_offset = (bsec-bstart)*HV_BLOCK_SIZE;
				if (src_offset >= sizeof(stress_src_buf))
					src_offset = src_offset%sizeof(stress_src_buf);
				src_buf = stress_src_buf + src_offset;
			}
			if (bsec+secs >= bsecs) 
				secs = bsecs-bsec;
			hv_log("random test (i 0x%lx bsec 0x%lx secs 0x%lx)\n", i,bsec,secs);
			for (j=0; j<secs; j=j+block_in_sector) {
				if (get_q()) {
					pr_notice("FAILURE: queue full\n");
					return -1;
				}
				spin_lock_irqsave(&lock, flags);
				if (get_ramdisk()) {
					hv_memcpy_cache_fromio(dst_buf, bsm_io+(bsec+j)*HV_BLOCK_SIZE, block_in_byte, async, stress_callback, tag);
					inc_q();
					tag ++;
				}
				else {
					if (!bsm_read_cache_command(tag, block_in_sector, (bsec+j), dst_buf,
					  async, stress_callback)) {
						inc_q();
						tag ++;
					}
				}
				spin_unlock_irqrestore(&lock, flags);
				if (tag == get_queue_size())
					tag = 0;
			}
		}

		if (mstart < msecs) {
			secs = RANDOM_SECTORS;
			msec = mstart+rand_short()*block_in_sector;
			src_buf = stress_src_buf;
			if (msec > msecs)
				msec = mstart;
			else {
				src_offset = (msec-mstart)*HV_BLOCK_SIZE;
				if (src_offset >= sizeof(stress_src_buf))
					src_offset = src_offset%sizeof(stress_src_buf);
				src_buf = stress_src_buf + src_offset;
			}
			if (msec+secs >= msecs) 
				secs = msecs-msec;			
			hv_log("random test (i 0x%lx msec 0x%lx secs 0x%lx)\n", i,msec,secs);
			for (j=0; j<secs; j=j+block_in_sector) {
				if (get_q()) {
					pr_notice("FAILURE: queue full\n");
					return -1;
				}
				spin_lock_irqsave(&lock, flags);
				if (get_ramdisk()) {
					hv_memcpy_cache_toio(mmls_io+(msec+j)*HV_BLOCK_SIZE, src_buf, block_in_byte, async, stress_callback, tag);
					inc_q();
					tag ++;
				}
				else {
					if (!mmls_write_cache_command(tag, block_in_sector, (msec+j), (unsigned long)src_buf,
					  async, stress_callback)) {
						inc_q();
						tag ++;
					}
				}
				src_buf = src_buf + block_in_byte;
				if (((unsigned long)src_buf - (unsigned long)stress_src_buf) >= sizeof(stress_src_buf))
					src_buf = stress_src_buf;
				spin_unlock_irqrestore(&lock, flags);
				if (tag == get_queue_size())
					tag = 0;
			}

			secs = RANDOM_SECTORS;
			msec = mstart+rand_short()*block_in_sector;
			src_buf = stress_src_buf;
			if (msec > msecs)
				msec = mstart;
			else {
				src_offset = (msec-mstart)*HV_BLOCK_SIZE;
				if (src_offset >= sizeof(stress_src_buf))
					src_offset = src_offset%sizeof(stress_src_buf);
				src_buf = stress_src_buf + src_offset;
			}
			if (msec+secs >= msecs) 
				secs = msecs-msec;			
			hv_log("random test (i 0x%lx msec 0x%lx secs 0x%lx)\n", i,msec,secs);
			for (j=0; j<secs; j=j+block_in_sector) {
				if (get_q()) {
					pr_notice("FAILURE: queue full\n");
					return -1;
				}
				spin_lock_irqsave(&lock, flags);
				if (get_ramdisk()) {
					hv_memcpy_cache_fromio(dst_buf, mmls_io+(msec+j)*HV_BLOCK_SIZE, block_in_byte, async, stress_callback, tag);
					inc_q();
					tag ++;
				}
				else {
					if (!mmls_read_cache_command(tag, block_in_sector, (msec+j), (unsigned long)dst_buf,
					  async, stress_callback)) {
						inc_q();
						tag ++;
					}
				}
				spin_unlock_irqrestore(&lock, flags);
				if (tag == get_queue_size())
					tag = 0;
			}
		}
	}

	a = hv_nstimeofday();
	latency = a - b;
	hv_log("random test write latency is %ldns\n", latency);

	wait_for_queue_flushed();

	b = hv_nstimeofday();
	/* read back the data and verify */
	ret = read_test(bstart, bsecs, mstart, msecs, RANDOM_TEST);
	a = hv_nstimeofday();
	latency = a - b;
	hv_log("random test read latency is %ldns\n", latency);

	wait_for_queue_flushed();

	return ret;
}

#ifndef SIMULATION_TB
void hv_cmd_stress_test(void)
#else
void hv_cmd_stress_test(int type)
#endif
{
	struct HV_BSM_IO_t bsm;
	struct HV_MMLS_IO_t mmls;
	unsigned long int bsmsecs, bstart=0, bend;
	unsigned long int mmlssecs, mstart=0, mend;

	calc_block_size(stress_block_size);

	async = get_async_mode();

	pr_notice("--------------------------------------------------------\n");
	pr_notice("start stress test (block size %ld async mode %ld integrity %ld)\n", \
			(long)block_in_byte, (long)async, (long)stress_data_compare);

	get_bsm_iodata(&bsm);
	get_mmls_iodata(&mmls);
	bsm_io = bsm.b_iomem;		
	mmls_io = mmls.m_iomem;		
	bsmsecs = bsm.b_size/HV_BLOCK_SIZE;	// size of BSM in sectors
	mmlssecs = mmls.m_size/HV_BLOCK_SIZE;	// size of MMLS in sectors

	spin_lock_init(&lock);
	spin_lock_init(&cb_lock);

	bend = bsmsecs/block_in_sector;
	if (bsmsecs) {
		bstart = (rand_int()%bend)*block_in_sector;
		if (bstart > bsmsecs)
			bstart = 0;
		bend = bstart+bsm_stress_size;
		if (bend > bsmsecs)
			bend = bsmsecs;
	}
	mend = mmlssecs/block_in_sector;
	if (mmlssecs) {
		mstart = (rand_int()%mend)*block_in_sector;
		if (mstart > mmlssecs)
			mstart = 0;
		mend = mstart+mmls_stress_size;
		if (mend > mmlssecs)
			mend = mmlssecs;
	}	
#ifdef SIMULATION_TB
	if (type == GROUP_BSM)
		mend = mstart;
	else
		bend = bstart;
#endif

	init_stress_src_buf(0x5a);
	if (sequential_test(bstart, bend, mstart, mend))
		pr_notice("sequential test failed\n");
	else
		pr_notice("sequential test passed\n");
	memcpy(untouched_buf, stress_src_buf, sizeof(stress_src_buf));

	if (stress_type != SEQUENTIAL_TEST) {
		init_stress_src_buf(0xa5);
		if (random_test(bstart, bend, mstart, mend))
			pr_notice("random test failed\n");
		else
			pr_notice("random test passed\n");
	}
}


