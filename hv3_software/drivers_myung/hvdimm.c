/*
 *
 *  HVDIMM block driver for BSM/MMLS.
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>

#include "hv_params.h"
#include "hv_mmio.h"
#include "hv_cmd.h"
#include "hvdimm.h"
#include "hv_timer.h"
#include "hv_queue.h"
#include "hv_cdev.h"
#include "hv_cache.h"
#include "hv_profile.h"

/* ----- Definitions ---- */

#define MAX_PARTITIONS 1
#define KERNEL_SECTOR_SIZE 512

/*
 * The following macros are needed to support definition changes
 * in different versions of the kernel
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
/* Device address in sectors */
#define BIO_SECTOR(bio)		    ((bio)->bi_iter.bi_sector)
#else
#define BIO_SECTOR(bio)		    ((bio)->bi_sector)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0)
#define BIO_KMAP_ATOMIC(bio, i)		__bio_kmap_atomic(bio, i, KM_USER0)
#define BIO_KUNMAP_ATOMIC(buffer)	__bio_kunmap_atomic(buffer, KM_USER0)
#else
#define BIO_KMAP_ATOMIC(bio, i)		__bio_kmap_atomic(bio, i)
#define BIO_KUNMAP_ATOMIC(bio)		__bio_kunmap_atomic(bio)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 3, 0)
/* Device address in sectors */
#define BIO_ENDIO(bio, err)		do { if(err) bio_io_error(bio); else bio_endio(bio);} while (0)
#else
#define BIO_ENDIO(bio, err)	    bio_endio(bio,err)
#endif

/* ----- Structures and Global Data ---- */

enum { /* device type */
	HV_DTYPE_BSM = 0,
	HV_DTYPE_MMLS = 1,
	HV_DTYPE_NUM = 2,
};

#ifdef DEMO_STUB_DRV
const char* HV_DISK_NAME[] = {"bsm", "pmem" }; // for demo
#else
const char* HV_DISK_NAME[] = {"hv_bsm", "hv_mmls" };
#endif

/*
 * The internal representation of our device.
 */
struct hv_device {
	struct list_head list;
	unsigned long size;
	unsigned long nsectors;
	u8 *data;
	struct request_queue *queue;
	struct gendisk *disk;
	struct hv_device_operations *ops;
};

struct hv_device_operations {
	int (*xfer) (struct hv_device *, sector_t, unsigned long, char *, int);
	int (*iomem_init) (struct hv_device *);
	void (*iomem_release) (struct hv_device *);
};

struct hv_device_operations hv_devops[HV_DTYPE_NUM];

static spinlock_t hv_lock;
static LIST_HEAD(hv_devlist);

static int bsm_major_num = 0;
static int mmls_major_num = 0;
static int bsm_init_status = 0;
static int mmls_init_status = 0;

static struct HV_BSM_IO_t BSM_io_data;
static struct HV_MMLS_IO_t MMLS_io_data;

static void hv_transfer_callback(int tag, int err)
{
	struct bio *bio;
	unsigned char is_last_segment=0;

	if (err == 1){
		/* called by CMD driver indicating no error and not queued */
	}
	else {
		bio = (struct bio *) hv_dequeue_cmdq(tag, &is_last_segment);	
		if (err)
			BIO_ENDIO(bio, -EIO);
		else if (is_last_segment) 
			BIO_ENDIO(bio, 0);
	}
}

void hv_memcpy_toio(void __iomem *dst, void *src, int count, int async,
		void (*callback)(int tag, int err), int tag)
{
	if (!async)
		memcpy_toio(dst, src, count);
	else {
		hv_timer_ops.set_timer_data(dst,src,count,async,callback,tag);
		hv_timer_ops.start(tag, hv_timer_ops.memcpy_toio_callback);
		hv_log("%s:   (tag %d)\n", __func__, tag);
	}
}

void hv_memcpy_fromio(void *dst, void __iomem *src, int count,
		int async, void (*callback)(int tag, int err), int tag)
{
	if (!async)
		memcpy_fromio(dst, src, count);
	else {
		hv_timer_ops.set_timer_data(dst,src,count,async,callback,tag);
		hv_timer_ops.start(tag, hv_timer_ops.memcpy_fromio_callback);
		hv_log("%s:   (tag %d)\n", __func__, tag);
	}
}

