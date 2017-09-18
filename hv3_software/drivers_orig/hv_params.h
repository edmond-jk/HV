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
#ifndef _HV_PARAMS_H_
#define _HV_PARAMS_H_

int get_hv_mmap_type(void);
int get_async_mode(void);
int get_queue_size(void);
long get_use_memmap(void);
int get_ramdisk(void);
long get_ramdisk_start(void);
int get_single_cmd_test(void);
int get_cache_enabled(void);
long get_pmem_default_size(void);
long get_bsm_default_size(void);
#endif  /* _HVDIMM_H_ */
