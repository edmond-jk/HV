
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2016 Netlist Inc.                                    *
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/timer.h>
#include "hv_mmio.h"
#include "hv_cmd.h"
#include "hv_params.h"
#include "hv_cache.h"
#include "hv_queue.h"

#ifdef CACHE_LOGGING
#define pr_cache(fmt, ...) printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_cache(fmt, ...) do { /* nothing */ } while (0)
#endif

/* descriptor for cache entries */
static struct hv_cache_desc cache_desc_tbl[CACHE_TYPE_NUM][CACHE_ENTRIES_NUM];

/* pointers to BSM/MMLS ramdisk, set up by hv_io_init() */
static void *bsm_ramdisk;
static void *mmls_ramdisk;

/* pointers to BSM/MMLS cache */
static void *bsm_cache;
static void *mmls_cache;

/* temporary buffer space */
static unsigned char tmp_cache_buf[CACHE_TYPE_NUM][cache_ent_bytes];

struct hv_cache_data_t {
	struct hrtimer timer;
} hv_cache_flush;	

void
cache_mcpy(void *dst, void *src, long size)
{
	pr_cache("cache_mcpy: dst 0x%lx src 0x%lx size %ld\n", (unsigned long)dst, (unsigned long)src, size);
	memcpy(dst, src, size);
}

/* 
 * read from BSM or MMLS flash 
 */
void read_hv(unsigned int type, unsigned long lba, unsigned int sectors, void *data)
{
	pr_cache("%s entered, type(%d) lba(0x%lx) sectors(%d) data(0x%lx)\n", 
				__func__, type, lba, sectors, (unsigned long)data);
	if (type == CACHE_BSM)
//MK0207		bsm_read_command(hv_next_cmdq_tag(), sectors, lba, data, 0, NULL);
//MK0207-begin
		bsm_read_command(hv_next_cmdq_tag(), sectors, lba, data, 0, NULL, 0);
//MK0207-end

	else
		mmls_read_command(hv_next_cmdq_tag(), sectors, lba, (unsigned long)data, 0, NULL);
}

/* 
 * write to BSM or MMLS flash 
 */
void write_hv(unsigned int type, unsigned long lba, unsigned int sectors, void *data)
{
	pr_cache("%s entered, type(%d) lba(0x%lx) sectors(%d) data(0x%lx)\n", 
				__func__, type, lba, sectors, (unsigned long)data);
	if (type == CACHE_BSM)
		bsm_write_command(hv_next_cmdq_tag(), sectors, lba, data, 0, NULL);
	else
		mmls_write_command(hv_next_cmdq_tag(), sectors, lba, (unsigned long)data, 0, NULL);
}

/* 
 * init cache descriptor tables 
 */
static void init_cache(void)
{
	int i, j;
	
	for (i=0; i<CACHE_TYPE_NUM; i++) {
		for (j=0; j<CACHE_ENTRIES_NUM; j++) {
			cache_desc_tbl[i][j].tag = CACHE_EMPTY_TAG;
			cache_desc_tbl[i][j].used_blks = 0;
		}
	}
}

/* 
 * check if the cache entry for lba is empty, hit or no hit 
 */
static int check_cache_hit(unsigned int type, unsigned int idx, unsigned long lba)
{
	if (cache_desc_tbl[type][idx].tag == CACHE_EMPTY_TAG)
		return CACHE_EMPTY;
	if (cache_desc_tbl[type][idx].tag == cache_ent_tag (lba))
		return CACHE_HIT;
	return CACHE_NO_HIT;
}

/* 
 * check if bitmask first contains bitmask second
 */
static int bitmask_contain(unsigned char first, unsigned char second) 
{
	unsigned char one;
	int i;

	one = 1;
	for (i=0; i<CACHE_LINE_SIZE; i++) {
		if (!(first & one) && (second & one))
			/* first has a '0' bit but second has a '1' bit */
			return 0;
		one = one << 1;
	}
	return 1;
}

/*
 * calculate used_blks bitmask per lba/sectors
 */