int hv_ramdisk_xfer(struct hv_device *dev, sector_t sector, unsigned long nsect, char *buffer, int write)
{

	unsigned long offset = sector * HV_BLOCK_SIZE;
	unsigned long nbytes = nsect * HV_BLOCK_SIZE;

	hv_log("%s_%s:   (offset x%lx nbytes %ld)\n", __func__,
			   (write) ? "write": "read", offset, nbytes);

	if (!dev->data) {
		pr_err("%s:dev->data is NULL\n", __func__);
		goto err_ptr;
	}

	if (!buffer) {
		pr_err("%s:buffer is NULL", __func__);
		goto err_ptr;
	}

	if (get_cache_enabled()) {
		if (write)
			return (cache_write(CACHE_BSM, sector, nsect, buffer, 
				hv_next_cmdq_tag(), get_async_mode(), hv_transfer_callback));
		else
			return (cache_read(CACHE_BSM, sector, nsect, buffer,
				hv_next_cmdq_tag(), get_async_mode(), hv_transfer_callback));
	}
	else {
		if (write)
			hv_memcpy_toio(dev->data + offset, buffer, nbytes, get_async_mode(),
				hv_transfer_callback, hv_next_cmdq_tag());
		else

			hv_memcpy_fromio(buffer, dev->data + offset, nbytes,
				get_async_mode(), hv_transfer_callback, hv_next_cmdq_tag());
		return 0;
	}

err_ptr:
	pr_err("%s Exit. Cannot allocate memory.\n", __func__);
	return -EIO;
}

int hv_bsm_xfer(struct hv_device *dev, sector_t sector, unsigned long nsect, char *buffer, int write)
{
	hv_log("%s_%s:   (offset x%lx nbytes %ld)\n", __func__,
			   (write) ? "write": "read",
			   sector * KERNEL_SECTOR_SIZE, nsect * KERNEL_SECTOR_SIZE);

	if (get_cache_enabled()) {
		if (write)
			return (cache_write(CACHE_BSM, sector, nsect, buffer, hv_next_cmdq_tag(), get_async_mode(), hv_transfer_callback));
		else
			return (cache_read(CACHE_BSM, sector, nsect, buffer, hv_next_cmdq_tag(), get_async_mode(), hv_transfer_callback));
		}
	else {
		if (write)
			return (bsm_write_command(hv_next_cmdq_tag(), nsect, sector, (unsigned char *) buffer, get_async_mode(), hv_transfer_callback));
		else
//MK0207			return (bsm_read_command(hv_next_cmdq_tag(), nsect, sector, (unsigned char *) buffer, get_async_mode(), hv_transfer_callback));
//MK0207-begin
			return (bsm_read_command(hv_next_cmdq_tag(), nsect, sector, (unsigned char *) buffer, get_async_mode(), hv_transfer_callback, 0));
//MK0207-end
	}
}

int hv_mmls_xfer(struct hv_device *dev, sector_t sector, unsigned long nsect, char *buffer, int write)
{
	hv_log("%s_%s:   (offset x%lx nbytes %ld)\n", __func__,
			   (write) ? "write": "read",
			   sector * KERNEL_SECTOR_SIZE, nsect * KERNEL_SECTOR_SIZE);

	if (get_cache_enabled()) {
		if (write)
			return (cache_write(CACHE_MMLS, sector, nsect, buffer, hv_next_cmdq_tag(), get_async_mode(), hv_transfer_callback));
		else
			return (cache_read(CACHE_MMLS, sector, nsect, buffer, hv_next_cmdq_tag(), get_async_mode(), hv_transfer_callback));
	}
	else {
		if (write)
			return (mmls_write_command(hv_next_cmdq_tag(), nsect, sector, (unsigned long)buffer, get_async_mode(), hv_transfer_callback));
		else
			return (mmls_read_command(hv_next_cmdq_tag(), nsect, sector, (unsigned long)buffer, get_async_mode(), hv_transfer_callback));
	}
}

