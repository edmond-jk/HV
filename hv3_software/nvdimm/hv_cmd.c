/*
 *
 *  HVDIMM Command driver for BSM/MMLS.
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
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
//#include "hv_params.h"
#else
#include <stdio.h>
#include <string.h>
#ifdef USER_SPACE_CMD_DRIVER
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#endif
#endif	// SIMULATION_TB

//#include "hv_mmio.h"
#include "hv_cmd.h"

#if 1	// debug logging disabled
#undef pr_warn
#undef pr_err
#undef pr_debug
#undef pr_info
#define pr_warn(fmt, ...) do { /* nothing */ } while (0)
#define pr_err(fmt, ...) do { /* nothing */ } while (0)
#define pr_debug(fmt, ...) do { /* nothing */ } while (0)
#define pr_info(fmt, ...) do { /* nothing */ } while (0)
#else
	#ifdef SIMULATION_TB
	#define pr_warn printf
	#define pr_err printf
	#define pr_debug printf
	#define pr_info printf
	#endif
#endif

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

/* remaining FPGA buffer before wrap-around for BSM or MMLS 1-way interleaving */
#define remain_sz(index)	((DATA_BUFFER_NUM-index)*DATA_BUFFER_SIZE)

static unsigned char fake_mmls_buf[4096*16]; 	/* 64 KB */
static unsigned char cmd_burst_buf[CMD_BUFFER_SIZE*4];

/* force group ID to 0 since we don't support multiple group */
static unsigned int gid=0;

/* cache flush control */
#ifndef SIMULATION_TB
#ifndef REV_B_MM
static unsigned int cmd_status_use_cache = 0;
static unsigned int bsm_read_use_cache = 1;
static unsigned int bsm_write_use_cache = 0;
#endif
static unsigned int fake_read_cache_flush = 0;
static unsigned int fake_write_cache_flush = 0;
#else
/* do not use cache line since clflush_cache_range() is not supported
   on simulation bench or in user space */
#ifndef REV_B_MM
static unsigned int cmd_status_use_cache = 0;
static unsigned int bsm_read_use_cache = 0;
static unsigned int bsm_write_use_cache = 0;
#endif
static unsigned int fake_read_cache_flush = 0;
static unsigned int fake_write_cache_flush = 0;
#endif
void
hv_mcpy(void *dst, void *src, long size)
{
	pr_mmio("    hv_mcpy: dst 0x%lx src 0x%lx size %ld\n", (unsigned long)dst, (unsigned long)src, size);
	memcpy(dst, src, size);
}

/* comment out these lines on target HW */
#define SW_SIM
#define STS_TST

#define READ_TIMEOUT 1000000 /* 1 ms */
#define MAX_TRY 10
#define INITIAL_DELAY 1 // usec

static int cmd_status[16];
static unsigned short query_tag = 128;  // query_tag: 128-255

#ifdef SIMULATION_TB
#define pr_notice printf
struct hrtimer {
	unsigned long function;
};
typedef int hrtimer_restart;
#define enum
#ifndef container_of
	#define container_of(ptr, type, member) \
	 ((type *)                              \
	   (  ((char *)(ptr))                   \
	    - ((char *)(&((type*)0)->member)) ))
#endif
#define HRTIMER_NORESTART	1
#define virt_to_phys(x)		x
#endif

struct hv_tm_data_t {
	struct hrtimer timer;
	unsigned short qtag;
};			/* my timer data */

static struct hv_data_t {
	unsigned short tag;
	unsigned int lba;
	unsigned int sector;
	unsigned long buffer_cb;
	void *cb_func;
	unsigned short active;
	unsigned short count;
	struct hv_tm_data_t mt_data;
} mq_data[QUEUE_DEPTH] = {{0}};		/* my queue data */

struct hv_group_tbl hv_group[MAX_HV_GROUP];

/* MMIO region address relative to v_mmio */
#define WRITE_STATUS_OFF	(hv_group[0].mem[0].v_mmio+0x00000000)
#define READ_STATUS_OFF		(hv_group[0].mem[0].v_mmio+0x00004000)
#define QUERY_STATUS_OFF	(hv_group[0].mem[0].v_mmio+0x00008000)
#define CMD_OFF				(hv_group[0].mem[0].v_mmio+0x00010000)
#define TERM_OFF			(hv_group[0].mem[0].v_mmio+0x00014000)
#define BCOM_OFF			(hv_group[0].mem[0].v_mmio+0x00018000)
#define ECC_OFF				(hv_group[0].mem[0].v_mmio+0x00020000)

#define MMLS_DRAM_OFF		(hv_group[0].mem[0].v_dram+0x00000000)
#define BSM_DRAM_OFF		(hv_group[0].mem[0].v_dram+HV_MMLS_DRAM_SIZE/2)

/*for HRtimer callback functions */
static enum hrtimer_restart bsm_r_callback(struct hrtimer *my_timer);
static enum hrtimer_restart bsm_w_callback(struct hrtimer *my_timer);
static enum hrtimer_restart mmls_r_callback(struct hrtimer *my_timer);
static enum hrtimer_restart mmls_w_callback(struct hrtimer *my_timer);

static char cmd_in_q = 0x0;
#ifndef SIMULATION_TB
static spinlock_t cmd_in_q_lock;
#else
static unsigned int cmd_enq_index=0;
static unsigned int cmd_deq_index=0;
#ifndef USER_SPACE_CMD_DRIVER
static unsigned int cmd_in_q_lock;
static unsigned long flags;
static void spin_lock_init(unsigned int *lock) {}
static spin_lock_irqsave(unsigned int *lock, unsigned long flags) {}
static spin_unlock_irqrestore(unsigned int *lock, unsigned long flags) {}
#else
static pthread_spinlock_t cmd_in_q_lock;
static unsigned long flags;
static void spin_lock_init(pthread_spinlock_t *lock)
{
	pthread_spin_init(lock, PTHREAD_PROCESS_PRIVATE);
}

static spin_lock_irqsave(pthread_spinlock_t *lock, unsigned long flags)
{
	pthread_spin_lock(lock);
}

static spin_unlock_irqrestore(pthread_spinlock_t *lock, unsigned long flags)
{
	pthread_spin_unlock(lock);
}
#endif	// USER_SPACE_CMD_DRIVER

static void usleep_range(unsigned long  min , unsigned long  max)
{
	usleep(min);
}

static void udelay(unsigned long delay)
{
	usleep(delay);
}
#endif

static int qry_sync_cnt[QUEUE_DEPTH] = {0};
static int qry_seq_num = 0;
static int rd_4k_idx_b = 0;  /* 4k-buffer index for bsm read: 0, 1,2,3 */
static int wr_4k_idx_b = 0;  /* 4k-buffer index for bsm write: 0, 1,2,3 */
static int rd_4k_idx_m = 0;  /* 4k-buffer index for mmls read: 0, 1,2,3 */
static int wr_4k_idx_m = 0;  /* 4k-buffer index for mmls write: 0, 1,2,3 */

/* local copy of hv command */
static unsigned char hv_cmd_local_buffer[CMD_BUFFER_SIZE];