static unsigned char calc_used_blks(unsigned long lba, int sectors)
{
	int blk_idx;
	unsigned char used_blks=0;

	blk_idx = cache_ent_blk_num(lba);
	while (sectors > 0) {
		used_blks |= (1 << blk_idx);
		sectors = sectors - SECTOR_PER_BLOCK;
		blk_idx ++;
	}
	return used_blks;
}

/* 
 * merge used blocks on cache to buf
 */
static void merge_cache_to_buffer(unsigned char *buf, unsigned int type, unsigned int idx) 
{
	int i;	
	unsigned char use_blks = cache_desc_tbl[type][idx].used_blks;

	for (i=0; i<CACHE_LINE_SIZE; i++) {
		if ((1<<i) & use_blks) {
			/* this block has data */
			cache_mcpy( (void *)(buf + i*cache_ent_blk_bytes), 
				    (void *)(cache_ent_idx_addr(type,idx) + i*cache_ent_blk_bytes), 
				    cache_ent_blk_bytes);
		}
	}
}

/* 
 * merge data on buf to empty blocks on cache 
 */
static void merge_buffer_to_cache(unsigned char *buf, unsigned int type, unsigned int idx) 
{
	int i;	
	unsigned char use_blks = cache_desc_tbl[type][idx].used_blks;

	for (i=0; i<CACHE_LINE_SIZE; i++) {
		if (!((1<<i) & use_blks)) {
			/* this block has no data */
			cache_mcpy( (void *)(cache_ent_idx_addr(type,idx) + i*cache_ent_blk_bytes), 
				    (void *)(buf + i*cache_ent_blk_bytes), 
				    cache_ent_blk_bytes);
		}
	}
}

/*
 * write to hvdimm, read-modify-write if cache line is not fully filled
 */
static int write_hvdimm(unsigned int type, unsigned int idx)
{
	long lba_addr;
	long ramdisk;

	pr_cache("%s entered, type(%d) idx(%d)\n", __func__, type, idx);

	lba_addr = cache_ent_lba(cache_desc_tbl[type][idx].tag)*HV_BLOCK_SIZE;
	ramdisk = (type == CACHE_BSM) ? (long)bsm_ramdisk : (long)mmls_ramdisk;

	/* write to HVDIMM, posssible RMW */
	if (cache_desc_tbl[type][idx].used_blks == CACHE_BLK_ALL_FILLED) {
		/* write full cache entry buffer to HVDIMM */
		if (get_ramdisk())
			cache_mcpy( (void *) (ramdisk + lba_addr), 
			    	    (void *) cache_ent_idx_addr(type, idx), 
			    	    (long) cache_ent_bytes);
		else
			write_hv(type, 
				 cache_ent_lba(cache_desc_tbl[type][idx].tag), /* lba */
				 cache_ent_sectors,
				 cache_ent_idx_addr(type, idx));
	}
	else {
		/* read to tmp_cache_buf */
		if (get_ramdisk())
			cache_mcpy( (void *)tmp_cache_buf[type], (void *)(ramdisk + lba_addr), cache_ent_bytes);
		else
			read_hv(type, 
				cache_ent_lba(cache_desc_tbl[type][idx].tag), /* lba */
				cache_ent_sectors,
				tmp_cache_buf[type]);

		/* merge cache data to tmp_cache_buf */
		merge_cache_to_buffer(tmp_cache_buf[type], type, idx);
		
		/* write tmp_cache_buf to disk */
		if (get_ramdisk())
			cache_mcpy( (void *)(ramdisk + lba_addr), (void *)tmp_cache_buf[type], cache_ent_bytes);
		else
			write_hv(type, 
				 cache_ent_lba(cache_desc_tbl[type][idx].tag), /* lba */
				 cache_ent_sectors,
				 tmp_cache_buf[type]);
	}

	/* mark clean */
	cache_desc_tbl[type][idx].dirty = CACHE_CLEAN;

	return 0;
}

/*
 * read from HVDIMM, merge w existing data if same tag and dirty, and mark dirty
 */