static int hv_transfer(struct hv_device *dev, sector_t sector,
		unsigned long nsect, char *buffer, int write)
{
	unsigned long offset = sector * KERNEL_SECTOR_SIZE;
	unsigned long nbytes = nsect * KERNEL_SECTOR_SIZE;

	if ((offset + nbytes) > dev->size) {
		pr_notice("%s: Beyond-end write (%ld %ld)\n", __func__, offset, nbytes);
		return -EIO;
	}
	return (dev->ops->xfer(dev, sector, nsect, buffer, write));
}

/*
 * Transfer a single BIO.
 */
static void hv_xfer_bio(struct hv_device *dev, struct bio *bio)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	struct bio_vec bvec;
        struct bvec_iter iter;
#else
	struct bio_vec *bvec;
	int iter;
#endif
	sector_t sector = BIO_SECTOR(bio);
	int nr_segments = bio_segments(bio);
	int err=0;
	unsigned int is_last_segment=0;

	/* Do each segment independently. */
	bio_for_each_segment(bvec, bio, iter) {
		char *buffer = BIO_KMAP_ATOMIC(bio, iter);

		/* wait if q is full */
		if (get_async_mode()) {
			hv_cmdq_queue_full_wait();
		}

		// spin_lock_irqsave(&hv_lock, flags);

		err = hv_transfer(dev, sector, bio_cur_bytes(bio) >> 9,
				buffer, bio_data_dir(bio) == WRITE);
		sector += bio_cur_bytes(bio) >> 9;

		BIO_KUNMAP_ATOMIC(bio);

		/* Track each bio segment as a separate cmd,
		 * but only store bio of last segment, so that bio_endio
		 * call be called during hv_transfer_callback */
		is_last_segment = (nr_segments == 1);
		if (get_async_mode()) {
			if (!err)
				hv_queue_cmdq((unsigned long)bio, is_last_segment);
		}

		nr_segments--;

		if (is_last_segment) {
			if (err == 1)
				BIO_ENDIO(bio, 0);
			else if (err)
				BIO_ENDIO(bio, -EIO);
			else if (!get_async_mode())
				BIO_ENDIO(bio, 0);
		}

		// spin_unlock_irqrestore(&hv_lock, flags);
	}
}

static void hv_bio_request(struct request_queue *q, struct bio *bio)
{
	struct hv_device *dev = q->queuedata;

	hv_xfer_bio(dev, bio);
}

/*
 * The HDIO_GETGEO ioctl is handled in blkdev_ioctl(), which
 * calls this. We need to implement getgeo, since we can't
 * use tools such as fdisk to partition the drive otherwise.
 */
int hv_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	long size;
	struct hv_device *hvdev = bdev->bd_queue->queuedata;

	hv_log("%s entered.\n", __func__);

	/* We have no real geometry, of course, so make something up. */
	size = hvdev->size * (HV_BLOCK_SIZE / KERNEL_SECTOR_SIZE);
	geo->cylinders = (size & ~0x3f) >> 6;
	geo->heads = 4;
	geo->sectors = 16;
	geo->start = 0;
	return 0;
}

/* The block device operations structure */
static const struct block_device_operations hv_blkdev_ops = {
	.owner  = THIS_MODULE,
	.getgeo = hv_getgeo,
};

/* ----- Initialization Functions ---- */

static int hv_bsm_io_init(struct hv_device *hvdev)
{
	get_bsm_iodata(&BSM_io_data);
	hvdev->nsectors = (BSM_io_data.b_size)/KERNEL_SECTOR_SIZE;	/* bsm_size */
	hvdev->data = BSM_io_data.b_iomem;
	return 0;
}

static void hv_bsm_iomem_release(struct hv_device *hvdev)
{
}


static int hv_mmls_io_init(struct hv_device *hvdev)
{
	get_mmls_iodata(&MMLS_io_data);
	hvdev->nsectors = (MMLS_io_data.m_size)/KERNEL_SECTOR_SIZE;	/* mmls_size */
	hvdev->data = MMLS_io_data.m_iomem;	/* mmls_iomem */
	return 0;
}