/* use this buffers temporarily until the addresses is defined by HW engr */
static char hv_status_buffer[STATUS_BUFFER_SIZE] = {0x41};

static int wait_for_cmd_done(unsigned int tag, int type, unsigned int lba);
static int get_wr_buf_size(int type, unsigned int lba);
static int get_rd_buf_size(int type, unsigned int lba);

static int hv_read(int type, int more_data, int rd_buf_size, unsigned int lba, unsigned char **buf, int ways, int *index);
static int hv_write(int type, int more_data, int wr_buf_size, unsigned int lba, unsigned char **buf, int ways, int *index);

static int hv_cmdq_cur_tag;		/* current tag */
static int queue_size = 64;
static int get_queue_size(void) { return queue_size; }
int hv_next_cmdq_tag(void)
{
	int tag;

	tag = hv_cmdq_cur_tag;
	hv_cmdq_cur_tag++;

	if (hv_cmdq_cur_tag == get_queue_size())
		hv_cmdq_cur_tag = 0;

	return tag;
}

static int interleave_ways (int gid)
{
	return 1;
}

static void hv_write_cmd (int gid, int type, long lba, void *cmd, void *addr)
{
	pr_mmio("%s: entered cmd off 0x%lx\n", __func__, (unsigned long) addr);
	if(!cmd)
		cmd = cmd_burst_buf;
	// copy data to the memory location
	hv_mcpy(addr, cmd, CMD_BUFFER_SIZE);
	// fake read to write data to FPGA
	clflush_cache_range(addr, CMD_BUFFER_SIZE);
	hv_mcpy(fake_mmls_buf, addr, CMD_BUFFER_SIZE);

}

#ifndef MMLS_16K_ALIGNMENT
#define mmls_dram_off(index)	(MMLS_DRAM_OFF+index*DATA_BUFFER_SIZE)
#define bsm_dram_off(index)		(BSM_DRAM_OFF+index*DATA_BUFFER_SIZE)
#else
#define mmls_dram_off(index)	(MMLS_DRAM_OFF+index*MMLS_ALIGNMENT_SIZE)
#define bsm_dram_off(index)		(BSM_DRAM_OFF+index*MMLS_ALIGNMENT_SIZE)
#endif

static void hv_write_bsm_data (int gid, long lba, int index, void *data, long size)
{

	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
#if defined (RDIMM_POPULATED) || defined (MMLS_16K_ALIGNMENT)
	hv_mcpy(bsm_dram_off(index), data, size);
	if (fake_read_cache_flush)
		clflush_cache_range(bsm_dram_off(index), size);
	hv_mcpy(fake_mmls_buf, bsm_dram_off(index), size); 	/* fake read */
#else
	if (fake_read_cache_flush)
		clflush_cache_range(data, size);
	hv_mcpy(fake_mmls_buf, data, size); 			/* fake read */
#endif

}

static void hv_read_bsm_data (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
#if defined (RDIMM_POPULATED) || defined (MMLS_16K_ALIGNMENT)
	hv_mcpy(bsm_dram_off(index), fake_mmls_buf, size); 	/* fake write */
	if (fake_write_cache_flush)
		clflush_cache_range(bsm_dram_off(index), size);
	hv_mcpy((void *)data, bsm_dram_off(index), size);
#else
	hv_mcpy(data, fake_mmls_buf, size); 			/* fake write */
	if (fake_write_cache_flush)
		clflush_cache_range(data, size);
#endif

}