static int read_hvdimm(unsigned int type, unsigned long lba, unsigned int idx)
{	
	long lba_addr;
	long ramdisk;

	pr_cache("%s entered, type(%d) lba(0x%lx) idx(%d)\n", __func__, type, lba, idx);

	lba_addr = cache_ent_lba(cache_ent_tag(lba))*HV_BLOCK_SIZE;
	ramdisk = (type == CACHE_BSM) ? (long)bsm_ramdisk : (long)mmls_ramdisk;

	if (cache_desc_tbl[type][idx].tag == cache_ent_tag(lba) &&
	    cache_desc_tbl[type][idx].tag != CACHE_EMPTY_TAG) {
		/* merge w existing data and update partial cache entry buffer */
		/* read to tmp_cache_buf */
		if (get_ramdisk())
			cache_mcpy( (void *)tmp_cache_buf[type], (void *)(ramdisk + lba_addr), cache_ent_bytes);
		else
			read_hv(type, 
				lba,
				cache_ent_sectors,
				tmp_cache_buf[type]);

		/* merge tmp_cache_buf to cache*/
		merge_buffer_to_cache(tmp_cache_buf[type], type, idx);
	}
	else {
		/* update full cache entry buffer */
		if (get_ramdisk())
			cache_mcpy( (void *)(cache_ent_idx_addr(type,idx)),
			    	    (void *)(ramdisk + lba_addr), 
			    	    cache_ent_bytes);
		else
			read_hv(type, 
				lba,
				cache_ent_sectors,
				cache_ent_idx_addr(type,idx));

		/* mark clean */
		cache_desc_tbl[type][idx].dirty = CACHE_CLEAN;
	}
	/* update used_blks and tag */
	cache_desc_tbl[type][idx].used_blks = CACHE_BLK_ALL_FILLED;
	cache_desc_tbl[type][idx].tag = cache_ent_tag(lba);

	return 0;
}

static int write_cache_entry(unsigned int type, unsigned long lba, int sectors, void *data)
{
	unsigned int idx = cache_ent_idx(lba);
	unsigned int err=0;
	unsigned int hit;
	
	hit = check_cache_hit(type, idx, lba);
	switch(hit) {
	case CACHE_EMPTY:
		pr_cache("%s entered, cache EMPTY idx(%d)\n", __func__, idx);
	case CACHE_HIT:
		if (hit == CACHE_HIT)
			pr_cache("%s entered, cache HIT idx(%d)\n", __func__, idx);

		/* update cache entry buffer and used_blks bits n tag */
		cache_mcpy (cache_ent_lba_addr(type, lba), data, sectors*HV_BLOCK_SIZE);
		cache_desc_tbl[type][idx].used_blks |= calc_used_blks(lba, sectors);
		cache_desc_tbl[type][idx].tag = cache_ent_tag(lba);

		/* mark dirty */
		cache_desc_tbl[type][idx].dirty = CACHE_DIRTY;
		break;
	case CACHE_NO_HIT:
		pr_cache("%s entered, cache NO HIT idx(%d)\n", __func__, idx);
		/* flush cache entry buffer if dirty */
		if (cache_desc_tbl[type][idx].dirty == CACHE_DIRTY) {
			err = write_hvdimm(type, idx);
			if (err) 
				return err;
		}

		/* update cache entry buffer and mark used_blks bits n tag*/
		cache_desc_tbl[type][idx].used_blks = 0;
		cache_mcpy (cache_ent_lba_addr(type, lba), data, sectors*HV_BLOCK_SIZE);
		cache_desc_tbl[type][idx].used_blks |= calc_used_blks(lba, sectors);
		cache_desc_tbl[type][idx].tag = cache_ent_tag(lba);

		/* mark dirty */
		cache_desc_tbl[type][idx].dirty = CACHE_DIRTY;
		break;
	}
	return 0;
}