static void hv_mmls_iomem_release(struct hv_device *hvdev)
{
}

static struct dentry *dirret, *ret;
static u64 pmem_value, bsm_value;
static int demo_debugfs_caps(void)
{
        pr_info("%s: entered demo_debugfs_caps ...\n", __func__);

        /* create a directory by the name dell in /sys/kernel/debugfs */
        dirret = debugfs_create_dir("hv_demo", NULL);

        /* create pmem_default_size */
        ret = debugfs_create_u64("pmem_default_size", 0644, dirret, &pmem_value);
        if (!ret) {
	        pr_err("error in creating pmem_default_size");
	        return (-ENODEV);
        }
        /* create bsm_default_size */
        ret = debugfs_create_u64("bsm_default_size", 0644, dirret, &bsm_value);
        if (!ret) {
	        pr_err("error in creating bsm_default_size");
	        return (-ENODEV);
        }
        return 0;
}

static int hv_add_disk(unsigned char dtype, int nid)
{
	struct hv_device *hvdev = NULL;
	struct gendisk *disk = NULL;
	int hv_major_num = 0;	

	hv_log("%s: entered\n", __func__);

        hvdev = kzalloc(sizeof(struct hv_device), GFP_KERNEL);
	if (!hvdev) {
		pr_err("Unable to allocate memory for disk %s0\n", HV_DISK_NAME[dtype]);
		return -ENOMEM;
	}

	hvdev->ops = &hv_devops[dtype];

	switch (dtype) {
	case HV_DTYPE_BSM:
		hvdev->ops->iomem_init = hv_bsm_io_init;
		hvdev->ops->iomem_release = hv_bsm_iomem_release;
		hvdev->ops->xfer = get_ramdisk() ? hv_ramdisk_xfer : hv_bsm_xfer;
		break;
	case HV_DTYPE_MMLS:
		hvdev->ops->iomem_init = hv_mmls_io_init;
		hvdev->ops->iomem_release = hv_mmls_iomem_release;
		hvdev->ops->xfer = get_ramdisk() ? hv_ramdisk_xfer : hv_mmls_xfer;
		break;
	}

	/* Set up device. */
	hvdev->ops->iomem_init(hvdev);
	hvdev->size = hvdev->nsectors * KERNEL_SECTOR_SIZE;

	if (!hvdev->size) {
		pr_err("%s: Disk not allocated, size=0", __func__);
		goto err_add_dev;
	}

	/* Get registered */
	hv_major_num = register_blkdev(hv_major_num, HV_DISK_NAME[dtype]);
	if (hv_major_num < 0) {
		pr_warn("unable to get major number\n");
		goto err_add_dev;
	}
	if (dtype == HV_DTYPE_BSM)
		bsm_major_num = hv_major_num;
	else
		mmls_major_num = hv_major_num;

	/* And the gendisk structure. */
	disk = alloc_disk_node(MAX_PARTITIONS+1, nid);
	if (!disk) {
		pr_err("%s: Failed to allocate disk", __func__);
		goto err_add_dev;
	}
	disk->major = hv_major_num;
	disk->first_minor = 0;
	disk->fops = &hv_blkdev_ops;
	disk->private_data = hvdev;
	sprintf(disk->disk_name, "%s0", HV_DISK_NAME[dtype]);
	set_capacity(disk, hvdev->nsectors);
	hvdev->disk = disk;

	/* Setup request queue */
	hvdev->queue = blk_alloc_queue_node(GFP_KERNEL, nid);
	if (hvdev->queue == NULL)
		goto err_add_dev;
	hvdev->queue->queuedata = hvdev;
	disk->queue = hvdev->queue;
	blk_queue_make_request(hvdev->queue, (make_request_fn *)hv_bio_request);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, disk->queue);
	blk_queue_logical_block_size(hvdev->queue, FS_BLOCK_SIZE);
	blk_queue_physical_block_size(hvdev->queue, FS_BLOCK_SIZE);
	/* For driver bring-up purposes, support only one sector at a time*/
	blk_queue_max_hw_sectors(disk->queue, 1);

	list_add_tail(&hvdev->list, &hv_devlist);

	/* The disk is "live" at this point */
	add_disk(disk);
	pr_info("Creating disk %s, size=0x%lx, nsectors=%lu\n",
		disk->disk_name, hvdev->size, hvdev->nsectors);

	// create debugfs for demo
	pr_info("Creating demo debug fs\n");
	demo_debugfs_caps();
	// init the dummy debugfs caps
	pmem_value = get_pmem_default_size();
	bsm_value = get_bsm_default_size();

	return 0;