static void hv_mmls_fake_read (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
#if defined (RDIMM_POPULATED) || defined (MMLS_16K_ALIGNMENT)
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

static void hv_mmls_fake_write (int gid, long lba, int index, void *data, long size)
{
	pr_mmio("%s: entered index=%d size=%ld\n", __func__, index, size);
#if defined (RDIMM_POPULATED) || defined (MMLS_16K_ALIGNMENT)
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

static unsigned char hv_read_status (int gid, int type, long lba, void *addr)
{
	unsigned char q_status;

	pr_mmio("%s: entered cmd off 0x%lx\n", __func__, (unsigned long) addr);
	// fake write
	hv_mcpy(addr, fake_mmls_buf, MEM_BURST_SIZE);
	clflush_cache_range(addr, MEM_BURST_SIZE);
	//read status data
	hv_mcpy(&q_status, addr, sizeof(unsigned char));
	return q_status;
}

static unsigned long hv_get_dram_addr (int type, void *addr)
{

	/* locate physical address for mmls-write command  for singel DIMM */
#if defined (RDIMM_POPULATED) || defined (MMLS_16K_ALIGNMENT)
	if (type == GROUP_BSM)
		return (unsigned long) (hv_group[0].mem[0].p_dram+HV_MMLS_DRAM_SIZE/2);
	else
		return (unsigned long) (hv_group[0].mem[0].p_dram);
#else
		return (unsigned long)virt_to_phys(addr);
#endif

}

static long bsm_start(void) { return 0; }

static long bsm_size(void) { return hv_group[gid].bsm_size*HV_BLOCK_SIZE;  }

static long mmls_start(void) { return bsm_start()+bsm_size(); }

static long mmls_size(void) { return hv_group[gid].mmls_size*HV_BLOCK_SIZE;  }

static long mmls_cdev(void) { return hv_group[gid].mmls_device==DEV_CHAR ? 1 : 0; }

static long bsm_cdev(void) { return hv_group[gid].bsm_device==DEV_CHAR ? 1 : 0; }

#ifndef SIMULATION_TB
unsigned long hv_nstimeofday(void)
{
	struct timespec ts;

	getnstimeofday(&ts);
	return(timespec_to_ns(&ts));
}
#else
#ifdef USER_SPACE_CMD_DRIVER
unsigned long hv_nstimeofday()
{
	struct timeval tv;
	struct timezone tz;

	gettimeofday(&tv, &tz);
	return (tv.tv_sec*1000000000+tv.tv_usec*1000);
}
#else
unsigned long hv_nstimeofday()
{
	// to be implemented for simulation TB
}
#endif	// USER_SPACE_CMD_DRIVER
#endif	// SIMULATION_TB

static void clear_cmd_status(void)
{
	struct HV_INQUIRY_STATUS_t	*pStatus;

	pStatus = (struct HV_INQUIRY_STATUS_t *)hv_status_buffer;
	/* do not clear, pending HW */
	/* pStatus->cmd_status = 0x0; */
#ifndef RAMDISK
	/* clear cmd status in MMIO */
	/* memcpy_toio(bsm_iomem+HV_STATUS_OFFSET, &clear_byte, 1); */
#endif
}

static int wait_for_cmd_status(int *status, int cnt)
{
	int j;
	struct HV_INQUIRY_STATUS_t	*pStatus;

	pStatus = (struct HV_INQUIRY_STATUS_t *)hv_status_buffer;
	while (1) {
		for (j = 0; j < cnt; j++) {
			if ((pStatus->cmd_status & status[j]) == status[j]) {
				return pStatus->cmd_status;
			}
		}
		/* wait... */
	}

	return 0;
}

/* request a free q entry */
static int request_q(void)
{
	int i;
	unsigned long cmdq_flags;

	spin_lock_irqsave(&cmd_in_q_lock, cmdq_flags);

	while (cmd_in_q > (QUEUE_DEPTH-1)){
		spin_unlock_irqrestore(&cmd_in_q_lock, cmdq_flags);
		pr_err("***overflow in queue buffer!!!\n");
		usleep_range(63,125);
		spin_lock_irqsave(&cmd_in_q_lock, cmdq_flags);
	}

#ifndef SIMULATION_TB
	for (i=0;i<QUEUE_DEPTH;i++)
		if (!mq_data[i].active) {
			mq_data[i].active = 1;
			cmd_in_q++;
			break;
		}

	spin_unlock_irqrestore(&cmd_in_q_lock, cmdq_flags);
	return i;
#else
	cmd_enq_index ++;
	if (cmd_enq_index == QUEUE_DEPTH)
		cmd_enq_index = 0;
	mq_data[cmd_enq_index].active = 1;
	cmd_in_q++;
	spin_unlock_irqrestore(&cmd_in_q_lock, cmdq_flags);
	return cmd_enq_index;
#endif
}

/* release an used q entry */
static void release_q(int q_index)
{
	unsigned long cmdq_flags;

	spin_lock_irqsave(&cmd_in_q_lock, cmdq_flags);
	cmd_in_q--;
	mq_data[q_index].active = 0;
	spin_unlock_irqrestore(&cmd_in_q_lock, cmdq_flags);
}

#ifdef SIMULATION_TB
static int get_cmd_deq_index()
{
	cmd_deq_index ++;
	if (cmd_deq_index == QUEUE_DEPTH)
		cmd_deq_index = 0;
	return cmd_deq_index;
}
#endif

/* get estimated time to completion */
static unsigned long get_estimated_TTC(unsigned int tag, int type, unsigned int lba)
{
	unsigned long estimated_time;
	unsigned char query_status;

	bsm_query_command(query_tag, tag, type, lba);
	query_tag = (query_tag+1)%128 + 128;
	query_status = hv_query_status (0, type, lba, CMD_DONE);

	pr_debug("estimated_TTC: query_status=%x\n", query_status);

	if ((query_status & 0x0C) != 0x0C){

		estimated_time = (query_status & 0xF0) >> 4;
		estimated_time = ( estimated_time + 1) * 12500;

		pr_debug("estimated_TTC: estimated_time=%ld\n", estimated_time);
#ifndef SW_SIM
		return 	estimated_time;
#else
		return 	12500;
#endif
	}
	else
		return 0;
}

#ifndef SIMULATION_TB
/* start the timer */
static void hv_start_timer(struct hrtimer *my_timer, int init, unsigned long estimated_time, void *tm_callback)
{
	if (init)
		hrtimer_init(my_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	my_timer->function = tm_callback;
	hrtimer_start(my_timer, ktime_set(0, estimated_time), HRTIMER_MODE_REL);
}

/* restart the timer */
static enum hrtimer_restart hv_restart_timer(struct hrtimer *my_timer, unsigned long estimated_time)
{
	hrtimer_forward(my_timer, ktime_get(), ktime_set(0, estimated_time));
	return HRTIMER_RESTART;
}
#else
#ifdef USER_SPACE_CMD_DRIVER
static struct sigaction act;
/* start the timer */
static void hv_start_timer(struct hrtimer *my_timer, int init, unsigned long estimated_time, void *tm_callback)
{
	act.sa_handler = tm_callback;
	sigaction (SIGALRM, & act, 0);
	ualarm (estimated_time/1000, 0);
}

/* restart the timer */
static enum hrtimer_restart hv_restart_timer(struct hrtimer *my_timer, unsigned long estimated_time)
{
	sigaction (SIGALRM, & act, 0);
	ualarm (estimated_time/1000, 0);
}
#else
/* start the timer */
static void hv_start_timer(struct hrtimer *my_timer, int init, unsigned long estimated_time, void *tm_callback)
{
	// to be implemented for simulation TB
}

/* restart the timer */
static enum hrtimer_restart hv_restart_timer(struct hrtimer *my_timer, unsigned long estimated_time)
{
	// to be implemented for simulation TB
}
#endif	// USER_SPACE_CMD_DRIVER
#endif	// SIMULATION_TB

/* configure q data and start timer */
static void hv_cmd_enqueue(int q_index, unsigned int tag, unsigned int sector,
                   unsigned int lba, unsigned char *buf, void *callback,
		   void *tm_callback, unsigned long estimated_time)
{
	/* save command data into queue array */
	mq_data[q_index].tag = tag;
	mq_data[q_index].lba = lba;
	mq_data[q_index].sector = sector;
	mq_data[q_index].buffer_cb = (unsigned long)buf;
	mq_data[q_index].cb_func = callback;
	mq_data[q_index].mt_data.qtag = q_index;
	mq_data[q_index].count = 0;

	/* start the timer */
	hv_start_timer(&mq_data[q_index].mt_data.timer,
			(mq_data[q_index].mt_data.timer.function == 0), estimated_time, tm_callback);
}

/* read data from MMIO driver */
static void hv_read_data(int type, unsigned int sector, unsigned int lba, unsigned char *buf)
{
	int rd_buf_size=0;
	int more_data;

	more_data = (sector * HV_BLOCK_SIZE)/DATA_BUFFER_SIZE;
	while (more_data > 0){
		/* get available buffer size from status */
		rd_buf_size = get_rd_buf_size(type, lba);

		/* read data from MMIO buffer */
		if (type == GROUP_BSM)
			more_data = hv_read(type, more_data, rd_buf_size, lba, &buf, interleave_ways(0), &rd_4k_idx_b);
		else
			more_data = hv_read(type, more_data, rd_buf_size, lba, &buf, interleave_ways(0), &rd_4k_idx_m);

		rd_buf_size = 0;
	}
}

/* write data to MMIO driver */
static void hv_write_data(int type, unsigned int sector, unsigned int lba, unsigned char *buf)
{
	int wr_buf_size;
	int more_data;

	more_data = (sector * HV_BLOCK_SIZE)/DATA_BUFFER_SIZE;
	wr_buf_size = get_wr_buf_size(type, lba);

	while (more_data > 0){
		if (type == GROUP_BSM)
			more_data = hv_write(type, more_data, wr_buf_size, lba, &buf, interleave_ways(0), &wr_4k_idx_b);
		else
			more_data = hv_write(type, more_data, wr_buf_size, lba, &buf, interleave_ways(0), &wr_4k_idx_m);

		if (more_data==0)
			break;

		wr_buf_size = get_wr_buf_size(type, lba);
	}
}

int bsm_read_command(   unsigned int tag,
			unsigned long len,
			unsigned long long dpa,
			unsigned char *buf,
			unsigned char async,
			void *callback_func)
{
	struct HV_CMD_BSM_t *pBsm;
	unsigned int sector = len/HV_BLOCK_SIZE; // convert len to num of sector for hv api
	unsigned int lba = 0; // convert dpa to lba for hv api

	pr_warn("*** bsm_read_command: tag=%d %d %d\n", tag, sector, lba );	//q-debug

	/* arrange command structure */
	pBsm = (struct HV_CMD_BSM_t *) hv_cmd_local_buffer;
	pBsm->cmd = BSM_READ;
	*(unsigned short *)&pBsm->tag = tag;
	*(unsigned int *)&pBsm->sector = sector;
	*(unsigned int *)&pBsm->lba = lba+bsm_start()/HV_BLOCK_SIZE;
	*(unsigned char *)&pBsm->more_data = 0x0;

#ifdef REV_B_MM
	*(unsigned long *)&pBsm->mm_addr = hv_get_dram_addr(GROUP_BSM, (void*)buf);
#endif
	/* prepare and send bsm read command */
	hv_write_command(0, GROUP_BSM, lba, hv_cmd_local_buffer);

	if (async == 0) {		/* synchronous mode */
		if (!wait_for_cmd_done(tag, GROUP_BSM, lba))
			/* read data from hv */
			hv_read_data(GROUP_BSM, sector, lba, buf);
		else {
			pr_warn("*** not get the BSM read CMD_DONE yet!!!\n");
			return (-1);
		}

		/* send termination cmd to specified addr */
		hv_write_termination (0, GROUP_BSM, lba, NULL);
	} else if (async == 1) {
		unsigned long estimated_time;
		int err=0;
		void (*my_cb_func)(unsigned short, int err);

		udelay(INITIAL_DELAY); // Initial delay

		estimated_time = get_estimated_TTC(tag,GROUP_BSM,lba);
		if ( estimated_time > 0 ) {
			/* request a free q entry */
			hv_cmd_enqueue(request_q(), tag, sector, lba, buf,
					callback_func, bsm_r_callback, estimated_time);
		}
		else {
			/* read data from hv */
			hv_read_data(GROUP_BSM, sector, lba, buf);

			/* send termination cmd to specified addr */
			hv_write_termination (0, GROUP_BSM, lba, NULL);

			/* move to the driver callback function*/
			my_cb_func = callback_func;
			if (my_cb_func && !err) {
				(*my_cb_func)(tag, 1);
				return 1;	// no error and not queued by CMD driver */
			}
		}
	} else {
		pr_err("asynchrounous mode was assigned with wrong value\n");
		return (-1);
	}

	return 0;
}

static enum hrtimer_restart bsm_r_callback(struct hrtimer *my_timer)
{

	struct hv_tm_data_t *my_tm_data;
	unsigned char *buf;
	unsigned short curr_tag;
	unsigned short curr_lba;
	unsigned short curr_sector;
	unsigned short curr_q_out_idx;
	unsigned long estimated_time;
	int err=0;
	void (*my_cb_func)(unsigned short, int err);

	pr_debug("## %s\n", __func__);	//q-debug

	my_tm_data = container_of(my_timer, struct hv_tm_data_t, timer);

#ifndef SIMULATION_TB
	curr_q_out_idx = my_tm_data->qtag;
#else
	curr_q_out_idx = get_cmd_deq_index();
#endif
	buf = (char *)mq_data[curr_q_out_idx].buffer_cb;
	curr_tag = mq_data[curr_q_out_idx].tag;
	curr_lba = mq_data[curr_q_out_idx].lba;
	curr_sector = mq_data[curr_q_out_idx].sector;

	if (mq_data[curr_q_out_idx].count < MAX_TRY) {
		estimated_time = get_estimated_TTC(curr_tag,GROUP_BSM,curr_lba);
#ifndef SW_SIM
		if (estimated_time > 0 ) {
#else
		if (0 ) {
#endif		
			/* schedule the next timer for callback*/
			mq_data[curr_q_out_idx].count++;
			return hv_restart_timer(my_timer, estimated_time);
		}
		else
			/* read data from hv */
			hv_read_data(GROUP_BSM, curr_sector, curr_lba, buf);
	}
	else 
		err = -1;

	/* send termination cmd to specified addr */
	hv_write_termination (0, GROUP_BSM, curr_lba, NULL);

	/* move to the driver callback function*/
	my_cb_func = mq_data[curr_q_out_idx].cb_func;
	if (my_cb_func)
		(*my_cb_func)(curr_tag, err);

	/* release q entry */
	release_q(curr_q_out_idx);

	return HRTIMER_NORESTART;
}

int bsm_write_command(  unsigned int tag,
			unsigned long len,
			unsigned long long dpa,
			unsigned char *buf,
			unsigned char async,
			void *callback_func)
{

	struct HV_CMD_BSM_t *pBsm;
	unsigned int sector = len/HV_BLOCK_SIZE; // convert len to num of sector for hv api
	unsigned int lba = 0; // convert dpa to lba for hv api
	pr_warn("*** bsm_write_command: tag=%d %d %d\n", tag, sector, lba); //q-debug

	/* arrange command structure */
	pBsm = (struct HV_CMD_BSM_t *) hv_cmd_local_buffer;
	pBsm->cmd = BSM_WRITE;
	*(unsigned short *)&pBsm->tag = tag;
	*(unsigned int *)&pBsm->sector = sector;
	*(unsigned int *)&pBsm->lba = lba+bsm_start()/HV_BLOCK_SIZE;
	*(unsigned char *)&pBsm->more_data = 0x0;

#ifdef REV_B_MM
	*(unsigned long *)&pBsm->mm_addr = hv_get_dram_addr(GROUP_BSM, (void*)buf);
#endif	
	/* prepare and send bsm write command */
	hv_write_command(0, GROUP_BSM, lba, hv_cmd_local_buffer);

	/* write data to hv */
	hv_write_data(GROUP_BSM, sector, lba, buf);

	/* send termination cmd to specified addr */
	hv_write_termination(0, GROUP_BSM, lba, NULL);

	if (async == 0) {		/* synchronous mode */
		if (wait_for_cmd_done(tag, GROUP_BSM, lba)) {
			pr_warn("*** not get the BSM write CMD_DONE yet!!!\n");
			return (-1);
		}
	} else if (async == 1) {
		unsigned long estimated_time;
		void (*my_cb_func)(unsigned short, int err);

		udelay(INITIAL_DELAY); // Initial delay

		estimated_time = get_estimated_TTC(tag,GROUP_BSM,lba);
		if ( estimated_time > 0 ) {
			/* request a free q entry */
			hv_cmd_enqueue(request_q(), tag, sector, lba, buf,
					callback_func, bsm_w_callback, estimated_time);
		}
		else {
			/* move to the driver callback function*/
			my_cb_func = callback_func;
			if (my_cb_func) {
				(*my_cb_func)(tag, 1);
				return 1;	/* no error and not queued by CMD driver */
			}
		}
	} else {
		pr_err("asynchrounous mode was assigned with wrong value\n");
		return (-1);		/* prepare HRtimer for callback later */
	}
	return 0;
}

static enum hrtimer_restart bsm_w_callback(struct hrtimer *my_timer)
{
	struct hv_tm_data_t *my_tm_data;
	unsigned char *buf;
	unsigned short curr_tag;
	unsigned short curr_lba;
	unsigned short curr_q_out_idx;
	unsigned long estimated_time;

	void (*my_cb_func)(unsigned short, int err);

	pr_debug("## %s, \n", __func__);	//q-debug

	my_tm_data = container_of(my_timer, struct hv_tm_data_t, timer);

#ifndef SIMULATION_TB
	curr_q_out_idx = my_tm_data->qtag;
#else
	curr_q_out_idx = get_cmd_deq_index();
#endif
	buf = (void *)mq_data[curr_q_out_idx].buffer_cb;
	curr_tag = mq_data[curr_q_out_idx].tag;
	curr_lba = mq_data[curr_q_out_idx].lba;

	if (mq_data[curr_q_out_idx].count < MAX_TRY) {
		estimated_time = get_estimated_TTC(curr_tag,GROUP_BSM,curr_lba);
#ifndef SW_SIM
		if (estimated_time > 0 ) {
#else
		if (0 ) {
#endif
			/* schedule the next timer for callback*/
			mq_data[curr_q_out_idx].count++;
			return hv_restart_timer(my_timer, estimated_time);
		}
	}

	/* move to the driver callback function*/
	my_cb_func = mq_data[curr_q_out_idx].cb_func;
	if (my_cb_func)
		(*my_cb_func)(curr_tag, 0);

	/* release q entry */
	release_q(curr_q_out_idx);

	return HRTIMER_NORESTART;
}

int mmls_read_command(  unsigned int tag,
			unsigned int sector,
			unsigned int lba,
			unsigned long mm_addr,
			unsigned char async,
			void *callback_func)
{
	struct HV_CMD_MMLS_t *pMMLS;

	pr_debug("*** %s: tag=%d, %x\n", __func__, tag, lba);	//q-debug

	/* arrange command structure */
	pMMLS = (struct HV_CMD_MMLS_t *) hv_cmd_local_buffer;
	pMMLS->cmd = MMLS_READ;
	*(unsigned short *)&pMMLS->tag = tag;
	*(unsigned int *)&pMMLS->sector = sector;
	*(unsigned int *)&pMMLS->lba = lba+mmls_start()/HV_BLOCK_SIZE;
	*(unsigned char *)&pMMLS->more_data = 0x0;

	/* locate physical address for mmls-write command */
#ifdef REV_B_MM
	*(unsigned long *)&pMMLS->mm_addr = hv_get_dram_addr(GROUP_MMLS, (void*)mm_addr);
#else
	*(unsigned long *)&pMMLS->mm_addr = (unsigned long)virt_to_phys((void *)mm_addr);
#endif

	/* prepare and send bsm read command */
	hv_write_command(0, GROUP_MMLS, lba, hv_cmd_local_buffer);

	if (async == 0) {		/* synchronous mode */
		if (!wait_for_cmd_done(tag, GROUP_MMLS, lba))
			hv_read_data(GROUP_MMLS, sector, lba, (unsigned char *)mm_addr);
		else {
			pr_warn("*** not get the MMLS read CMD_DONE yet!!!\n");
			return (-1);
		}

		/* send termination cmd to specified addr */
		hv_write_termination (0, GROUP_MMLS, lba, NULL);
	} else if (async == 1) {		/* async mode */
		unsigned long estimated_time;
		int err=0;
		void (*my_cb_func)(unsigned short, int err);

		udelay(INITIAL_DELAY); // Initial delay

		estimated_time = get_estimated_TTC(tag,GROUP_MMLS,lba);
		if ( estimated_time > 0 ) {
			/* request a free q entry */
			hv_cmd_enqueue(request_q(), tag, sector, lba, (void *)mm_addr, 
					callback_func, mmls_r_callback, estimated_time);
		}
		else{
			/* read data from hv */
			hv_read_data(GROUP_MMLS, sector, lba, (unsigned char *)mm_addr);
			
			/* send termination cmd to specified addr */
			hv_write_termination (0, GROUP_MMLS, lba, NULL);

			/* move to the driver callback function*/
			my_cb_func = callback_func;
			if (my_cb_func && !err) {
				(*my_cb_func)(tag, 1);
				return 1;	/* no error and not queued by CMD driver */
			}
		}
	} else {
		pr_err("MMLS-READ async mode was assigned with wrong value\n");
		return (-1);
	}

	return 0;
}

static enum hrtimer_restart mmls_r_callback(struct hrtimer *my_timer)
{
	struct hv_tm_data_t *my_tm_data;
	void *mm_addr;
	unsigned short curr_lba;
	unsigned short curr_sector;
	unsigned short curr_q_out_idx, curr_tag;
	unsigned long estimated_time;
	int err=0;
	void (*my_cb_func)(unsigned short, int err);

	pr_debug("## %s \n", __func__);	//q-debug

	my_tm_data = container_of(my_timer, struct hv_tm_data_t, timer);
#ifndef SIMULATION_TB
	curr_q_out_idx = my_tm_data->qtag;
#else
	curr_q_out_idx = get_cmd_deq_index();
#endif

	mm_addr = (void *)mq_data[curr_q_out_idx].buffer_cb;
	curr_tag = mq_data[curr_q_out_idx].tag;
	curr_lba = mq_data[curr_q_out_idx].lba;
	curr_sector = mq_data[curr_q_out_idx].sector;

	if (mq_data[curr_q_out_idx].count < MAX_TRY) {
		estimated_time = get_estimated_TTC(curr_tag, GROUP_MMLS, curr_lba);
#ifndef SW_SIM
		if (estimated_time > 0 ) {
#else
		if (0 ) {
#endif		
			/* schedule the next timer for callback*/
			mq_data[curr_q_out_idx].count++;
			return hv_restart_timer(my_timer, estimated_time);
		}
		else
			/* read data from hv */
			hv_read_data(GROUP_MMLS, curr_sector, curr_lba, (unsigned char *)mm_addr);
	}
	else 
		err = -1;

	/* send termination cmd to specified addr */
	hv_write_termination (0, GROUP_MMLS, curr_lba, NULL);

	/* move to the driver callback function*/
	my_cb_func = mq_data[curr_q_out_idx].cb_func;
	if (my_cb_func)
		(*my_cb_func)(curr_tag, err);
	
	/* release q entry */
	release_q(curr_q_out_idx);
	
	return HRTIMER_NORESTART;
}

int mmls_write_command( unsigned int tag,
			unsigned int sector,
			unsigned int lba,
			unsigned long mm_addr,
			unsigned char async,
			void *callback_func)
{
	struct HV_CMD_MMLS_t *pMMLS;

	pr_warn("** mmls_write_command: tag=%d %d %d\n", tag, sector, lba);	//q-debug

	/* arrange command structure */
	pMMLS = (struct HV_CMD_MMLS_t *) hv_cmd_local_buffer;
	pMMLS->cmd = MMLS_WRITE;
	*(unsigned short *)&pMMLS->tag = tag;
	*(unsigned int *)&pMMLS->sector = sector;
	*(unsigned int *)&pMMLS->lba = lba+mmls_start()/HV_BLOCK_SIZE;
	*(unsigned char *)&pMMLS->more_data = 0x0;

	/* locate physical address for mmls-write command, DDR3 platform */
#ifdef REV_B_MM
	*(unsigned long *)&pMMLS->mm_addr = hv_get_dram_addr(GROUP_MMLS, (void*)mm_addr);
#else
	*(unsigned long *)&pMMLS->mm_addr = (unsigned long)virt_to_phys((void *)mm_addr);
#endif
	/* prepare and send command */
	hv_write_command(0, GROUP_MMLS, lba, hv_cmd_local_buffer);

	/* write data to hv */
	hv_write_data(GROUP_MMLS, sector, lba, (unsigned char *)mm_addr);

	/* send termination cmd to specified addr */
	hv_write_termination(0, GROUP_MMLS, lba, NULL);

	if (async == 0) {		/* synchronous mode */
		if (wait_for_cmd_done(tag, GROUP_MMLS, lba)) {
			pr_warn("*** not get the MMLS write CMD_DONE yet!!!\n");
			return (-1);
		}
	} else if (async == 1) {
		unsigned long estimated_time;
		void (*my_cb_func)(unsigned short, int err);

		udelay(INITIAL_DELAY); // Initial delay 1 usec

		estimated_time = get_estimated_TTC(tag,GROUP_MMLS,lba);
		if ( estimated_time > 0 ) {
			/* request a free q entry */
			hv_cmd_enqueue(request_q(), tag, sector, lba, (void *)mm_addr,
					callback_func, mmls_w_callback, estimated_time);
		}
		else {
			/* move to the driver callback function*/
			my_cb_func = callback_func;
			if (my_cb_func) {
				(*my_cb_func)(tag, 1);
				return 1;	/* no error and not queued by CMD driver */
			}
		}
	} else {
		pr_err("asynchrounous mode was assigned with wrong value\n");
		return (-1);		/* prepare HRtimer for callback later */
	}

	return 0;
}

static enum hrtimer_restart mmls_w_callback(struct hrtimer *my_timer)
{
	struct hv_tm_data_t *my_tm_data;
	unsigned char *buf;
	unsigned short curr_tag;
	unsigned short curr_lba;
	unsigned short curr_q_out_idx;
	unsigned long estimated_time;

	void (*my_cb_func)(unsigned short, int err);

	pr_debug("## %s, \n", __func__); //q-debug

	my_tm_data = container_of(my_timer, struct hv_tm_data_t, timer);

#ifndef SIMULATION_TB
	curr_q_out_idx = my_tm_data->qtag;
#else
	curr_q_out_idx = get_cmd_deq_index();
#endif
	buf = (void *)mq_data[curr_q_out_idx].buffer_cb;
	curr_tag = mq_data[curr_q_out_idx].tag;
	curr_lba = mq_data[curr_q_out_idx].lba;

	if (mq_data[curr_q_out_idx].count < MAX_TRY) {
		estimated_time = get_estimated_TTC(curr_tag, GROUP_MMLS, curr_lba);
#ifndef SW_SIM
		if (estimated_time > 0 ) {
#else
		if (0 ) {
#endif
			/* schedule the next timer for callback*/
			mq_data[curr_q_out_idx].count++;
			return hv_restart_timer(my_timer, estimated_time);
		}
	}
	/* move to the driver callback function*/
	my_cb_func = mq_data[curr_q_out_idx].cb_func;
	if (my_cb_func)
		(*my_cb_func)(curr_tag, 0);

	/* release q entry */
	release_q(curr_q_out_idx);

	return HRTIMER_NORESTART;
}

static int reset_command(unsigned int tag)
{
	struct HV_CMD_RESET_t *pReset;

	pr_debug("Received RESET command tag=%d\n", tag);

	pReset = (struct HV_CMD_RESET_t *)hv_cmd_local_buffer;
	pReset->cmd = RESET;
	*(short *)&pReset->tag = tag;

	/* clear command status */
	clear_cmd_status();

	/* trigger the command */
	pr_debug("trigger the command\n");
	hv_write_command(0, 0, 0, hv_cmd_local_buffer);

	/* wait for completion */
	pr_debug("wait for completion\n");
	cmd_status[0] = 0x41;
	wait_for_cmd_status(cmd_status, 1);

	return 1;
}

static int bsm_query_command(unsigned short tag, unsigned short tag_id, int type, unsigned int lba)
{
	struct HV_CMD_QUERY_t *pQuery;

	/* pr_notice("Received BSM QUERY command tag=%d, tag_id=%d\n", tag, tag_id); */

	/* prepare query command */
	pQuery = (struct HV_CMD_QUERY_t *)hv_cmd_local_buffer;
	pQuery->cmd = QUERY;
	*(short *)&pQuery->tag = tag;
	*(short *)&pQuery->tag_id = tag_id;

	/* transmit the command */
	hv_write_command(0, type, lba, hv_cmd_local_buffer);

	return 1;
}

static int mmls_query_command(unsigned short tag, unsigned short tag_id, int type, unsigned int lba)
{
	struct HV_CMD_QUERY_t *pQuery;

	/* pr_notice("Received MMLS QUERY command tag=%d, tag_id=%d\n", tag, tag_id); */

	pQuery = (struct HV_CMD_QUERY_t *)hv_cmd_local_buffer;
	pQuery->cmd = QUERY;
	*(short *)&pQuery->tag = tag;
	*(short *)&pQuery->tag_id = tag_id;

	/* transmit the command */
	hv_write_command(0, type, lba, hv_cmd_local_buffer);

	return 1;
}

static int ecc_train_command(void)
{
	unsigned short i, j;
	unsigned long ecc_data[ECC_REPEAT_NUM];

	pr_debug("Received ecc train command!\n");

	/* prepare and sending training sequence */
	for (i = 0; i < ECC_CMDS_NUM; i++) {

		for (j = 0; j < ECC_REPEAT_NUM; j++)
			ecc_data[j] = (unsigned long)(i << ECC_ADR_SHFT);

		/* transmit the command */
		hv_write_ecc(0, 0, ecc_data);
	}

	pr_debug("eee train command is done...\n");
	return 1;
}


static int inquiry_command(unsigned int tag)
{
	struct HV_CMD_INQUIRY_t	*pInquiry;

	pr_debug("Received Inquiry Command tag=%d\n", tag);

	pInquiry = (struct HV_CMD_INQUIRY_t *)hv_cmd_local_buffer;
	pInquiry->cmd = INQUIRY;
	*(short *)&pInquiry->tag = tag;

	/* clear command status */
	clear_cmd_status();

	/* trigger the command */
	pr_debug("trigger the command\n");
	hv_write_command(0, 0, 0, hv_cmd_local_buffer);

	/* wait for completion */
	pr_debug("wait for completion\n");
	cmd_status[0] = 0x41;
	wait_for_cmd_status(cmd_status, 1);

	return 1;
}

static int config_command(unsigned int tag,
					unsigned int sz_emmc,
					unsigned int sz_rdimm,
					unsigned int sz_mmls,
					unsigned int sz_bsm,
					unsigned int sz_nvdimm,
					unsigned int to_emmc,
					unsigned int to_rdimm,
					unsigned int to_mmls,
					unsigned int to_bsm,
					unsigned int to_nvdimm)
{
	struct HV_CMD_CONFIG_t *pConfig;

	pr_debug("Received Config command\n");

	pConfig = (struct HV_CMD_CONFIG_t *) hv_cmd_local_buffer;
	pConfig->cmd = CONFIG;
	*(short *)&pConfig->tag = tag;
	*(int *)&pConfig->size_of_emmc = sz_emmc;
	*(int *)&pConfig->size_of_rdimm = sz_rdimm;
	*(int *)&pConfig->size_of_mmls = sz_mmls;
	*(int *)&pConfig->size_of_bsm = sz_bsm;
	*(int *)&pConfig->size_of_nvdimm = sz_nvdimm;
	*(int *)&pConfig->timeout_emmc = to_emmc;
	*(int *)&pConfig->timeout_rdimm = to_rdimm;
	*(int *)&pConfig->timeout_mmls = to_mmls;
	*(int *)&pConfig->timeout_bsm = to_bsm;
	*(int *)&pConfig->timeout_nvdimm = to_nvdimm;

	/* clear command status */
	clear_cmd_status();

	/* trigger the command */
	pr_debug("trigger the command\n");
	hv_write_command(0, 0, 0, hv_cmd_local_buffer);

	/* wait for completion */
	pr_debug("wait for completion\n");
	cmd_status[0] = 0x41;
	wait_for_cmd_status(cmd_status, 1);

	return 1;
}

static int page_swap_command(unsigned int tag,
					   unsigned int o_sector,
					   unsigned int i_sector,
					   unsigned int o_lba,
					   unsigned int i_lba,
					   unsigned int o_mm_addr,
					   unsigned int i_mm_addr)
{
	struct HV_CMD_SWAP_t *pSwap;

	pr_debug("Received PAGE SWAP command\n");

	pSwap = (struct HV_CMD_SWAP_t *) hv_cmd_local_buffer;
	pSwap->cmd = PAGE_SWAP;
	*(unsigned short *)&pSwap->tag = tag;
	*(unsigned int *)&pSwap->page_out_sector = o_sector;
	*(unsigned int *)&pSwap->page_in_sector = i_sector;
	*(unsigned int *)&pSwap->page_out_lba = o_lba;
	*(unsigned int *)&pSwap->page_in_lba = i_lba;
	*(unsigned int *)&pSwap->mm_addr_out = o_mm_addr;
	*(unsigned int *)&pSwap->mm_addr_in = i_mm_addr;

	/* clear command status */
	clear_cmd_status();

	/* trigger the command */
	pr_debug("trigger the command\n");
	hv_write_command(0, 0, 0, hv_cmd_local_buffer);

	/* wait for completion */
	pr_debug("wait for completion\n");
	cmd_status[0] = 0x41;
	wait_for_cmd_status(cmd_status, 1);

	return 1;
}

static int bsm_qread_command(unsigned int tag,
					  unsigned int sector,
					  unsigned int lba,
					  unsigned char *buf)
{
	struct HV_CMD_BSM_t	*pBsm;

	pr_debug("Received BSM QREAD command");
	pr_debug(" tag=%d sector=0x%x lba=0x%x buf=0x%lx\n",
		tag, sector, lba, (unsigned long)buf);

	pBsm = (struct HV_CMD_BSM_t *) hv_cmd_local_buffer;
	pBsm->cmd = BSM_QREAD;
	*(unsigned short *)&pBsm->tag = tag;
	*(unsigned int *)&pBsm->sector = sector;
	*(unsigned int *)&pBsm->lba = lba;

	/* clear command status */
	clear_cmd_status();

	/* trigger the command */
	pr_debug("trigger the command\n");
	hv_write_command(0, GROUP_BSM, lba, hv_cmd_local_buffer);

	/* wait for completion */
	pr_debug("wait for completion\n");
	cmd_status[0] = 0x41;
	wait_for_cmd_status(cmd_status, 1);

	return 1;
}

static int bsm_qwrite_command(unsigned int tag,
					   unsigned int sector,
					   unsigned int lba,
					   unsigned char *buf)
{
	struct HV_CMD_BSM_t	*pBsm;

	pr_debug("Received BSM QWRITE command");
	pr_debug("tag=%d sector=0x%x lba=0x%x buf=0x%lx\n",
		tag, sector, lba, (unsigned long)buf);

	pBsm = (struct HV_CMD_BSM_t *) hv_cmd_local_buffer;
	pBsm->cmd = BSM_QWRITE;
	*(unsigned short *)&pBsm->tag = tag;
	*(unsigned int *)&pBsm->sector = sector;
	*(unsigned int *)&pBsm->lba = lba;

	/* clear command status */
	clear_cmd_status();

	/* trigger the command */
	pr_debug("trigger the command\n");
	hv_write_command(0, GROUP_BSM, lba, hv_cmd_local_buffer);

	/* wait for completion */
	pr_debug("wait for completion\n");
	cmd_status[0] = 0x41;
	wait_for_cmd_status(cmd_status, 1);

	return 1;
}

static int bsm_backup_command(unsigned int tag,
					   unsigned int sector,
					   unsigned int lba)
{
	struct HV_CMD_BSM_t	*pBsm;

	pr_debug("Received BSM BACKUP Command");
	pr_debug(" tag=%d sector=0x%x lba=0x%x\n", tag, sector, lba);

	pBsm = (struct HV_CMD_BSM_t *) hv_cmd_local_buffer;
	pBsm->cmd = BSM_BACKUP;
	*(unsigned short *)&pBsm->tag = tag;
	*(unsigned int *)&pBsm->sector = sector;
	*(unsigned int *)&pBsm->lba = lba;

	/* clear command status */
	clear_cmd_status();

	/* trigger the command */
	pr_debug("trigger the command\n");
	hv_write_command(0, GROUP_BSM, lba, hv_cmd_local_buffer);

	/* wait for completion */
	pr_debug("wait for completion\n");
	cmd_status[0] = 0x41;
	wait_for_cmd_status(cmd_status, 1);

	return 1;
}

static int bsm_restore_command(unsigned int tag,
					    unsigned int sector,
					    unsigned int lba)
{
	struct HV_CMD_BSM_t	*pBsm;

	pr_debug("Received BSM RESTORE command");
	pr_debug(" tag=%d sector=0x%x lba=0x%x\n", tag, sector, lba);

	pBsm = (struct HV_CMD_BSM_t *) hv_cmd_local_buffer;
	pBsm->cmd = BSM_RESTORE;
	*(unsigned short *)&pBsm->tag = tag;
	*(unsigned int *)&pBsm->sector = sector;
	*(unsigned int *)&pBsm->lba = lba;

	/* clear command status */
	clear_cmd_status();

	/* trigger the command */
	pr_debug("trigger the command\n");
	hv_write_command(0, GROUP_BSM, lba, hv_cmd_local_buffer);

	/* wait for completion */
	pr_debug("wait for completion\n");
	cmd_status[0] = 0x41;
	wait_for_cmd_status(cmd_status, 1);

	return 1;
}

static void spin_for_cmd_init(void)
{
	spin_lock_init(&cmd_in_q_lock);
}

#define QUERY_TIMEOUT	1000000000	/* 1 sec */
static int wait_for_cmd_done(unsigned int tag, int type, unsigned int lba)
{	
	int i, j=0;
	unsigned char query_status;
	unsigned long b, a;
	unsigned long latency=0;
	unsigned int expect_sync_cnt;
	unsigned int curr_sync_cnt;
	unsigned int cmd_dn_flg;

	//return 0;
	//===================
	b = hv_nstimeofday();
	cmd_dn_flg=0;

	//for (j=0; j<80;){
	while(latency < QUERY_TIMEOUT) {

		bsm_query_command(query_tag, tag, type, lba);
		query_tag = (query_tag+1)%128 + 128;
		expect_sync_cnt = (qry_sync_cnt[qry_seq_num]+1)&3;

	   	for (i=0; i<20; i++){
		        query_status = hv_query_status (0, type, lba, CMD_DONE);
#ifdef SW_SIM
			return 0;
#endif

			curr_sync_cnt = query_status & CMD_SYNC_COUNTER;

		        if (((query_status & CMD_DONE) == CMD_DONE) &&
				(curr_sync_cnt == expect_sync_cnt)){

				qry_sync_cnt[qry_seq_num]= expect_sync_cnt;
				qry_seq_num = (qry_seq_num + 1) % QUEUE_DEPTH;
				cmd_dn_flg=1;
			        break;
			}
                }
	
		if (cmd_dn_flg == 1) 
			break;

		a = hv_nstimeofday();
		latency = a - b;
		j++;
	}		
	a = hv_nstimeofday();
	latency = a - b;
	pr_info("latency is %ldns, query count is %d times\n", latency, j);

	if (latency > QUERY_TIMEOUT)
		return -1;
	return 0;
}

static int get_rd_buf_size(int type, unsigned int lba)
{
	unsigned char gr_status;
	int rd_buf_size;
	unsigned long b, a;
	unsigned long latency;
#ifdef STS_TST
	static int i=0;
	unsigned char tst_sts[7]={0x03, 0x02, 0x03, 0x02, 0x04, 0x03, 0x03};
#endif
	rd_buf_size = 0;
	latency = 0;
	b = hv_nstimeofday();
		
	while ((rd_buf_size == 0)&&(latency < READ_TIMEOUT)){
		gr_status = hv_read_buf_status (0, type, lba);
#ifdef STS_TST
		gr_status = tst_sts[i];
		i = (i+1)%7;
#endif
		rd_buf_size = gr_status & G_STS_CNT_MASK;
#ifdef STS_TST
		pr_debug("gr_status=0x%x, rd_buf_size=%d, i=%d\n", gr_status,rd_buf_size, i);
#endif
		a = hv_nstimeofday();
		latency = a - b;
	}

	if (latency >= READ_TIMEOUT){
		pr_info("latency is %lds, gr_status=%x\n", latency, gr_status);
		return -1;
	}

	return rd_buf_size;
}

static int get_wr_buf_size(int type, unsigned int lba)
{
	unsigned char gw_status;
	int wr_buf_size;
	unsigned long b, a;
	unsigned long latency;
#ifdef STS_TST
	static int i=0;
	unsigned char tst_sts[7]={0x03, 0x02, 0x03, 0x02, 0x04, 0x03, 0x03};
#endif
	wr_buf_size = 0;
	latency = 0;
	b = hv_nstimeofday();
	//pr_debug("m_data=%d, wr_4k_idx=%d, buf=%lx\n", more_data, wr_4k_idx, buf);	
	while ((wr_buf_size == 0)&&(latency < READ_TIMEOUT)){
		gw_status = hv_write_buf_status (0, type, lba);
#ifdef STS_TST
		gw_status = tst_sts[i];
		i = (i+1)%7;
#endif
		wr_buf_size = gw_status & G_STS_CNT_MASK;
#ifdef STS_TST
		pr_debug("gw_status=0x%x, wr_buf_size=%d, i=%d\n", gw_status,wr_buf_size, i);
#endif
		a = hv_nstimeofday();
		latency = a - b;
	}

	if (latency >= READ_TIMEOUT){
		pr_info("latency is %lds, gw_status=%x\n", latency, gw_status);
		return -1;
	}

	return wr_buf_size;
}



static int hv_read(int type, int more_data, int rd_buf_size, unsigned int lba, unsigned char **buf, int ways, int *index)
{
	int avail_size = ways*rd_buf_size;
	void (*read)(int gid, long lba, int index, void *data, long size);

	if (type == GROUP_BSM)
		read = hv_read_bsm_data;
	else
		read = hv_mmls_fake_write;

	if (more_data==1){
		read (0, lba, *index, *buf, DATA_BUFFER_SIZE);
		*index = 0;
		return 0;
	} else if(more_data==2){
		if (ways ==1){
			if (rd_buf_size==1){			
				read (0, lba, *index, *buf, DATA_BUFFER_SIZE);
				*buf += DATA_BUFFER_SIZE;
				*index = (*index + 1)% 4;
				return 1;
			} else {
				read (0, lba, *index, *buf, more_data*DATA_BUFFER_SIZE);
				*index = 0;
				return 0;
			}

		} else{	//2-way or 4-way
			read (0, lba, *index, *buf, more_data*DATA_BUFFER_SIZE);
			*index = 0;
			return 0;
		}
	} else{
		if (more_data > avail_size){
			read (0, lba, *index, *buf, avail_size*DATA_BUFFER_SIZE);
			*buf += avail_size*DATA_BUFFER_SIZE;
			*index = (*index + rd_buf_size)% 4;
			return (more_data-avail_size);
		} else {
			read (0, lba, *index, *buf, more_data*DATA_BUFFER_SIZE);
			*index = 0;
			return 0;
		}
	}
}

static int hv_write(int type, int more_data, int wr_buf_size, unsigned int lba, unsigned char **buf, int ways, int *index)
{
	int avail_size = ways*wr_buf_size;
	void (*write)(int gid, long lba, int index, void *data, long size);

	if (type == GROUP_BSM)
		write = hv_write_bsm_data;
	else
		write = hv_mmls_fake_read;

	if (more_data==1){
		write (0, lba, *index, *buf, DATA_BUFFER_SIZE);
		*index = 0;
		return 0;
	} else if(more_data==2){
		if (ways ==1){
			if (wr_buf_size==1){			
				write (0, lba, *index, *buf, DATA_BUFFER_SIZE);
				*buf += DATA_BUFFER_SIZE;
				*index = (*index + 1)% 4;
				return 1;
			} else {
				write (0, lba, *index, *buf, more_data*DATA_BUFFER_SIZE);
				*index = 0;
				return 0;
			}

		} else{	//2-way or 4-way
			write (0, lba, *index, *buf, more_data*DATA_BUFFER_SIZE);
			*index = 0;
			return 0;
		}
	} else{
		if (more_data > avail_size){
			write (0, lba, *index, *buf, avail_size*DATA_BUFFER_SIZE);
			*buf += avail_size*DATA_BUFFER_SIZE;
			*index = (*index + wr_buf_size)% 4;
			return (more_data - avail_size);
		} else {
			write (0, lba, *index, *buf, more_data*DATA_BUFFER_SIZE);
			*index = 0;
			return 0;
		}
	}
}