static int read_cache_entry(unsigned int type, unsigned long lba, int sectors, void *data)
{
	unsigned int idx = cache_ent_idx(lba);
	unsigned char used_blks=0;
	unsigned int err=0;
	
	switch(check_cache_hit(type, idx, lba)) {
	case CACHE_EMPTY:
		pr_cache("%s entered, cache EMPTY idx(%d)\n", __func__, idx);
		/* read from HV and update cache*/
		err = read_hvdimm(type, lba, idx);
		if (err) 
			return err;
		else
			/* return the data */
			cache_mcpy (data, cache_ent_lba_addr(type, lba), sectors*HV_BLOCK_SIZE);		
		break;
	case CACHE_HIT:
		pr_cache("%s entered, cache HIT idx(%d)\n", __func__, idx);
		used_blks = calc_used_blks(lba, sectors);

		/* if used_blks indicates data is on cache */
		if (bitmask_contain(cache_desc_tbl[type][idx].used_blks, used_blks)) {
			/* return the data */
			cache_mcpy (data, cache_ent_lba_addr(type, lba), sectors*HV_BLOCK_SIZE);
		}
		else {
			/* read from HVDIMM, merge w existing data */
			err = read_hvdimm(type, lba, idx);
			if (err)
				return err;
			else 
				/* return the data */
				cache_mcpy (data, cache_ent_lba_addr(type, lba), sectors*HV_BLOCK_SIZE);	
		}
		break;
	case CACHE_NO_HIT:
		pr_cache("%s entered, cache NOT HIT idx(%d)\n", __func__, idx);
		/* flush cache entry buffer if dirty */
		if (cache_desc_tbl[type][idx].dirty == CACHE_DIRTY) {
			err = write_hvdimm(type, idx);
			if (err)
				return err;
		}

		/* read from HV and update cache*/
		err = read_hvdimm(type, lba, idx);
		if (err)
			return err;
		else
			/* return the data */
			cache_mcpy (data, cache_ent_lba_addr(type, lba), sectors*HV_BLOCK_SIZE);

		break;
	}
	return 0;
}

static int cache_access(unsigned int type, unsigned long lba, int sectors, void *data, int access)
{
	int remain_sec;
	int err;

	if (lba % SECTOR_PER_BLOCK) {
		pr_cache("%s exit with error -1.\n", __func__);
		return -1;
	}

	remain_sec = cache_ent_sectors - cache_ent_sec_num(lba);
	while(sectors) {
		if (remain_sec >= sectors) {
			/* last cache entry */
			if (access == CACHE_WRITE) {
				err = write_cache_entry(type, lba, sectors, data);
				if (err)
					return err;
			}
			else {
				err = read_cache_entry(type, lba, sectors, data);
				if (err)
					return err;
			}
			return 0;
		}
		else {
			if (access == CACHE_WRITE) {
				err = write_cache_entry(type, lba, remain_sec, data);
				if (err)
					return err;
			}
			else {
				err = read_cache_entry(type, lba, remain_sec, data);
				if (err)
					return err;
			}

			sectors = sectors - remain_sec;
			lba = lba + remain_sec;
			data = (void *) ((unsigned long)data + remain_sec*HV_BLOCK_SIZE);

			/* now lba is aligned w the beginning of a new cache entry */
			remain_sec = cache_ent_sectors;
		}
	}
	return 0;
}

int cache_write(unsigned int type, unsigned long lba, int sectors, void *data, int tag, unsigned int async, void (*callback)(int tag, int err)) 
{
	int ret;

	pr_cache("%s entered, type(%d) lba(0x%lx) sectors(%d) data(0x%lx)\n", 
				__func__, type, lba, sectors, (unsigned long)data);
	ret = cache_access(type, lba, sectors, data, CACHE_WRITE);
	if (ret)
		pr_notice("%s exit w ERROR, type(%d) lba(0x%lx) sectors(%d) data(0x%lx)\n", 
				__func__, type, lba, sectors, (unsigned long)data);

	if (!async)
		return ret;	
	else {
		if (ret)
			return ret;
		else {
			if (callback)
				callback(tag, 1);
			return 1;	
		}
	}
}