err_add_dev:
	if (hvdev->queue)
		blk_cleanup_queue(hvdev->queue);
	if (hvdev->data)
		hvdev->ops->iomem_release(hvdev);
	kfree(hvdev);


	if (hv_major_num)
        	unregister_blkdev(hv_major_num, HV_DISK_NAME[dtype]);

	return -EINVAL;
}

static void hv_del_all_disks(void)
{
	struct hv_device *hvdev, *next;

	hv_log("%s entered\n", __func__);

	list_for_each_entry_safe(hvdev, next, &hv_devlist, list) {
		list_del(&hvdev->list);
		pr_info("Removing disk %s\n", hvdev->disk->disk_name);
		del_gendisk(hvdev->disk);
		put_disk(hvdev->disk);
		blk_cleanup_queue(hvdev->queue);
		hvdev->ops->iomem_release(hvdev);
		kfree(hvdev);
	}
}

extern int __init hv_cdev_init(int cdev_type);
extern void __exit hv_cdev_exit(int cdev_type);

static int __init hv_init(void)
{
	int nid = NUMA_NO_NODE;

//MK	hv_log("%s: entered\n", __func__);
//MK-begin
	hv_log("[%s]: entered\n", __func__);
//MK-end

	spin_lock_init(&hv_lock);

	hv_io_init();
	hv_profile_init();

	if (get_cache_enabled()) {
		if (cache_init()) {
			pr_notice("ERROR %s: cache_init failed", __func__);
			pr_notice("ERROR %s: cache_init failed", __func__);
			return -1;
		}
	}

	bsm_init_status = mmls_init_status = -1;
	hv_timer_ops.init();
	if (get_single_cmd_test() == 0) {
		spin_for_cmd_init();
		if (bsm_size()) {
			if (!bsm_cdev())
				bsm_init_status = hv_add_disk(HV_DTYPE_BSM, nid);
			else
				bsm_init_status = hv_cdev_init(HV_CDEV_BSM);
		}
		if (mmls_size()) {
			if (!mmls_cdev())
				mmls_init_status = hv_add_disk(HV_DTYPE_MMLS, nid);
			else
				mmls_init_status = hv_cdev_init(HV_CDEV_MMLS);
		}
		return (bsm_init_status & mmls_init_status);
	} else {
		single_cmd_init();
	}

//MK	return 0;
//MK-begin
	// Enable communication with HVDIMM & train ECC table
	hv_open_sesame();
	return(hv_train_ecc_table());
//MK-end
}

static void __exit hv_exit(void)
{
	hv_log("%s: entered\n", __func__);
	if (bsm_init_status == 0) {
		if (!bsm_cdev()) 
			unregister_blkdev(bsm_major_num, HV_DISK_NAME[HV_DTYPE_BSM]);
		else
			hv_cdev_exit(HV_CDEV_BSM);
	}
	if (mmls_init_status == 0) {
		if (!mmls_cdev())
			unregister_blkdev(mmls_major_num, HV_DISK_NAME[HV_DTYPE_MMLS]);
		else
			hv_cdev_exit(HV_CDEV_MMLS);
	}
	if (get_single_cmd_test() == 0) {
		hv_del_all_disks();
	} else
		single_cmd_exit();


	if (get_cache_enabled())
		cache_exit();

	hv_io_release();
	
	hv_profile_print();
}

module_init(hv_init);
module_exit(hv_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Netlist, Inc. <info@netlist.com>");
MODULE_DESCRIPTION("Netlist HVDIMM device driver");
MODULE_VERSION(".01 Alpha");
MODULE_INFO(Copyright, "Copyright (c) 2015 Netlist Inc.  All rights reserved.");
