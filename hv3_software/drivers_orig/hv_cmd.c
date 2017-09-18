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
#include "hv_params.h"
#else
#include <stdio.h>
#include <string.h>
#ifdef USER_SPACE_CMD_DRIVER
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#endif	 
#endif	// SIMULATION_TB

#include "hv_mmio.h"
#include "hv_cmd.h"

#if 0	//MK1	// debug logging disabled
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

/* comment out these lines on target HW */
//MK moved the following two build switches to hv_mmio.h
//MK#define SW_SIM
//MK#define STS_TST

//MK1110#define READ_TIMEOUT 1000000 /* 1 ms */
//MK1110-begin
//MK0221#define READ_TIMEOUT 5000000000 // 5 sec for devel purpose
//SJ0227#define READ_TIMEOUT 20000 //MK0221 Hao says 1 usec but let's give enough (20 us)
//MK0613#define READ_TIMEOUT 200000	//SJ0227 SJ set it to 200us
#define READ_TIMEOUT 750000	//MK0613 750us
//MK1110-end
#define MAX_TRY 10
#define INITIAL_DELAY 1 // usec
//MK-begin
#define MAX_STATUS_WAIT_TIME_NS		0x3B9ACA00	// Set it to 1 second for now
//MK-end

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

//MK-begin
static struct hv_rw_cmd rw_cmd_buff[4];
//MK0201-begin
struct hv_query_cmd query_cmd_buff;
//MK0201-end

struct fake_func_tbl {
   char *name;
   int (*get_blk_cnt) (int type, unsigned int lba);
   void (*do_fake) (int gid, long lba, int index, void *pdata, long size);
};

//MK0729static int hv_fake_operation(fake_type_t fake_op_index, unsigned int blk_cnt, unsigned int lba, int type, unsigned char *buf);
//MK0729-begin
static int hv_fake_operation(fake_type_t fake_op_index, unsigned int sector_cnt, unsigned int lba, int type, unsigned char *buf);
//MK0729-end
//MK-end

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

static void usleep_range(unsigned long  min , unsigned long  max ) 
{ 
	usleep(min);
}

static void udelay(unsigned long delay) 
{ 
	usleep(delay);
}
#endif	// SIMULATION_TB

//MKstatic int qry_sync_cnt[QUEUE_DEPTH] = {0};
//MKstatic int qry_seq_num = 0;
static int rd_4k_idx_b = 0;  /* 4k-buffer index for bsm read: 0, 1,2,3 */
static int wr_4k_idx_b = 0;  /* 4k-buffer index for bsm write: 0, 1,2,3 */
static int rd_4k_idx_m = 0;  /* 4k-buffer index for mmls read: 0, 1,2,3 */
static int wr_4k_idx_m = 0;  /* 4k-buffer index for mmls write: 0, 1,2,3 */

/* local copy of hv command */
static unsigned char hv_cmd_local_buffer[CMD_BUFFER_SIZE];

/* use this buffers temporarily until the addresses is defined by HW engr */
static char hv_status_buffer[STATUS_BUFFER_SIZE] = {0x41};

//MK0125-begin
/* local copy of hv command */
static unsigned char temp_fake_buffer[4096];
//MK0125-end

//MK0224static int wait_for_cmd_done(unsigned int tag, int type, unsigned int lba);
//MK0224-begin
//MK0628static int wait_for_cmd_done(unsigned int tag, int type, unsigned int lba, unsigned long delay);
//MK0628-begin
static int wait_for_cmd_done(unsigned int tag, int type, unsigned int lba, unsigned long delay, unsigned char cmd_opcode);
//MK0628-end
//MK0224-end
static int get_wr_buf_size(int type, unsigned int lba);
static int get_rd_buf_size(int type, unsigned int lba);

static int hv_read(int type, int more_data, int rd_buf_size, unsigned int lba, unsigned char **buf, int ways, int *index);
static int hv_write(int type, int more_data, int wr_buf_size, unsigned int lba, unsigned char **buf, int ways, int *index);

//MK-begin
struct fake_func_tbl fake_op [] = {
    {"presight-read", get_wr_buf_size, hv_mmls_fake_read},		// for MMLS_WRITE and BSM_WRITE
    {"presight-write", get_rd_buf_size, hv_mmls_fake_write}		// for MMLS_READ and BSM_READ
};
//MK-end

//MK0627-begin
/* Timestamp buffer */
static unsigned long timestamp[10];
//MK0627-end

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
unsigned long get_estimated_TTC(unsigned int tag, int type, unsigned int lba)
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

//MK1201-begin
int get_vid_command(void *cmd_desc)
{
	struct hd_vid_t *pbuff=(struct hd_vid_t *)cmd_desc;
	unsigned char byte_data;
	int ret_code=0;

	pr_debug("[%s]: spd_dimm_id = %d\n", __func__, pbuff->spd_dimm_id);

	set_i2c_page_address(pbuff->spd_dimm_id, 1);
	byte_data = (unsigned char) read_smbus(pbuff->spd_dimm_id, SPD_ID, 65);
	pr_debug("[%s]: [321] = 0x%.2X\n", __func__, byte_data);

	pbuff->vid = (unsigned int)byte_data << 8;
	byte_data = (unsigned char) read_smbus(pbuff->spd_dimm_id, SPD_ID, 64);
	pr_debug("[%s]: [320] = 0x%.2X\n", __func__, byte_data);

	pbuff->vid |= (unsigned int)byte_data;
	pr_debug("[%s]: [321:320] = 0x%.4X\n", __func__, (unsigned int)pbuff->vid);

	set_i2c_page_address(pbuff->spd_dimm_id, 0);
	pbuff->error_code = ret_code;
	return(ret_code);
}
//MK1201-end

//MK0324-begin
int bsm_write_command_emmc(unsigned int tag, unsigned int sector, unsigned int lba,
		unsigned char *buf, unsigned char *vbuf, unsigned char async, void *callback_func)
{
	int	ret_code=0;
	unsigned int i, lba_offset=0, max_lba_offset=(64/EMMC_CH_CNT - 8);
	unsigned long *pqword=(unsigned long *)vbuf;

	if ( EMMC_CH_CNT == 8 ) {
		/* LBA should be multiple of 8: 0, 8, 16, 24 32 etc. */
		ret_code = bsm_write_command_3(tag, sector, lba, buf, vbuf, async, callback_func, 0);
//MK0512-begin
//MK0518		(void) bsm_write_command_3(tag, sector, 0x200000, buf, vbuf, async, callback_func, 0);
//MK0518		(void) bsm_write_command_3(tag, sector, 0x200008, buf, vbuf, async, callback_func, 0);
//MK0518		(void) bsm_write_command_3(tag, sector, 0x200010, buf, vbuf, async, callback_func, 0);
//MK0518-begin
///		(void) bsm_write_command_3(tag, sector, 0x1d000000, buf, vbuf, async, callback_func, 0);	// at LBA for 232GB
///		(void) bsm_write_command_3(tag, sector, 0x1d000008, buf, vbuf, async, callback_func, 0);
///		(void) bsm_write_command_3(tag, sector, 0x1d000010, buf, vbuf, async, callback_func, 0);
//MK0518-end
//MK0512-end
	} else {
		/* Make a back-up copy of F.R.B. in LRDIMM buffer */
		memcpy((void*)temp_fake_buffer, (void*)vbuf, 4096);

		/*
		 * Each byte goes to each eMMC channel. (byte 0 to ch 0, ... byte 7 to ch 7)
		 * This means that when a 4KB page is divided in 64-bit words (qwords)
		 * there are 512 qwords. Byte 0 from each qword (byte 0 lane: total 512
		 * bytes) is stored in the same sector in eMMC channel 0 and byte 1 from
		 * each qword is stored in the same sector in eMMC channel 1 etc.
		 * Data in one 4KB page are distributed in all eight channels with the
		 * same LBA.
		 * (byte 0 lane (512 bytes) -> LBA x in eMMC CH0,
		 *  byte 1 lane (512 bytes) -> LBA x in eMMC CH1,
		 *  ......
		 *  byte 7 lane (512 bytes) -> LBA x in eMMC CH7)
		 */
		while ( lba_offset <= max_lba_offset )
		{
			ret_code = bsm_write_command_3(tag, sector, lba+lba_offset, buf, vbuf, async, callback_func, 0);
			lba_offset += 8;
			if ( (ret_code == 0) && (lba_offset <= max_lba_offset) ) {
				/* Prepare the next byte lane(s) */
				for (i=0; i < 512; i++)
				{
					pqword[i] >>= ((EMMC_CH_CNT * 8) % 64);
				}
			} else {
				break;
			} // end of if ( (ret_code ...
		} // end of while

		/* Restore the input buffer */
		memcpy((void*)vbuf, (void*)temp_fake_buffer, 4096);

	} // end of if ( EMMC_CH_CNT ...

	return ret_code;
}
EXPORT_SYMBOL(bsm_write_command_emmc);

int bsm_read_command_emmc(unsigned int tag, unsigned int sector, unsigned int lba,
		unsigned char *buf,	unsigned char *vbuf, unsigned char async, void *callback_func)
{
	int	ret_code=0;
	unsigned int i, lba_offset, max_lba_offset=(64/EMMC_CH_CNT - 8);
	unsigned int s1=64-(EMMC_CH_CNT * 8), s2=(EMMC_CH_CNT * 8) % 64;
	unsigned int *pdword=(unsigned int *)vbuf;
	unsigned int *pdword2=(unsigned int *)temp_fake_buffer;
	unsigned long qmask=0xFFFFFFFFFFFFFFFF >> (64-(EMMC_CH_CNT * 8));

// EMMC_CH_CNT	lba_offset	s1	s2	Initial	qmask			SHL
//		8		0			0	0	0xFFFF_FFFF_FFFF_FFFF	0
//		4		8,0			32	32	0x0000_0000_FFFF_FFFF	32
//		2		24,16,8,0	48	16	0x0000_0000_0000_FFFF	48/32/16
//		1		56,48,40,32	56	8	0x0000_0000_0000_00FF	56/48/40/32/24/16/8
//				24,16,8,0

	/*
	 * Each byte goes to each eMMC channel. (byte 0 to ch 0, ... byte 7 to ch 7)
	 * This means that when a 4KB page is divided in 64-bit words (qwords)
	 * there are 512 qwords. Byte 0 from each qword (byte 0 lane: total 512
	 * bytes) is stored in the same sector in eMMC channel 0 and byte 1 from
	 * each qword is stored in the same sector in eMMC channel 1 etc.
	 * Data in one 4KB page are distributed in all eight channels with the
	 * same LBA.
	 * (byte 0 lane (512 bytes) -> LBA x in eMMC CH0,
	 *  byte 1 lane (512 bytes) -> LBA x in eMMC CH1,
	 *  ......
	 *  byte 7 lane (512 bytes) -> LBA x in eMMC CH7)
	 */
	if ( EMMC_CH_CNT == 8 ) {
		ret_code = bsm_read_command_3(tag, sector, lba, buf, (void *)pdword, async, callback_func, 0);
	} else {
		/* This buffer will collect data from FPGA during the while loop */
		memset((void*)temp_fake_buffer, 0, 4096);

		lba_offset = max_lba_offset;
		while ( lba_offset >= 0 && lba_offset <= max_lba_offset )
		{
			ret_code = bsm_read_command_3(tag, sector, lba+lba_offset, buf, (void *)pdword, async, callback_func, 0);
			if ( ret_code == 0 ) {
				/* Prepare the next byte lane(s) */
				for (i=0; i < 512; i++)
				{
					pdword2[i] |= (pdword[i] & qmask) << s1;
				}
				s1 -= s2;
				lba_offset -= 8;
			} else {
				break;
			} // end of if ( ret_code ...
		} // end of while

		/* Copy all data from FPGA to F.W.B. */
		memcpy((void*)vbuf, (void*)temp_fake_buffer, 4096);

	} // end of if ( EMMC_CH_CNT ...

	return ret_code;
}
//MK0324-end

//MK0125-begin
int bsm_write_command_master(unsigned int tag, unsigned int sector, unsigned int lba,
		unsigned char *buf, unsigned char *vbuf, unsigned char async, void *callback_func)
{
	int	ret_code=0;
	unsigned int i;
	unsigned long *pqword=(unsigned long *)vbuf;

	/* Make a back-up copy of the input buffer */
	memcpy((void*)temp_fake_buffer, (void*)vbuf, 4096);

	/*
	 * The entire fake-read buffer (4KB) is filled with data. Since only the
	 * master FPGA operates, we need to send one half of the buffer to the
	 * FPGA at a time. The first BSM_WRITE will carry the original buffer but
	 * the FPGA will take the lower 32-bit of each qword in the buffer because
	 * the master FPGA is responsible for the lower 32-bit data while the slave
	 * FPGA responsible for the upper 32-bit data.
	 */

	/* Send command followed by fake-read, query cmd, query status */
//	ret_code = bsm_write_command(tag, sector, lba, buf, async, callback_func);
//	ret_code = bsm_write_command_2(tag, sector, lba, buf, vbuf, async, callback_func, 5);
	ret_code = bsm_write_command_3(tag, sector, lba, buf, vbuf, async, callback_func, 5);

	if (ret_code == 0) {
		/* Shift each qword right by 32-bit */
		for (i=0; i < 512; i++)
		{
			pqword[i] >>= 32;
		}

		mdelay(1);	//MK0303

		/* Send command followed by fake-read, query cmd, query status */
//		ret_code = bsm_write_command(tag, sector, lba+0x200, buf, async, callback_func);
//		ret_code = bsm_write_command_2(tag, sector, lba+0x200, buf, vbuf, async, callback_func, 5);
		ret_code = bsm_write_command_3(tag, sector, lba+0x200, buf, vbuf, async, callback_func, 5);
	}

	mdelay(1);	//MK0303

	/* Restore the input buffer */
	memcpy((void*)vbuf, (void*)temp_fake_buffer, 4096);

	return ret_code;
}

int bsm_read_command_master(unsigned int tag, unsigned int sector, unsigned int lba,
		unsigned char *buf,	unsigned char *vbuf, unsigned char async,
		void *callback_func)
{
	int	ret_code=0;
	unsigned int i;
//MK0222	unsigned long *pqword=(unsigned long *)vbuf;
	unsigned int *pdword=(unsigned int *)vbuf;				//MK0222
	unsigned int *pdword2=(unsigned int *)temp_fake_buffer;	//MK0222

	pr_debug("[%s]: entered\n", __func__);

	/*
	 * The fake-write buffer has been filled with the master FPGA only. The
	 * first call will get the upper 32-bit of each qword that were stored
	 * before. The second call will get the lower 32-bit of each qword.
	 * So,
	 */

	/* Send command followed by query cmd, query status, fake-write */
//	ret_code = bsm_read_command(tag, sector, lba+0x200, buf, async, callback_func, 5);
//	ret_code = bsm_read_command_2(tag, sector, lba+0x200, buf, vbuf, async, callback_func, 5);
//MK0222	ret_code = bsm_read_command_3(tag, sector, lba+0x200, buf, vbuf, async, callback_func, 5);
	ret_code = bsm_read_command_3(tag, sector, lba+0x200, buf, (void *)pdword, async, callback_func, 5);	//MK0222

	if (ret_code == 0) {
//MK0222		/* Shift the upper 32-bit data from TBM to the upper 32-bit */
//MK0222		for (i=0; i < 512; i++)
//MK0222		{
//MK0222			pqword[i] <<= 32;
//MK0222		}
//MK0222-begin
		for (i=0; i < 512; i++)
		{
			pdword2[i*2+1] = pdword[i*2];
		}
//MK0222-end

		mdelay(1);	//MK0303

		/* Send command followed by query cmd, query status, fake-write */
//		ret_code = bsm_read_command(tag, sector, lba, buf, async, callback_func, 5);
//		ret_code = bsm_read_command_2(tag, sector, lba, buf, vbuf, async, callback_func, 5);
//MK0222		ret_code = bsm_read_command_3(tag, sector, lba, buf, vbuf, async, callback_func, 5);
		ret_code = bsm_read_command_3(tag, sector, lba, buf, (void *)pdword, async, callback_func, 5);	//MK0222

		if (ret_code == 0) {
//MK0222			/* Shift the upper 32-bit data from TBM to the upper 32-bit */
//MK0222			for (i=0; i < 512; i++)
//MK0222			{
//MK0222				pqword[i] <<= 32;
//MK0222			}
//MK0222-begin
			for (i=0; i < 512; i++)
			{
				pdword2[i*2] = pdword[i*2];
			}

			memcpy((void*)pdword, (void*)pdword2, 4096);

			pr_debug("[%s]: data from fake-write buffer\n", __func__);
//MK0301			display_buffer((unsigned long *)vbuf, 4096/8, 0xFFFFFFFFFFFFFFFF);
			display_buffer((unsigned long *)vbuf, 4096/8, get_pattern_mask());	//MK0301
//MK0222-end

			mdelay(1);	//MK0303
		}
	}

	return ret_code;
}
//MK0125-end

