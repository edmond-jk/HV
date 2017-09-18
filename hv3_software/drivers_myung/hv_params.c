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

#include "hv_mmio.h"
#include "hv_cmd.h"
#include "hvdimm.h"
#include "hv_timer.h"
#include "hv_queue.h"
#include "hv_cdev.h"

extern struct hv_group_tbl hv_group[MAX_HV_GROUP];;
extern struct hv_description_tbl hv_desc[MAX_HV_DIMM];

/* ----- Module Parameters ---- */
/*************************************************************************
#	hv_mmap_type:
#		HV mem mmap type: 0: wb, 1: wc, 2: uncached
#	async_mode: 0-disabled (default) 1-enabled
#		when disabled, driver confirms a HV command has completed before
#		sending next command. when enabled, driver is allowed to send 
#		multiple HV commands per queue_size
#	queue_size: 2-64 (default 64)
#		size of queue when async_mode is set to 1
#	use_memmap: 0 (default)
#		use memmap when reserving HVDIMM space during linux boot
#	ramdisk: 0-disabled 1-enabled (default)
#		use system memory as storage instead of HVDIMM
#	ramdisk_start: (default 0x100000000)
#		beginning physical address of system memory used as ramdisk
# 	single_cmd_test: 1-enable hv_cmd debugfs, 0-disable (default)
*************************************************************************/

static int hv_mmap_type;
module_param(hv_mmap_type, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hv_mmap_type, "HV mem mmap type: 0: wb, 1: wc, 2: uncached");
int get_hv_mmap_type(void) { return hv_mmap_type; }

static int async_mode = 0;
module_param(async_mode, int, S_IRUGO);
MODULE_PARM_DESC(async_mode,
		" 1=Multiple Outstanding Cmds, 0=Single Outstanding Cmd (Default)\n");
int get_async_mode(void) { return async_mode; }

static int queue_size = 64;
module_param(queue_size, int, S_IRUGO);
MODULE_PARM_DESC(queue_size,
		" Size of queue entries, maximum 64\n");
int get_queue_size(void) { return queue_size; }

static long use_memmap = 0x1;
module_param(use_memmap, long, 0);
MODULE_PARM_DESC(use_memmap, " Use memmap to reserve HVDIMM space during linux boot\n");
long get_use_memmap(void) { return use_memmap; }

static int ramdisk = 0x1;
module_param(ramdisk, int, S_IRUGO);
MODULE_PARM_DESC(ramdisk, " 1=Use Ramdisk (Default), 0=Use Hardware\n");
int get_ramdisk(void) { return ramdisk; }

static long ramdisk_start = 0x100000000;
module_param(ramdisk_start, long, 0);
MODULE_PARM_DESC(ramdisk_start, " starting system memory address for Ramdisk\n");
long get_ramdisk_start(void) { return ramdisk_start; }

static int single_cmd_test = 0x0;
module_param(single_cmd_test, int, 0);
MODULE_PARM_DESC(single_cmd_test, " 1=Test hv_cmd Utility, 0=Block Driver (Default)\n");
int get_single_cmd_test(void) { return single_cmd_test; }

static int cache_enabled = 0x0;
module_param(cache_enabled, int, 0);
MODULE_PARM_DESC(cache_enabled, " 1=Cache Enabled, 0=Cache Disabled (Default)\n");
int get_cache_enabled(void) { return cache_enabled; }

static long pmem_default_size = 0x380000*512;//0x4000000000; //256GB
module_param(pmem_default_size, long, 0);
MODULE_PARM_DESC(pmem_default_size, " pmem_default_size (Default 1.8GB)\n");
long get_pmem_default_size(void) { return pmem_default_size; }

static long bsm_default_size = 0x380000*512;//0x4000000000; //256GB
module_param(bsm_default_size, long, 0);
MODULE_PARM_DESC(bsm_default_size, " bsm_default_size (Default 1.8GB)\n");
long get_bsm_default_size(void) { return bsm_default_size; }
