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

/**
 * For SIMULATION_TB to work, the following addresses have to be hardcoded 
 * on data structure below
 *
 * p_dram and p_mmio are starting physcal addresses of HV DRAM and MMIO space
 *
 * v_*mmio are MMIO addresses where the driver can access cmd/status and bsm data
 * v_dram is the address where the driver can accesss HV DRAM. The v_* addresses 
 * can be different from physical addresses if virutal addresses are used like on linux
 *
	0x100000000,		// p_dram
	(void *)0x100000000,	// v_dram
	0x180000000,		// p_mmio
	(void *)0x180000000,	// v_mmio
	(void *)(0x180000000+MMIO_BSM_W_OFF),	// v_bsm_w_mmio
	(void *)(0x180000000+MMIO_BSM_R_OFF),	// v_bsm_r_mmio
	(void *)(0x180000000+MMIO_OTHER_OFF),	// v_other_mmio
 */

#include "../hv_mmio.h"

#define ONE_WAY

/* these tables are filled up by discoveryy module and consumed by HV module */
struct hv_description_tbl hv_desc[MAX_HV_DIMM];

struct hv_group_tbl hv_group[MAX_HV_GROUP] =
{	// hv_group[]
	{
		0,		// gid
		0x400000,	// bsm_size
		0x400000,	// mmls_size
		DEV_BLOCK,	// bsm_device
		DEV_CHAR,	// mmls_device
#ifdef ONE_WAY
		1,		// num_hv
#endif
#ifdef TWO_WAY
		2,		// num_hv
#endif
#ifdef FOUR_WAY
		4,		// num_hv
#endif
		{	// emmc[]
		},
		{	// intv
#ifdef ONE_WAY
			1,		// chnl_way
#endif
#ifdef TWO_WAY
			2,		// chnl_way
#endif
#ifdef FOUR_WAY
			4,		// chnl_way
#endif
			MEM_BURST_SIZE,	// chnl_size
			1,		// rank_way;
		},
		{	// mem[]
			{
#ifndef REV_B_MM
			0x100000000,		// p_dram
			(void *)0x100000000,	// v_dram
			0x180000000,		// p_mmio
			(void *)0x180000000,	// v_mmio
			(void *)(0x180000000+MMIO_BSM_W_OFF),	// v_bsm_w_mmio
			(void *)(0x180000000+MMIO_BSM_R_OFF),	// v_bsm_r_mmio
			(void *)(0x180000000+MMIO_OTHER_OFF),	// v_other_mmio
#else
			0x100000000,		// p_dram
			(void *)0x100000000,	// v_dram
			(HV_DRAM_SIZE - HV_MMIO_SIZE),		// p_mmio
			(void *)(HV_DRAM_SIZE - HV_MMIO_SIZE),	// v_mmio
#endif
			},
		},
	},
};