//MK0207int bsm_read_command(   unsigned int tag,
//MK0207			unsigned int sector,
//MK0207			unsigned int lba,
//MK0207			unsigned char *buf,
//MK0207			unsigned char async,
//MK0207			void *callback_func)
//MK0207-begin
int bsm_read_command(unsigned int tag, unsigned int sector, unsigned int lba,
		unsigned char *buf, unsigned char async, void *callback_func,
		unsigned char max_retry_count)
//MK0207-end
{
//MK	struct HV_CMD_BSM_t *pBsm;
//MK-begin
	struct hv_rw_cmd *pBsm;

#ifndef SW_SIM
//MK0729	unsigned int block_count;
#endif
//MK-end

//MK0207-begin
	int ret_code=0;
	unsigned char retry_count=max_retry_count;
	struct fpga_debug_info_t fpga_debug_info;
//MK0207-end

//MK1102-begin
#if 0	//MK1118 do we still need this code for test?
	unsigned int i, j;
	unsigned char regb[32];

	for (i=0; i < 8; i++)
	{
		for (j=0; j < 16; j++)
		{
			regb[j] = (unsigned char) read_smbus(3, 5, i*16+j);
		}
		pr_debug("%.3x: %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x\n",
				i*16, regb[0], regb[1], regb[2], regb[3], regb[4], regb[5], regb[6], regb[7],
				regb[8], regb[9], regb[10], regb[11], regb[12], regb[13], regb[14], regb[15]);
	}
#endif	//MK1118
//MK1102-end


//MK	pr_warn("*** bsm_read_command: tag=%d %d %d\n", tag, sector, lba );	//q-debug

	/* arrange command structure */
//MK	pBsm = (struct HV_CMD_BSM_t *) hv_cmd_local_buffer;
//MK	pBsm->cmd = BSM_READ;
//MK	*(unsigned short *)&pBsm->tag = tag;
//MK	*(unsigned int *)&pBsm->sector = sector;
//MK	*(unsigned int *)&pBsm->lba = lba+bsm_start()/HV_BLOCK_SIZE;
//MK	*(unsigned char *)&pBsm->more_data = 0x0;
//MK
//MK#ifdef REV_B_MM
//MK	*(unsigned long *)&pBsm->mm_addr = hv_get_dram_addr(GROUP_BSM, (void*)buf);
//MK#endif
//MK-begin
	// Just clear 1/4 of the command buffer
	pBsm = (struct hv_rw_cmd *) rw_cmd_buff;
	memset(pBsm, 0, CMD_BUFFER_SIZE);

//MK1018-begin
	/* Buffer to be used to fake-write to status registers */
	/* Debugging only - We don't need to do this if HW works correctly. */
//MK0519	clear_fake_mmls_buf();
//MK1018-end

//MK0207-begin
send_cmd_again:
//MK0207-end

	// Build command structure
#if 1
	pBsm->command[0].cmd_field.cmd = MMLS_READ;
//MK0209	pBsm->command[0].cmd_field.tag = (unsigned char) tag;
//MK0209-begin
	pBsm->command[0].cmd_field.tag = get_command_tag();
//MK0209-end
//	pBsm->command[1].cmd_field_.cmd = pBsm->command[0].cmd_field.cmd;
//	pBsm->command[1].cmd_field.tag = pBsm->command[0].cmd_field.tag;
	pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
	pBsm->sector[0] = sector;
	pBsm->sector[1] = sector;
	pBsm->lba[0] = lba;
	pBsm->lba[1] = lba;
//MK0727	pBsm->dram_addr[0] = hv_get_dram_addr(GROUP_BSM, (void*)buf);
//MK0727-begin
//MK1024	pBsm->dram_addr[0] = (unsigned int) ((unsigned long) (FAKE_BUFF_SYS_PADDR - LRDIMM_SIZE - SYS_RESERVED_MEM_SIZE) >> 3);	// physical addr >> 3
//MK1024-begin
//MK1118	pBsm->dram_addr[0] = (unsigned int) ((unsigned long) (MMLS_READ_BUFF_SYS_PADDR - LRDIMM_SIZE - SYS_RESERVED_MEM_SIZE) >> 3);	// physical addr >> 3
//MK1118-begin
	pBsm->dram_addr[0] = (unsigned int) (((unsigned long)buf - LRDIMM_SIZE - SYS_RESERVED_MEM_SIZE) >> 3);
//MK1118-end
//MK1024-end
//MK0727-end
	pBsm->dram_addr[1] = pBsm->dram_addr[0];
	pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
						pBsm->lba[0] ^ pBsm->dram_addr[0];
	pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
						pBsm->lba[1] ^ pBsm->dram_addr[1];
#else // for testing slave I/O on HVDIMM
	pBsm->command[0].cmd_field.cmd = tag;
	pBsm->command[0].cmd_field.tag = tag;
	pBsm->command[0].cmd_field.reserve1[0] = tag;
	pBsm->command[0].cmd_field.reserve1[1] = tag;
//	pBsm->command[1].cmd_field.cmd = tag;
//	pBsm->command[1].cmd_field.tag = tag;
//	pBsm->command[1].cmd_field.reserve1[0] = tag;
//	pBsm->command[1].cmd_field.reserve1[1] = tag;
	pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
	pBsm->sector[0] = pBsm->command[0].cmd_field_32b;
	pBsm->sector[1] = pBsm->sector[0];
	pBsm->lba[0] = pBsm->sector[0];
	pBsm->lba[1] = pBsm->sector[0];
	pBsm->dram_addr[0] = pBsm->sector[0];
	pBsm->dram_addr[1] = pBsm->sector[0];
	pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
						pBsm->lba[0] ^ pBsm->dram_addr[0];
	pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
						pBsm->lba[1] ^ pBsm->dram_addr[1];
#endif
//MK-end

//MK-begin
	pr_debug("[%s]: *buf=%#.16lx\n", __func__, (unsigned long)buf);
	pr_debug("[%s]: cmd[07]:[00] = 0x%.8X - 0x%.8X\n", __func__, pBsm->command[1].cmd_field_32b, pBsm->command[0].cmd_field_32b);
	pr_debug("[%s]: cmd[15]:[08] = 0x%.8X - 0x%.8X\n", __func__, pBsm->sector[1], pBsm->sector[0]);
	pr_debug("[%s]: cmd[23]:[16] = 0x%.8X - 0x%.8X\n", __func__, pBsm->lba[1], pBsm->lba[0]);
	pr_debug("[%s]: cmd[31]:[24] = 0x%.8X - 0x%.8X\n", __func__, pBsm->dram_addr[1], pBsm->dram_addr[0]);
	pr_debug("[%s]: cmd[39]:[32] = 0x%.8X - 0x%.8X\n", __func__, pBsm->checksum[1], pBsm->checksum[0]);
	pr_debug("[%s]: cmd[47]:[40] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[1], pBsm->reserve1[0]);
	pr_debug("[%s]: cmd[55]:[48] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[3], pBsm->reserve1[2]);
	pr_debug("[%s]: cmd[63]:[56] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[5], pBsm->reserve1[4]);
//MK-end

	/* prepare and send bsm read command */
//MK	hv_write_command(0, GROUP_BSM, lba, hv_cmd_local_buffer);
//MK-begin
		hv_write_command(0, GROUP_BSM, lba, pBsm);
//MK-end

//MK0209-begin
	inc_command_tag();
//MK0209-end

//MK0207	return 0;
//MK0207-begin
	/* Get MMIO command checksum from FPGA */
	hv_read_mmio_command_checksum(&fpga_debug_info);

	if (fpga_debug_info.mmio_cmd_checksum != pBsm->checksum[0]) {
		if (retry_count != 0) {
			pr_debug("[%s]: MMIO checksum mismatched, do retry (retry_count=%d) FPGA checksum=0x%.8X, MMIO checksum=0x%.8X\n",
					__func__, retry_count, fpga_debug_info.mmio_cmd_checksum, pBsm->checksum[0]);
//			hv_display_mmio_command_slaveio();
			retry_count--;
			goto send_cmd_again;
		} else {
			pr_debug("[%s]: BSM_WRITE failed due to MMIO checksum mismatch (retry_count=%d), FPGA checksum=0x%.8X, MMIO checksum=0x%.8X\n",
					__func__, retry_count, fpga_debug_info.mmio_cmd_checksum, pBsm->checksum[0]);
			ret_code = -5;

//			hv_display_mmio_command_slaveio();
			goto exit;
		}
	}

	pr_debug("[%s]: cmd checksum from FPGA (0x%.8X) %s cmd checksum sent by host (0x%.8X) (retry_count=%d)\n",
			__func__, fpga_debug_info.mmio_cmd_checksum,
			(fpga_debug_info.mmio_cmd_checksum == pBsm->checksum[0]) ? "==" : "!=", pBsm->checksum[0], retry_count);

exit:
	return ret_code;
//MK0207-end


	if (async == 0) {		/* synchronous mode */
//MK0224		if (!wait_for_cmd_done(tag, GROUP_BSM, lba))
//MK0224-begin
//MK0628		if ( !wait_for_cmd_done(tag, GROUP_BSM, lba, get_bsm_rd_qc_status_delay()) )
//MK0628-begin
		if ( !wait_for_cmd_done(tag, GROUP_BSM, lba, get_bsm_rd_qc_status_delay(), MMLS_READ) )
//MK0628-end
//MK0224-end
//MK			/* read data from hv */
//MK			hv_read_data(GROUP_BSM, sector, lba, buf);
//MK-begin
		{
#ifdef SW_SIM
			/* read data from hv */
			hv_read_data(GROUP_BSM, sector, lba, buf);
#else

#if 0 	//MK0729
			/* Min block size of 4KB */
			/* Calculate data size in 4KB blocks (sector*512/4096) */
			block_count = ((sector << 9) >> 12);
			if ((sector % 8) != 0) {
				/* Read one extra 4KB block */
				block_count++;
			}

			/* Do fake-write */
			if (hv_fake_operation(FAKE_WRITE, block_count, lba, GROUP_BSM, buf) != 0)
				return -1;
#endif	//MK0729
//MK1014-begin
//MK1014 testing "data fake write". Data fake write will be done by devmem2h.
return 0;
//MK1014-end
//MK0729-begin
			/* Do fake-write */
			if (hv_fake_operation(FAKE_WRITE, sector, lba, GROUP_BSM, buf) != 0)
				return -1;
//MK0729-end
#endif
		}
//MK-end
		else {
			pr_warn("*** not get the BSM read CMD_DONE yet!!!\n");
			return (-1);
		}

		/* send termination cmd to specified addr */
//MK1013		hv_write_termination (0, GROUP_BSM, lba, NULL);
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

//MK0817-begin	-- added to support FPGA team to debug address decoding during the fake_write operation
//MK1118int bsm_read_command_2(   unsigned int tag,
//MK1118			unsigned int sector,
//MK1118			unsigned int lba,
//MK1118			unsigned char *buf,
//MK1118			unsigned char async,
//MK1118			void *callback_func)
//MK0120//MK1118-begin
//MK0120int bsm_read_command_2(   unsigned int tag,
//MK0120			unsigned int sector,
//MK0120			unsigned int lba,
//MK0120			unsigned char *buf,
//MK0120			unsigned char *vbuf,
//MK0120			unsigned char async,
//MK0120			void *callback_func)
//MK0120//MK1118-end
//MK0201//MK0120-begin
//MK0201int bsm_read_command_2(unsigned int tag, unsigned int sector, unsigned int lba,
//MK0201			unsigned char *buf,	unsigned char *vbuf, unsigned char async,
//MK0201			void *callback_func, unsigned char *checksum)
//MK0201//MK0120-end
//MK0201-begin
int bsm_read_command_2(unsigned int tag, unsigned int sector, unsigned int lba,
			unsigned char *buf,	unsigned char *vbuf, unsigned char async,
			void *callback_func, unsigned char max_retry_count)
//MK0201-end
{
//MK1109	struct hv_rw_cmd *pBsm;

#if 0	//MK1109
//MK1024-begin
	/* Debug only - to help Hao debug status register problem */
	unsigned char query_status;
	bsm_query_command(query_tag, tag, GROUP_BSM, lba);
	query_status = hv_query_status(0, GROUP_BSM, lba, 0);
	pr_debug("[%s]: query_status=0x%.2x\n", __func__, (unsigned char)query_status);
	return 0;
//MK1024-end
	// Just clear 1/4 of the command buffer
	pBsm = (struct hv_rw_cmd *) rw_cmd_buff;
	memset(pBsm, 0, CMD_BUFFER_SIZE);

	// Build command structure
	pBsm->command[0].cmd_field.cmd = MMLS_READ;
	pBsm->command[0].cmd_field.tag = (unsigned char) tag;
	pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
	pBsm->sector[0] = sector;
	pBsm->sector[1] = sector;
	pBsm->lba[0] = lba;
	pBsm->lba[1] = lba;
//MK1024	pBsm->dram_addr[0] = (unsigned int) ((unsigned long) (FAKE_BUFF_SYS_PADDR - LRDIMM_SIZE - SYS_RESERVED_MEM_SIZE) >> 3);	// physical addr >> 3
//MK1024-begin
	pBsm->dram_addr[0] = (unsigned int) ((unsigned long) (MMLS_READ_BUFF_SYS_PADDR - LRDIMM_SIZE - SYS_RESERVED_MEM_SIZE) >> 3);	// physical addr >> 3
//MK1024-end
	pBsm->dram_addr[1] = pBsm->dram_addr[0];
	pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
						pBsm->lba[0] ^ pBsm->dram_addr[0];
	pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
						pBsm->lba[1] ^ pBsm->dram_addr[1];

	pr_debug("[%s]: *buf=%#.16lx\n", __func__, (unsigned long)buf);
	pr_debug("[%s]: cmd[07]:[00] = 0x%.8X - 0x%.8X\n", __func__, pBsm->command[1].cmd_field_32b, pBsm->command[0].cmd_field_32b);
	pr_debug("[%s]: cmd[15]:[08] = 0x%.8X - 0x%.8X\n", __func__, pBsm->sector[1], pBsm->sector[0]);
	pr_debug("[%s]: cmd[23]:[16] = 0x%.8X - 0x%.8X\n", __func__, pBsm->lba[1], pBsm->lba[0]);
	pr_debug("[%s]: cmd[31]:[24] = 0x%.8X - 0x%.8X\n", __func__, pBsm->dram_addr[1], pBsm->dram_addr[0]);
	pr_debug("[%s]: cmd[39]:[32] = 0x%.8X - 0x%.8X\n", __func__, pBsm->checksum[1], pBsm->checksum[0]);
	pr_debug("[%s]: cmd[47]:[40] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[1], pBsm->reserve1[0]);
	pr_debug("[%s]: cmd[55]:[48] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[3], pBsm->reserve1[2]);
	pr_debug("[%s]: cmd[63]:[56] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[5], pBsm->reserve1[4]);

	/* prepare and send bsm read command */
//		hv_write_command(0, GROUP_BSM, lba, pBsm);
#endif	//MK1109

//MK0126-begin
	int ret_code=0;
//MK0201	unsigned int retry_count=1;
//MK0201-begin
	unsigned int retry_count=max_retry_count;
//MK0201-end
	struct block_checksum_t fpga_checksum, fwb_checksum;
//MK0126-end

	if (async == 0) {		/* synchronous mode */

//MK0126-begin
send_query_command_again:
//MK0126-end

//MK1214		if (!wait_for_cmd_done(tag, GROUP_BSM, lba))
//MK1214-begin
//MK0209		if (wait_for_cmd_done(tag, GROUP_BSM, lba) == 0)
//MK0209-begin
//MK0224if (wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba) == 0)
//MK0224-begin
//MK0628		if ( wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba, get_bsm_rd_qc_status_delay()) == 0 )
//MK0628-begin
		if ( wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba, get_bsm_rd_qc_status_delay(), MMLS_READ) == 0 )
//MK0628-end
//MK0224-end
//MK0209-end
//MK1214-end
		{
//#ifdef SW_SIM
//			/* read data from hv */
//			hv_read_data(GROUP_BSM, sector, lba, buf);
//#else
			/* Do fake-write */
//MK1118			if (hv_fake_operation(FAKE_WRITE, sector, lba, GROUP_BSM, buf) != 0)
//MK1118				return -1;
//MK1118-begin
			if (hv_fake_operation(FAKE_WRITE, sector, lba, GROUP_BSM, vbuf) != 0) {
//MK0106-begin
				/* Display the content of fake write buffer */
//MK0301				display_buffer((unsigned long *)vbuf, 4096/8, 0x00000000FFFFFFFF);
				display_buffer((unsigned long *)vbuf, 4096/8, get_pattern_mask());	//MK0301
//MK0106-end
//MK0126				return -1;
//MK0126-begin
				pr_warn("[%s]: fake-write failed\n", __func__);
				ret_code = -3;
				goto exit;
//MK0126-end
			}
//MK1118-end
//#endif

//MK0106-begin
			/* Display the content of fake write buffer */
//MK0301			display_buffer((unsigned long *)vbuf, 4096/8, 0x00000000FFFFFFFF);
			display_buffer((unsigned long *)vbuf, 4096/8, get_pattern_mask());	//MK0301
//MK0106-end

		}
		else {
//MK1215-begin
		/* Display the content of fake buffer */
//		display_buffer((unsigned long *)vbuf, 4096/8, 0x00000000FFFFFFFF);
//MK1215-end

//MK0126			pr_warn("*** not get the BSM read CMD_DONE yet!!!\n");
//MK0126			return (-1);
//MK0126-begin
			pr_warn("[%s]: CMD_DONE not found\n", __func__);
			ret_code = -2;
			goto exit;
//MK0126-end
		}

		/* send termination cmd to specified addr */
//MK1109		hv_write_termination (0, GROUP_BSM, lba, NULL);
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

//MK0126	return 0;
//MK0126//MK0120-begin

	/* Get checksum from FPGA */
//	pr_debug("[%s]: calling hv_read_checksum from bsm_read_command_2\n", __func__);
	hv_read_fake_buffer_checksum(0, &fpga_checksum);
	pr_debug("[%s]: fpga_checksum = 0x%.2x%.2x%.2x%.2x\n",
			__func__, fpga_checksum.sub_cs[3], fpga_checksum.sub_cs[2], fpga_checksum.sub_cs[1], fpga_checksum.sub_cs[0]);

	/* Compute checksum of data in fake-write buffer */
//MK0214	memset((void*)&fwb_checksum, 0, sizeof(struct block_checksum_t));
	calculate_checksum((void*)vbuf, &fwb_checksum);
	pr_debug("[%s]: fwb_checksum  = 0x%.2x%.2x%.2x%.2x\n",
			__func__, fwb_checksum.sub_cs[3], fwb_checksum.sub_cs[2], fwb_checksum.sub_cs[1], fwb_checksum.sub_cs[0]);

//	memcpy((void*)&fpga_checksum, (void*)&fwb_checksum, sizeof(fpga_checksum));
//	if ( memcmp((void *)&fpga_checksum, (void*)&fwb_checksum, 4) != 0) {
	if ( (fpga_checksum.sub_cs[0] == fwb_checksum.sub_cs[0]) &&
			(fpga_checksum.sub_cs[1] == fwb_checksum.sub_cs[1]) &&
			(fpga_checksum.sub_cs[2] == fwb_checksum.sub_cs[2]) &&
			(fpga_checksum.sub_cs[3] == fwb_checksum.sub_cs[3]) ) {
		pr_debug("[%s]: fpga_checksum & fwb_checksum are the same!\n", __func__);
	} else {
		if (retry_count != 0) {
			pr_debug("[%s]: checksum mismatched, do retry (retry_count=%d)\n", __func__, retry_count);
			retry_count--;
			goto send_query_command_again;
		} else {
			pr_debug("[%s]: BSM_READ failed due to checksum mismatch\n", __func__);
			ret_code = -4;
		}
	}
	pr_debug("[%s]: retry_count=%d\n", __func__, retry_count);

exit:
	return ret_code;
//MK0126//MK0120-end

}
//MK0817-end


//MK0410-begin
int bsm_read_command_3_cmd_only(unsigned int tag, unsigned int sector, unsigned int lba,
			unsigned char *buf,	unsigned char *vbuf, unsigned char async,
			void *callback_func, unsigned char max_retry_count)
{
	struct hv_rw_cmd *pBsm;
	int ret_code=0;


	// Just clear 1/4 of the command buffer
	pBsm = (struct hv_rw_cmd *) rw_cmd_buff;
	memset(pBsm, 0, CMD_BUFFER_SIZE);

	/* Buffer to be used to fake-write to status registers */
	/* Debugging only - We don't need to do this if HW works correctly. */
//MK0519	clear_fake_mmls_buf();

	// Build command structure
	pBsm->command[0].cmd_field.cmd = MMLS_READ;
	pBsm->command[0].cmd_field.tag = get_command_tag();
	pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
	pBsm->sector[0] = sector;
	pBsm->sector[1] = sector;
	pBsm->lba[0] = lba;
	pBsm->lba[1] = lba;
	pBsm->dram_addr[0] = (unsigned int) (((unsigned long)buf - LRDIMM_SIZE - SYS_RESERVED_MEM_SIZE) >> 3);
	pBsm->dram_addr[1] = pBsm->dram_addr[0];
	pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
						pBsm->lba[0] ^ pBsm->dram_addr[0];
	pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
						pBsm->lba[1] ^ pBsm->dram_addr[1];

	pr_debug("[%s]: *buf=%#.16lx\n", __func__, (unsigned long)buf);
	pr_debug("[%s]: cmd[07]:[00] = 0x%.8X - 0x%.8X\n", __func__, pBsm->command[1].cmd_field_32b, pBsm->command[0].cmd_field_32b);
	pr_debug("[%s]: cmd[15]:[08] = 0x%.8X - 0x%.8X\n", __func__, pBsm->sector[1], pBsm->sector[0]);
	pr_debug("[%s]: cmd[23]:[16] = 0x%.8X - 0x%.8X\n", __func__, pBsm->lba[1], pBsm->lba[0]);
	pr_debug("[%s]: cmd[31]:[24] = 0x%.8X - 0x%.8X\n", __func__, pBsm->dram_addr[1], pBsm->dram_addr[0]);
	pr_debug("[%s]: cmd[39]:[32] = 0x%.8X - 0x%.8X\n", __func__, pBsm->checksum[1], pBsm->checksum[0]);
	pr_debug("[%s]: cmd[47]:[40] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[1], pBsm->reserve1[0]);
	pr_debug("[%s]: cmd[55]:[48] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[3], pBsm->reserve1[2]);
	pr_debug("[%s]: cmd[63]:[56] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[5], pBsm->reserve1[4]);

	/* prepare and send bsm read command */
	hv_write_command(0, GROUP_BSM, lba, pBsm);

	return ret_code;
}
//MK0410-end


//MK0213-begin
int bsm_read_command_3(unsigned int tag, unsigned int sector, unsigned int lba,
			unsigned char *buf,	unsigned char *vbuf, unsigned char async,
			void *callback_func, unsigned char max_retry_count)
{
	struct hv_rw_cmd *pBsm;
	int ret_code=0;
	unsigned char retry_count=max_retry_count, mmio_retry_count=max_retry_count;
	struct block_checksum_t fpga_checksum, fwb_checksum;
//MK0214-begin
	struct block_checksum_t fpga_checksum2, fwb_checksum2;
//MK0214-end
//MK0307-begin
	struct block_checksum_t slave_fpga_checksum, slave_fwb_checksum;
	struct block_checksum_t slave_fpga_checksum2, slave_fwb_checksum2;
//MK0418	int popcnt_m=0, popcnt_s=0, fpga_popcnt_m=0, fpga_popcnt_s=0;
	short int popcnt_m=0, popcnt_s=0, fpga_popcnt_m=0, fpga_popcnt_s=0;	//MK0418
//MK0307-end
	struct fpga_debug_info_t fpga_debug_info;
//MK0223-begin
	unsigned char fw_retry_count=get_bsm_rd_fw_max_retry_count();
	unsigned char qc_retry_count=get_bsm_rd_qc_max_retry_count();
//MK0223-end
//SJ0313-begin
	int bcom_reset_retry=0;
//SJ0313-end
//MK0616-begin
	unsigned int crc_retry_count=7;
//MK0616-end
/////////////////////////////////////

//MK0627-begin
	timestamp[0] = hv_nstimeofday();
//MK0627-end

	// Just clear 1/4 of the command buffer
	pBsm = (struct hv_rw_cmd *) rw_cmd_buff;
	memset(pBsm, 0, CMD_BUFFER_SIZE);

	/* Buffer to be used to fake-write to status registers */
	/* Debugging only - We don't need to do this if HW works correctly. */
//MK0519	clear_fake_mmls_buf();

//MK0215-begin
	mmio_retry_count = (unsigned int)get_bsm_rd_cmd_checksum_max_retry_count();
	retry_count = (unsigned int)get_bsm_rd_data_checksum_max_retry_count();
	pr_debug("[%s]: mmio_retry_count = %d  retry_count = %d\n", __func__, mmio_retry_count, retry_count);
//MK0215-end

	// Build command structure
	pBsm->command[0].cmd_field.cmd = MMLS_READ;
	pBsm->command[0].cmd_field.tag = get_command_tag();
	pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
	pBsm->sector[0] = sector;
	pBsm->sector[1] = sector;
	pBsm->lba[0] = lba;
	pBsm->lba[1] = lba;
	pBsm->dram_addr[0] = (unsigned int) (((unsigned long)buf - LRDIMM_SIZE - SYS_RESERVED_MEM_SIZE) >> 3);
	pBsm->dram_addr[1] = pBsm->dram_addr[0];
	pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
						pBsm->lba[0] ^ pBsm->dram_addr[0];
	pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
						pBsm->lba[1] ^ pBsm->dram_addr[1];

//MK0301-begin
	if ( bsm_wrt_send_dummy_command_enabled() && (pBsm->lba[0] == get_bsm_wrt_dummy_command_lba()) ) {
		pBsm->lba[0] = lba - 0x200;
		pBsm->lba[1] = lba - 0x200;
		pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
							pBsm->lba[0] ^ pBsm->dram_addr[0];
		pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
							pBsm->lba[1] ^ pBsm->dram_addr[1];

		hv_write_command(0, GROUP_BSM, lba, pBsm);

		pBsm->lba[0] = lba;
		pBsm->lba[1] = lba;
		pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
							pBsm->lba[0] ^ pBsm->dram_addr[0];
		pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
							pBsm->lba[1] ^ pBsm->dram_addr[1];
	}
//MK0301-end

send_cmd_again:

	pr_debug("[%s]: *buf=%#.16lx\n", __func__, (unsigned long)buf);
	pr_debug("[%s]: cmd[07]:[00] = 0x%.8X - 0x%.8X\n", __func__, pBsm->command[1].cmd_field_32b, pBsm->command[0].cmd_field_32b);
	pr_debug("[%s]: cmd[15]:[08] = 0x%.8X - 0x%.8X\n", __func__, pBsm->sector[1], pBsm->sector[0]);
	pr_debug("[%s]: cmd[23]:[16] = 0x%.8X - 0x%.8X\n", __func__, pBsm->lba[1], pBsm->lba[0]);
	pr_debug("[%s]: cmd[31]:[24] = 0x%.8X - 0x%.8X\n", __func__, pBsm->dram_addr[1], pBsm->dram_addr[0]);
	pr_debug("[%s]: cmd[39]:[32] = 0x%.8X - 0x%.8X\n", __func__, pBsm->checksum[1], pBsm->checksum[0]);
	pr_debug("[%s]: cmd[47]:[40] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[1], pBsm->reserve1[0]);
	pr_debug("[%s]: cmd[55]:[48] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[3], pBsm->reserve1[2]);
	pr_debug("[%s]: cmd[63]:[56] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[5], pBsm->reserve1[4]);

	/* prepare and send bsm read command */
	hv_write_command(0, GROUP_BSM, lba, pBsm);

	inc_command_tag();
//MK0413//MK0306-begin
//MK0413	/* Update the command buffer since the command tag was incremented */
//MK0413	pBsm->command[0].cmd_field.tag = get_command_tag();
//MK0413	pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
//MK0413	pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
//MK0413						pBsm->lba[0] ^ pBsm->dram_addr[0];
//MK0413	pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
//MK0413						pBsm->lba[1] ^ pBsm->dram_addr[1];
//MK0413//MK0306-end

//MK0223-begin
	if ( bsm_rd_cmd_checksum_verification_enabled() ) {
//MK0223-end

	/* ----- DEBUG: Get MMIO command checksum from FPGA ----- */
	hv_read_mmio_command_checksum(&fpga_debug_info);

	if (fpga_debug_info.mmio_cmd_checksum != pBsm->checksum[0]) {
		if (mmio_retry_count != 0) {
			pr_debug("[%s]: [DBG] MMIO checksum mismatched, do retry (mmio_retry_count=%d) FPGA checksum=0x%.8X, MMIO checksum=0x%.8X\n",
					__func__, mmio_retry_count, fpga_debug_info.mmio_cmd_checksum, pBsm->checksum[0]);
//			hv_display_mmio_command_slaveio();
			mmio_retry_count--;

//MK0413-begin
			/* Update the command buffer since the command tag was incremented */
			pBsm->command[0].cmd_field.tag = get_command_tag();
			pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
			pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
								pBsm->lba[0] ^ pBsm->dram_addr[0];
			pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
								pBsm->lba[1] ^ pBsm->dram_addr[1];
//MK0413-end

			goto send_cmd_again;
		} else {
			pr_debug("[%s]: [DBG] BSM_READ failed due to MMIO checksum mismatch (mmio_retry_count=%d), FPGA checksum=0x%.8X, MMIO checksum=0x%.8X\n",
					__func__, mmio_retry_count, fpga_debug_info.mmio_cmd_checksum, pBsm->checksum[0]);
			ret_code = -5;

//			hv_display_mmio_command_slaveio();
			goto exit;
		}
	}

	/* ----- DEBUG: show command checksum ----- */
	pr_debug("[%s]: [DBG] cmd checksum from FPGA (0x%.8X) %s cmd checksum sent by host (0x%.8X) (mmio_retry_count=%d)\n",
			__func__, fpga_debug_info.mmio_cmd_checksum,
			(fpga_debug_info.mmio_cmd_checksum == pBsm->checksum[0]) ? "==" : "!=", pBsm->checksum[0], mmio_retry_count);

//MK0223-begin
	} else {
		pr_debug("[%s]: bsm_rd_cmd_checksum_verification disabled\n", __func__);
	}
//MK0223-end


//MK0413-begin
	/* Update the command buffer since the command tag was incremented */
	pBsm->command[0].cmd_field.tag = get_command_tag();
	pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
	pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
						pBsm->lba[0] ^ pBsm->dram_addr[0];
	pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
						pBsm->lba[1] ^ pBsm->dram_addr[1];
//MK0413-end


	/* BSM_READ command was successfully sent to FPGA */
	if (async == 0) {		/* synchronous mode */

send_query_command_again:

//MK0223-begin
	if ( bsm_rd_skip_query_command_enabled() ) {
		pr_debug("[%s]: [DBG] Skipping query command and query command status check\n", __func__);
	} else {

//MK0405-begin
//MK0605		mdelay(get_user_defined_delay_ms(1));
//MK20170712		udelay(get_user_defined_delay_us(2));	//MK0605
//MK0405-end

//MK0605-begin
send_qcmd_again:
//MK0605-end
//MK0627-begin
	timestamp[1] = hv_nstimeofday();
//MK0627-end

		//SJ0301
//MK20170712		bsm_query_command(query_tag,(unsigned int)(get_command_tag()-1), GROUP_BSM, lba);
//MK20170712		query_tag = (query_tag+1)%128 + 128;

//MK0224		if (wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba) != 0) {
//MK0224-begin
//MK0616		if ( wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba, get_bsm_rd_qc_status_delay()) != 0 ) {
//MK0628		if ( (ret_code = wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba, get_bsm_rd_qc_status_delay())) != 0 ) {	//MK0616
//MK0628-begin
//MK20170712		if ( (ret_code = wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba, get_bsm_rd_qc_status_delay(), MMLS_READ)) != 0 ) {
//MK20170712-begin
	if ( (ret_code = wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba, get_user_defined_delay_us(2), MMLS_READ)) != 0 ) {
//MK20170712-end
//MK0628-end
//MK0224-end

//MK0616-begin
			/* Check CRC error */
			if ( ret_code == -2 ) {
				if ( crc_retry_count != 0 ) {
					crc_retry_count--;		// Count down checksum retry count
					ret_code = 0;			// Clear error code
					goto send_cmd_again;	// Send BSM_READ command again
				} else {
					ret_code = -8;
					pr_debug("[%s]: BSM_READ failed due to CRC error\n", __func__);
					goto exit;
				}
			}
//MK0616-end

//SJ0313//MK0301-begin
//SJ0313			/* For debugging only requested by SJ */
//SJ0313			if ( bsm_rd_do_dummy_read_enabled() ) {
//SJ0313//MK0306				memcpy((void *)hv_cmd_local_buffer, (void *)get_bsm_rd_dummy_read_addr(), 64);
//SJ0313				memcpy((void *)hv_cmd_local_buffer, ECC_OFF+0x2000, 64);	//MK0306
//SJ0313			}
//SJ0313//MK0301-end

			if ( bsm_rd_qc_retry_enabled() ) {
				if ( qc_retry_count == 0 ) {
					pr_warn("[%s]: CMD_DONE not found (qc_retry_count=%d) (LBA=0x%.8X)\n", __func__, qc_retry_count, pBsm->lba[0]);
					ret_code = -6;
					goto exit;
				} else {
					pr_debug("[%s]: CMD_DONE not found, do retry (qc_retry_count=%d) (LBA=0x%.8X)\n", __func__, qc_retry_count, pBsm->lba[0]);
					qc_retry_count--;
//MK0605					hv_reset_internal_state_machine();
//MK0605					goto send_cmd_again;
					goto send_qcmd_again;	//MK0605
				}
			} else {
				pr_warn("[%s]: CMD_DONE not found, query command retry disabled (LBA=0x%.8X)\n", __func__, pBsm->lba[0]);
				ret_code = -5;
				goto exit;
			}
		}
	}

		/* For debugging only */
//MK0605		hv_delay_us(get_user_defined_delay());

//MK0627-begin
	timestamp[2] = hv_nstimeofday();
//MK0627-end
		/* Do fake-write */
		if (hv_fake_operation(FAKE_WRITE, sector, lba, GROUP_BSM, vbuf) != 0) {
			/* Display the content of fake write buffer */
//MK0301			display_buffer((unsigned long *)vbuf, 4096/8, 0x00000000FFFFFFFF);
			display_buffer((unsigned long *)vbuf, 4096/8, get_pattern_mask());	//MK0301
			if ( bsm_rd_fw_retry_enabled() ) {
				if ( fw_retry_count == 0 ) {
					pr_warn("[%s]: fake-write failed (fw_retry_count=%d) (LBA=0x%.8X)\n", __func__, fw_retry_count, pBsm->lba[0]);
					ret_code = -4;
					goto exit;
				} else {
					pr_debug("[%s]: fake-write failed, do retry (fw_retry_count=%d) (LBA=0x%.8X)\n", __func__, fw_retry_count, pBsm->lba[0]);
					fw_retry_count--;
					hv_reset_internal_state_machine();
					goto send_cmd_again;
				}
			} else {
				pr_warn("[%s]: fake-write failed, fake-write retry disabled (LBA=0x%.8X)\n", __func__, pBsm->lba[0]);
				ret_code = -3;
				goto exit;
			}
		}
		/* Display the content of fake write buffer */
//MK0301		display_buffer((unsigned long *)vbuf, 4096/8, 0x00000000FFFFFFFF);
		display_buffer((unsigned long *)vbuf, 4096/8, get_pattern_mask());	//MK0301
//MK0223-end

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

//MK0627-begin
	timestamp[3] = hv_nstimeofday();
//MK0627-end
//MK0223-begin
	if ( bsm_rd_data_checksum_verification_enabled() ) {
//MK0223-end

	/* ----- DEBUG: Compute checksum of data in fake-write buffer and get checksum from FPGA ----- */
	calculate_checksum((void*)vbuf, &fwb_checksum);
	hv_read_fake_buffer_checksum(0, &fpga_checksum);
	pr_debug("[%s]: [DBG] fwb_checksum  = 0x%.2x%.2x%.2x%.2x  fpga_checksum = 0x%.2x%.2x%.2x%.2x\n",
			__func__, fwb_checksum.sub_cs[3], fwb_checksum.sub_cs[2], fwb_checksum.sub_cs[1], fwb_checksum.sub_cs[0],
			fpga_checksum.sub_cs[3], fpga_checksum.sub_cs[2], fpga_checksum.sub_cs[1], fpga_checksum.sub_cs[0]);

//MK0214-begin
	/* ----- DEBUG: Compute the 2nd checksum of data in fake-write buffer and get the 2nd checksum from FPGA ----- */
	calculate_checksum_2((void*)vbuf, &fwb_checksum2);
	hv_read_fake_buffer_checksum_2(0, &fpga_checksum2);
	pr_debug("[%s]: [DBG] fwb_checksum2  = 0x%.2x%.2x%.2x%.2x  fpga_checksum2 = 0x%.2x%.2x%.2x%.2x\n",
			__func__, fwb_checksum2.sub_cs[3], fwb_checksum2.sub_cs[2], fwb_checksum2.sub_cs[1], fwb_checksum2.sub_cs[0],
			fpga_checksum2.sub_cs[3], fpga_checksum2.sub_cs[2], fpga_checksum2.sub_cs[1], fpga_checksum2.sub_cs[0]);
//MK0214-end

	/* ----- DEBUG: verify data checksum & retry if not same ----- */
#if 0	//MK0307
//	memcpy((void*)&fpga_checksum, (void*)&fwb_checksum, sizeof(fpga_checksum));
//	if ( memcmp((void *)&fpga_checksum, (void*)&fwb_checksum, 4) != 0) {
//MK0214	if ( (fpga_checksum.sub_cs[0] == fwb_checksum.sub_cs[0]) &&
//MK0214			(fpga_checksum.sub_cs[1] == fwb_checksum.sub_cs[1]) &&
//MK0214			(fpga_checksum.sub_cs[2] == fwb_checksum.sub_cs[2]) &&
//MK0214			(fpga_checksum.sub_cs[3] == fwb_checksum.sub_cs[3]) ) {
//MK0214-begin
	if ( (fpga_checksum.sub_cs[0] == fwb_checksum.sub_cs[0]) &&
			(fpga_checksum.sub_cs[1] == fwb_checksum.sub_cs[1]) &&
			(fpga_checksum.sub_cs[2] == fwb_checksum.sub_cs[2]) &&
			(fpga_checksum.sub_cs[3] == fwb_checksum.sub_cs[3]) &&
			(fpga_checksum2.sub_cs[0] == fwb_checksum2.sub_cs[0]) &&
			(fpga_checksum2.sub_cs[1] == fwb_checksum2.sub_cs[1]) &&
			(fpga_checksum2.sub_cs[2] == fwb_checksum2.sub_cs[2]) &&
			(fpga_checksum2.sub_cs[3] == fwb_checksum2.sub_cs[3]) ) {
//MK0214-end
		pr_debug("[%s]: [DBG] fpga_checksum & fwb_checksum are the same!\n", __func__);
	} else {
		if (retry_count != 0) {
			pr_debug("[%s]: [DBG] checksum mismatched, do retry (retry_count=%d) (LBA=0x%.8X)\n", __func__, retry_count, pBsm->lba[0]);
			retry_count--;
			goto send_query_command_again;
		} else {
			pr_debug("[%s]: [DBG] BSM_READ failed due to checksum mismatch (LBA=0x%.8X)\n", __func__, pBsm->lba[0]);
			ret_code = -4;
		}
	}
	pr_debug("[%s]: [DBG] retry_count=%d\n", __func__, retry_count);
#endif	//MK0307
//MK0307-begin
	/* ----- DEBUG: Compute popcnt for the master FPGA ----- */
	if ( bsm_rd_popcnt_enabled() )
	{
//MK0418		popcnt_m = calculate_popcnt((void*)vbuf, 4096, 0);
		popcnt_m = (short int) calculate_popcnt((void*)vbuf, 4096, 0);	//MK0418
		fpga_popcnt_m = hv_read_fake_buffer_popcnt(0);
		pr_debug("[%s]: [DBG] master_popcnt (calculated) = 0x%.8X  master_popcnt (FPGA) = 0x%.8X\n",
				__func__, popcnt_m,	fpga_popcnt_m);
	}

	if ( slave_data_cs_enabled() ) {
		/* ----- DEBUG: Compute checksum of data in slave side of fake-write buffer and get checksum from Slave FPGA ----- */
		calculate_checksum((void*)(vbuf+4), &slave_fwb_checksum);
		hv_read_fake_buffer_checksum(2, &slave_fpga_checksum);
		pr_debug("[%s]: [DBG] slave_fwb_checksum  = 0x%.2x%.2x%.2x%.2x  slave_fpga_checksum = 0x%.2x%.2x%.2x%.2x\n",
				__func__, slave_fwb_checksum.sub_cs[3], slave_fwb_checksum.sub_cs[2], slave_fwb_checksum.sub_cs[1], slave_fwb_checksum.sub_cs[0],
				slave_fpga_checksum.sub_cs[3], slave_fpga_checksum.sub_cs[2], slave_fpga_checksum.sub_cs[1], slave_fpga_checksum.sub_cs[0]);

		/* ----- DEBUG: Compute the 2nd checksum of data in slave side of fake-write buffer and get the 2nd checksum from Slave FPGA ----- */
		calculate_checksum_2((void*)(vbuf+4), &slave_fwb_checksum2);
		hv_read_fake_buffer_checksum_2(2, &slave_fpga_checksum2);
		pr_debug("[%s]: [DBG] slave_fwb_checksum2  = 0x%.2x%.2x%.2x%.2x  slave_fpga_checksum2 = 0x%.2x%.2x%.2x%.2x\n",
				__func__, slave_fwb_checksum2.sub_cs[3], slave_fwb_checksum2.sub_cs[2], slave_fwb_checksum2.sub_cs[1], slave_fwb_checksum2.sub_cs[0],
				slave_fpga_checksum2.sub_cs[3], slave_fpga_checksum2.sub_cs[2], slave_fpga_checksum2.sub_cs[1], slave_fpga_checksum2.sub_cs[0]);

		/* ----- DEBUG: Compute & retrieve popcnt for the slave FPGA ----- */
		if ( bsm_rd_popcnt_enabled() ) {
//MK0418			popcnt_s = calculate_popcnt((void*)vbuf, 4096, 1);
			popcnt_s = (short int) calculate_popcnt((void*)vbuf, 4096, 1);	//MK0418
			fpga_popcnt_s = hv_read_fake_buffer_popcnt(2);
			pr_debug("[%s]: [DBG] slave_popcnt (calculated) = 0x%.8X  slave_popcnt (FPGA) = 0x%.8X\n",
					__func__, popcnt_s,	fpga_popcnt_s);
		}

		if ( (data_cs_comp(&fpga_checksum, &fwb_checksum) == 0) &&
				(data_cs_comp(&fpga_checksum2, &fwb_checksum2) == 0) &&
				(popcnt_m == fpga_popcnt_m) &&
				(data_cs_comp(&slave_fpga_checksum, &slave_fwb_checksum) == 0) &&
				(data_cs_comp(&slave_fpga_checksum2, &slave_fwb_checksum2) == 0) &&
				(popcnt_s == fpga_popcnt_s) ) {
			pr_debug("[%s]: [DBG] fpga_checksum & fwb_checksum are the same!\n", __func__);
		} else {
//SJ0313			if (retry_count != 0) {
//SJ0313				pr_debug("[%s]: [DBG] checksum mismatched, do retry (retry_count=%d) (LBA=0x%.8X)\n", __func__, retry_count, pBsm->lba[0]);
//SJ0313				retry_count--;
//SJ0313				goto send_query_command_again;
//SJ0313			} else {
//SJ0313				pr_debug("[%s]: [DBG] BSM_READ failed due to checksum mismatch (LBA=0x%.8X)\n", __func__, pBsm->lba[0]);
//SJ0313				ret_code = -4;
//SJ0313			}
//SJ0313-begin
			if ( bsm_rd_do_dummy_read_enabled() ) {
				memcpy((void *)hv_cmd_local_buffer, ECC_OFF+0x2000, 64);
			}
			pr_debug("[%s]: [DBG] checksum mismatched, do retry (retry_count=%d) (LBA=0x%.8X)", __func__, retry_count, pBsm->lba[0]);
			pr_debug(" Master XOR: 0x%.8X,0x%.8X, Slave XOR: 0x%.8X,0x%.8X,  popcnt: 0x%.8X,0x%.8X, 0x%.8X,0x%.8X",
					fwb_checksum.cs^fpga_checksum.cs, fwb_checksum2.cs^fpga_checksum2.cs,
					slave_fwb_checksum.cs^slave_fwb_checksum.cs, slave_fwb_checksum2.cs^slave_fpga_checksum2.cs,
					popcnt_m, fpga_popcnt_m, popcnt_s, fpga_popcnt_s);
			if (retry_count != 0) {
				retry_count--;
				if ( (retry_count==3) && (bcom_reset_retry) ) {
					bcom_reset_retry = 0;
					hv_reset_bcom_control();
					goto send_cmd_again;
				} else {
					goto send_query_command_again;
				}
			} else {
				pr_debug("[%s]: [DBG] BSM_READ failed due to checksum mismatch (LBA=0x%.8X)\n", __func__, pBsm->lba[0]);
				ret_code = -4;
			}
//SJ0313-end
		}
	} else {
		if ( (data_cs_comp(&fpga_checksum, &fwb_checksum) == 0) &&
				(data_cs_comp(&fpga_checksum2, &fwb_checksum2) == 0) &&
				(popcnt_m == fpga_popcnt_m) ) {
			pr_debug("[%s]: [DBG] fpga_checksum & fwb_checksum are the same!\n", __func__);
		} else {
//SJ0313			if (retry_count != 0) {
//SJ0313				pr_debug("[%s]: [DBG] checksum mismatched, do retry (retry_count=%d) (LBA=0x%.8X)\n", __func__, retry_count, pBsm->lba[0]);
//SJ0313				retry_count--;
//SJ0313				goto send_query_command_again;
//SJ0313			} else {
//SJ0313				pr_debug("[%s]: [DBG] BSM_READ failed due to checksum mismatch (LBA=0x%.8X)\n", __func__, pBsm->lba[0]);
//SJ0313				ret_code = -4;
//SJ0313			}
//SJ0313-begin
			pr_debug("[%s]: [DBG] checksum mismatched, do retry (retry_count=%d) (LBA=0x%.8X)", __func__, retry_count, pBsm->lba[0]);
			pr_debug(" Master XOR: 0x%.8X,0x%.8X, Slave XOR: ------, ------,  popcnt: 0x%.8X,0x%.8X, XOR:0x%.8X  Slave ----, ----",
					fwb_checksum.cs^fpga_checksum.cs, fwb_checksum2.cs^fpga_checksum2.cs,
					popcnt_m, fpga_popcnt_m, popcnt_m^fpga_popcnt_m);
			if (retry_count != 0) {
				retry_count--;
				if ( (retry_count==3) && (bcom_reset_retry) ) {
					bcom_reset_retry=0;
					hv_reset_bcom_control();
					goto send_cmd_again;
				} else {
					goto send_query_command_again;
				}
			} else {
				pr_debug("[%s]: [DBG] BSM_READ failed due to checksum mismatch (LBA=0x%.8X)\n", __func__, pBsm->lba[0]);
				ret_code = -4;
			}
//SJ0313-end
		}
	}
	pr_debug("[%s]: [DBG] retry_count=%d\n", __func__, retry_count);
//MK0307-end

//MK0223-begin
	} else {
		pr_debug("[%s]: bsm_rd_data_checksum_verification disabled (LBA=0x%.8X)\n", __func__, pBsm->lba[0]);
	}
//MK0223-end

//MK0627-begin
	timestamp[4] = hv_nstimeofday();
	pr_debug("[%s]: =ET= (retry_count=%d)  LBA=0x%.8X TOTAL:%.8ldns CMD:%.8ldns QC:%.8ldns FAKE:%.8ldns CS:%.8ldns\n",
			__func__, retry_count, pBsm->lba[0], timestamp[4]-timestamp[0],
			timestamp[1]-timestamp[0], timestamp[2]-timestamp[1],
			timestamp[3]-timestamp[2], timestamp[4]-timestamp[3]);
//MK0627-end

exit:
	return ret_code;

}
//MK0213-end

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
			unsigned int sector,
			unsigned int lba,
			unsigned char *buf,
			unsigned char async,
			void *callback_func)
{
#if 0	//MK - this is the original code
	struct HV_CMD_BSM_t *pBsm;

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
#endif	//MK
//MK-begin
	struct hv_rw_cmd *pBsm;

	/*
	 * Just clear 1/4 of the command buffer since we are targeting
	 * for one HVDIMM for now.
	 */
	pBsm = (struct hv_rw_cmd *) rw_cmd_buff;
	memset(pBsm, 0, CMD_BUFFER_SIZE);

//MK1018-begin
	/* Buffer to be used to fake-write to status registers */
	/* Debugging only - We don't need to do this if HW works correctly. */
//MK0519	clear_fake_mmls_buf();
//MK1018-end

	/* Build command structure */
	pBsm->command[0].cmd_field.cmd = MMLS_WRITE;
//MK0209	pBsm->command[0].cmd_field.tag = (unsigned char) tag;
//MK0209-begin
	pBsm->command[0].cmd_field.tag = get_command_tag();
//MK0209-end
	pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
	pBsm->sector[0] = sector;
	pBsm->sector[1] = sector;
	pBsm->lba[0] = lba;
	pBsm->lba[1] = lba;
//MK0727	pBsm->dram_addr[0] = (unsigned int) hv_get_dram_addr(GROUP_BSM, (void*)buf);
//MK0727-begin
//MK1024	pBsm->dram_addr[0] = (unsigned int) ((unsigned long) (FAKE_BUFF_SYS_PADDR - LRDIMM_SIZE - SYS_RESERVED_MEM_SIZE) >> 3);
//MK1024-begin
//MK1118	pBsm->dram_addr[0] = (unsigned int) ((unsigned long) (MMLS_WRITE_BUFF_SYS_PADDR - LRDIMM_SIZE - SYS_RESERVED_MEM_SIZE) >> 3);
//MK1118-begin
	pBsm->dram_addr[0] = (unsigned int) (((unsigned long)buf - LRDIMM_SIZE - SYS_RESERVED_MEM_SIZE) >> 3);
//MK1118-end
//MK1024-end
//MK0727-end
	pBsm->dram_addr[1] = pBsm->dram_addr[0];
	pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
						pBsm->lba[0] ^ pBsm->dram_addr[0];
	pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
						pBsm->lba[1] ^ pBsm->dram_addr[1];

	pr_debug("[%s]: *buf=%#.16lx\n", __func__, (unsigned long)buf);
	pr_debug("[%s]: cmd[07]:[00] = 0x%.8X - 0x%.8X\n", __func__, pBsm->command[1].cmd_field_32b, pBsm->command[0].cmd_field_32b);
	pr_debug("[%s]: cmd[15]:[08] = 0x%.8X - 0x%.8X\n", __func__, pBsm->sector[1], pBsm->sector[0]);
	pr_debug("[%s]: cmd[23]:[16] = 0x%.8X - 0x%.8X\n", __func__, pBsm->lba[1], pBsm->lba[0]);
	pr_debug("[%s]: cmd[31]:[24] = 0x%.8X - 0x%.8X\n", __func__, pBsm->dram_addr[1], pBsm->dram_addr[0]);
	pr_debug("[%s]: cmd[39]:[32] = 0x%.8X - 0x%.8X\n", __func__, pBsm->checksum[1], pBsm->checksum[0]);
	pr_debug("[%s]: cmd[47]:[40] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[1], pBsm->reserve1[0]);
	pr_debug("[%s]: cmd[55]:[48] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[3], pBsm->reserve1[2]);
	pr_debug("[%s]: cmd[63]:[56] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[5], pBsm->reserve1[4]);

	/* Send the command to HVDIMM */
	hv_write_command(0, GROUP_BSM, lba, pBsm);

//MK0209-begin
	inc_command_tag();
//MK0209-end

	return 0;
}

//MK0724-begin
//MK1118int bsm_write_command_2(unsigned int tag, unsigned int sector, unsigned int lba,
//MK1118			unsigned char *buf,	unsigned char async, void *callback_func)
//MK0201//MK1118-begin
//MK0201int bsm_write_command_2(unsigned int tag, unsigned int sector, unsigned int lba,
//MK0201			unsigned char *buf,	unsigned char *vbuf, unsigned char async, void *callback_func)
//MK0201//MK1118-end
//MK0201-begin
int bsm_write_command_2(unsigned int tag, unsigned int sector, unsigned int lba,
			unsigned char *buf,	unsigned char *vbuf, unsigned char async,
			void *callback_func, unsigned char max_retry_count)
//MK0201-end
{
#ifndef SW_SIM
//MK0729	unsigned int block_count;
#endif

//MK0126-begin
	int ret_code=0;
//MK0201	unsigned int retry_count=1;
//MK0201-begin
	unsigned int retry_count=max_retry_count;
//MK0201-end
	struct block_checksum_t frb_checksum, fpga_checksum;
//MK0126-end

//MK1102-begin this code is only for helping Hao debug presight-read.
	/* Do fake-read */
//MK1118	if (hv_fake_operation(FAKE_READ, sector, lba, GROUP_BSM, buf) != 0)
//MK1118		return -1;

//MK0126-begin
do_fake_read_again:
//MK0126-end

//MK1118-begin
	if (hv_fake_operation(FAKE_READ, sector, lba, GROUP_BSM, vbuf) != 0)
//MK0126		return -1;
//MK0126-begin
	{
		pr_warn("[%s]: fake-write failed\n", __func__);
		ret_code = -3;
		goto exit;
	}
//MK0126-end
//MK1118-end
//MK0117	return 0;
//MK1102-end

	if (async == 0) {		/* synchronous mode */
//MK0209		if (wait_for_cmd_done(tag, GROUP_BSM, lba) != 0)
//MK0209-begin
//MK0224		if (wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba) != 0)
//MK0224-begin
//MK0628		if ( wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba, get_bsm_wrt_qc_status_delay()) != 0 )
//MK0628-begin
		if ( wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba, get_bsm_wrt_qc_status_delay(), MMLS_WRITE) != 0 )
//MK0628-end
//MK0224-end
//MK0209-end
		{
//MK0126			pr_warn("[%s]: Query Command Status timeout\n", __func__);
//MK0126			return -1;
//MK0126-begin
			pr_warn("[%s]: CMD_DONE not found\n", __func__);
			ret_code = -2;
			goto exit;
//MK0126-end
		}

		/* Send termination signal to HVDIMM */
//MK0117		hv_write_termination (0, GROUP_BSM, lba, NULL);
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
		pr_err("asynchronous mode was assigned with wrong value\n");
		return -1;
	}

//MK0126-begin
//	pr_debug("[%s]: calling hv_read_checksum from bsm_write_command_2\n", __func__);
	/* Get checksum from FPGA */
	hv_read_fake_buffer_checksum(1, &fpga_checksum);
	pr_debug("[%s]: fpga_checksum = 0x%.2x%.2x%.2x%.2x\n",
			__func__, fpga_checksum.sub_cs[3], fpga_checksum.sub_cs[2], fpga_checksum.sub_cs[1], fpga_checksum.sub_cs[0]);

	/* Compute checksum of data in fake-read buffer */
//MK0214	memset((void*)&frb_checksum, 0, sizeof(struct block_checksum_t));
	calculate_checksum((void*)vbuf, &frb_checksum);
	pr_debug("[%s]: frb_checksum  = 0x%.2x%.2x%.2x%.2x\n",
			__func__, frb_checksum.sub_cs[3], frb_checksum.sub_cs[2], frb_checksum.sub_cs[1], frb_checksum.sub_cs[0]);

//	if ( memcmp((void *)&fpga_checksum, (void*)&frb_checksum, 4) != 0) {
	if ( (fpga_checksum.sub_cs[0] == frb_checksum.sub_cs[0]) &&
			(fpga_checksum.sub_cs[1] == frb_checksum.sub_cs[1]) &&
			(fpga_checksum.sub_cs[2] == frb_checksum.sub_cs[2]) &&
			(fpga_checksum.sub_cs[3] == frb_checksum.sub_cs[3]) ) {
		pr_debug("[%s]: fpga_checksum & frb_checksum are the same! (retry_count=%d)\n", __func__, retry_count);
	} else {
		if (retry_count != 0) {
			pr_debug("[%s]: checksum mismatched, do retry (retry_count=%d)\n", __func__, retry_count);
			retry_count--;
			goto do_fake_read_again;
		} else {
			pr_debug("[%s]: BSM_WRITE failed due to checksum mismatch (retry_count=%d)\n", __func__, retry_count);
			ret_code = -4;
		}
	}

exit:
	return ret_code;
//MK0126-end

//MK0126	return 0;
}
//MK0724-end

//MK0201-begin
int bsm_write_command_3(unsigned int tag, unsigned int sector, unsigned int lba,
		unsigned char *buf, unsigned char *vbuf, unsigned char async,
		void *callback_func, unsigned char max_retry_count)
{
	struct hv_rw_cmd *pBsm;
	int ret_code=0;
	unsigned int retry_count=max_retry_count, mmio_retry_count=max_retry_count;
	struct block_checksum_t frb_checksum, fpga_checksum;
//MK0307-begin
//	struct block_checksum_t frb_checksum2, fpga_checksum2;
	struct block_checksum_t slave_frb_checksum, slave_fpga_checksum;
//	struct block_checksum_t slave_frb_checksum2, slave_fpga_checksum2;
//MK0418	int popcnt_m=0, popcnt_s=0, fpga_popcnt_m=0, fpga_popcnt_s=0;
	short int popcnt_m=0, popcnt_s=0, fpga_popcnt_m=0, fpga_popcnt_s=0;	//MK0418
//MK0307-end
//MK0202-begin
	struct fpga_debug_info_t fpga_debug_info;
//MK0202-end
//MK0223-begin
	unsigned char fr_retry_count=get_bsm_wrt_fr_max_retry_count();
	unsigned char qc_retry_count=get_bsm_wrt_qc_max_retry_count();
//MK0223-end
//SJ0313-begin
	int bcom_reset_retry=0;
//SJ0313-end
//MK0616-begin
	unsigned int crc_retry_count=7;
//MK0616-end

//MK0627-begin
	timestamp[0] = hv_nstimeofday();
//MK0627-end
	/*
	 * Just clear 1/4 of the command buffer since we are targeting
	 * for one HVDIMM for now.
	 */
	pBsm = (struct hv_rw_cmd *) rw_cmd_buff;
	memset(pBsm, 0, CMD_BUFFER_SIZE);

	/* Buffer to be used to fake-write to status registers */
	/* Debugging only - We don't need to do this if HW works correctly. */
//MK0519	clear_fake_mmls_buf();

//MK0215-begin
	mmio_retry_count = (unsigned int)get_bsm_wrt_cmd_checksum_max_retry_count();
	retry_count = (unsigned int)get_bsm_wrt_data_checksum_max_retry_count();
	pr_debug("[%s]: mmio_retry_count = %d  retry_count = %d\n", __func__, mmio_retry_count, retry_count);
//MK0215-end

	/* Build command structure */
	pBsm->command[0].cmd_field.cmd = MMLS_WRITE;
//MK0209	pBsm->command[0].cmd_field.tag = (unsigned char) tag;
//MK0209-begin
	pBsm->command[0].cmd_field.tag = get_command_tag();
//MK0209-end
	pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
	pBsm->sector[0] = sector;
	pBsm->sector[1] = sector;
	pBsm->lba[0] = lba;
	pBsm->lba[1] = lba;
	pBsm->dram_addr[0] = (unsigned int) (((unsigned long)buf - LRDIMM_SIZE - SYS_RESERVED_MEM_SIZE) >> 3);
	pBsm->dram_addr[1] = pBsm->dram_addr[0];
	pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
						pBsm->lba[0] ^ pBsm->dram_addr[0];
	pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
						pBsm->lba[1] ^ pBsm->dram_addr[1];

//MK0301-begin
	if ( bsm_wrt_send_dummy_command_enabled() && (pBsm->lba[0] == get_bsm_wrt_dummy_command_lba()) ) {
		pBsm->lba[0] = lba - 0x200;
		pBsm->lba[1] = lba - 0x200;
		pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
							pBsm->lba[0] ^ pBsm->dram_addr[0];
		pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
							pBsm->lba[1] ^ pBsm->dram_addr[1];

		hv_write_command(0, GROUP_BSM, lba, pBsm);

		pBsm->lba[0] = lba;
		pBsm->lba[1] = lba;
		pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
							pBsm->lba[0] ^ pBsm->dram_addr[0];
		pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
							pBsm->lba[1] ^ pBsm->dram_addr[1];
	}
//MK0301-end

//MK0207-begin
send_cmd_again:
//MK0207-end

	pr_debug("[%s]: *buf=%#.16lx\n", __func__, (unsigned long)buf);
	pr_debug("[%s]: cmd[07]:[00] = 0x%.8X - 0x%.8X\n", __func__, pBsm->command[1].cmd_field_32b, pBsm->command[0].cmd_field_32b);
	pr_debug("[%s]: cmd[15]:[08] = 0x%.8X - 0x%.8X\n", __func__, pBsm->sector[1], pBsm->sector[0]);
	pr_debug("[%s]: cmd[23]:[16] = 0x%.8X - 0x%.8X\n", __func__, pBsm->lba[1], pBsm->lba[0]);
	pr_debug("[%s]: cmd[31]:[24] = 0x%.8X - 0x%.8X\n", __func__, pBsm->dram_addr[1], pBsm->dram_addr[0]);
	pr_debug("[%s]: cmd[39]:[32] = 0x%.8X - 0x%.8X\n", __func__, pBsm->checksum[1], pBsm->checksum[0]);
	pr_debug("[%s]: cmd[47]:[40] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[1], pBsm->reserve1[0]);
	pr_debug("[%s]: cmd[55]:[48] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[3], pBsm->reserve1[2]);
	pr_debug("[%s]: cmd[63]:[56] = 0x%.8X - 0x%.8X\n", __func__, pBsm->reserve1[5], pBsm->reserve1[4]);

	/* Send the command to HVDIMM */
	hv_write_command(0, GROUP_BSM, lba, pBsm);

//MK0209-begin
	inc_command_tag();
//MK0209-end
//MK0413//MK0306-begin
//MK0413	/* Update the command buffer since the command tag was incremented */
//MK0413	pBsm->command[0].cmd_field.tag = get_command_tag();
//MK0413	pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
//MK0413	pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
//MK0413						pBsm->lba[0] ^ pBsm->dram_addr[0];
//MK0413	pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
//MK0413						pBsm->lba[1] ^ pBsm->dram_addr[1];
//MK0413//MK0306-end

//MK0223-begin
	if ( bsm_wrt_cmd_checksum_verification_enabled() ) {
//MK0223-end

//MK0207-begin
	/* Get MMIO command checksum from FPGA */
	hv_read_mmio_command_checksum(&fpga_debug_info);

	if (fpga_debug_info.mmio_cmd_checksum != pBsm->checksum[0]) {
		if (mmio_retry_count != 0) {
			pr_debug("[%s]: [DBG] MMIO checksum mismatched, do retry (mmio_retry_count=%d) FPGA checksum=0x%.8X, MMIO checksum=0x%.8X\n",
					__func__, mmio_retry_count, fpga_debug_info.mmio_cmd_checksum, pBsm->checksum[0]);

//			hv_display_mmio_command_slaveio();
			mmio_retry_count--;

//MK0413-begin
			/* Update the command buffer since the command tag was incremented */
			pBsm->command[0].cmd_field.tag = get_command_tag();
			pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
			pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
								pBsm->lba[0] ^ pBsm->dram_addr[0];
			pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
								pBsm->lba[1] ^ pBsm->dram_addr[1];
//MK0413-end

			goto send_cmd_again;
		} else {
			pr_debug("[%s]: [DBG] BSM_WRITE failed due to MMIO checksum mismatch (mmio_retry_count=%d), FPGA checksum=0x%.8X, MMIO checksum=0x%.8X\n",
					__func__, mmio_retry_count, fpga_debug_info.mmio_cmd_checksum, pBsm->checksum[0]);
			ret_code = -2;

//			hv_display_mmio_command_slaveio();
			goto exit;
		}
	}

	pr_debug("[%s]: [DBG] cmd checksum from FPGA (0x%.8X) %s cmd checksum sent by host (0x%.8X) (retry_count=%d)\n",
			__func__, fpga_debug_info.mmio_cmd_checksum,
			(fpga_debug_info.mmio_cmd_checksum == pBsm->checksum[0]) ? "==" : "!=", pBsm->checksum[0], retry_count);
//	hv_display_mmio_command_slaveio();

//MK0207-end

//MK0223-begin
	} else {
		pr_debug("[%s]: bsm_wrt_cmd_checksum_verification disabled\n", __func__);
	}
//MK0223-end

//MK0413-begin
	/* Update the command buffer since the command tag was incremented */
	pBsm->command[0].cmd_field.tag = get_command_tag();
	pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
	pBsm->checksum[0] = pBsm->command[0].cmd_field_32b ^ pBsm->sector[0] ^
						pBsm->lba[0] ^ pBsm->dram_addr[0];
	pBsm->checksum[1] = pBsm->command[1].cmd_field_32b ^ pBsm->sector[1] ^
						pBsm->lba[1] ^ pBsm->dram_addr[1];
//MK0413-end

//*****************************************************************************
// The following code came from bsm_write_command_2()
//*****************************************************************************
//MK0627-begin
	timestamp[1] = hv_nstimeofday();
//MK0627-end

	if (hv_fake_operation(FAKE_READ, sector, lba, GROUP_BSM, vbuf) != 0)
	{
//MK0223		pr_warn("[%s]: fake-read failed\n", __func__);
//MK0223		ret_code = -3;
//MK0223		goto exit;
//MK0223-begin
		if ( bsm_wrt_fr_retry_enabled() ) {
			if ( fr_retry_count == 0 ) {
				pr_warn("[%s]: fake-read failed (fr_retry_count=%d) (LBA=0x%.8X)\n", __func__, fr_retry_count, pBsm->lba[0]);
				ret_code = -4;
				goto exit;
			} else {
				pr_debug("[%s]: fake-read failed, do retry (fr_retry_count=%d) (LBA=0x%.8X)\n", __func__, fr_retry_count, pBsm->lba[0]);
				fr_retry_count--;
				hv_reset_internal_state_machine();
				goto send_cmd_again;
			}
		} else {
			pr_warn("[%s]: fake-read failed, fake-read retry disabled (LBA=0x%.8X)\n", __func__, pBsm->lba[0]);
			ret_code = -3;
			goto exit;
		}
//MK0223-end
	}

//MK0223-begin
	if ( bsm_wrt_skip_query_command_enabled() ) {
		pr_debug("[%s]: [DBG] Skipping query command and query command status check\n", __func__);
	} else {
//MK0223-end

//MK0202 - test only - Hao wants to skip Query Command portion of the command protocol just to see what happens
//MK0215#if (DEBUG_FEAT_SKIP_QUERY_BSM_WRITE == 1)
//MK0215	pr_debug("[%s]: [DBG] Skipping query command and query command status check\n", __func__);
//MK0215-begin
	/* Give enough delay to FPGA to finish up fake-read operation */
//	mdelay(1);
//	udelay(1000);
//MK0215-end
//MK0215#else
	if (async == 0) {		/* synchronous mode */
//MK0209		if (wait_for_cmd_done(tag, GROUP_BSM, lba) != 0)
//MK0223//MK0209-begin
//MK0223		if (wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba) != 0)
//MK0223//MK0209-end
//MK0223		{
//MK0223			pr_warn("[%s]: CMD_DONE not found\n", __func__);
//MK0223			ret_code = -2;
//MK0223			goto exit;
//MK0223		}
//MK0223-begin
//MK0224		if (wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba) != 0)
//MK0224-begin

//MK0405-begin
//MK0605		mdelay(get_user_defined_delay_ms(0));
//MK20170712		udelay(get_user_defined_delay_us(1));	//MK0605
//MK0405-end

//MK0605-begin
send_qcmd_again:
//MK0605-end
//MK0627-begin
	timestamp[2] = hv_nstimeofday();
//MK0627-end
		//SJ0301
//MK20170712		bsm_query_command(query_tag,(unsigned int)(get_command_tag()-1), GROUP_BSM, lba);
//MK20170712		query_tag = (query_tag+1)%128 + 128;

//MK0616		if ( wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba, get_bsm_wrt_qc_status_delay()) != 0 )
//MK0628		if ( (ret_code = wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba, get_bsm_wrt_qc_status_delay())) != 0 )	//MK0616
//MK0628-begin
//MK20170712		if ( (ret_code = wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba, get_bsm_wrt_qc_status_delay(), MMLS_WRITE)) != 0 )
//MK20170712-begin
			if ( (ret_code = wait_for_cmd_done((unsigned int)(get_command_tag()-1), GROUP_BSM, lba, get_user_defined_delay_us(1), MMLS_WRITE)) != 0 )
//MK20170712-end
//MK0628-end
//MK0224-end
		{
//MK0616-begin
			/* Check CRC error */
			if ( ret_code == -2 ) {
				if ( crc_retry_count != 0 ) {
					crc_retry_count--;		// Count down checksum retry count
					ret_code = 0;			// Clear error code
					goto send_cmd_again;	// Send BSM_WRITE command again
				} else {
					ret_code = -8;
					pr_debug("[%s]: BSM_WRITE failed due to CRC error\n", __func__);
					goto exit;
				}
			}
//MK0616-end

//MK0301-begin
			/* For debugging only requested by SJ */
			if ( bsm_wrt_do_dummy_read_enabled() ) {
//MK0306				memcpy((void *)hv_cmd_local_buffer, (void *)get_bsm_wrt_dummy_read_addr(), 64);
				memcpy((void *)hv_cmd_local_buffer, ECC_OFF, 64);	//MK0306
			}
//MK0301-end

			if ( bsm_wrt_qc_retry_enabled() ) {
				if ( qc_retry_count == 0 ) {
					pr_warn("[%s]: CMD_DONE not found (qc_retry_count=%d) (LBA=0x%.8X)\n", __func__, qc_retry_count, pBsm->lba[0]);
					ret_code = -6;
					goto exit;
				} else {
					pr_debug("[%s]: CMD_DONE not found, do retry (qc_retry_count=%d) (LBA=0x%.8X)\n", __func__, qc_retry_count, pBsm->lba[0]);
					qc_retry_count--;
//MK0605					hv_reset_internal_state_machine();
//MK0605					goto send_cmd_again;
					goto send_qcmd_again;	//MK0605
				}
			} else {
				pr_warn("[%s]: CMD_DONE not found, query command retry disabled (LBA=0x%.8X)\n", __func__, pBsm->lba[0]);
				ret_code = -5;
				goto exit;
			}
		}
//MK0223-end

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
		pr_err("asynchronous mode was assigned with wrong value\n");
		return -1;
	}
//MK0215#endif	//MK0202 - test only for Hao

//MK0627-begin
	timestamp[3] = hv_nstimeofday();
//MK0627-end
//MK0223-begin
	}
//MK0223-end

//MK0223-begin
	if ( bsm_wrt_data_checksum_verification_enabled() ) {
//MK0223-end

//MK0202-begin
	/* ----- DEBUG: Show LBA from FPGA and LBA sent by host ----- */
	hv_read_fpga_debug_info(&fpga_debug_info);
	pr_debug("[%s]: [DBG] LBA from FPGA (0x%.8X) %s LBA sent by host (0x%.8X) >> (%s) 0x%.8X <<\n",
			__func__, fpga_debug_info.lba,
			(fpga_debug_info.lba == pBsm->lba[0]) ? "==" : "!=", pBsm->lba[0],
			(fpga_debug_info.lba >= pBsm->lba[0]) ? "+" : "-",
			(fpga_debug_info.lba >= pBsm->lba[0]) ? (fpga_debug_info.lba - pBsm->lba[0]) : (pBsm->lba[0] - fpga_debug_info.lba));
//MK0209-begin
	/* ----- DEBUG: Show current and previous LBA received by FPGA ----- */
	pr_debug("[%s]: [DBG] LBA from FPGA (MMIO) (0x%.8X) %s LBA from FPGA (FAKE-RD) (0x%.8X) >> (%s) 0x%.8X <<\n",
			__func__, fpga_debug_info.lba,
			(fpga_debug_info.lba == fpga_debug_info.lba2) ? "==" : "!=", fpga_debug_info.lba2,
			(fpga_debug_info.lba >= fpga_debug_info.lba2) ? "+" : "-",
			(fpga_debug_info.lba >= fpga_debug_info.lba2) ? (fpga_debug_info.lba - fpga_debug_info.lba2) : (fpga_debug_info.lba2 - fpga_debug_info.lba));
//MK0209-end
	/* ----- DEBUG: Show fake-read buff addr from FPGA and the one sent by host ----- */
	pr_debug("[%s]: [DBG] fake-read buff addr from FPGA (0x%.8X) %s fake-read buff addr sent by host (0x%.8X)\n",
			__func__, fpga_debug_info.fr_buff_addr, (fpga_debug_info.fr_buff_addr == pBsm->dram_addr[0]) ? "==" : "!=", pBsm->dram_addr[0]);
//MK0202-end

	/* ----- DEBUG: Compute checksum of data in fake-read buffer and get checksum from FPGA ----- */
	calculate_checksum((void*)vbuf, &frb_checksum);
	hv_read_fake_buffer_checksum(1, &fpga_checksum);
	pr_debug("[%s]: [DBG] frb_checksum  = 0x%.2x%.2x%.2x%.2x  fpga_checksum = 0x%.2x%.2x%.2x%.2x\n",
			__func__, frb_checksum.sub_cs[3], frb_checksum.sub_cs[2], frb_checksum.sub_cs[1], frb_checksum.sub_cs[0],
			fpga_checksum.sub_cs[3], fpga_checksum.sub_cs[2], fpga_checksum.sub_cs[1], fpga_checksum.sub_cs[0]);

//MK0307-begin
	/* ----- DEBUG: Compute the 2nd checksum of data in fake-read buffer and get the 2nd checksum from FPGA ----- */
//	calculate_checksum_2((void*)vbuf, &frb_checksum2);
//	hv_read_fake_buffer_checksum_2(1, &fpga_checksum2);
//	pr_debug("[%s]: [DBG] frb_checksum2  = 0x%.2x%.2x%.2x%.2x  fpga_checksum2 = 0x%.2x%.2x%.2x%.2x\n",
//			__func__, frb_checksum2.sub_cs[3], frb_checksum2.sub_cs[2], frb_checksum2.sub_cs[1], frb_checksum2.sub_cs[0],
//			fpga_checksum2.sub_cs[3], fpga_checksum2.sub_cs[2], fpga_checksum2.sub_cs[1], fpga_checksum2.sub_cs[0]);
//MK0307-end

#if 0	//MK0307
//	if ( memcmp((void *)&fpga_checksum, (void*)&frb_checksum, 4) != 0) {
	if ( (fpga_checksum.sub_cs[0] == frb_checksum.sub_cs[0]) &&
			(fpga_checksum.sub_cs[1] == frb_checksum.sub_cs[1]) &&
			(fpga_checksum.sub_cs[2] == frb_checksum.sub_cs[2]) &&
			(fpga_checksum.sub_cs[3] == frb_checksum.sub_cs[3]) ) {
		pr_debug("[%s]: [DBG] fpga_checksum & frb_checksum are the same! (retry_count=%d) (LBA=0x%.8X) (DRAM addr=0x%.8X)\n",
				__func__, retry_count, pBsm->lba[0], pBsm->dram_addr[0]);
	} else {
		if (retry_count != 0) {
			pr_debug("[%s]: [DBG] checksum mismatched, do retry (retry_count=%d) (LBA=0x%.8X) (DRAM addr=0x%.8X)\n",
					__func__, retry_count, pBsm->lba[0], pBsm->dram_addr[0]);
			retry_count--;
			goto send_cmd_again;
		} else {
			pr_debug("[%s]: [DBG] BSM_WRITE failed due to checksum mismatch (retry_count=%d) (LBA=0x%.8X) (DRAM addr=0x%.8X)\n",
					__func__, retry_count, pBsm->lba[0], pBsm->dram_addr[0]);
			ret_code = -7;
		}
	}
#endif	//MK0307
//MK0307-begin

//MK0418-begin
	/* ----- DEBUG: Compute popcnt for the master FPGA ----- */
	if ( bsm_wrt_popcnt_enabled() )
	{
		popcnt_m = (short int) calculate_popcnt((void*)vbuf, 4096, 0);
		fpga_popcnt_m = hv_read_fake_buffer_popcnt(1);
		pr_debug("[%s]: [DBG] master_popcnt (calculated) = 0x%.8X  master_popcnt (FPGA) = 0x%.8X\n",
				__func__, popcnt_m,	fpga_popcnt_m);
	}
//MK0418-end

	if ( slave_data_cs_enabled() ) {
		/* ----- DEBUG: Compute checksum of data in slave side of fake-read buffer and get checksum from Slave FPGA ----- */
		calculate_checksum((void*)(vbuf+4), &slave_frb_checksum);
		hv_read_fake_buffer_checksum(3, &slave_fpga_checksum);
		pr_debug("[%s]: [DBG] slave_frb_checksum  = 0x%.2x%.2x%.2x%.2x  slave_fpga_checksum = 0x%.2x%.2x%.2x%.2x\n",
				__func__, slave_frb_checksum.sub_cs[3], slave_frb_checksum.sub_cs[2], slave_frb_checksum.sub_cs[1], slave_frb_checksum.sub_cs[0],
				slave_fpga_checksum.sub_cs[3], slave_fpga_checksum.sub_cs[2], slave_fpga_checksum.sub_cs[1], slave_fpga_checksum.sub_cs[0]);

		/* ----- DEBUG: Compute the 2nd checksum of data in slave side of fake-read buffer and get the 2nd checksum from Slave FPGA ----- */
//		calculate_checksum_2((void*)(vbuf+4), &slave_frb_checksum2);
//		hv_read_fake_buffer_checksum_2(3, &slave_fpga_checksum2);
//		pr_debug("[%s]: [DBG] slave_frb_checksum2  = 0x%.2x%.2x%.2x%.2x  slave_fpga_checksum2 = 0x%.2x%.2x%.2x%.2x\n",
//				__func__, slave_frb_checksum2.sub_cs[3], slave_frb_checksum2.sub_cs[2], slave_frb_checksum2.sub_cs[1], slave_frb_checksum2.sub_cs[0],
//				slave_fpga_checksum2.sub_cs[3], slave_fpga_checksum2.sub_cs[2], slave_fpga_checksum2.sub_cs[1], slave_fpga_checksum2.sub_cs[0]);

//MK0418-begin
		/* ----- DEBUG: Compute & retrieve popcnt for the slave FPGA ----- */
		if ( bsm_wrt_popcnt_enabled() ) {
			popcnt_s = (short int) calculate_popcnt((void*)vbuf, 4096, 1);
			fpga_popcnt_s = hv_read_fake_buffer_popcnt(3);
			pr_debug("[%s]: [DBG] slave_popcnt (calculated) = 0x%.8X  slave_popcnt (FPGA) = 0x%.8X\n",
					__func__, popcnt_s,	fpga_popcnt_s);
		}
//MK0418-end

		if ( (data_cs_comp(&fpga_checksum, &frb_checksum) == 0) &&
//				(data_cs_comp(&fpga_checksum2, &frb_checksum2) == 0) &&
				(popcnt_m == fpga_popcnt_m) &&
				(data_cs_comp(&slave_fpga_checksum, &slave_frb_checksum) == 0) &&
//				(data_cs_comp(&slave_fpga_checksum2, &slave_frb_checksum2) == 0) &&
				(popcnt_s == fpga_popcnt_s) ) {
			pr_debug("[%s]: [DBG] fpga_checksum & frb_checksum are the same! (retry_count=%d) (LBA=0x%.8X) (DRAM addr=0x%.8X)\n",
					__func__, retry_count, pBsm->lba[0], pBsm->dram_addr[0]);
		} else {
//SJ0313			if (retry_count != 0) {
//SJ0313				pr_debug("[%s]: [DBG] checksum mismatched, do retry (retry_count=%d) (LBA=0x%.8X) (DRAM addr=0x%.8X)\n",
//SJ0313						__func__, retry_count, pBsm->lba[0], pBsm->dram_addr[0]);
//SJ0313				retry_count--;
//SJ0313				goto send_cmd_again;
//SJ0313			} else {
//SJ0313				pr_debug("[%s]: [DBG] BSM_WRITE failed due to checksum mismatch (retry_count=%d) (LBA=0x%.8X) (DRAM addr=0x%.8X)\n",
//SJ0313						__func__, retry_count, pBsm->lba[0], pBsm->dram_addr[0]);
//SJ0313				ret_code = -7;
//SJ0313			}
//SJ0313-begin
			if ( bsm_wrt_do_dummy_read_enabled() ) {
				memcpy((void *)hv_cmd_local_buffer, ECC_OFF+0x2000, 64);
			}
			pr_debug("[%s]: [DBG] checksum mismatched, do retry (retry_count=%d) (LBA=0x%.8X)", __func__, retry_count, pBsm->lba[0]);
			pr_debug(" Master XOR: 0x%.8X,------, Slave XOR: ------,------,  popcnt: 0x%.8X,0x%.8X, ------,------",
					frb_checksum.cs^fpga_checksum.cs, popcnt_m, fpga_popcnt_m);
			if (retry_count != 0) {
				retry_count--;
				if ( (retry_count==3) && (bcom_reset_retry) ) {
					bcom_reset_retry = 0;
					hv_reset_bcom_control();
				}
				goto send_cmd_again;
			} else {
				pr_debug("[%s]: [DBG] BSM_WRITE failed due to checksum mismatch (LBA=0x%.8X)\n", __func__, pBsm->lba[0]);
				ret_code = -7;
			}
//SJ0313-end
		}
	} else {
		if ( (data_cs_comp(&fpga_checksum, &frb_checksum) == 0) &&
//				(data_cs_comp(&fpga_checksum2, &frb_checksum2) == 0) &&
				(popcnt_m == fpga_popcnt_m) ) {
			pr_debug("[%s]: [DBG] fpga_checksum & frb_checksum are the same! (retry_count=%d) (LBA=0x%.8X) (DRAM addr=0x%.8X)\n",
					__func__, retry_count, pBsm->lba[0], pBsm->dram_addr[0]);
		} else {
//SJ0313			if (retry_count != 0) {
//SJ0313				pr_debug("[%s]: [DBG] checksum mismatched, do retry (retry_count=%d) (LBA=0x%.8X) (DRAM addr=0x%.8X)\n",
//SJ0313						__func__, retry_count, pBsm->lba[0], pBsm->dram_addr[0]);
//SJ0313				retry_count--;
//SJ0313				goto send_cmd_again;
//SJ0313			} else {
//SJ0313				pr_debug("[%s]: [DBG] BSM_WRITE failed due to checksum mismatch (retry_count=%d) (LBA=0x%.8X) (DRAM addr=0x%.8X)\n",
//SJ0313						__func__, retry_count, pBsm->lba[0], pBsm->dram_addr[0]);
//SJ0313				ret_code = -7;
//SJ0313			}
//SJ0313-begin
			pr_debug("[%s]: [DBG] checksum mismatched, do retry (retry_count=%d) (LBA=0x%.8X)", __func__, retry_count, pBsm->lba[0]);
			pr_debug(" Master XOR: 0x%.8X,------, Slave XOR: ------,------,  popcnt: 0x%.8X,0x%.8X, XOR:0x%.8X ------,------",
					frb_checksum.cs^fpga_checksum.cs, popcnt_m, fpga_popcnt_m, popcnt_m^fpga_popcnt_m);
			if (retry_count != 0) {
				retry_count--;
				if ( (retry_count==3) && (bcom_reset_retry) ) {
					bcom_reset_retry=0;
					hv_reset_bcom_control();
				}
				goto send_cmd_again;
			} else {
				pr_debug("[%s]: [DBG] BSM_WRITE failed due to checksum mismatch (LBA=0x%.8X)\n", __func__, pBsm->lba[0]);
				ret_code = -7;
			}
//SJ0313-end
		}
	}
//MK0307-end

//MK0223-begin
	} else {
		pr_debug("[%s]: bsm_wrt_data_checksum_verification disabled\n", __func__);
//MK0302		mdelay(1);
	}
//MK0223-end
//MK0627-begin
	timestamp[4] = hv_nstimeofday();
	pr_debug("[%s]: =ET= (retry_count=%d)  LBA=0x%.8X TOTAL:%.8ldns CMD:%.8ldns FAKE:%.8ldns QC:%.8ldns CS:%.8ldns\n",
			__func__, retry_count, pBsm->lba[0], timestamp[4]-timestamp[0],
			timestamp[1]-timestamp[0], timestamp[2]-timestamp[1],
			timestamp[3]-timestamp[2], timestamp[4]-timestamp[3]);
//MK0627-end

exit:
	return ret_code;
}
//MK0201-end

//MK-begin
/**
 * hv_fake_operation - Performs fake-read or fake-write based on fake_index
 * @fake_index: 0 for fake-read, 1 for fake-write
 * @blk_cnt: Number of 4KB blocks
 * @lba: Logical Block Address
 * @type:
 * @*buf:
 *
 *
 *
 *
 **/
//MK0729static int hv_fake_operation(fake_type_t fake_op_index, unsigned int blk_cnt,
//MK0729		unsigned int lba, int type, unsigned char *buf)
//MK0729-begin
static int hv_fake_operation(fake_type_t fake_op_index, unsigned int sector_cnt,
		unsigned int lba, int type, unsigned char *buf)
//MK0729-end
{
//MK1110	unsigned long ts1, elapsed_time;
//MK0729	unsigned int block_count=blk_cnt, fpga_block_count, dbg_status_count=0;
//MK0729-begin
//MK1110	unsigned int block_count, fpga_block_count, dbg_status_count=0;
//MK1110-begin
	unsigned int block_count, fpga_block_count=0;
//MK1110-end
//MK0729-end
	unsigned char *dataBuff = buf;
//MK1013-begin
	int	ret_code=0;
//MK1013-end

#if 0	//MK1214
//MK0729-begin
	/* Min block size of 4KB */
	/* Calculate data size in 4KB blocks (sector*512/4096) */
	block_count = ((sector_cnt << 9) >> 12);

	/* The following scenario should not happen...., I think. */
	if ((sector_cnt % 8) != 0) {
		/* Read one extra 4KB block */
		block_count++;
	}
//MK0729-end
#endif	//MK1214
//MK1214-begin
	/* Convert sector count to 4KB block count */
	block_count = (sector_cnt + 7) >> 3;
//MK1214-end

//MK1018-begin
	pr_debug("[%s]: %s block_count=%d\n", __func__, fake_op[fake_op_index].name, block_count);
//MK1018-end

	/*
	 * The following code performs true fake-read or fake-write operation
	 * based on the input parameter, fake_index: 0 for fake-read and
	 * 1 for fake-write.
	 */
	while (block_count > 0)
	{
#if 0	//MK1110 we do not need the time out check because it is already done
		//MK1110 inside fake_op[fake_op_index].get_blk_cnt(type, lba).
		elapsed_time = 0;
		ts1 = 0;
		fpga_block_count = 0;
		dbg_status_count = 0;
		while (elapsed_time < MAX_STATUS_WAIT_TIME_NS)
		{
			/* Ask HVDIMM how many 4KB blocks are available for fake-read/write */
//			fpga_block_count = get_wr_buf_size(GROUP_BSM, lba);
//MK1013#ifdef FINAL_DEMO
			fpga_block_count = fake_op[fake_op_index].get_blk_cnt(type, lba);
//MK1013#else
			// jcao: assume one buffer(4kb) avaialbe to avoid fake write, which will cause
			// system crash with current FPGA test image(07/23/16)
//MK1013			fpga_block_count = 1;
//MK1013#endif

			/* We need to be alerted if there is nothing to fake-read */
			if (!fpga_block_count) {
				/* Get a timestamp if this is the first alert */
				if (ts1 == 0)
					ts1 = hv_nstimeofday();

				dbg_status_count++;

				/* time elapsed since the first alert */
				elapsed_time = hv_nstimeofday() - ts1;
			} else {
				/* Got 4KB block count from HVDIMM */
				break;
			}
		} // end of while

		/* Do a fake read for the number of 4KB specified by FPGA */
		if (fpga_block_count) {
//		hv_mmls_fake_read(0, lba, 0, (void *)&pBsm->dram_addr[0], fpga_block_count << 12);
//MK0818-begin
			pr_debug("[%s]: %s from 0x%.16lx current fpga_block_count=%d current block_count=%d\n",
					__func__, fake_op[fake_op_index].name, (unsigned long)dataBuff, fpga_block_count, block_count);
//MK0818-end
			fake_op[fake_op_index].do_fake(0, lba, 0, (void*)dataBuff, fpga_block_count << 12);
			block_count -= fpga_block_count;
			dataBuff += (fpga_block_count << 12);	// 1 block = 4KB
		} else {
			/*
			 * We got here because fpga_block_count is zero, which means
			 * it was timed out while reading the general read status.
			 * Send termination signal to HVDIMM and exit with error.
			 */
//MK1013			hv_write_termination(0, type, lba, NULL);
			pr_warn("[%s]: %s timeout while reading General %s Status (%u:%lu ns)\n",
					__func__, fake_op[fake_op_index].name, (fake_op_index==0) ? "Read" : "Write", dbg_status_count, elapsed_time);
//MK1013			return -1;
//MK1013-begin
			ret_code = -1;
//MK1013-end
		}
#endif	//MK1110

#if 0	//MK0223
//MK1110-begin
		ret_code = fake_op[fake_op_index].get_blk_cnt(type, lba);
		if (ret_code > 0) {
			if (ret_code <= block_count) {
				fpga_block_count = (unsigned int)ret_code;
				ret_code = 0;
				pr_debug("[%s]: %s from 0x%.16lx current fpga_block_count=%d current block_count=%d\n",
						__func__, fake_op[fake_op_index].name, (unsigned long)dataBuff, fpga_block_count, block_count);

				fake_op[fake_op_index].do_fake(0, lba, 0, (void*)dataBuff, fpga_block_count << 12);
				block_count -= fpga_block_count;
				dataBuff += (fpga_block_count << 12);	// 1 block = 4KB
			} else {
				pr_warn("[%s]: %s : block size (%d) from General %s Status greater than block_count (%d)\n",
						__func__, fake_op[fake_op_index].name, ret_code, (fake_op_index==0) ? "Read" : "Write", block_count);
				ret_code = -1;
				break;
			}
		} else {
			/*
			 * Ran into an error in get_rd_buf_size or get_wd_buf_size. Cannot
			 * continue the presight operation.
			 */
			pr_warn("[%s]: error from %s while reading General %s Status\n",
					__func__, fake_op[fake_op_index].name, (fake_op_index==0) ? "Read" : "Write");
			ret_code = -1;
			break;
		}
//MK1110-end
#endif	//MK0223
//MK0223-begin
		ret_code = fake_op[fake_op_index].get_blk_cnt(type, lba);
		if (ret_code > 0) {
			fpga_block_count = (unsigned int)ret_code;
			pr_debug("[%s]: %s from 0x%.16lx current fpga_block_count=%d current block_count=%d\n",
					__func__, fake_op[fake_op_index].name, (unsigned long)dataBuff, fpga_block_count, block_count);

			if (fpga_block_count >= block_count) {
				/* FPGA has enough space in IO buffer. So, finish up the rest of our data */
				fake_op[fake_op_index].do_fake(0, lba, 0, (void*)dataBuff, block_count << 12);
				block_count = 0;
				ret_code = 0;
			} else {
				fake_op[fake_op_index].do_fake(0, lba, 0, (void*)dataBuff, fpga_block_count << 12);
				block_count -= fpga_block_count;
				dataBuff += (fpga_block_count << 12);	// 1 block = 4KB
			}
		} else {
			/*
			 * Ran into an error in get_rd_buf_size or get_wd_buf_size. Cannot
			 * continue the presight operation.
			 */
			pr_warn("[%s]: error from %s while reading General %s Status\n",
					__func__, fake_op[fake_op_index].name, (fake_op_index==0) ? "Write" : "Read");
			ret_code = -1;
			break;
		}
//MK0223-end
	} // end of while

/* ========================================================================== */
#if 0	//MK0518-begin
//MK0301//MK0123-begin
//MK0301	/* For the time being, termination command is required only for fake-rd */
//MK0301	if (fake_op_index == 0)
//MK0301		hv_write_termination(0, type, lba, NULL);
//MK0301//MK0123-end
//MK0301-begin
	if (fake_op_index == 0) {
		/* fake-rd (BSM_WRITE) case */
		if ( bsm_wrt_skip_termination_enabled() ) {
//SJ0313-begin
			if ( bcom_toggle_enabled() ) {
			} else {
				udelay(2);
				pr_debug("[%s]: User Delay 2us \n", __func__);
			}
//SJ0313-end
			pr_debug("[%s]: %s - skip termination enabled\n", __func__, fake_op[fake_op_index].name);
		} else {
			hv_write_termination(0, type, lba, NULL);
		}
	} else {
		/* fake-wrt (BSM_READ) case */
		if ( bsm_rd_skip_termination_enabled() ) {
//SJ0313-begin
			if ( bcom_toggle_enabled() ) {
			} else {
				udelay(2);
				pr_debug("[%s]: User Delay 2us \n", __func__);
			}
//SJ0313-end
			pr_debug("[%s]: %s - skip termination enabled\n", __func__, fake_op[fake_op_index].name);
		} else {
			hv_write_termination(0, type, lba, NULL);
		}
	}
//MK0301-end
#endif	//MK0518-end

	return ret_code;
}
//MK-end

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
//MK0224		if (!wait_for_cmd_done(tag, GROUP_MMLS, lba))
//MK0224-begin
//MK0628		if ( !wait_for_cmd_done(tag, GROUP_MMLS, lba, get_bsm_rd_qc_status_delay()) )
//MK0628-begin
		if ( !wait_for_cmd_done(tag, GROUP_MMLS, lba, get_bsm_rd_qc_status_delay(), MMLS_READ) )
//MK0628-end
//MK0224-end
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
//MK0224		if (wait_for_cmd_done(tag, GROUP_MMLS, lba)) {
//MK0224-begin
//MK0628		if ( wait_for_cmd_done(tag, GROUP_MMLS, lba, get_bsm_wrt_qc_status_delay()) ) {
//MK0628-begin
		if ( wait_for_cmd_done(tag, GROUP_MMLS, lba, get_bsm_wrt_qc_status_delay(), MMLS_WRITE) ) {
//MK0628-end
//MK0224-end
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

int reset_command(unsigned int tag)
{
#if 0	//MK0213
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
#endif	//MK0213
//MK0213-begin
	/* Reset FPGA internal stuff */
	hv_reset_internal_state_machine();
	return 0;
//MK0213-end
}

int bsm_query_command(unsigned short tag, unsigned short tag_id, int type, unsigned int lba)
{
//MK	struct HV_CMD_QUERY_t *pQuery;
//MK-begin
	struct hv_query_cmd *pQuery;
//MK-end

	/* pr_notice("Received BSM QUERY command tag=%d, tag_id=%d\n", tag, tag_id); */

	/* prepare query command */
//MK	pQuery = (struct HV_CMD_QUERY_t *)hv_cmd_local_buffer;
//MK	pQuery->cmd = QUERY;
//MK	*(short *)&pQuery->tag = tag;
//MK	*(short *)&pQuery->tag_id = tag_id;
//MK-begin
	// Just clear 1/4 of the command buffer
//MK	pQuery = (struct hv_query_cmd *) rw_cmd_buff;
//MK0201	memset(pQuery, 0, CMD_BUFFER_SIZE);
//MK0201-begin
	pQuery = &query_cmd_buff;
	memset(pQuery, 0, CMD_BUFFER_SIZE);
//MK0201-end

	// Build the command
	pQuery->command[0].cmd_field.cmd = QUERY;
	pQuery->command[0].cmd_field.query_tag = (unsigned char) tag;
	pQuery->command[0].cmd_field.cmd_tag = (unsigned char) tag_id;
//	pQuery->command[1].cmd_field.cmd = pQuery->command[0].cmd_field.cmd;
//	pQuery->command[1].cmd_field.query_tag = pQuery->command[0].cmd_field.query_tag;
//	pQuery->command[1].cmd_field.cmd_tag = pQuery->command[0].cmd_field.cmd_tag;
	pQuery->command[1].cmd_field_32b = pQuery->command[0].cmd_field_32b;
	pQuery->checksum[0] = pQuery->command[0].cmd_field_32b;
	pQuery->checksum[1] = pQuery->command[1].cmd_field_32b;
//MK-end

	/* transmit the command */
//MK	hv_write_command(0, type, lba, hv_cmd_local_buffer);
//MK-begin
	hv_write_command(0, type, lba, pQuery);
//MK-end

//MK1024-begin
#if 0	//MK1110 too much info in dmesg, the cmd data was verified.
	/* debugging only - for Hao to debug status register problem */
	pr_debug("[%s]: cmd[07]:[00] = 0x%.8X - 0x%.8X\n", __func__, pQuery->command[1].cmd_field_32b, pQuery->command[0].cmd_field_32b);
	pr_debug("[%s]: cmd[15]:[08] = 0x%.8X - 0x%.8X\n", __func__, pQuery->reserve1[1], pQuery->reserve1[0]);
	pr_debug("[%s]: cmd[23]:[16] = 0x%.8X - 0x%.8X\n", __func__, pQuery->reserve1[3], pQuery->reserve1[2]);
	pr_debug("[%s]: cmd[31]:[24] = 0x%.8X - 0x%.8X\n", __func__, pQuery->reserve1[5], pQuery->reserve1[4]);
	pr_debug("[%s]: cmd[39]:[32] = 0x%.8X - 0x%.8X\n", __func__, pQuery->checksum[1], pQuery->checksum[0]);
	pr_debug("[%s]: cmd[47]:[40] = 0x%.8X - 0x%.8X\n", __func__, pQuery->reserve2[1], pQuery->reserve2[0]);
	pr_debug("[%s]: cmd[55]:[48] = 0x%.8X - 0x%.8X\n", __func__, pQuery->reserve2[3], pQuery->reserve2[2]);
	pr_debug("[%s]: cmd[63]:[56] = 0x%.8X - 0x%.8X\n", __func__, pQuery->reserve2[5], pQuery->reserve2[4]);
#endif	//MK1110
//MK1024-end
	return 1;
}

int mmls_query_command(unsigned short tag, unsigned short tag_id, int type, unsigned int lba)
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

int ecc_train_command(void)
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


int inquiry_command(unsigned int tag)
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

int config_command(unsigned int tag,
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

int page_swap_command(unsigned int tag,
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

int bsm_qread_command(unsigned int tag,
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

int bsm_qwrite_command(unsigned int tag,
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

int bsm_backup_command(unsigned int tag,
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

int bsm_restore_command(unsigned int tag,
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

//MK0123-begin
int bsm_terminate_command(void)
{
	pr_debug("[%s]: entered\n", __func__);

	hv_write_termination(0, 0, 0, NULL);
	return 0;
}
//MK0123-end

void spin_for_cmd_init(void)
{
	spin_lock_init(&cmd_in_q_lock);
}

//MK1110#define QUERY_TIMEOUT	1000000000	/* 1 sec */
//MK1110-begin
/* For devel only - for some reason it takes so much time for FPGA to respond */
//SJ0301#define QUERY_TIMEOUT	100000000	/* 0.1 sec */
#define QUERY_TIMEOUT	1000000	/* 1 ms */	//SJ0301
//MK1110-end

//MK-begin
#define	ESTIMATED_COMPLETION_TIME_MASK	0xF0
#define	ESTIMATED_COMPLETION_TIME_SHIFT	4
#define	QUERY_PROGRESS_STATUS_MASK		0x0C
#define	QUERY_PROGRESS_STATUS_SHIFT		2
#define	COMMAND_SYNC_COUNTER_MASK		0x03
#define	COMMAND_SYNC_COUNTER_SHIFT		0
//MK-end
//MK0224static int wait_for_cmd_done(unsigned int tag, int type, unsigned int lba)
//MK0224-begin
//MK0628static int wait_for_cmd_done(unsigned int tag, int type, unsigned int lba, unsigned long delay)
//MK0628-begin
static int wait_for_cmd_done(unsigned int tag, int type, unsigned int lba, unsigned long delay, unsigned char cmd_flag)
//MK0628-end
//MK0224-end
{
#if 0 //MK
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
#endif	//MK

//MK-begin
	int error_code=-1;
	unsigned long ts, elapsed_time=0;
	unsigned char query_status=0;
	unsigned int dbg_query_status_count=0;

	pr_debug("[%s]: entered\n", __func__);

	ts = hv_nstimeofday();
	while(elapsed_time < QUERY_TIMEOUT) {
//SJ0301		bsm_query_command(query_tag, tag, type, lba);
//MK20170712-begin
		bsm_query_command(query_tag,(unsigned int)(get_command_tag()-1), GROUP_BSM, lba);
		query_tag = (query_tag+1)%128 + 128;
//MK20170712-end

//MK0224-begin
		/* For debugging - give some time delay before starting to check the status */
		ndelay(delay);
//MK0224-end
		query_status = hv_query_status(0, type, lba, 0);

		/* Elapsed time since the first query command */
		elapsed_time = hv_nstimeofday() - ts;
		dbg_query_status_count++;

//MK20170712-begin
		/* Ignore eMMC CRC error temporarily */
		query_status &= 0xF6;
//MK20170712-end
#ifdef SW_SIM
		return 0;
#endif

#if 0	//MK0307
		/* Check for command done */
		if (((query_status & QUERY_PROGRESS_STATUS_MASK)  >> 2) == 3) {
			/* Detected command done, exit the loop */
			error_code = 0;
			break;
		}
#endif	//MK0307
//MK0307-begin
		if ( slave_cmd_done_check_enabled() ) {
			/* Check for command done for both FPGAs */
//MK0616			if ( query_status == 0x0F ) {
//MK0616				/* Detected command done, exit the loop */
//MK0616				error_code = 0;
//MK0616				break;
//MK0616			}
//MK0616-begin
			if ( query_status & 0x09 ) {
				pr_debug("[%s]: eMMC CRC error detected (0x%.2X) (LBA=0x%.8X, OPCODE=0x%.2X)\n",
						__func__, query_status, lba, cmd_flag);
				error_code = -2;	// CRC error
				break;	// error exit
//MK0628			} else if ( query_status == 0x36 ) {
//MK0628-begin
			} else if ( cmd_flag == MMLS_WRITE ) {
				if ( query_status == 0x12 ||
						query_status == 0x16 ||
						query_status == 0x32 ||
						query_status == 0x36 ) {
					/* Detected command done, exit the loop */
					error_code = 0;
					break;
				}
			} else if ( cmd_flag == MMLS_READ ) {
				if ( query_status == 0x36 ) {
					/* Detected command done, exit the loop */
					error_code = 0;
					break;
				}
			}
//MK0628-end
//MK0616-end
		} else {
			/* Check for command done */
			if (((query_status & QUERY_PROGRESS_STATUS_MASK)  >> 2) == 3) {
				/* Detected command done, exit the loop */
				error_code = 0;
				break;
			}
		}
//MK0307-end

	} // end of while

//MK0307-begin
	/* Copy the entire burst to access later */
	if (error_code == 0) {
		save_query_status();
	}
//MK0307-end

//MK0224	pr_debug("[%s]: query_status=0x%.2x dbg_query_status_count=%d elapsed time=%luns\n",
//MK0224			__func__, (unsigned int)query_status, dbg_query_status_count, elapsed_time);
//MK0224-begin
	pr_debug("[%s]: %s: query_status=0x%.2x dbg_query_status_count=%d delay=%lu ns elapsed time=%lu ns\n",
			__func__, (error_code == 0) ? "PASS" : "FAIL", (unsigned int)query_status, dbg_query_status_count, delay, elapsed_time);
//MK0224-end
	return error_code;
//MK-end
}

int get_rd_buf_size(int type, unsigned int lba)
{
	unsigned char gr_status;
	int rd_buf_size;
	unsigned long b, a;
	unsigned long latency;
#ifdef STS_TST
	static int i=0;
	unsigned char tst_sts[7]={0x03, 0x02, 0x03, 0x02, 0x04, 0x03, 0x03};
#endif
//MK0613-begin
	unsigned int i=0;
//MK0613-end
	rd_buf_size = 0;
	latency = 0;
	b = hv_nstimeofday();

//MK0223-begin
	if ( bsm_rd_skip_grs_enabled() ) {
		gr_status = 1;
		rd_buf_size = gr_status & G_STS_CNT_MASK;
		a = hv_nstimeofday();
		latency = a - b;
	} else {
//MK0223-end

	while ( (rd_buf_size == 0) && (latency < READ_TIMEOUT) ) {
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
//MK0613-begin
//		pr_debug("[%s]: General Status[%d]: gr_status=0x%X\n", __func__, i, gr_status);
		i++;
//MK0613-end
	}

//MK0223-begin
	}
//MK0223-end

	if (latency >= READ_TIMEOUT){
//MK1018		pr_info("latency is %lds, gr_status=%x\n", latency, gr_status);
//MK1018-begin
//MK0613		pr_info("[%s]: ERROR: latency=%ld ns, gr_status=0x%.2x\n", __func__, latency, gr_status);
//MK0613-begin
		pr_info("[%s]: General Status[%d]: ERROR: latency=%ld ns, gr_status=0x%.2x\n", __func__, i, latency, gr_status);
//MK0613-end
//MK1018-end
		return -1;
	}
//MK1110-begin
	else if (rd_buf_size > 4) {
		pr_info("[%s]: ERROR: FPGA returned unexpected block size %d\n", __func__, rd_buf_size);
		return -1;
	}
//MK1110-end

	return rd_buf_size;
}

int get_wr_buf_size(int type, unsigned int lba)
{
	unsigned char gw_status;
	int wr_buf_size;
	unsigned long b, a;
	unsigned long latency;
#ifdef STS_TST
	static int i=0;
	unsigned char tst_sts[7]={0x03, 0x02, 0x03, 0x02, 0x04, 0x03, 0x03};
#endif
//MK0613-begin
	unsigned int i=0;
//MK0613-end
	wr_buf_size = 0;
	latency = 0;
	b = hv_nstimeofday();
	//pr_debug("m_data=%d, wr_4k_idx=%d, buf=%lx\n", more_data, wr_4k_idx, buf);

//MK0223-begin
	if ( bsm_wrt_skip_gws_enabled() ) {
		gw_status = 1;
		wr_buf_size = gw_status & G_STS_CNT_MASK;
		a = hv_nstimeofday();
		latency = a - b;
	} else {
//MK0223-end

	while ( (wr_buf_size == 0) && (latency < READ_TIMEOUT) ) {
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
//MK0613-begin
//		pr_debug("[%s]: General Status[%d]: gw_status=0x%X\n", __func__, i, gw_status);
		i++;
//MK0613-end
	}

//MK0223-begin
	}
//MK0223-end


	if (latency >= READ_TIMEOUT){
//MK1018		pr_info("latency is %lds, gw_status=%x\n", latency, gw_status);
//MK1018-begin
//MK0613		pr_info("[%s]: ERROR: latency=%ld ns, gw_status=0x%.2x\n", __func__, latency, gw_status);
//MK0613-begin
		pr_info("[%s]: General Status[%d]: ERROR: latency=%ld ns, gw_status=0x%.2x\n", __func__, i, latency, gw_status);
//MK0613-end
//MK1018-end
		return -1;
	}

//MK1110-begin
	else if (wr_buf_size > 4) {
		pr_info("[%s]: ERROR: FPGA returned unexpected block size %d\n", __func__, wr_buf_size);
		return -1;
	}
//MK1110-end

	return wr_buf_size;
}

int hv_read(int type, int more_data, int rd_buf_size, unsigned int lba, unsigned char **buf, int ways, int *index)
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

int hv_write(int type, int more_data, int wr_buf_size, unsigned int lba, unsigned char **buf, int ways, int *index)
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

//MK0725-begin
void td_memcpy_8x8_movnti(void *dst, const void *src, unsigned int len)
{
	register uint64_t t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"pushfq                               \n"
		"cli                                  \n"
		"1:                                   \n"
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
		"leaq    8*8(%[src]),   %[src]        \n"
		"                                     \n"
		"sfence                               \n"
		"                                     \n"
		"movnti  %[t1],         0*8(%[dst])   \n"
		"movnti  %[t2],         1*8(%[dst])   \n"
		"movnti  %[t3],         2*8(%[dst])   \n"
		"movnti  %[t4],         3*8(%[dst])   \n"
		"movnti  %[t5],         4*8(%[dst])   \n"
		"movnti  %[t6],         5*8(%[dst])   \n"
		"movnti  %[t7],         6*8(%[dst])   \n"
		"movnti  %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"addq    $64,           %[dst]        \n"
		"                                     \n"
		"subl    $(8*8),        %[len]        \n"
		"jnz     1b                           \n"
		"                                     \n"
		"sfence                               \n"
		"                                     \n"
		"sti                                  \n"
		"popfq                                \n"
		: [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3), [t4]"=&r"(t4),
		  [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7), [t8]"=&r"(t8),
		  [src]"+S"(src), [dst]"+D"(dst), [len]"+c"(len)
		:
		: "cc"
		);
}
//MK0725-end