int cache_read(unsigned int type, unsigned long lba, int sectors, void *data, int tag, unsigned int async, void (*callback)(int tag, int err))
{
	int ret;

	pr_cache("%s entered, type(%d) lba(0x%lx) sectors(%d) data(0x%lx)\n", 
				__func__, type, lba, sectors, (unsigned long)data);
	ret = cache_access(type, lba, sectors, data, CACHE_READ);
	if (ret)
		pr_notice("%s exit w ERROR, type(%d) lba(0x%lx) sectors(%d) data(0x%lx)\n", 
					__func__, type, lba, sectors, (unsigned long)data);

	if (!async)
		return ret;	
	else {
		if (ret)
			return ret;
		else 
			if (callback)
				callback(tag, 1);
			return 1;	
	}
}

static void cache_flush(unsigned int type)
{
	unsigned int idx;

	pr_cache("%s entered\n", __func__);

	/* flush out dirty entries */
	for (idx=0; idx<CACHE_ENTRIES_NUM; idx++) {
		if (cache_desc_tbl[type][idx].dirty == CACHE_DIRTY) {
			write_hvdimm(type, idx);
		}
	}
}

static enum hrtimer_restart hv_cache_callback(struct hrtimer *my_timer)
{
	pr_cache("%s entered\n", __func__);

	hrtimer_forward(my_timer, ktime_get(), ktime_set(CACHE_PERI_FLUSH, 0));
	cache_flush(CACHE_BSM);
	cache_flush(CACHE_MMLS);
	return HRTIMER_RESTART;
}

int cache_init(void)
{
	struct HV_BSM_IO_t bio_data;
	struct HV_MMLS_IO_t mio_data;

	pr_cache("%s entered\n", __func__);
	init_cache();

	if (get_ramdisk()) {
		get_bsm_iodata(&bio_data);
		bsm_ramdisk = (void *) bio_data.b_iomem;
		get_mmls_iodata(&mio_data);
		mmls_ramdisk = (void *) mio_data.m_iomem;
		pr_notice("%s entered, bsm_ramdisk(0x%lx) mmls_ramdisk(0x%lx)\n", 
				__func__, (unsigned long)bsm_ramdisk, (unsigned long)mmls_ramdisk);
	}

#ifdef USE_KZALLOC
        bsm_cache = kzalloc(cache_ent_bytes*CACHE_ENTRIES_NUM, GFP_KERNEL);
        mmls_cache = kzalloc(cache_ent_bytes*CACHE_ENTRIES_NUM, GFP_KERNEL);
#else
#ifdef MULTI_DIMM
        bsm_cache = hv_group[0].mem[0].v_dram+BSM_CACHE_OFF;
        mmls_cache = hv_group[0].mem[0].v_dram+MMLS_DRAM_OFF;
#else
        bsm_cache = BSM_CACHE_OFF;
        mmls_cache = MMLS_CACHE_OFF;
#endif
	memset(bsm_cache, 0, CACHE_MEM_SIZE);
	memset(mmls_cache, 0, CACHE_MEM_SIZE);
#endif

	pr_notice("%s entered, bsm_cache(0x%lx) mmls_cache(0x%lx)\n", 
			__func__, (unsigned long)bsm_cache, (unsigned long)mmls_cache);
	if ((unsigned long)bsm_cache == 0 || (unsigned long)mmls_cache == 0) {
		pr_notice("%s exit with ERROR.\n", __func__);
		return -1;
	}

	hrtimer_init(&hv_cache_flush.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hv_cache_flush.timer.function = hv_cache_callback;
	hrtimer_start(&hv_cache_flush.timer, ktime_set(CACHE_PERI_FLUSH, 0), HRTIMER_MODE_REL);

	return 0;
}

void cache_exit(void)
{
	pr_cache("%s entered\n", __func__);
	hrtimer_cancel(&hv_cache_flush.timer);
#ifdef USE_KZALLOC
	kfree(bsm_cache);
	kfree(mmls_cache);
#endif
}



