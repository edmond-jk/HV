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
#ifndef _HV_CACHE_H_
#define _HV_CACHE_H_

/* enable cache logging, default disabled */
// #define CACHE_LOGGING

/* use kzalloc() to alloc cache storage vs using static DRAM space */
// #define USE_KZALLOC

/* cache type */
#define	CACHE_BSM		0
#define CACHE_MMLS		1
#define CACHE_TYPE_NUM		2

/* cache opreation */
#define CACHE_READ		1
#define CACHE_WRITE		2

/* # of cache entries */
#ifdef USE_KZALLOC
/* kzalloc fails when using 512 */
#define CACHE_ENTRIES_NUM	256
#else
#define CACHE_ENTRIES_NUM	(CACHE_MEM_SIZE/cache_ent_bytes)
#endif

/* indicating cache entry is empty */
#define	CACHE_EMPTY_TAG		0xffffffffffffffff

/* cache entry */
#define	SECTOR_PER_BLOCK	8	/* sectors per block */
#define CACHE_LINE_SIZE		4	/* 4 blocks per cache entry */

/* interval of periodical cache flush in s */
#define CACHE_PERI_FLUSH	2

/* cache hit */
#define	CACHE_HIT		1
#define CACHE_NO_HIT		2
#define CACHE_EMPTY		3

/* dirty or clean */
#define CACHE_DIRTY		1
#define CACHE_CLEAN		2

/* cache entry all used mask */
#define	CACHE_BLK_ALL_FILLED	0x0f	/* assuming 4 blocks per cache entry */

struct hv_cache_desc {
	unsigned long 	tag;
	unsigned char	used_blks;	/* indicating occupied blocks */
	unsigned char	dirty;		/* dirty or clean */			
};

/* useful macros */
/* number of sectors in a cache entry buffer */
#define cache_ent_sectors		(CACHE_LINE_SIZE*SECTOR_PER_BLOCK)
/* number of bytes in a cache entry buffer */
#define cache_ent_bytes			(CACHE_LINE_SIZE*SECTOR_PER_BLOCK*HV_BLOCK_SIZE)
/* number of bytes in a cache entry block */
#define cache_ent_blk_bytes		(SECTOR_PER_BLOCK*HV_BLOCK_SIZE)
/* index to 1024 cache entries */
#define	cache_ent_idx(lba)		((lba/cache_ent_sectors)%CACHE_ENTRIES_NUM)
/* tag for lba */
#define	cache_ent_tag(lba)		(lba/cache_ent_sectors)
/* starting lba for tag */
#define	cache_ent_lba(tag)		(tag*cache_ent_sectors)
/* cache entry block # for lba */
#define cache_ent_blk_num(lba)		((lba%cache_ent_sectors)/SECTOR_PER_BLOCK)
/* cache entry sector # for lba */
#define cache_ent_sec_num(lba)		(lba%cache_ent_sectors)
/* starting DRAM addr of a cache entry buffer */
#define cache_ent_idx_addr(type,idx)	((type==CACHE_BSM?bsm_cache:mmls_cache) + \
					(idx*cache_ent_bytes))
/* starting DRAM addr on a cache entry buffer for lba */
#define cache_ent_lba_addr(type,lba)	((type==CACHE_BSM?bsm_cache:mmls_cache) + \
					(cache_ent_idx(lba)*cache_ent_bytes) + \
					(cache_ent_blk_num(lba)*cache_ent_blk_bytes))

int cache_init(void);
void cache_exit(void);
/*
 * type : CACHE_BSM or CACHE_MMLS
 * lba : target lba on BSM or MMLS
 * sectors : number of sectors
 * data : read/write data pointer
 */
int cache_write(unsigned int type, unsigned long lba, int sectors, void *data, int tag, unsigned int async, void (*callback)(int tag, int err));
int cache_read(unsigned int type, unsigned long lba, int sectors, void *data, int tag, unsigned int async, void (*callback)(int tag, int err));

#endif  /* _HV_CACHE_H_ */

