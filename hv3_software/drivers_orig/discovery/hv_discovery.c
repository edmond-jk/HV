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

/*
===============================================================================
Driver Name		:		hv_discovery
Author			:		sgosali@netlist.com
License			:		GPL
Description		:		Kernel Module for HybriDIMM Discovery
===============================================================================
*/

#include "hv_discovery.h"
#include "../hvdimm.h"
#include "../hv_mmio.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("sgosali@netlist.com");
MODULE_DESCRIPTION("HyperVault discovery");

#define ONE_WAY

/* these tables are filled up by discoveryy module and consumed by HV module */
struct hv_description_tbl hv_desc[MAX_HV_DIMM];
EXPORT_SYMBOL(hv_desc);

//MK1101-begin
/* HybriDIMM info for each node */
struct hdimm_info hd_desc[MAX_NPS];
EXPORT_SYMBOL(hd_desc);
//MK1101-end

//MK0412-begin
#define  SMB_TIMEOUT    100000000	// 100 ms

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
//MK0412-end

//MK1104-begin
/* This driver */
static const unsigned int spd_dimm_id_table[MAX_CPN][MAX_DPC] = {
	{0, 1, 2}, {3,  4,  5}, {12, 13, 14}, {15, 16, 17},
	{6, 7, 8}, {9, 10, 11},	{18, 19, 20}, {21, 22, 23}
};
//MK1104-end

struct hv_group_tbl hv_group[MAX_HV_GROUP] =
{	// hv_group[]
	{
		0,		// gid
//MK		0x380000,	// bsm_size
//MK		0x380000,	// mmls_size
//MK-begin
#ifdef SW_SIM
//		0x800000,	// bsm_size  = 0x0080_0000 sectors = 4GB
		0x7CE000,	// bsm_size  = 0x007C_E000 sectors = 4GB - 100MB
		0x000000,	// mmls_size = 0 sectors
#else
		0x380000,	// bsm_size
		0x380000,	// mmls_size
#endif
//MK-end
		DEV_BLOCK,	// bsm_device
		DEV_CHAR,	// mmls_device
#ifdef ONE_WAY
		1,			// num_hv
#endif
#ifdef TWO_WAY
		2,			// num_hv
#endif
#ifdef FOUR_WAY
		4,			// num_hv
#endif
		{			// emmc[]
		},
		{			// intv
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
#ifndef 	REV_B_MM
			{
			0x100000000,	// p_dram
			0x0,			// v_dram
			0x180000000,	// p_mmio
			0x0,			// v_mmio
			0x0,			// v_bsm_w_mmio
			0x0,			// v_bsm_r_mmio
			0x0,			// v_other_mmio
			},
#else
			{
//MK			0x0,		// p_dram
//MK			0x0,		// v_dram
//MK			HV_DRAM_SIZE - HV_MMIO_SIZE,	// p_mmio
//MK			0x0,		// v_mmio
//MK-begin
//MK These values need to be hard coded for now. Eventually the discovery code
//MK will fill in the table. This table represents a system with one 8GB hvdimm.
//MK The system reserved 2GB memory space @ 2GB physical address and this
//MK divides the HVDIMM DRAM area into two sections: 0 ~ 2GB & 4GB ~ 10GB.
			0,					// p_dram, DRAM physical base address
			(void *) 0,			// v_dram, DRAM virtual base address
/*
 * When SW_SIM enabled - 4GB of RAMDISK space is created @ 4GB addr
 * using kernel parameter, memmap at boot time. (memmap=4G\$4G)
 * In this case, we can use the top of RAMDISK space for MMIO
 * since we don't actually communicate with FPGA.
 */

/*
 * p_mmio (= 26GB - 1MB) for 16GB LRDIMM + 8GB HVDIMM
 * p_mmio (= 18GB - 1MB) for 8GB LRDIMM + 8GB HVDIMM
 * p_mmio (= 8GB - 1MB) for 8GB HVDIMM
 */
//MK			HV_DRAM_SIZE - HV_MMIO_SIZE,	// p_mmio
			TOP_OF_SYS_MEM - HV_MMIO_SIZE,	// p_mmio
			0,								// v_mmio
//MK-end
			},
#endif
		},
	},
};
EXPORT_SYMBOL(hv_group);

/**********************************************
* Discovery pre-integration code starts here
*/

static uint num_hv;
static uint num_nodes;
static uint node0_ch_way = 1;
static uint node1_ch_way = 0;   /* 0 = node is not used */
static ulong hv_start_addr0;
static ulong hv_start_addr1;
static ulong hv_mmio_start0;
static ulong hv_mmio_start1;
static ulong hv_mem_size0;
static ulong hv_mem_size1;
static ulong hv_mmio_size0;
static ulong hv_mmio_size1;

typedef struct pci_private {
	struct pci_dev *pdev;
	spinlock_t hv_discovery_spinlock;
} hv_discovery_private;

/* Static vars */
static LIST_HEAD(sbridge_edac_list);
static DEFINE_MUTEX(sbridge_edac_lock);
static int probed;

/*
 * Alter this version for the module when modifications are made
 */
#define SBRIDGE_REVISION    " Ver: 1.1.1 "
#define EDAC_MOD_STR      "sbridge_edac"

/*
 * Get a bit field at register value <v>, from bit <lo> to bit <hi>
 */

#define GET_BITFIELD(v, lo, hi)	\
	(((v) & GENMASK_ULL(hi, lo)) >> (lo))

/* Devices 12 Function 6, Offsets 0x80 to 0xcc */
static const u32 sbridge_dram_rule[] = {
	0x80, 0x88, 0x90, 0x98, 0xa0,
	0xa8, 0xb0, 0xb8, 0xc0, 0xc8,
};

static const u32 ibridge_dram_rule[] = {
	0x60, 0x68, 0x70, 0x78, 0x80,
	0x88, 0x90, 0x98, 0xa0,	0xa8,
	0xb0, 0xb8, 0xc0, 0xc8, 0xd0,
	0xd8, 0xe0, 0xe8, 0xf0, 0xf8,
};

static const u32 knl_dram_rule[] = {
	0x60, 0x68, 0x70, 0x78, 0x80, /* 0-4 */
	0x88, 0x90, 0x98, 0xa0, 0xa8, /* 5-9 */
	0xb0, 0xb8, 0xc0, 0xc8, 0xd0, /* 10-14 */
	0xd8, 0xe0, 0xe8, 0xf0, 0xf8, /* 15-19 */
	0x100, 0x108, 0x110, 0x118,   /* 20-23 */
};

#define DRAM_RULE_ENABLE(reg)	GET_BITFIELD(reg, 0,  0)
#define A7MODE(reg)		GET_BITFIELD(reg, 26, 26)

/* From edac_mc.c */
const char *edac_layer_name[] = {
	[EDAC_MC_LAYER_BRANCH] = "branch",
	[EDAC_MC_LAYER_CHANNEL] = "channel",
	[EDAC_MC_LAYER_SLOT] = "slot",
	[EDAC_MC_LAYER_CHIP_SELECT] = "csrow",
	[EDAC_MC_LAYER_ALL_MEM] = "memory",
};
/************************************/

static char *show_dram_attr(u32 attr)
{
	switch (attr) {
		case 0:
			return "DRAM";
		case 1:
			return "MMCFG";
		case 2:
			return "NXM";
		default:
			return "unknown";
	}
}

static const u32 sbridge_interleave_list[] = {
	0x84, 0x8c, 0x94, 0x9c, 0xa4,
	0xac, 0xb4, 0xbc, 0xc4, 0xcc,
};

static const u32 ibridge_interleave_list[] = {
	0x64, 0x6c, 0x74, 0x7c, 0x84,
	0x8c, 0x94, 0x9c, 0xa4, 0xac,
	0xb4, 0xbc, 0xc4, 0xcc, 0xd4,
	0xdc, 0xe4, 0xec, 0xf4, 0xfc,
};

static const u32 knl_interleave_list[] = {
	0x64, 0x6c, 0x74, 0x7c, 0x84, /* 0-4 */
	0x8c, 0x94, 0x9c, 0xa4, 0xac, /* 5-9 */
	0xb4, 0xbc, 0xc4, 0xcc, 0xd4, /* 10-14 */
	0xdc, 0xe4, 0xec, 0xf4, 0xfc, /* 15-19 */
	0x104, 0x10c, 0x114, 0x11c,   /* 20-23 */
};

struct interleave_pkg {
	unsigned char start;
	unsigned char end;
};

static const struct interleave_pkg sbridge_interleave_pkg[] = {
	{ 0, 2 },
	{ 3, 5 },
	{ 8, 10 },
	{ 11, 13 },
	{ 16, 18 },
	{ 19, 21 },
	{ 24, 26 },
	{ 27, 29 },
};

static const struct interleave_pkg ibridge_interleave_pkg[] = {
	{ 0, 3 },
	{ 4, 7 },
	{ 8, 11 },
	{ 12, 15 },
	{ 16, 19 },
	{ 20, 23 },
	{ 24, 27 },
	{ 28, 31 },
};

static inline int sad_pkg(const struct interleave_pkg *table, u32 reg,
			  int interleave)
{
	return GET_BITFIELD(reg, table[interleave].start,
			    table[interleave].end);
}

/* Devices 12 Function 7 */

#define TOLM		0x80
#define TOHM		0x84
#define HASWELL_TOLM	0xd0
#define HASWELL_TOHM_0	0xd4
#define HASWELL_TOHM_1	0xd8
#define KNL_TOLM	0xd0
#define KNL_TOHM_0	0xd4
#define KNL_TOHM_1	0xd8

#define GET_TOLM(reg)		((GET_BITFIELD(reg, 0,  3) << 28) | 0x3ffffff)
#define GET_TOHM(reg)		((GET_BITFIELD(reg, 0, 20) << 25) | 0x3ffffff)

/* Device 13 Function 6 */

#define SAD_TARGET	0xf0

#define SOURCE_ID(reg)		GET_BITFIELD(reg, 9, 11)

#define SOURCE_ID_KNL(reg)	GET_BITFIELD(reg, 12, 14)

#define SAD_CONTROL	0xf4

/* Device 14 function 0 */

static const u32 tad_dram_rule[] = {
	0x40, 0x44, 0x48, 0x4c,
	0x50, 0x54, 0x58, 0x5c,
	0x60, 0x64, 0x68, 0x6c,
};
#define MAX_TAD	ARRAY_SIZE(tad_dram_rule)

#define TAD_LIMIT(reg)		((GET_BITFIELD(reg, 12, 31) << 26) | 0x3ffffff)
#define TAD_SOCK(reg)		GET_BITFIELD(reg, 10, 11)
#define TAD_CH(reg)			GET_BITFIELD(reg,  8,  9)
#define TAD_TGT3(reg)		GET_BITFIELD(reg,  6,  7)
#define TAD_TGT2(reg)		GET_BITFIELD(reg,  4,  5)
#define TAD_TGT1(reg)		GET_BITFIELD(reg,  2,  3)
#define TAD_TGT0(reg)		GET_BITFIELD(reg,  0,  1)

/* Device 15, function 0 */

#define MCMTR			0x7c
#define KNL_MCMTR		0x624

#define IS_ECC_ENABLED(mcmtr)		GET_BITFIELD(mcmtr, 2, 2)
#define IS_LOCKSTEP_ENABLED(mcmtr)	GET_BITFIELD(mcmtr, 1, 1)
#define IS_CLOSE_PG(mcmtr)		GET_BITFIELD(mcmtr, 0, 0)

/* Device 15, function 1 */

#define RASENABLES		0xac
#define IS_MIRROR_ENABLED(reg)		GET_BITFIELD(reg, 0, 0)

/* Device 15, functions 2-5 */

static const int mtr_regs[] = {
	0x80, 0x84, 0x88,
};

static const int knl_mtr_reg = 0xb60;

#define RANK_DISABLE(mtr)		GET_BITFIELD(mtr, 16, 19)
#define IS_DIMM_PRESENT(mtr)		GET_BITFIELD(mtr, 14, 14)
#define RANK_CNT_BITS(mtr)		GET_BITFIELD(mtr, 12, 13)
#define RANK_WIDTH_BITS(mtr)		GET_BITFIELD(mtr, 2, 4)
#define COL_WIDTH_BITS(mtr)		GET_BITFIELD(mtr, 0, 1)

static const u32 tad_ch_nilv_offset[] = {
	0x90, 0x94, 0x98, 0x9c,
	0xa0, 0xa4, 0xa8, 0xac,
	0xb0, 0xb4, 0xb8, 0xbc,
};
#define CHN_IDX_OFFSET(reg)		GET_BITFIELD(reg, 28, 29)
#define TAD_OFFSET(reg)			(GET_BITFIELD(reg,  6, 25) << 26)

static const u32 rir_way_limit[] = {
	0x108, 0x10c, 0x110, 0x114, 0x118,
};
#define MAX_RIR_RANGES ARRAY_SIZE(rir_way_limit)

#define IS_RIR_VALID(reg)	GET_BITFIELD(reg, 31, 31)
#define RIR_WAY(reg)		GET_BITFIELD(reg, 28, 29)

#define MAX_RIR_WAY	8

static const u32 rir_offset[MAX_RIR_RANGES][MAX_RIR_WAY] = {
	{ 0x120, 0x124, 0x128, 0x12c, 0x130, 0x134, 0x138, 0x13c },
	{ 0x140, 0x144, 0x148, 0x14c, 0x150, 0x154, 0x158, 0x15c },
	{ 0x160, 0x164, 0x168, 0x16c, 0x170, 0x174, 0x178, 0x17c },
	{ 0x180, 0x184, 0x188, 0x18c, 0x190, 0x194, 0x198, 0x19c },
	{ 0x1a0, 0x1a4, 0x1a8, 0x1ac, 0x1b0, 0x1b4, 0x1b8, 0x1bc },
};

#define RIR_RNK_TGT(reg)		GET_BITFIELD(reg, 16, 19)
#define RIR_OFFSET(reg)		GET_BITFIELD(reg,  2, 14)

/* Device 16, functions 2-7 */

/*
 * FIXME: Implement the error count reads directly
 */

static const u32 correrrcnt[] = {
	0x104, 0x108, 0x10c, 0x110,
};

#define RANK_ODD_OV(reg)		GET_BITFIELD(reg, 31, 31)
#define RANK_ODD_ERR_CNT(reg)		GET_BITFIELD(reg, 16, 30)
#define RANK_EVEN_OV(reg)		GET_BITFIELD(reg, 15, 15)
#define RANK_EVEN_ERR_CNT(reg)		GET_BITFIELD(reg,  0, 14)

static const u32 correrrthrsld[] = {
	0x11c, 0x120, 0x124, 0x128,
};

#define RANK_ODD_ERR_THRSLD(reg)	GET_BITFIELD(reg, 16, 30)
#define RANK_EVEN_ERR_THRSLD(reg)	GET_BITFIELD(reg,  0, 14)


/* Device 17, function 0 */

#define SB_RANK_CFG_A		0x0328

#define IB_RANK_CFG_A		0x0320

/*
 * sbridge structs
 */

#define NUM_CHANNELS		8	/* 2MC per socket, four chan per MC */
#define MAX_DIMMS		3	/* Max DIMMS per channel */
#define KNL_MAX_CHAS		38	/* KNL max num. of Cache Home Agents */
#define KNL_MAX_CHANNELS	6	/* KNL max num. of PCI channels */
#define KNL_MAX_EDCS		8	/* Embedded DRAM controllers */
#define CHANNEL_UNSPECIFIED	0xf	/* Intel IA32 SDM 15-14 */

//MK1101-begin
/* Top of reserved memory */
#define	TORM			0x100000000		// 4GB
//MK1101-end

/* Supported archs */
#define TOTAL_ARCHS 5
enum type {
	SANDY_BRIDGE,
	IVY_BRIDGE,
	HASWELL,
	BROADWELL,
	KNIGHTS_LANDING,
};

const char* type_name[TOTAL_ARCHS] = {"Sandy Bridge",
        "Ivy Bridge", "Haswell", "Broadwell", "KnightsLanding"};

struct sbridge_pvt;
struct sbridge_info {
	enum type	type;
	u32		mcmtr;
	u32		rankcfgr;
	u64		(*get_tolm)(struct sbridge_pvt *pvt);
	u64		(*get_tohm)(struct sbridge_pvt *pvt);
	u64		(*rir_limit)(u32 reg);
	u64		(*sad_limit)(u32 reg);
	u32		(*interleave_mode)(u32 reg);
	char*		(*show_interleave_mode)(u32 reg);
	u32		(*dram_attr)(u32 reg);
	const u32	*dram_rule;
	const u32	*interleave_list;
	const struct interleave_pkg *interleave_pkg;
	u8		max_sad;
	u8		max_interleave;
	u8		(*get_node_id)(struct sbridge_pvt *pvt);
	enum mem_type	(*get_memory_type)(struct sbridge_pvt *pvt);
	enum dev_type	(*get_width)(struct sbridge_pvt *pvt, u32 mtr);
	struct pci_dev	*pci_vtd;
};

struct sbridge_channel {
	u32		ranks;
	u32		dimms;
};

struct pci_id_descr {
	int			dev_id;
	int			optional;
};

struct pci_id_table {
	const struct pci_id_descr	*descr;
	int				n_devs;
};

struct sbridge_dev {
	struct list_head	list;
	u8			bus, mc;
	u8			node_id, source_id;
	struct pci_dev		**pdev;
	int			n_devs;
	struct mem_ctl_info	*mci;
};

struct knl_pvt {
	struct pci_dev          *pci_cha[KNL_MAX_CHAS];
	struct pci_dev          *pci_channel[KNL_MAX_CHANNELS];
	struct pci_dev          *pci_mc0;
	struct pci_dev          *pci_mc1;
	struct pci_dev          *pci_mc0_misc;
	struct pci_dev          *pci_mc1_misc;
	struct pci_dev          *pci_mc_info; /* tolm, tohm */
};

struct sbridge_pvt {
	struct pci_dev		*pci_ta, *pci_ddrio, *pci_ras;
	struct pci_dev		*pci_sad0, *pci_sad1;
	struct pci_dev		*pci_ha0, *pci_ha1;
	struct pci_dev		*pci_br0, *pci_br1;
	struct pci_dev		*pci_ha1_ta;
	struct pci_dev		*pci_tad[NUM_CHANNELS];

	struct sbridge_dev	*sbridge_dev;

	struct sbridge_info	info;
	struct sbridge_channel	channel[NUM_CHANNELS];

	/* Memory type detection */
	bool			is_mirrored, is_lockstep, is_close_pg;

	/* Fifo double buffers */
	struct mce		mce_entry[MCE_LOG_LEN];
	struct mce		mce_outentry[MCE_LOG_LEN];

	/* Fifo in/out counters */
	unsigned		mce_in, mce_out;

	/* Count indicator to show errors not got */
	unsigned		mce_overrun;

	/* Memory description */
	u64			tolm, tohm;
	struct knl_pvt knl;
};


#define PCI_DESCR(device_id, opt)	\
	.dev_id = (device_id),		\
	.optional = opt

static const struct pci_id_descr pci_dev_descr_sbridge[] = {
		/* Processor Home Agent */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_HA0, 0)	},

		/* Memory controller */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TA, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_RAS, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD0, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD1, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD2, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD3, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_DDRIO, 1)	},

		/* System Address Decoder */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_SAD0, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_SAD1, 0)	},

		/* Broadcast Registers */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_SBRIDGE_BR, 0)		},
};

#define PCI_ID_TABLE_ENTRY(A) { .descr=A, .n_devs = ARRAY_SIZE(A) }
static const struct pci_id_table pci_dev_descr_sbridge_table[] = {
	PCI_ID_TABLE_ENTRY(pci_dev_descr_sbridge),
	{0,}			/* 0 terminated list. */
};

/* This changes depending if 1HA or 2HA:
 * 1HA:
 *	0x0eb8 (17.0) is DDRIO0
 * 2HA:
 *	0x0ebc (17.4) is DDRIO0
 */
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_1HA_DDRIO0	0x0eb8
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_2HA_DDRIO0	0x0ebc

/* pci ids */
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0		0x0ea0
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TA		0x0ea8
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_RAS		0x0e71
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD0	0x0eaa
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD1	0x0eab
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD2	0x0eac
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD3	0x0ead
#define PCI_DEVICE_ID_INTEL_IBRIDGE_SAD			0x0ec8
#define PCI_DEVICE_ID_INTEL_IBRIDGE_BR0			0x0ec9
#define PCI_DEVICE_ID_INTEL_IBRIDGE_BR1			0x0eca
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1		0x0e60
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TA		0x0e68
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_RAS		0x0e79
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD0	0x0e6a
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD1	0x0e6b
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD2	0x0e6c
#define PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD3	0x0e6d

static const struct pci_id_descr pci_dev_descr_ibridge[] = {
		/* Processor Home Agent */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0, 0)		},

		/* Memory controller */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TA, 0)		},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_RAS, 0)		},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD0, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD1, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD2, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TAD3, 0)	},

		/* System Address Decoder */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_SAD, 0)			},

		/* Broadcast Registers */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_BR0, 1)			},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_BR1, 0)			},

		/* Optional, mode 2HA */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1, 1)		},
#if 0
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TA, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_RAS, 1)	},
#endif
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD0, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD1, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD2, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA1_TAD3, 1)	},

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_1HA_DDRIO0, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_2HA_DDRIO0, 1)	},
};

static const struct pci_id_table pci_dev_descr_ibridge_table[] = {
	PCI_ID_TABLE_ENTRY(pci_dev_descr_ibridge),
	{0,}			/* 0 terminated list. */
};

/* Haswell support */
/* EN processor:
 *	- 1 IMC
 *	- 3 DDR3 channels, 2 DPC per channel
 * EP processor:
 *	- 1 or 2 IMC
 *	- 4 DDR4 channels, 3 DPC per channel
 * EP 4S processor:
 *	- 2 IMC
 *	- 4 DDR4 channels, 3 DPC per channel
 * EX processor:
 *	- 2 IMC
 *	- each IMC interfaces with a SMI 2 channel
 *	- each SMI channel interfaces with a scalable memory buffer
 *	- each scalable memory buffer supports 4 DDR3/DDR4 channels, 3 DPC
 */
#define HASWELL_DDRCRCLKCONTROLS 0xa10 /* Ditto on Broadwell */
#define HASWELL_HASYSDEFEATURE2 0x84
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_VTD_MISC 0x2f28
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0	0x2fa0
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1	0x2f60
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TA	0x2fa8
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_THERMAL 0x2f71
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TA	0x2f68
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_THERMAL 0x2f79
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_CBO_SAD0 0x2ffc
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_CBO_SAD1 0x2ffd
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD0 0x2faa
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD1 0x2fab
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD2 0x2fac
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD3 0x2fad
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD0 0x2f6a
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD1 0x2f6b
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD2 0x2f6c
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD3 0x2f6d
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO0 0x2fbd
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO1 0x2fbf
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO2 0x2fb9
#define PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO3 0x2fbb
static const struct pci_id_descr pci_dev_descr_haswell[] = {
	/* first item must be the HA */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0, 0)		},

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_CBO_SAD0, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_CBO_SAD1, 0)	},

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1, 1)		},

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TA, 0)		},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_THERMAL, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD0, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD1, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD2, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD3, 1)	},

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO0, 1)		},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO1, 1)		},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO2, 1)		},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO3, 1)		},

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TA, 1)		},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_THERMAL, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD0, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD1, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD2, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD3, 1)	},
};

static const struct pci_id_table pci_dev_descr_haswell_table[] = {
	PCI_ID_TABLE_ENTRY(pci_dev_descr_haswell),
	{0,}			/* 0 terminated list. */
};

/* Knight's Landing Support */
/*
 * KNL's memory channels are swizzled between memory controllers.
 * MC0 is mapped to CH3,5,6 and MC1 is mapped to CH0,1,2
 */
#define knl_channel_remap(channel) ((channel + 3) % 6)

/* Memory controller, TAD tables, error injection - 2-8-0, 2-9-0 (2 of these) */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_MC       0x7840
/* DRAM channel stuff; bank addrs, dimmmtr, etc.. 2-8-2 - 2-9-4 (6 of these) */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_CHANNEL  0x7843
/* kdrwdbu TAD limits/offsets, MCMTR - 2-10-1, 2-11-1 (2 of these) */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_TA       0x7844
/* CHA broadcast registers, dram rules - 1-29-0 (1 of these) */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_SAD0     0x782a
/* SAD target - 1-29-1 (1 of these) */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_SAD1     0x782b
/* Caching / Home Agent */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_CHA      0x782c
/* Device with TOLM and TOHM, 0-5-0 (1 of these) */
#define PCI_DEVICE_ID_INTEL_KNL_IMC_TOLHM    0x7810

/*
 * KNL differs from SB, IB, and Haswell in that it has multiple
 * instances of the same device with the same device ID, so we handle that
 * by creating as many copies in the table as we expect to find.
 * (Like device ID must be grouped together.)
 */

static const struct pci_id_descr pci_dev_descr_knl[] = {
	[0]         = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_SAD0, 0) },
	[1]         = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_SAD1, 0) },
	[2 ... 3]   = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_MC, 0)},
	[4 ... 41]  = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_CHA, 0) },
	[42 ... 47] = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_CHANNEL, 0) },
	[48]        = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_TA, 0) },
	[49]        = { PCI_DESCR(PCI_DEVICE_ID_INTEL_KNL_IMC_TOLHM, 0) },
};

static const struct pci_id_table pci_dev_descr_knl_table[] = {
	PCI_ID_TABLE_ENTRY(pci_dev_descr_knl),
	{0,}
};

/*
 * Broadwell support
 *
 * DE processor:
 *	- 1 IMC
 *	- 2 DDR3 channels, 2 DPC per channel
 * EP processor:
 *	- 1 or 2 IMC
 *	- 4 DDR4 channels, 3 DPC per channel
 * EP 4S processor:
 *	- 2 IMC
 *	- 4 DDR4 channels, 3 DPC per channel
 * EX processor:
 *	- 2 IMC
 *	- each IMC interfaces with a SMI 2 channel
 *	- each SMI channel interfaces with a scalable memory buffer
 *	- each scalable memory buffer supports 4 DDR3/DDR4 channels, 3 DPC
 */
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_VTD_MISC 0x6f28
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0	0x6fa0
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1	0x6f60
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TA	0x6fa8
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_THERMAL 0x6f71
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TA	0x6f68
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_THERMAL 0x6f79
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_CBO_SAD0 0x6ffc
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_CBO_SAD1 0x6ffd
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD0 0x6faa
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD1 0x6fab
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD2 0x6fac
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD3 0x6fad
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD0 0x6f6a
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD1 0x6f6b
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD2 0x6f6c
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD3 0x6f6d
#define PCI_DEVICE_ID_INTEL_BROADWELL_IMC_DDRIO0 0x6faf

static const struct pci_id_descr pci_dev_descr_broadwell[] = {
	/* first item must be the HA */
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0, 0)		},

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_CBO_SAD0, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_CBO_SAD1, 0)	},

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1, 1)		},

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TA, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_THERMAL, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD0, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD1, 0)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD2, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD3, 1)	},

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_DDRIO0, 1)	},

	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TA, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_THERMAL, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD0, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD1, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD2, 1)	},
	{ PCI_DESCR(PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD3, 1)	},
};

static const struct pci_id_table pci_dev_descr_broadwell_table[] = {
	PCI_ID_TABLE_ENTRY(pci_dev_descr_broadwell),
	{0,}			/* 0 terminated list. */
};

/*
 *	pci_device_id	table for which devices we are looking for
 *
 *	NOTE: straight copy from sb_edac.c
 */
static const struct pci_device_id sbridge_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_HA0)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TA)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_KNL_IMC_SAD0)},
	{0,}			/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, sbridge_pci_tbl);

/***************************************************************************
 * HV discovery related variables and functions
 *
 * Note: algo and population rules are published in grub-side code
 ***************************************************************************
 */

/* HV specific constants */
#define MAX_CPU_NODES   4               /* Number of CPU nodes (arbitrary #) */
#define HV_SIZE     (u64)0x200000000    /* By design, it is 4GB visible memory */
#define MAX_HVDIMMS 8                   /* Support up to 8 HVDIMMS by design */
static struct hvdimm_discovered_s {
	u64	mem_start;  // HV DDR4 internal memory
	u64 mem_size;
	u64 mmio_start; // HV MMIO space
	u64 mmio_size;
} hvdimm_discovered[MAX_CPU_NODES] = {{0}};

//u8 num_nodes = 0;     /* Number of CPU nodes used with HV */
//u8 num_hv = 0;        /* Index for hvdimm_e820_info[] (number of hvdimms found) */
static u8 hv_ch_way = 1;            /* interleaving mode for hv */
static u64 tolm_upper_gb = 0;       /* in hex 64 bit, upper address of TOLM */
/* 1 = only populated with hv, 0 = mixed with LDRDIMM, negative = no HV */
static int system_has_hv_only = 0;
static u64 system_mmio_size;        /* 4GB - TOLM */
static u64 nvdimm_e820_addr[8];	    /* to save nvdimm found in e820 */
static u64 nvdimm_e820_size[8];
static u8 nvdimm_count = 0;


//MK0417-begin
#define MMIO_WINDOW_SIZE			0x100000

struct home_agent_t {
	unsigned int vid;
	unsigned int did;
};

struct SAD {
	unsigned long addr;
	unsigned char a7_mode;
	unsigned char interleave_mode;
	unsigned char bank_xor_enable;
	unsigned int reg;
};

struct TAD {
	unsigned long Addr;
	int CH[4];
	unsigned char ChWay, SckWay;
};

struct DIMM_INFO {
	char sSN[12],sPN[18];
	int Temp, pop, slot, HW[2];
};

struct DIMM {
	struct DIMM_INFO id[12];
	int count;
};

struct HVDIMM {
	unsigned long mmio[8];
	int slot[8];
	int count;
};


static const struct home_agent_t home_agent[] = {
	{ 0x8086, 0x6FA0 },
	{ 0x8086, 0x6F60 },
};

static const struct home_agent_t imc_tad[] = {
	{ 0x8086, 0x6FAA },
	{ 0x8086, 0x6FAB },
	{ 0x8086, 0x6F6A },
	{ 0x8086, 0x6F6B },
};

struct DIMM dimm;
struct HVDIMM hvdimm;
struct SAD sad[20];
struct TAD tad[12];
unsigned int bus, mcmtr;
unsigned long tohm=0, tohm0=0, tohm1=0;
int FpgaWRDly=100, FpgaRDDly=1, FpgaCMDDly=100, FpgaSetPageDly=1000;

unsigned char check_hdimm(unsigned int spd_dimm_id);
int set_i2c_page_address(unsigned int dimm, int page);

void checkPopDimm(void)
{
	int i, N, C;

	for (dimm.count=0, hvdimm.count=0, i=0;i<12;i++) {
		N =  i<12?0:1;
		C = (int)(i)<12?(int)(i/3):(int)(i/3)-4;

//MK0417		SetPageAddress(i, 0);
//MK0417		usleep(FpgaSetPageDly);
		set_i2c_page_address(i, 0);		//MK0417
		udelay(FpgaSetPageDly);

//MK0417		if (ReadSMBus(SPD,i,0) == 0x23) {
		if ( (unsigned char) read_smbus(i, SPD_ID, 0) == 0x23 ) {	//MK0417
			dimm.id[i].pop=1;
			dimm.id[dimm.count++].slot = i;

//MK0417			getSN(i);
//MK0417			if (strstr(dimm.id[i].sPN,"25082")) {
//MK0417				dimm.id[i].pop=2;
//MK0417				hvdimm.slot[hvdimm.count++] = i;
//MK0417			}
//MK0417-begin
			if ( check_hdimm(i) == 0 ) {
				dimm.id[i].pop=2;
				hvdimm.slot[hvdimm.count++] = i;
			}
//MK0417-end
		}
	}
	PDEBUG("\n #DIMM: %d,  #HV: %d\n", dimm.count, hvdimm.count);
}

void get_ddr4_address(unsigned long addr)
{
	unsigned char ChWay, SckWay;
	unsigned char pkg, sad_ha;
	unsigned int n_sads, n_tads, reg;
	unsigned long limit, prv, offs, Ca, Ra;
	int N=0, C, bits, idx, shiftup=0, tad_offset, sck_xch, bg, ba, row, col;
	unsigned char a7mode, interleave_mode;
	struct pci_dev *pdev = NULL;	//MK0417
	unsigned char bank_xor_enable;	//MK0417
	unsigned char buff[200];		//MK0417
	int len=0;						//MK0417


//MK0417	PDEBUG("\n 0x%010lx =>", addr);
	memset(buff, 0, sizeof(buff));
	len = sprintf(buff, "0x%010lx =>", addr);
	if (addr > tohm) {
		PDEBUG(" ERROR - TOHM 0x%010lx\n\n", tohm);
		return;
	}

	for (prv = 0, n_sads = 0; n_sads < 20; n_sads++) {
//MK0417		reg = MMioReadDword(Bus,15,4,ibridge_dram_rule[n_sads]);
		pdev = pci_get_device(0x8086, 0x6FFC, NULL);
		pci_read_config_dword(pdev, ibridge_dram_rule[n_sads], &reg);

		if (!DRAM_RULE_ENABLE(reg))	continue;

		limit = (GET_BITFIELD(reg,6,25)<<26) | 0x3ffffff;

		if (limit <= prv) { PDEBUG(" ERROR - memory socket\n\n"); return; }
		if (addr <= limit) break;
		prv = limit;
	}
	if (n_sads == 20) { PDEBUG(" ERROR - memory socket\n\n"); return; }

	a7mode = (unsigned char)GET_BITFIELD(reg,26,26);
	interleave_mode = (unsigned char)GET_BITFIELD(reg,1,1);
	bank_xor_enable = (unsigned char) GET_BITFIELD(mcmtr, 9, 9);	//MK0417

	if (a7mode) {
		/* A7 mode swaps P9 with P6 */
		bits = GET_BITFIELD(addr, 7, 8) << 1;
		bits |= GET_BITFIELD(addr, 9, 9);
	} else
		bits = GET_BITFIELD(addr, 7, 9);

	if (interleave_mode) {
		/* interleave mode will XOR {8,7,6} with {18,17,16} */
		idx = GET_BITFIELD(addr, 16, 18);
		idx ^= bits;
	} else
		idx = bits;

//MK0417	reg = MMioReadDword(Bus+N*(Bus+1),15,4, 0x64+n_sads*8);
	pdev = pci_get_device(0x8086, 0x6FFC, NULL);
	pci_read_config_dword(pdev, 0x64+n_sads*8, &reg);

	pkg = (reg>>(idx*4))&0xF;
	N = ((pkg >> 3) << 2) | (pkg & 0x3);
	sad_ha = (pkg >>2) & 1;

	if (a7mode) { /* MCChanShiftUpEnable */
//MK0417		reg = MMioReadDword(Bus+N*(Bus+1),18,sad_ha*4,0x84);
  		pdev = pci_get_device(home_agent[sad_ha].vid, home_agent[sad_ha].did, NULL);
		pci_read_config_dword(pdev, 0x84, &reg);

		shiftup = GET_BITFIELD(reg, 22, 22);
	}

	prv = 0;

	for (n_tads = 0; n_tads < 12; n_tads++) {
//		reg = MMioReadDword(Bus+N*(Bus+1),18,sad_ha*4,tad_dram_rule[n_tads]);
  		pdev = pci_get_device(home_agent[sad_ha].vid, home_agent[sad_ha].did, NULL);
		pci_read_config_dword(pdev, tad_dram_rule[n_tads], &reg);

		limit = TAD_LIMIT(reg);
	//	printf("\n n_tads=%d reg= %x addr= %lx, limit= %lx", n_tads, reg, addr, limit);
		if (limit <= prv) { PDEBUG(" ERROR - memory channel\n\n"); return; }
		if  (addr <= limit) break;
		prv = limit;
	}

	ChWay = TAD_CH(reg) + 1;
	SckWay = TAD_SOCK(reg) + 0;

	sck_xch = (1 << SckWay) * ChWay;

	idx = (addr >> (6 + SckWay + shiftup)) & 0x3;
	idx = idx % ChWay;

	switch (idx) {
		case 0:	C = GET_BITFIELD(reg, 0, 1); break;
		case 1:	C = GET_BITFIELD(reg, 2, 3); break;
		case 2:	C = GET_BITFIELD(reg, 4, 5); break;
		case 3:	C = GET_BITFIELD(reg, 6, 7); break;
		default: PDEBUG(" ERROR - TAD target\n\n"); return;
	}
	C |= ( sad_ha << 1 );


//MK0417	tad_offset = MMioReadDword(Bus+N*(Bus+1),19+sad_ha*3,F2323[C],tad_ch_nilv_offset[n_tads]);
	pdev = pci_get_device(imc_tad[(sad_ha<<1) | (C&1)].vid, imc_tad[(sad_ha<<1) | (C&1)].did, NULL);
	pci_read_config_dword(pdev, tad_ch_nilv_offset[n_tads], &tad_offset);

	offs = TAD_OFFSET(tad_offset);

	if (shiftup) Ca = ((((addr-offs)>>7) / sck_xch ) << 7) | ((addr - offs) & 0x7f) ;
	else		 Ca = ((((addr-offs)>>6) / sck_xch ) << 6) | ((addr - offs) & 0x3f) ;


	// Skip Rank Address decoding

	Ra = Ca;

	if(bank_xor_enable) {
		bg = ((((Ra>>21)&0x1)^((Ra>>17)&0x1))<<1) | (((Ra>>20)&0x1)^((Ra>> 6)&0x1));// Bg[1:0] = Ra[21^17,20^13]
		ba = ((((Ra>>23)&0x1)^((Ra>>19)&0x1))<<1) | (((Ra>>22)&0x1)^((Ra>>18)&0x1));// Ba[1:0] = Ra[23^19,22^18]
	} else {
		bg = (((Ra>>17)&0x1)<<1) | ((Ra>>6)&0x1);	// Bg[1:0] = Ra[17,6]
		ba = (((Ra>>18)&0x3));						// Ba[1:0] = Ra[19,18]
	}

	//Row[11:0] = Ra[27:21,28,20,16:14]				// DDR4
	row  =(((Ra>>21)&0x7F)<<5) | (((Ra>>28)&0x1)<<4) | (((Ra>>20)&0x1)<<3) | ((Ra>>14)&0x7);
	row |=(((Ra>>29)&0x1f)<<12); 					//Row[16:12] = Ra[33:29] ... DDR4

	col  =((Ra>>7)&0x7F)<<3; 						//Col[9:3] = Ra[13:7] for page open mode
	col |=((Ra>>3)&0x7); 							//Col[2:0] = Ra[5:3] in Independent mode

//MK0417	PDEBUG(" 0x%010lX => 0x%010lX => N%d.C%d.D%d B%d-%d X:%05X Y:%03X",
//MK0417				   Ca, Ra, N, C, 0, bg, ba, row, col );
	len = sprintf(buff+len, " 0x%010lX => 0x%010lX => N%d.C%d.D%d B%d-%d X:%05X Y:%03X\n",
			Ca, Ra, N, C, 0, bg, ba, row, col);
	PDEBUG("%s", buff);
}

void get_memory_layout2(void)
{
	unsigned int n_sads, n_tads, reg;
	unsigned long limit, prv=0, offs;
	int i, j, k, N, ha, ntad, HA=1, nWay;
	struct pci_dev *pdev = NULL;			//MK0417
	unsigned char sad_count=0, buff[200];	//MK0417
	int len=0;								//MK0417


//MK0417-begin
//	tohm0 = MMioReadDword(0,5,0,0xd4);
	pdev = pci_get_device(0x8086, 0x6F28, NULL);
	if (pdev == NULL) {
		PDEBUG("ERROR: 0x8086-0x6F28 not found\n");
		return;
	}
	pci_read_config_dword(pdev, 0xD4, &reg);
	tohm0 = reg;

//	tohm1 = MMioReadDword(0,5,0,0xd8);
	pci_read_config_dword(pdev, 0xD8, &reg);
	tohm1 = reg;

	tohm0 = GET_BITFIELD(tohm0, 26, 31);
	tohm = ((tohm1 << 6) | tohm0) << 26;
	tohm = (tohm | 0x3FFFFFF) + 1;


//	Bus = (MMioReadDword(0,5,0,0x108)>>8) & 0xFF;
//	if (MMioReadDword(Bus,18,4,0)==0x2F608086) HA=1;		// HSW - HA0: 2FA0, HA1:2F60
//	if (MMioReadDword(Bus,18,4,0)==0x6F608086) HA=1;		// BDW - HA0: 6FA0, HA1:6F60
//	mcmtr = MMioReadDword(Bus,19,0,0x7C);
//	bank_xor_enable = GET_BITFIELD(mcmtr,9,9);
	pci_read_config_dword(pdev, 0x108, &reg);
	bus = (reg >> 8) & 0xFF;

	pdev = pci_get_device(0x8086, 0x6F60, NULL);	// Home Agent #1
	if (pdev == NULL) {
		PDEBUG("ERROR: 0x8086-0x6F60 not found\n");
		return;
	}
	pci_read_config_dword(pdev, 0, &reg);
	if (reg == 0x6F608086) {
		HA = 1;
	}

	pdev = pci_get_device(0x8086, 0x6FA8, NULL);
	if (pdev == NULL) {
		PDEBUG("ERROR: 0x8086-0x6FA8 not found\n");
		return;
	}
	pci_read_config_dword(pdev, 0x7C, &mcmtr);

	/* Search for bus:0F:04 */
	pdev = pci_get_device(0x8086, 0x6FFC, NULL);
	if (pdev == NULL) {
		PDEBUG("ERROR: 0x8086-0x6FFC not found\n");
		return;
	}
//MK0417-end

	for (n_sads = 0; n_sads < 20; n_sads++) {
//MK0417		reg = MMioReadDword(Bus,15,4,ibridge_dram_rule[n_sads]);
		pci_read_config_dword(pdev, ibridge_dram_rule[n_sads], &reg);	//MK0417

		if (!DRAM_RULE_ENABLE(reg))	continue;

		limit = (GET_BITFIELD(reg,6,25)<<26) | 0x3ffffff;
		if (limit <= prv) break;

//		a7mode = (int)GET_BITFIELD(reg,26,26);
//		interleave_mode = GET_BITFIELD(reg,1,1);
		sad[n_sads].reg = reg;
		sad[n_sads].a7_mode = (unsigned char) GET_BITFIELD(reg, 26, 26);
		sad[n_sads].interleave_mode = (unsigned char) GET_BITFIELD(reg, 1, 1);
		sad[n_sads].addr = limit + 1;
		sad[n_sads].bank_xor_enable = (unsigned char) GET_BITFIELD(mcmtr, 9, 9);
		sad_count++;

//MK0417		if (log)
//MK0417			printf("\n SAD#%d: up to %3ld GB (0x%010lx) Interleave: [%s], A7Mode: %d, BaXOR: %d, reg=0x%08X",
//MK0417					n_sads, (limit+1)>>30, (limit+1),
//MK0417						 interleave_mode ? "8:6" : "[8:6]XOR[18:16]",
//MK0417						 a7mode, bank_xor_enable, reg);
		prv = limit;
	}

	N = prv = 0;

  	for(ntad=0, ha=0;ha<(HA+1);ha++) {
//MK0417-begin
  		pdev = pci_get_device(home_agent[ha].vid, home_agent[ha].did, NULL);
  		if (pdev == NULL) {
  			PDEBUG("ERROR: 0x%.4X-0x%.4X not found\n", home_agent[ha].vid, home_agent[ha].did);
  			return;
  		}
//MK0417-end
		for (n_tads = 0; n_tads < 12; n_tads++) {
//MK0417			reg = MMioReadDword(Bus+N*(Bus+1),18,ha*4,tad_dram_rule[n_tads]);
			pci_read_config_dword(pdev, tad_dram_rule[n_tads], &reg);	//MK0417
			limit = TAD_LIMIT(reg);
			if (limit <= prv) break;

			tad[ntad].Addr = limit+1;
			tad[ntad].ChWay = 1+(u32)TAD_CH(reg);
			tad[ntad].SckWay = 1<<(u32)TAD_SOCK(reg);

//MK0417			printf("\n TAD#%d: up to %3ld GB (0x%010lx) Way= (%d,%d) CH: ",
//MK0417			 				ntad, (limit+1)>>30, (limit+1), tad[ntad].SckWay, tad[ntad].ChWay);
			for(i=0;i<1+TAD_CH(reg);i++) {
				tad[ntad].CH[i] = (ha*2)+((reg>>(2*i))&0x03);
//MK0417				printf( "%d, ",tad[ntad].CH[i]);
			}
//MK0417			printf(" reg=0x%08x", reg );
			prv = limit;
			ntad++;
		}

	}

//MK0417	if (log)
//MK0417		printf("\n");
//MK0417-begin
	for (i=0; i<sad_count; i++)
	{
		PDEBUG("SAD#%d: up to %3ld GB (0x%010lx) Interleave: [%s],"
				" A7Mode: %d, BaXOR: %d, reg=0x%08X\n",
				i, sad[i].addr>>30, sad[i].addr,
				sad[i].interleave_mode ? "8:6" : "[8:6]XOR[18:16]",
				sad[i].a7_mode, sad[i].bank_xor_enable, sad[i].reg);
//		memset(buff, 0, sizeof(buff));
//		len = sprintf(buff, "SAD#%d: up to %3ld GB (0x%010lx) Interleave: [%s], A7Mode: %d, BaXOR: %d, reg=0x%08X\n",
//				sad_count, sad[i].addr>>30, sad[i].addr,
//				sad[i].interleave_mode ? "8:6" : "[8:6]XOR[18:16]",
//				sad[i].a7_mode, sad[i].bank_xor_enable, sad[i].reg);
//		PDEBUG("%s", buff);
	}
//MK0417-end

	for (i=0;i<ntad;i++) {
//MK0417		if (log)
//MK0417			PDEBUG("TAD#%d: up to %3ld GB (0x%010lX) Way= (%d,%d) CH: ",
//MK0417			 				i, (tad[i].Addr)>>30, tad[i].Addr, tad[i].SckWay, tad[i].ChWay);
		memset(buff, 0, sizeof(buff));
		len = sprintf(buff, "TAD#%d: up to %3ld GB (0x%010lX) Way= (%d,%d) CH: ",
				i, (tad[i].Addr)>>30, tad[i].Addr, tad[i].SckWay, tad[i].ChWay);
		nWay = tad[i].SckWay * tad[i].ChWay;
		for (j=0;j<nWay;j++) {
//MK0417			if (log)
//MK0417				PDEBUG("%d%c ",tad[i].CH[j], nWay>1?',':' ');
			len += sprintf(buff+len, "%d%c ", tad[i].CH[j], nWay>1?',':' ');
			for (k=0;k<hvdimm.count;k++) {
				if (tad[i].CH[j]*3 == hvdimm.slot[k]) {
					offs = nWay * MMIO_WINDOW_SIZE;
					hvdimm.mmio[k] = tad[i].Addr - offs + (j * 0x40);
//MK0417					if (log)
//MK0417						PDEBUG(" HB 0x%010lX", hvdimm.mmio[k]);
					len += sprintf(buff+len, " HB 0x%010lX", hvdimm.mmio[k]);
				}
			}
		}
		PDEBUG("%s\n", buff);	//MK0417
	}

//MK0417	if (log) {
		PDEBUG("\n");
		for (i=0;i<hvdimm.count;i++) {
			get_ddr4_address(hvdimm.mmio[i]);
		}
		PDEBUG("\n");
//MK0417	}

}

//int get_mrcVersion(void)
//{
//	return(MMioReadDword(Bus,16,7,0x90));
//}
//MK0417-end

//MK0412-begin
unsigned long hv_nstimeofday(void)
{
	struct timespec ts;

	getnstimeofday(&ts);
	return(timespec_to_ns(&ts));
}

unsigned char check_hdimm(unsigned int spd_dimm_id)
{
	unsigned char mfid[SPD_MFID_LEN], mpn[SPD_MPN_LEN+1], i;
	unsigned int spd_page, spd_offset;

	memset(mpn, sizeof(mpn), 0);

	/* Compute page number for MFID */
	spd_page = SPD_MFID_OFFSET / 256;
	set_i2c_page_address(spd_dimm_id, (int)spd_page);

	/* Adjust offset */
	spd_offset = (spd_page == 0) ? (SPD_MFID_OFFSET) : (SPD_MFID_OFFSET-256);

	/* Get manufacturer ID from SPD */
	mfid[0] = (unsigned char) read_smbus(spd_dimm_id, SPD_ID, spd_offset);
	udelay(100);
	mfid[1] = (unsigned char) read_smbus(spd_dimm_id, SPD_ID, spd_offset+1);
	udelay(100);

	/* Compute page number for MPN */
	spd_page = SPD_MPN_OFFSET / 256;
	set_i2c_page_address(spd_dimm_id, (int)spd_page);

	/* Adjust offset */
	spd_offset = (spd_page == 0) ? (SPD_MPN_OFFSET) : (SPD_MPN_OFFSET-256);

	/* Get manufacturer part number from SPD */
	for (i=0; i < SPD_MPN_LEN; i++)
	{
		mpn[i] = (unsigned char) read_smbus(spd_dimm_id, SPD_ID, spd_offset+i);
		udelay(100);
	}

//	PDEBUG("----- MFID = 0x%.2x%.2x\n", mfid[1], mfid[0]);
//	PDEBUG("----- MFID = 0x%.4X\n", *(unsigned short *)mfid);
//	PDEBUG("----- MPN  = %s\n", mpn);
//	PDEBUG("----- MPN  = %s\n", SPD_MPN_HDIMM);
	set_i2c_page_address(spd_dimm_id, 0);

	if ( (*(unsigned short *)mfid == SPD_MFID_NETLIST) && (strcmp(mpn, SPD_MPN_HDIMM) == 0) ) {
		return 0;
	}

	return 1;
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
		PDEBUG("[%s]: IMC, 0x8086-0x6FA8, not found\n", __func__);
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
		PDEBUG("[%s]: SPDCMD access not enabled\n", __func__);
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
		PDEBUG("[%s]: IMC, 0x8086-0x6FA8, not found\n", __func__);
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
		PDEBUG("[%s]: SPDCMD access not enabled\n", __func__);
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
		PDEBUG("[%s]: IMC, 0x8086-0x6FA8, not found\n", __func__);
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
		PDEBUG("[%s]: SPDCMD access not enabled\n", __func__);
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
//MK0412-end


/***************************************************************************
 * e820 related and hv specific functions
 ***************************************************************************
 */

#define E820_NVDIMM	12

static struct e820map e820_addr;
static void e820_print_type(u32 type)
{
	switch (type) {
	case E820_RAM:
	case E820_RESERVED_KERN:
		printk(KERN_CONT "usable");
		break;
	case E820_RESERVED:
		printk(KERN_CONT "reserved");
		break;
	case E820_ACPI:
		printk(KERN_CONT "ACPI data");
		break;
	case E820_NVS:
		printk(KERN_CONT "ACPI NVS");
		break;
	case E820_UNUSABLE:
		printk(KERN_CONT "unusable");
		break;
	// TODO:  new bios will have this type=12
	case E820_NVDIMM:
		printk(KERN_CONT "nvdimm");
		break;
	default:
		printk(KERN_CONT "type %u", type);
		break;
	}
}

/**
 *   System architect defines algorithm to find HV in HV-only configuration
 *   (no mixing with LRDIMM)
 *   Discovery driver is currently designed to discover HV in HV-only config
 *   TBD (1) comments below mean code might need adjustment when system
 *   arch is ready with a mixed of LRDIMM/HV configuration
 */

static int find_installed_memory_types(void)
{
    char *sym_name = "e820_saved";
    unsigned long sym_addr = kallsyms_lookup_name(sym_name);
	int i;
//MK1101    u8 ram_found = 0;
//MK1101    u8 nvdimm_found = 0;
//MK0908-begin
    unsigned long total_mem_size=0;
//MK0908-end

	e820_addr = *(struct e820map *)sym_addr;

	/* Print e820 table first to help debug */
	for (i = 0; i < e820_addr.nr_map; i++) {
//MK0908		PDEBUG("BIOS-e820: [mem %#018Lx-%#018Lx] ",
//MK0908		       (unsigned long long) e820_addr.map[i].addr,
//MK0908		       (unsigned long long)
//MK0908		       (e820_addr.map[i].addr + e820_addr.map[i].size - 1));
//MK0908-begin
		PDEBUG("BIOS-e820: [%d][mem %#018lx-%#018lx][%#018lx] ",
				e820_addr.map[i].type,
				(unsigned long) e820_addr.map[i].addr,
				(unsigned long) (e820_addr.map[i].addr + e820_addr.map[i].size - 1),
				(unsigned long) e820_addr.map[i].size);
		total_mem_size += (unsigned long) e820_addr.map[i].size;
//MK0908-end
		e820_print_type(e820_addr.map[i].type);
		printk(KERN_CONT "\n");
	}
//MK0908-begin
	PDEBUG("total memory size = 0x%.16lx\n", total_mem_size);
//MK0908-end

	/* Discover nvdimm */
	for (i = 0; i < e820_addr.nr_map; i++) {
		if ((e820_addr.map[i].type == E820_RAM) && e820_addr.map[i].addr == 0x100000000) {
			PINFO("hybridimm found at %llX", e820_addr.map[i].addr);
			PINFO("size = %llx", e820_addr.map[i].size);
			//ram_found = 1;            /* TBD (1) */
			system_has_hv_only = 1;     /* TBD (1) */

			nvdimm_e820_addr[nvdimm_count] = e820_addr.map[i].addr;
			nvdimm_e820_size[nvdimm_count] = e820_addr.map[i].size;
		}
	}

/* When system arch figure out the algo to differentiate lrdimm and hv */
/* This code with be revisited */
/*
	if (ram_found && nvdimm_found) {
		system_has_hv_only = 0;
		PINFO("Found dram and nvdimm installed");
	}
	else if (ram_found == 0 && nvdimm_found == 1) {
		system_has_hv_only = 1;
		PINFO("Found nvdimm only installed. No DRAM.");
	}
	else {
		system_has_hv_only = -1;
		PINFO("Found DRAM only. No HVDIMM");
	}

	PINFO("# nvdimm found = %d\n", nvdimm_count);

	system_has_hv_only = 0;
*/

	/* need to indicate that hvdimm is discovered for the TAD# calculation to go through */
	//if (nvdimm_found)
	//	hvdimm_discovered[0].mem_start = nvdimm_e820_addr[0];

	return system_has_hv_only;
}

static void print_discovered_hvs(void)
{
//MK1101	int i;
//MK1101-begin
	unsigned int i, j, k;
//MK1101-end

	for (i = 0; i < 2; i++ ) {
		PINFO("hvdimm_discovered[%d].mem_start=%llX\n", i, hvdimm_discovered[i].mem_start);
		PINFO("hvdimm_discovered[%d].mem_size=%llX\n", i, hvdimm_discovered[i].mem_size);
		PINFO("hvdimm_discovered[%d].mmio_start=%llX\n", i, hvdimm_discovered[i].mmio_start);
		PINFO("hvdimm_discovered[%d].mmio_size=%llX\n", i, hvdimm_discovered[i].mmio_size);
	}

//MK1101-begin
//#if 0	// Display this when loading hv.ko
	for (i = 0; i < MAX_NPS; i++ ) {
		PINFO("hd_desc[%d].tolm_addr=0x%.16lx\n", i, hd_desc[i].tolm_addr);
		PINFO("hd_desc[%d].tohm_addr=0x%.16lx\n", i, hd_desc[i].tohm_addr);
		PINFO("hd_desc[%d].sys_rsvd_mem_size=%ld GB\n", i, hd_desc[i].sys_rsvd_mem_size);
		PINFO("hd_desc[%d].sys_mem_size=%ld GB\n", i, hd_desc[i].sys_mem_size);
		PINFO("hd_desc[%d].spd_dimm_mask=0x%.8x\n", i, hd_desc[i].spd_dimm_mask);
		PINFO("hd_desc[%d].sys_dimm_count=%d\n", i, hd_desc[i].sys_dimm_count);
		PINFO("hd_desc[%d].sys_hdimm_count=%d\n", i, hd_desc[i].sys_hdimm_count);
		PINFO("hd_desc[%d].a7_mode=%d\n", i, hd_desc[i].a7_mode);
		PINFO("hd_desc[%d].sock_way=%d\n", i, hd_desc[i].sock_way);
		PINFO("hd_desc[%d].chan_way=%d\n", i, hd_desc[i].chan_way);
		PINFO("hd_desc[%d].rank_way=%d\n\n", i, hd_desc[i].rank_way);
		for (j=0; j < MAX_CPN; j++) {
			/* If the current channel has any dimm, display its info */
			if ( hd_desc[i].channel[j].ch_dimms != 0 ) {
				PINFO("hd_desc[%d].channel[%d].ch_dimms=%d\n", i, j, hd_desc[i].channel[j].ch_dimms);
				PINFO("hd_desc[%d].channel[%d].ch_dimm_mask=0x%.2X\n", i, j, hd_desc[i].channel[j].ch_dimm_mask);
				for (k=0; k < MAX_DPC; k++) {
					/* If there is a dimm in the current dimm socket, display its info */
					if ( hd_desc[i].channel[j].ch_dimm_mask & (unsigned char)(1 << k) ) {
						PINFO("hd_desc[%d].channel[%d].dimm[%d].dram_size=%d GB\n", i, j, k, hd_desc[i].channel[j].dimm[k].dram_size);
						PINFO("hd_desc[%d].channel[%d].dimm[%d].nv_size=%d GB\n", i, j, k, hd_desc[i].channel[j].dimm[k].nv_size);
						PINFO("hd_desc[%d].channel[%d].dimm[%d].node_num=%d\n", i, j, k, hd_desc[i].channel[j].dimm[k].node_num);
						PINFO("hd_desc[%d].channel[%d].dimm[%d].chan_num=%d\n", i, j, k, hd_desc[i].channel[j].dimm[k].chan_num);
						PINFO("hd_desc[%d].channel[%d].dimm[%d].slot_num=%d\n", i, j, k, hd_desc[i].channel[j].dimm[k].slot_num);
						PINFO("hd_desc[%d].channel[%d].dimm[%d].banks=%d\n", i, j, k, hd_desc[i].channel[j].dimm[k].banks);
						PINFO("hd_desc[%d].channel[%d].dimm[%d].ranks=%d\n", i, j, k, hd_desc[i].channel[j].dimm[k].ranks);
						PINFO("hd_desc[%d].channel[%d].dimm[%d].iowidth=%d\n", i, j, k, hd_desc[i].channel[j].dimm[k].iowidth);
						PINFO("hd_desc[%d].channel[%d].dimm[%d].row=0x%x\n", i, j, k, hd_desc[i].channel[j].dimm[k].row);
						PINFO("hd_desc[%d].channel[%d].dimm[%d].col=0x%x\n", i, j, k, hd_desc[i].channel[j].dimm[k].col);
						PINFO("hd_desc[%d].channel[%d].dimm[%d].spd_dimm_id=%d\n", i, j, k, hd_desc[i].channel[j].dimm[k].spd_dimm_id);
					}
				}
			}
		}
	}
//#endif
//MK1101-end
}

static int discover_hvdimm(void)
{
	/* if no HV found, then exit */
    if (system_has_hv_only < 0)
        return -ENODEV;

	PDEBUG("%s\n", __func__);

	if (nvdimm_e820_addr[0] == (u64)0x100000000) {
		system_mmio_size = 0x100000000 - tolm_upper_gb;
		PINFO("system mmio size = %llX \n", system_mmio_size);
		PINFO("NVDIMM size = %llX \n", nvdimm_e820_size[0]);
		PINFO("tolm upper limit = %llX", tolm_upper_gb);
		num_hv = (uint)((nvdimm_e820_size[0] + tolm_upper_gb) / HV_SIZE);    // for testing, since size is 61GB
		PINFO("number hv = %u\n", num_hv);

		switch (num_hv) {
		case 1:     /* Population rule no. 4 */
			//hvdimm_discovered[0].mem_start = nvdimm_e820_addr[0];
			if (system_has_hv_only) {	/* no need to check -1, because already exit way above, if -1 */
				hvdimm_discovered[0].mem_size = HV_SIZE / 2;
				hvdimm_discovered[0].mmio_start = 0 + (HV_SIZE / 2) + system_mmio_size;
				hvdimm_discovered[0].mmio_size = HV_SIZE / 2;
			}
			else {
				hvdimm_discovered[0].mem_size = HV_SIZE / 2;
				hvdimm_discovered[0].mmio_start = nvdimm_e820_addr[0] + (HV_SIZE / 2);
				hvdimm_discovered[0].mmio_size = HV_SIZE / 2;
			}
			num_nodes = 1;
			break;

		case 2:     /* Population rule no. 3 -> total hv = 32GB 2-way interleaved, 1 CPU */
			//hvdimm_discovered[0].mem_start = nvdimm_e820_addr[0];
			if (system_has_hv_only) {
				hvdimm_discovered[0].mem_size = HV_SIZE;                /* 16GB */
				hvdimm_discovered[0].mmio_start = 0 + HV_SIZE + system_mmio_size;
				hvdimm_discovered[0].mmio_size = HV_SIZE;
			}
			else {
				hvdimm_discovered[0].mem_size = HV_SIZE;                /* 16GB */
				hvdimm_discovered[0].mmio_start = nvdimm_e820_addr[0] + HV_SIZE;
				hvdimm_discovered[0].mmio_size = HV_SIZE;
			}
			num_nodes = 1;
			break;
		case 8:     /* Population rule no. 1 -> 128GB, 8xHV, 4-way interleaved, 2 CPU. */
			//hvdimm_discovered[0].mem_start = nvdimm_e820_addr[0];
			if (system_has_hv_only) {
				/* Node 0 */
				hvdimm_discovered[0].mem_size = HV_SIZE * 2;
				hvdimm_discovered[0].mmio_start = nvdimm_e820_addr[0] + HV_SIZE * 2;
				hvdimm_discovered[0].mmio_size = HV_SIZE * 2;
				/* Node 1 */
				hvdimm_discovered[1].mem_start = nvdimm_e820_addr[0] + HV_SIZE * 4;
				hvdimm_discovered[1].mem_size = HV_SIZE * 2;
				hvdimm_discovered[1].mmio_start = nvdimm_e820_addr[0] + HV_SIZE * 6;
				hvdimm_discovered[1].mmio_size = HV_SIZE * 2;
			}
			else {
				/* Node 0 */
				hvdimm_discovered[0].mem_size = HV_SIZE * 2;
				hvdimm_discovered[0].mmio_start = nvdimm_e820_addr[0] + (HV_SIZE * 2) + system_mmio_size;
				hvdimm_discovered[0].mmio_size = HV_SIZE * 2;
				/* Node 1 */
				hvdimm_discovered[1].mem_start = nvdimm_e820_addr[0] + (HV_SIZE * 4) + system_mmio_size;
				hvdimm_discovered[1].mem_size = HV_SIZE * 2;
				hvdimm_discovered[1].mmio_start = nvdimm_e820_addr[0] + (HV_SIZE * 6) + system_mmio_size;
				hvdimm_discovered[1].mmio_size = HV_SIZE * 2;
			}

			num_nodes = 2;
			break;
		case 4:
			/* Get interleaving mode by running dimmcfg's main_program() */
			/* Use the addr to find if it is in between TAD# range and get the ch_way */
			PINFO("hv_ch_way=%x\n", hv_ch_way);

            if (hv_ch_way ==  1) {
				if (system_has_hv_only > 0) {
                    hvdimm_discovered[0].mem_size = HV_SIZE * 2;
                    hvdimm_discovered[0].mmio_start = 0 + (HV_SIZE * 2) + system_mmio_size;   /* mmio at top half */
                    hvdimm_discovered[0].mmio_size = HV_SIZE * 2;
                    num_nodes = 1;
				}
            }
			else if (hv_ch_way ==  2) {
				if (system_has_hv_only > 0) {
                    hvdimm_discovered[0].mem_size = HV_SIZE * 2;
                    hvdimm_discovered[0].mmio_start = 0 + (HV_SIZE * 2) + system_mmio_size;   /* mmio at top half */
                    hvdimm_discovered[0].mmio_size = HV_SIZE * 2;
				}
				else { /* TBD: SJ will provide the algorithm for both LRDIMM and HV combined */
					hvdimm_discovered[0].mem_start = nvdimm_e820_addr[0];
					hvdimm_discovered[0].mem_size = HV_SIZE;                /* 16 GB */
					hvdimm_discovered[0].mmio_start = nvdimm_e820_addr[0] + HV_SIZE;       /* mmio at top half of bottom node memory at 16GB */
					hvdimm_discovered[0].mmio_size = HV_SIZE;              /* 16 GB */

					hvdimm_discovered[1].mem_start = nvdimm_e820_addr[0] + HV_SIZE * 2;
					hvdimm_discovered[1].mem_size = HV_SIZE;                /* 16 GB */
					hvdimm_discovered[1].mmio_start = nvdimm_e820_addr[0] + HV_SIZE * 3;   /* mmio at top half of top node memory at 48GB */
					hvdimm_discovered[1].mmio_size = HV_SIZE;               /* 16 GB */
				}

				num_nodes = 2;
			}
			else if (hv_ch_way == 4) {
				if (system_has_hv_only > 0) {
					hvdimm_discovered[0].mem_start = nvdimm_e820_addr[0];
					hvdimm_discovered[0].mem_size = HV_SIZE * 2;                /* 32 GB */
					hvdimm_discovered[0].mmio_start = nvdimm_e820_addr[0] + HV_SIZE * 2;       /* mmio at top half of bottom node memory at 32GB */
					hvdimm_discovered[0].mmio_size = HV_SIZE * 2;              /* 32 GB */
				}
				else
				{
					hvdimm_discovered[0].mem_start = nvdimm_e820_addr[0];
					hvdimm_discovered[0].mem_size = HV_SIZE * 2;                /* 32 GB */
					hvdimm_discovered[0].mmio_start = nvdimm_e820_addr[0] + (HV_SIZE * 2) + system_mmio_size; /* mmio at top half of bottom node memory at 32GB */
					hvdimm_discovered[0].mmio_size = HV_SIZE * 2;              /* 32 GB */
				}
			}
			else
				PERR("ERROR: %s: something wrong, config does not match population rule\n", __func__);

			break;
		default:
			PERR("num_hv is not 1,2,4,8. ERROR\n");
			break;
		}

	}

    hv_start_addr0 = hvdimm_discovered[0].mem_start;
    hv_start_addr1 = hvdimm_discovered[1].mem_start;
    hv_mmio_start0 = hvdimm_discovered[0].mmio_start;
    hv_mmio_start1 = hvdimm_discovered[1].mmio_start;
    hv_mmio_size0  = hvdimm_discovered[0].mmio_size;
    hv_mmio_size1  = hvdimm_discovered[1].mmio_size;
    hv_mem_size0   = hvdimm_discovered[0].mem_size;
    hv_mem_size1   = hvdimm_discovered[1].mem_size;



	print_discovered_hvs();

    return 0;
}

static void populate_hvdata(void)
{
	return;	// return until discovery module is verified

    hv_group[0].mem[0].p_dram = hvdimm_discovered[0].mem_start;
    hv_group[0].mem[0].p_mmio = hvdimm_discovered[0].mmio_start;
    hv_group[0].mem[1].p_dram = hvdimm_discovered[1].mem_start;
    hv_group[0].mem[1].p_mmio = hvdimm_discovered[1].mmio_start;
    hv_group[0].intv.chnl_way = (unsigned int)hv_ch_way;
    hv_group[0].num_hv = (unsigned int)num_hv;
}


/***************************************************************************
 * Knight landing stuff
 ***************************************************************************/
/*
 * Retrieve the n'th Target Address Decode table entry
 * from the memory controller's TAD table.
 *
 * @pvt:	driver private data
 * @entry:	which entry you want to retrieve
 * @mc:		which memory controller (0 or 1)
 * @offset:	output tad range offset
 * @limit:	output address of first byte above tad range
 * @ways:	output number of interleave ways
 *
 * The offset value has curious semantics.  It's a sort of running total
 * of the sizes of all the memory regions that aren't mapped in this
 * tad table.
 */


/* Low bits of TAD limit, and some metadata. */
static const u32 knl_tad_dram_limit_lo[] = {
	0x400, 0x500, 0x600, 0x700,
	0x800, 0x900, 0xa00, 0xb00,
};

/* Low bits of TAD offset. */
static const u32 knl_tad_dram_offset_lo[] = {
	0x404, 0x504, 0x604, 0x704,
	0x804, 0x904, 0xa04, 0xb04,
};

/* High 16 bits of TAD limit and offset. */
static const u32 knl_tad_dram_hi[] = {
	0x408, 0x508, 0x608, 0x708,
	0x808, 0x908, 0xa08, 0xb08,
};

/* Number of ways a tad entry is interleaved. */
static const u32 knl_tad_ways[] = {
	8, 6, 4, 3, 2, 1,
};

static int knl_get_tad(const struct sbridge_pvt *pvt,
		const int entry,
		const int mc,
		u64 *offset,
		u64 *limit,
		int *ways)
{
	u32 reg_limit_lo, reg_offset_lo, reg_hi;
	struct pci_dev *pci_mc;
	int way_id;

	switch (mc) {
	case 0:
		pci_mc = pvt->knl.pci_mc0;
		break;
	case 1:
		pci_mc = pvt->knl.pci_mc1;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	pci_read_config_dword(pci_mc,
			knl_tad_dram_limit_lo[entry], &reg_limit_lo);
	pci_read_config_dword(pci_mc,
			knl_tad_dram_offset_lo[entry], &reg_offset_lo);
	pci_read_config_dword(pci_mc,
			knl_tad_dram_hi[entry], &reg_hi);

	/* Is this TAD entry enabled? */
	if (!GET_BITFIELD(reg_limit_lo, 0, 0))
		return -ENODEV;

	way_id = GET_BITFIELD(reg_limit_lo, 3, 5);

	if (way_id < ARRAY_SIZE(knl_tad_ways)) {
		*ways = knl_tad_ways[way_id];
	} else {
		*ways = 0;
		PERR("Unexpected value %d in mc_tad_limit_lo wayness field\n",
				way_id);
		return -ENODEV;
	}

	/*
	 * The least significant 6 bits of base and limit are truncated.
	 * For limit, we fill the missing bits with 1s.
	 */
	*offset = ((u64) GET_BITFIELD(reg_offset_lo, 6, 31) << 6) |
				((u64) GET_BITFIELD(reg_hi, 0,  15) << 32);
	*limit = ((u64) GET_BITFIELD(reg_limit_lo,  6, 31) << 6) | 63 |
				((u64) GET_BITFIELD(reg_hi, 16, 31) << 32);

	return 0;
}

/* Determine which memory controller is responsible for a given channel. */
static int knl_channel_mc(int channel)
{
	WARN_ON(channel < 0 || channel >= 6);

	return channel < 3 ? 1 : 0;
}

/*
 * Get the Nth entry from EDC_ROUTE_TABLE register.
 * (This is the per-tile mapping of logical interleave targets to
 *  physical EDC modules.)
 *
 * entry 0: 0:2
 *       1: 3:5
 *       2: 6:8
 *       3: 9:11
 *       4: 12:14
 *       5: 15:17
 *       6: 18:20
 *       7: 21:23
 * reserved: 24:31
 */
static u32 knl_get_edc_route(int entry, u32 reg)
{
	WARN_ON(entry >= KNL_MAX_EDCS);
	return GET_BITFIELD(reg, entry*3, (entry*3)+2);
}

/*
 * Get the Nth entry from MC_ROUTE_TABLE register.
 * (This is the per-tile mapping of logical interleave targets to
 *  physical DRAM channels modules.)
 *
 * entry 0: mc 0:2   channel 18:19
 *       1: mc 3:5   channel 20:21
 *       2: mc 6:8   channel 22:23
 *       3: mc 9:11  channel 24:25
 *       4: mc 12:14 channel 26:27
 *       5: mc 15:17 channel 28:29
 * reserved: 30:31
 *
 * Though we have 3 bits to identify the MC, we should only see
 * the values 0 or 1.
 */

static u32 knl_get_mc_route(int entry, u32 reg)
{
	int mc, chan;

	WARN_ON(entry >= KNL_MAX_CHANNELS);

	mc = GET_BITFIELD(reg, entry*3, (entry*3)+2);
	chan = GET_BITFIELD(reg, (entry*2) + 18, (entry*2) + 18 + 1);

	return knl_channel_remap(mc*3 + chan);
}

/*
 * Render the EDC_ROUTE register in human-readable form.
 * Output string s should be at least KNL_MAX_EDCS*2 bytes.
 */
static void knl_show_edc_route(u32 reg, char *s)
{
	int i;

	for (i = 0; i < KNL_MAX_EDCS; i++) {
		s[i*2] = knl_get_edc_route(i, reg) + '0';
		s[i*2+1] = '-';
	}

	s[KNL_MAX_EDCS*2 - 1] = '\0';
}

/*
 * Render the MC_ROUTE register in human-readable form.
 * Output string s should be at least KNL_MAX_CHANNELS*2 bytes.
 */
static void knl_show_mc_route(u32 reg, char *s)
{
	int i;

	for (i = 0; i < KNL_MAX_CHANNELS; i++) {
		s[i*2] = knl_get_mc_route(i, reg) + '0';
		s[i*2+1] = '-';
	}

	s[KNL_MAX_CHANNELS*2 - 1] = '\0';
}

#define KNL_EDC_ROUTE 0xb8
#define KNL_MC_ROUTE 0xb4

/* Is this dram rule backed by regular DRAM in flat mode? */
#define KNL_EDRAM(reg) GET_BITFIELD(reg, 29, 29)

/* Is this dram rule cached? */
#define KNL_CACHEABLE(reg) GET_BITFIELD(reg, 28, 28)

/* Is this rule backed by edc ? */
#define KNL_EDRAM_ONLY(reg) GET_BITFIELD(reg, 29, 29)

/* Is this rule backed by DRAM, cacheable in EDRAM? */
#define KNL_CACHEABLE(reg) GET_BITFIELD(reg, 28, 28)

/* Is this rule mod3? */
#define KNL_MOD3(reg) GET_BITFIELD(reg, 27, 27)


/****************************************************************************
			Ancillary status routines
 ****************************************************************************/

static inline int numrank(enum type type, u32 mtr)
{
	int ranks = (1 << RANK_CNT_BITS(mtr));
	int max = 4;

	if (type == HASWELL || type == BROADWELL || type == KNIGHTS_LANDING)
		max = 8;

	if (ranks > max) {
		PDEBUG("Invalid number of ranks: %d (max = %i) raw value = %x (%04x)\n",
			 ranks, max, (unsigned int)RANK_CNT_BITS(mtr), mtr);
		return -EINVAL;
	}

	return ranks;
}

static inline int numrow(u32 mtr)
{
	int rows = (RANK_WIDTH_BITS(mtr) + 12);

	if (rows < 13 || rows > 18) {
		PDEBUG("Invalid number of rows: %d (should be between 14 and 17) raw value = %x (%04x)\n",
			 rows, (unsigned int)RANK_WIDTH_BITS(mtr), mtr);
		return -EINVAL;
	}

	return 1 << rows;
}

static inline int numcol(u32 mtr)
{
	int cols = (COL_WIDTH_BITS(mtr) + 10);

	if (cols > 12) {
		PDEBUG("Invalid number of cols: %d (max = 4) raw value = %x (%04x)\n",
			 cols, (unsigned int)COL_WIDTH_BITS(mtr), mtr);
		return -EINVAL;
	}

	return 1 << cols;
}

static u64 sbridge_get_tolm(struct sbridge_pvt *pvt)
{
	u32 reg;

	/* Address range is 32:28 */
	pci_read_config_dword(pvt->pci_sad1, TOLM, &reg);
	return GET_TOLM(reg);
}

static u64 sbridge_get_tohm(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->pci_sad1, TOHM, &reg);
	return GET_TOHM(reg);
}

/* Not ported for ivy bridge
static u64 ibridge_get_tolm(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->pci_br1, TOLM, &reg);

	return GET_TOLM(reg);
}

static u64 ibridge_get_tohm(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->pci_br1, TOHM, &reg);

	return GET_TOHM(reg);
}
*/

static u64 rir_limit(u32 reg)
{
	return ((u64)GET_BITFIELD(reg,  1, 10) << 29) | 0x1fffffff;
}

static u64 sad_limit(u32 reg)
{
	return (GET_BITFIELD(reg, 6, 25) << 26) | 0x3ffffff;
}

static u32 interleave_mode(u32 reg)
{
	return GET_BITFIELD(reg, 1, 1);
}

char *show_interleave_mode(u32 reg)
{
	return interleave_mode(reg) ? "8:6" : "[8:6]XOR[18:16]";
}

static u32 dram_attr(u32 reg)
{
	return GET_BITFIELD(reg, 2, 3);
}

static u64 knl_sad_limit(u32 reg)
{
	return (GET_BITFIELD(reg, 7, 26) << 26) | 0x3ffffff;
}

static u32 knl_interleave_mode(u32 reg)
{
	return GET_BITFIELD(reg, 1, 2);
}

static char *knl_show_interleave_mode(u32 reg)
{
	char *s;

	switch (knl_interleave_mode(reg)) {
	case 0:
		s = "use address bits [8:6]";
		break;
	case 1:
		s = "use address bits [10:8]";
		break;
	case 2:
		s = "use address bits [14:12]";
		break;
	case 3:
		s = "use address bits [32:30]";
		break;
	default:
		WARN_ON(1);
		break;
	}

	return s;
}

static u32 dram_attr_knl(u32 reg)
{
	return GET_BITFIELD(reg, 3, 4);
}


static enum mem_type get_memory_type(struct sbridge_pvt *pvt)
{
	u32 reg;
	enum mem_type mtype;

	if (pvt->pci_ddrio) {
		pci_read_config_dword(pvt->pci_ddrio, pvt->info.rankcfgr,
				      &reg);
		if (GET_BITFIELD(reg, 11, 11))
			/* FIXME: Can also be LRDIMM */
			mtype = MEM_RDDR3;
		else
			mtype = MEM_DDR3;
	} else
		mtype = MEM_UNKNOWN;

	return mtype;
}

static enum mem_type haswell_get_memory_type(struct sbridge_pvt *pvt)
{
	u32 reg;
	bool registered = false;
	enum mem_type mtype = MEM_UNKNOWN;

	if (!pvt->pci_ddrio)
		goto out;

	pci_read_config_dword(pvt->pci_ddrio,
			      HASWELL_DDRCRCLKCONTROLS, &reg);
	/* Is_Rdimm */
	if (GET_BITFIELD(reg, 16, 16))
		registered = true;

	pci_read_config_dword(pvt->pci_ta, MCMTR, &reg);
	if (GET_BITFIELD(reg, 14, 14)) {
		if (registered)
			mtype = MEM_RDDR4;
		else
			mtype = MEM_DDR4;
	} else {
		if (registered)
			mtype = MEM_RDDR3;
		else
			mtype = MEM_DDR3;
	}

out:
	return mtype;
}

static enum dev_type knl_get_width(struct sbridge_pvt *pvt, u32 mtr)
{
	/* for KNL value is fixed */
	return DEV_X16;
}

static enum dev_type sbridge_get_width(struct sbridge_pvt *pvt, u32 mtr)
{
	/* there's no way to figure out */
	return DEV_UNKNOWN;
}

static enum dev_type __ibridge_get_width(u32 mtr)
{
	enum dev_type type;

	switch (mtr) {
	case 3:
		type = DEV_UNKNOWN;
		break;
	case 2:
		type = DEV_X16;
		break;
	case 1:
		type = DEV_X8;
		break;
	case 0:
		type = DEV_X4;
		break;
	}

	return type;
}

static enum dev_type ibridge_get_width(struct sbridge_pvt *pvt, u32 mtr)
{
	/*
	 * ddr3_width on the documentation but also valid for DDR4 on
	 * Haswell
	 */
	return __ibridge_get_width(GET_BITFIELD(mtr, 7, 8));
}

static enum dev_type broadwell_get_width(struct sbridge_pvt *pvt, u32 mtr)
{
	/* ddr3_width on the documentation but also valid for DDR4 */
//MK1101	return __ibridge_get_width(GET_BITFIELD(mtr, 8, 9));
//MK1101-begin
	return (4 << (GET_BITFIELD(mtr, 8, 9)));
//MK1101-end
}

static enum mem_type knl_get_memory_type(struct sbridge_pvt *pvt)
{
	/* DDR4 RDIMMS and LRDIMMS are supported */
	return MEM_RDDR4;
}

static u8 get_node_id(struct sbridge_pvt *pvt)
{
	u32 reg;
	pci_read_config_dword(pvt->pci_br0, SAD_CONTROL, &reg);
	return GET_BITFIELD(reg, 0, 2);
}

static u8 haswell_get_node_id(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->pci_sad1, SAD_CONTROL, &reg);
	return GET_BITFIELD(reg, 0, 3);
}

static u8 knl_get_node_id(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->pci_sad1, SAD_CONTROL, &reg);
	return GET_BITFIELD(reg, 0, 2);
}


static u64 haswell_get_tolm(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->info.pci_vtd, HASWELL_TOLM, &reg);
	return (GET_BITFIELD(reg, 26, 31) << 26) | 0x3ffffff;
}

static u64 haswell_get_tohm(struct sbridge_pvt *pvt)
{
	u64 rc;
	u32 reg;

	pci_read_config_dword(pvt->info.pci_vtd, HASWELL_TOHM_0, &reg);
	rc = GET_BITFIELD(reg, 26, 31);
	pci_read_config_dword(pvt->info.pci_vtd, HASWELL_TOHM_1, &reg);
	rc = ((reg << 6) | rc) << 26;

//MK1101	return rc | 0x1ffffff;
//MK1101-begin
	return rc | 0x3FFFFFF;
//MK1101-end
}

static u64 knl_get_tolm(struct sbridge_pvt *pvt)
{
	u32 reg;

	pci_read_config_dword(pvt->knl.pci_mc_info, KNL_TOLM, &reg);
	return (GET_BITFIELD(reg, 26, 31) << 26) | 0x3ffffff;
}

static u64 knl_get_tohm(struct sbridge_pvt *pvt)
{
	u64 rc;
	u32 reg_lo, reg_hi;

	pci_read_config_dword(pvt->knl.pci_mc_info, KNL_TOHM_0, &reg_lo);
	pci_read_config_dword(pvt->knl.pci_mc_info, KNL_TOHM_1, &reg_hi);
	rc = ((u64)reg_hi << 32) | reg_lo;
	return rc | 0x3ffffff;
}


static u64 haswell_rir_limit(u32 reg)
{
	return (((u64)GET_BITFIELD(reg,  1, 11) + 1) << 29) - 1;
}

static inline u8 sad_pkg_socket(u8 pkg)
{
	/* on Ivy Bridge, nodeID is SASS, where A is HA and S is node id */
	return ((pkg >> 3) << 2) | (pkg & 0x3);
}

static inline u8 sad_pkg_ha(u8 pkg)
{
	return (pkg >> 2) & 0x1;
}

static struct sbridge_dev *get_sbridge_dev(u8 bus, int multi_bus)
{
	struct sbridge_dev *sbridge_dev;

	/*
	 * If we have devices scattered across several busses that pertain
	 * to the same memory controller, we'll lump them all together.
	 */
	if (multi_bus) {
		return list_first_entry_or_null(&sbridge_edac_list,
				struct sbridge_dev, list);
	}

	list_for_each_entry(sbridge_dev, &sbridge_edac_list, list) {
		if (sbridge_dev->bus == bus)
			return sbridge_dev;
	}

	return NULL;
}

static int sbridge_mci_bind_devs(struct mem_ctl_info *mci,
				 struct sbridge_dev *sbridge_dev)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	struct pci_dev *pdev;
	u8 saw_chan_mask = 0;
	int i;

	for (i = 0; i < sbridge_dev->n_devs; i++) {
		pdev = sbridge_dev->pdev[i];
		if (!pdev)
			continue;

		switch (pdev->device) {
		case PCI_DEVICE_ID_INTEL_SBRIDGE_SAD0:
			pvt->pci_sad0 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_SAD1:
			pvt->pci_sad1 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_BR:
			pvt->pci_br0 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_HA0:
			pvt->pci_ha0 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TA:
			pvt->pci_ta = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_RAS:
			pvt->pci_ras = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD0:
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD1:
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD2:
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD3:
		{
			int id = pdev->device - PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TAD0;
			pvt->pci_tad[id] = pdev;
			saw_chan_mask |= 1 << id;
		}
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_DDRIO:
			pvt->pci_ddrio = pdev;
			break;
		default:
			goto error;
		}

		PDEBUG("Associated PCI %02x:%02x, bus %d with dev = %p\n",
			 pdev->vendor, pdev->device,
			 sbridge_dev->bus,
			 pdev);
	}

	/* Check if everything were registered */
	if (!pvt->pci_sad0 || !pvt->pci_sad1 || !pvt->pci_ha0 ||
	    !pvt-> pci_tad || !pvt->pci_ras  || !pvt->pci_ta)
		goto enodev;

	if (saw_chan_mask != 0x0f)
		goto enodev;
	return 0;

enodev:
	PERR("Some needed devices are missing\n");
	return -ENODEV;

error:
	PERR("Unexpected device %02x:%02x\n",
		       PCI_VENDOR_ID_INTEL, pdev->device);
	return -EINVAL;
}


static struct sbridge_dev *alloc_sbridge_dev(u8 bus,
					   const struct pci_id_table *table)
{
	struct sbridge_dev *sbridge_dev;

	sbridge_dev = kzalloc(sizeof(*sbridge_dev), GFP_KERNEL);
	if (!sbridge_dev)
		return NULL;

	sbridge_dev->pdev = kzalloc(sizeof(*sbridge_dev->pdev) * table->n_devs,
				   GFP_KERNEL);
	if (!sbridge_dev->pdev) {
		kfree(sbridge_dev);
		return NULL;
	}

	sbridge_dev->bus = bus;
	sbridge_dev->n_devs = table->n_devs;
	list_add_tail(&sbridge_dev->list, &sbridge_edac_list);

	return sbridge_dev;
}

static void free_sbridge_dev(struct sbridge_dev *sbridge_dev)
{
	list_del(&sbridge_dev->list);
	kfree(sbridge_dev->pdev);
	kfree(sbridge_dev);
}

static int haswell_mci_bind_devs(struct mem_ctl_info *mci,
				 struct sbridge_dev *sbridge_dev)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	struct pci_dev *pdev;
	u8 saw_chan_mask = 0;
	int i;

	/* there's only one device per system; not tied to any bus */
	if (pvt->info.pci_vtd == NULL)
		/* result will be checked later */
		pvt->info.pci_vtd = pci_get_device(PCI_VENDOR_ID_INTEL,
						   PCI_DEVICE_ID_INTEL_HASWELL_IMC_VTD_MISC,
						   NULL);

	for (i = 0; i < sbridge_dev->n_devs; i++) {
		pdev = sbridge_dev->pdev[i];
		if (!pdev)
			continue;

		switch (pdev->device) {
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_CBO_SAD0:
			pvt->pci_sad0 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_CBO_SAD1:
			pvt->pci_sad1 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0:
			pvt->pci_ha0 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TA:
			pvt->pci_ta = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_THERMAL:
			pvt->pci_ras = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD0:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD1:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD2:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD3:
		{
			int id = pdev->device - PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TAD0;

			pvt->pci_tad[id] = pdev;
			saw_chan_mask |= 1 << id;
		}
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD0:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD1:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD2:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD3:
		{
			int id = pdev->device - PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TAD0 + 4;

			pvt->pci_tad[id] = pdev;
			saw_chan_mask |= 1 << id;
		}
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO0:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO1:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO2:
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_DDRIO3:
			if (!pvt->pci_ddrio)
				pvt->pci_ddrio = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1:
			pvt->pci_ha1 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA1_TA:
			pvt->pci_ha1_ta = pdev;
			break;
		default:
			break;
		}

		PDEBUG("Associated PCI %02x.%02d.%d with dev = %p\n",
			 sbridge_dev->bus,
			 PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
			 pdev);
	}

	/* Check if everything were registered */
	if (!pvt->pci_sad0 || !pvt->pci_ha0 || !pvt->pci_sad1 ||
	    !pvt->pci_ras  || !pvt->pci_ta || !pvt->info.pci_vtd)
		goto enodev;

	if (saw_chan_mask != 0x0f && /* -EN */
	    saw_chan_mask != 0x33 && /* -EP */
	    saw_chan_mask != 0xff)   /* -EX */
		goto enodev;
	return 0;

enodev:
	PERR("Some needed devices are missing\n");
	return -ENODEV;
}

static int broadwell_mci_bind_devs(struct mem_ctl_info *mci,
				 struct sbridge_dev *sbridge_dev)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	struct pci_dev *pdev;
	u8 saw_chan_mask = 0;
	int i;
//MK1101-begin
	u32 reg;
//MK1101-end
//MK0414-begin
	int id = -1;
//MK0414-end

	/* there's only one device per system; not tied to any bus */
	if (pvt->info.pci_vtd == NULL)
		/* result will be checked later */
		pvt->info.pci_vtd = pci_get_device(PCI_VENDOR_ID_INTEL,
						   PCI_DEVICE_ID_INTEL_BROADWELL_IMC_VTD_MISC,
						   NULL);

	for (i = 0; i < sbridge_dev->n_devs; i++) {
		pdev = sbridge_dev->pdev[i];
		if (!pdev)
			continue;

		switch (pdev->device) {
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_CBO_SAD0:
			pvt->pci_sad0 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_CBO_SAD1:
			pvt->pci_sad1 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0:
			pvt->pci_ha0 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TA:
			pvt->pci_ta = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_THERMAL:
			pvt->pci_ras = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD0:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD1:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD2:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD3:
		{
//MK0414			int id = pdev->device - PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TAD0;
//MK0414-begin
			id++;
//MK0414-end
			pvt->pci_tad[id] = pdev;
			saw_chan_mask |= 1 << id;
//MK1101-begin
			pci_read_config_dword(pvt->pci_tad[id], 0, &reg);
			PINFO("id = %d - DID:VID = 0x%.8x\n", id, reg);
//MK1101-end
		}
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD0:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD1:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD2:
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD3:
		{
//MK0414			int id = pdev->device - PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TAD0 + 4;
//MK0414-begin
			id++;
//MK0414-end
			pvt->pci_tad[id] = pdev;
			saw_chan_mask |= 1 << id;
//MK1101-begin
			pci_read_config_dword(pvt->pci_tad[id], 0, &reg);
			PINFO("id = %d - DID:VID = 0x%.8x\n", id, reg);
//MK1101-end
		}
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_DDRIO0:
			pvt->pci_ddrio = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1:
			pvt->pci_ha1 = pdev;
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA1_TA:
			pvt->pci_ha1_ta = pdev;
			break;
		default:
			break;
		}

		PDEBUG("Associated PCI %02x.%02d.%d with dev = %p\n",
			 sbridge_dev->bus,
			 PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
			 pdev);
	}

	/* Check if everything were registered */
	if (!pvt->pci_sad0 || !pvt->pci_ha0 || !pvt->pci_sad1 ||
	    !pvt->pci_ras  || !pvt->pci_ta || !pvt->info.pci_vtd)
		goto enodev;

	if (saw_chan_mask != 0x0f && /* -EN */
	    saw_chan_mask != 0x33 && /* -EP */
	    saw_chan_mask != 0xff)   /* -EX */
		goto enodev;
	return 0;

enodev:
	PERR("Some needed devices are missing\n");
	return -ENODEV;
}

static int knl_mci_bind_devs(struct mem_ctl_info *mci,
			struct sbridge_dev *sbridge_dev)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	struct pci_dev *pdev;
	int dev, func;

	int i;
	int devidx;

	for (i = 0; i < sbridge_dev->n_devs; i++) {
		pdev = sbridge_dev->pdev[i];
		if (!pdev)
			continue;

		/* Extract PCI device and function. */
		dev = (pdev->devfn >> 3) & 0x1f;
		func = pdev->devfn & 0x7;

		switch (pdev->device) {
		case PCI_DEVICE_ID_INTEL_KNL_IMC_MC:
			if (dev == 8)
				pvt->knl.pci_mc0 = pdev;
			else if (dev == 9)
				pvt->knl.pci_mc1 = pdev;
			else {
				PERR("Memory controller in unexpected place! (dev %d, fn %d)\n",
					dev, func);
				continue;
			}
			break;

		case PCI_DEVICE_ID_INTEL_KNL_IMC_SAD0:
			pvt->pci_sad0 = pdev;
			break;

		case PCI_DEVICE_ID_INTEL_KNL_IMC_SAD1:
			pvt->pci_sad1 = pdev;
			break;

		case PCI_DEVICE_ID_INTEL_KNL_IMC_CHA:
			/* There are one of these per tile, and range from
			 * 1.14.0 to 1.18.5.
			 */
			devidx = ((dev-14)*8)+func;

			if (devidx < 0 || devidx >= KNL_MAX_CHAS) {
				PERR("Caching and Home Agent in unexpected place! (dev %d, fn %d)\n",
					dev, func);
				continue;
			}

			WARN_ON(pvt->knl.pci_cha[devidx] != NULL);

			pvt->knl.pci_cha[devidx] = pdev;
			break;

		case PCI_DEVICE_ID_INTEL_KNL_IMC_CHANNEL:
			devidx = -1;

			/*
			 *  MC0 channels 0-2 are device 9 function 2-4,
			 *  MC1 channels 3-5 are device 8 function 2-4.
			 */

			if (dev == 9)
				devidx = func-2;
			else if (dev == 8)
				devidx = 3 + (func-2);

			if (devidx < 0 || devidx >= KNL_MAX_CHANNELS) {
				PERR("DRAM Channel Registers in unexpected place! (dev %d, fn %d)\n",
					dev, func);
				continue;
			}

			WARN_ON(pvt->knl.pci_channel[devidx] != NULL);
			pvt->knl.pci_channel[devidx] = pdev;
			break;

		case PCI_DEVICE_ID_INTEL_KNL_IMC_TOLHM:
			pvt->knl.pci_mc_info = pdev;
			break;

		case PCI_DEVICE_ID_INTEL_KNL_IMC_TA:
			pvt->pci_ta = pdev;
			break;

		default:
			PERR("Unexpected device %d\n",
				pdev->device);
			break;
		}
	}

	if (!pvt->knl.pci_mc0  || !pvt->knl.pci_mc1 ||
	    !pvt->pci_sad0     || !pvt->pci_sad1    ||
	    !pvt->pci_ta) {
		goto enodev;
	}

	for (i = 0; i < KNL_MAX_CHANNELS; i++) {
		if (!pvt->knl.pci_channel[i]) {
			PERR("Missing channel %d\n", i);
			goto enodev;
		}
	}

	for (i = 0; i < KNL_MAX_CHAS; i++) {
		if (!pvt->knl.pci_cha[i]) {
			PERR("Missing CHA %d\n", i);
			goto enodev;
		}
	}

	return 0;

enodev:
	PERR("Some needed devices are missing\n");
	return -ENODEV;
}


/****************************************************************************
	Device initialization routines: put/get, init/exit
 ****************************************************************************/

/*
 *	sbridge_put_all_devices	'put' all the devices that we have
 *				reserved via 'get'
 */
static void sbridge_put_devices(struct sbridge_dev *sbridge_dev)
{
	int i;

	PDEBUG("%s\n", __func__);
	for (i = 0; i < sbridge_dev->n_devs; i++) {
		struct pci_dev *pdev = sbridge_dev->pdev[i];
		if (!pdev)
			continue;
		PDEBUG("Removing dev %02x:%02x.%d\n",
			 pdev->bus->number,
			 PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
		pci_dev_put(pdev);
	}
}

static void sbridge_put_all_devices(void)
{
	struct sbridge_dev *sbridge_dev, *tmp;

	PDEBUG("%s\n", __func__);
	list_for_each_entry_safe(sbridge_dev, tmp, &sbridge_edac_list, list) {
		sbridge_put_devices(sbridge_dev);
		free_sbridge_dev(sbridge_dev);
	}
}


static int sbridge_get_onedevice(struct pci_dev **prev,
				 u8 *num_mc,
				 const struct pci_id_table *table,
				 const unsigned devno,
				 const int multi_bus)
{
	struct sbridge_dev *sbridge_dev;
	const struct pci_id_descr *dev_descr = &table->descr[devno];
	struct pci_dev *pdev = NULL;
	u8 bus = 0;

	//PDEBUG("Seeking for: PCI ID %04x:%04x\n", PCI_VENDOR_ID_INTEL, dev_descr->dev_id);
//MK0414-begin
	PDEBUG("[%s] Seeking for: PCI ID %04x:%04x\n", __func__, PCI_VENDOR_ID_INTEL, dev_descr->dev_id);
//MK0414-end
	pdev = pci_get_device(PCI_VENDOR_ID_INTEL, dev_descr->dev_id, *prev);

	if (!pdev) {
		if (*prev) {
			*prev = pdev;
			return 0;
		}

		if (dev_descr->optional)
			return 0;

		/* if the HA wasn't found */
		if (devno == 0)
			return -ENODEV;

		PDEBUG("Device not found: %04x:%04x\n", PCI_VENDOR_ID_INTEL, dev_descr->dev_id);

		/* End of list, leave */
		return -ENODEV;
	}
	bus = pdev->bus->number;

	sbridge_dev = get_sbridge_dev(bus, multi_bus);
	if (!sbridge_dev) {
		sbridge_dev = alloc_sbridge_dev(bus, table);
		if (!sbridge_dev) {
			pci_dev_put(pdev);
			return -ENOMEM;
		}
		(*num_mc)++;
	}

	if (sbridge_dev->pdev[devno]) {
		PERR("Duplicated device for %04x:%04x\n",
			PCI_VENDOR_ID_INTEL, dev_descr->dev_id);
		pci_dev_put(pdev);
		return -ENODEV;
	}

	sbridge_dev->pdev[devno] = pdev;

	/* Be sure that the device is enabled */
	if (unlikely(pci_enable_device(pdev) < 0)) {
		PERR("Couldn't enable %04x:%04x\n",
			PCI_VENDOR_ID_INTEL, dev_descr->dev_id);
		return -ENODEV;
	}

//MK0414	PDEBUG("Detected %04x:%04x\n", PCI_VENDOR_ID_INTEL, dev_descr->dev_id);
//MK0414-begin
	PDEBUG("[%s] Detected %04x:%04x, num_mc = %d\n", __func__, PCI_VENDOR_ID_INTEL, dev_descr->dev_id, *num_mc);
//MK0414-end

	/*
	 * As stated on drivers/pci/search.c, the reference count for
	 * @from is always decremented if it is not %NULL. So, as we need
	 * to get all devices up to null, we need to do a get for the device
	 */
	pci_dev_get(pdev);

	*prev = pdev;

	return 0;
}

/*
 * sbridge_get_all_devices - Find and perform 'get' operation on the MCH's
 *			     devices we want to reference for this driver.
 * @num_mc: pointer to the memory controllers count, to be incremented in case
 *	    of success.
 * @table: model specific table
 * @allow_dups: allow for multiple devices to exist with the same device id
 *              (as implemented, this isn't expected to work correctly in the
 *              multi-socket case).
 * @multi_bus: don't assume devices on different buses belong to different
 *             memory controllers.
 *
 * returns 0 in case of success or error code
 */
static int sbridge_get_all_devices_full(u8 *num_mc,
					const struct pci_id_table *table,
					int allow_dups,
					int multi_bus)
{
	int i, rc;
	struct pci_dev *pdev = NULL;

	while (table && table->descr) {
		for (i = 0; i < table->n_devs; i++) {
			if (!allow_dups || i == 0 ||
					table->descr[i].dev_id !=
						table->descr[i-1].dev_id) {
				pdev = NULL;
			}
			do {
				rc = sbridge_get_onedevice(&pdev, num_mc,
							   table, i, multi_bus);
				if (rc < 0) {
					if (i == 0) {
						i = table->n_devs;
						break;
					}
					sbridge_put_all_devices();
					return -ENODEV;
				}
			} while (pdev && !allow_dups);
		}
		table++;
	}

	return 0;
}

#define sbridge_get_all_devices(num_mc, table) \
		sbridge_get_all_devices_full(num_mc, table, 0, 0)
#define sbridge_get_all_devices_knl(num_mc, table) \
		sbridge_get_all_devices_full(num_mc, table, 1, 1)

/****************************************************************************
			EDAC register/unregister logic
 ****************************************************************************/

/**
 * edac_mc_free
 *	'Free' a previously allocated 'mci' structure
 * @mci: pointer to a struct mem_ctl_info structure
 */

static void _edac_mc_free(struct mem_ctl_info *mci)
{
	int i, chn, row;
	struct csrow_info *csr;
	const unsigned int tot_dimms = mci->tot_dimms;
	const unsigned int tot_channels = mci->num_cschannel;
	const unsigned int tot_csrows = mci->nr_csrows;

	if (mci->dimms) {
		for (i = 0; i < tot_dimms; i++)
			kfree(mci->dimms[i]);
		kfree(mci->dimms);
	}
	if (mci->csrows) {
		for (row = 0; row < tot_csrows; row++) {
			csr = mci->csrows[row];
			if (csr) {
				if (csr->channels) {
					for (chn = 0; chn < tot_channels; chn++)
						kfree(csr->channels[chn]);
					kfree(csr->channels);
				}
				kfree(csr);
			}
		}
		kfree(mci->csrows);
	}
	kfree(mci);
}


/**
 * edac_align_ptr - Prepares the pointer offsets for a single-shot allocation
 * @p:		pointer to a pointer with the memory offset to be used. At
 *		return, this will be incremented to point to the next offset
 * @size:	Size of the data structure to be reserved
 * @n_elems:	Number of elements that should be reserved
 *
 * If 'size' is a constant, the compiler will optimize this whole function
 * down to either a no-op or the addition of a constant to the value of '*p'.
 *
 * The 'p' pointer is absolutely needed to keep the proper advancing
 * further in memory to the proper offsets when allocating the struct along
 * with its embedded structs, as edac_device_alloc_ctl_info() does it
 * above, for example.
 *
 * At return, the pointer 'p' will be incremented to be used on a next call
 * to this function.
 */
void *edac_align_ptr(void **p, unsigned size, int n_elems)
{
	unsigned align, r;
	void *ptr = *p;

	*p += size * n_elems;

	/*
	 * 'p' can possibly be an unaligned item X such that sizeof(X) is
	 * 'size'.  Adjust 'p' so that its alignment is at least as
	 * stringent as what the compiler would provide for X and return
	 * the aligned result.
	 * Here we assume that the alignment of a "long long" is the most
	 * stringent alignment that the compiler will ever provide by default.
	 * As far as I know, this is a reasonable assumption.
	 */
	if (size > sizeof(long))
		align = sizeof(long long);
	else if (size > sizeof(int))
		align = sizeof(long);
	else if (size > sizeof(short))
		align = sizeof(int);
	else if (size > sizeof(char))
		align = sizeof(short);
	else
		return (char *)ptr;

	r = (unsigned long)p % align;

	if (r == 0)
		return (char *)ptr;

	*p += align - r;

	return (void *)(((unsigned long)ptr) + align - r);
}

/**
 * edac_mc_alloc: Allocate and partially fill a struct mem_ctl_info structure
 * @mc_num:		Memory controller number
 * @n_layers:		Number of MC hierarchy layers
 * layers:		Describes each layer as seen by the Memory Controller
 * @size_pvt:		size of private storage needed
 *
 *
 * Everything is kmalloc'ed as one big chunk - more efficient.
 * Only can be used if all structures have the same lifetime - otherwise
 * you have to allocate and initialize your own structures.
 *
 * Use edac_mc_free() to free mc structures allocated by this function.
 *
 * NOTE: drivers handle multi-rank memories in different ways: in some
 * drivers, one multi-rank memory stick is mapped as one entry, while, in
 * others, a single multi-rank memory stick would be mapped into several
 * entries. Currently, this function will allocate multiple struct dimm_info
 * on such scenarios, as grouping the multiple ranks require drivers change.
 *
 * Returns:
 *	On failure: NULL
 *	On success: struct mem_ctl_info pointer
 */
struct mem_ctl_info *edac_mc_alloc(unsigned mc_num,
				   unsigned n_layers,
				   struct edac_mc_layer *layers,
				   unsigned sz_pvt)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer *layer;
	struct csrow_info *csr;
	struct rank_info *chan;
	struct dimm_info *dimm;
	u32 *ce_per_layer[EDAC_MAX_LAYERS], *ue_per_layer[EDAC_MAX_LAYERS];
	unsigned pos[EDAC_MAX_LAYERS];
	unsigned size, tot_dimms = 1, count = 1;
	unsigned tot_csrows = 1, tot_channels = 1, tot_errcount = 0;
	void *pvt, *p, *ptr = NULL;
	int i, j, row, chn, n, len, off;
	bool per_rank = false;

	BUG_ON(n_layers > EDAC_MAX_LAYERS || n_layers == 0);
	/*
	 * Calculate the total amount of dimms and csrows/cschannels while
	 * in the old API emulation mode
	 */
	for (i = 0; i < n_layers; i++) {
		tot_dimms *= layers[i].size;
		if (layers[i].is_virt_csrow)
			tot_csrows *= layers[i].size;
		else
			tot_channels *= layers[i].size;

		if (layers[i].type == EDAC_MC_LAYER_CHIP_SELECT)
			per_rank = true;
	}

	/* Figure out the offsets of the various items from the start of an mc
	 * structure.  We want the alignment of each item to be at least as
	 * stringent as what the compiler would provide if we could simply
	 * hardcode everything into a single struct.
	 */
	mci = edac_align_ptr(&ptr, sizeof(*mci), 1);
	layer = edac_align_ptr(&ptr, sizeof(*layer), n_layers);
	for (i = 0; i < n_layers; i++) {
		count *= layers[i].size;
		PDEBUG("errcount layer %d size %d\n", i, count);
		ce_per_layer[i] = edac_align_ptr(&ptr, sizeof(u32), count);
		ue_per_layer[i] = edac_align_ptr(&ptr, sizeof(u32), count);
		tot_errcount += 2 * count;
	}

	PDEBUG("allocating %d error counters\n", tot_errcount);
	pvt = edac_align_ptr(&ptr, sz_pvt, 1);
	size = ((unsigned long)pvt) + sz_pvt;

	PDEBUG("allocating %u bytes for mci data (%d %s, %d csrows/channels)\n",
		 size,
		 tot_dimms,
		 per_rank ? "ranks" : "dimms",
		 tot_csrows * tot_channels);

	mci = kzalloc(size, GFP_KERNEL);
	if (mci == NULL)
		return NULL;

	/* Adjust pointers so they point within the memory we just allocated
	 * rather than an imaginary chunk of memory located at address 0.
	 */
	layer = (struct edac_mc_layer *)(((char *)mci) + ((unsigned long)layer));
	for (i = 0; i < n_layers; i++) {
		mci->ce_per_layer[i] = (u32 *)((char *)mci + ((unsigned long)ce_per_layer[i]));
		mci->ue_per_layer[i] = (u32 *)((char *)mci + ((unsigned long)ue_per_layer[i]));
	}
	pvt = sz_pvt ? (((char *)mci) + ((unsigned long)pvt)) : NULL;

	/* setup index and various internal pointers */
	mci->mc_idx = mc_num;
	mci->tot_dimms = tot_dimms;
	mci->pvt_info = pvt;
	mci->n_layers = n_layers;
	mci->layers = layer;
	memcpy(mci->layers, layers, sizeof(*layer) * n_layers);
	mci->nr_csrows = tot_csrows;
	mci->num_cschannel = tot_channels;
	mci->csbased = per_rank;

	/*
	 * Alocate and fill the csrow/channels structs
	 */
	mci->csrows = kcalloc(tot_csrows, sizeof(*mci->csrows), GFP_KERNEL);
	if (!mci->csrows)
		goto error;
	for (row = 0; row < tot_csrows; row++) {
		csr = kzalloc(sizeof(**mci->csrows), GFP_KERNEL);
		if (!csr)
			goto error;
		mci->csrows[row] = csr;
		csr->csrow_idx = row;
		csr->mci = mci;
		csr->nr_channels = tot_channels;
		csr->channels = kcalloc(tot_channels, sizeof(*csr->channels),
					GFP_KERNEL);
		if (!csr->channels)
			goto error;

		for (chn = 0; chn < tot_channels; chn++) {
			chan = kzalloc(sizeof(**csr->channels), GFP_KERNEL);
			if (!chan)
				goto error;
			csr->channels[chn] = chan;
			chan->chan_idx = chn;
			chan->csrow = csr;
		}
	}

	/*
	 * Allocate and fill the dimm structs
	 */
	mci->dimms  = kcalloc(tot_dimms, sizeof(*mci->dimms), GFP_KERNEL);
	if (!mci->dimms)
		goto error;

	memset(&pos, 0, sizeof(pos));
	row = 0;
	chn = 0;
	for (i = 0; i < tot_dimms; i++) {
		chan = mci->csrows[row]->channels[chn];
		off = EDAC_DIMM_OFF(layer, n_layers, pos[0], pos[1], pos[2]);
		if (off < 0 || off >= tot_dimms) {
			PERR("EDAC core bug: EDAC_DIMM_OFF is trying to do an illegal data access\n");
			goto error;
		}

		dimm = kzalloc(sizeof(**mci->dimms), GFP_KERNEL);
		if (!dimm)
			goto error;
		mci->dimms[off] = dimm;
		dimm->mci = mci;

		/*
		 * Copy DIMM location and initialize it.
		 */
		len = sizeof(dimm->label);
		p = dimm->label;
		n = snprintf(p, len, "mc#%u", mc_num);
		p += n;
		len -= n;
		for (j = 0; j < n_layers; j++) {
			n = snprintf(p, len, "%s#%u",
				     edac_layer_name[layers[j].type],
				     pos[j]);
			p += n;
			len -= n;
			dimm->location[j] = pos[j];

			if (len <= 0)
				break;
		}

		/* Link it to the csrows old API data */
		chan->dimm = dimm;
		dimm->csrow = row;
		dimm->cschannel = chn;

		/* Increment csrow location */
		if (layers[0].is_virt_csrow) {
			chn++;
			if (chn == tot_channels) {
				chn = 0;
				row++;
			}
		} else {
			row++;
			if (row == tot_csrows) {
				row = 0;
				chn++;
			}
		}

		/* Increment dimm location */
		for (j = n_layers - 1; j >= 0; j--) {
			pos[j]++;
			if (pos[j] < layers[j].size)
				break;
			pos[j] = 0;
		}
	}

	mci->op_state = OP_ALLOC;

	return mci;

error:
	_edac_mc_free(mci);

	return NULL;
}
//EXPORT_SYMBOL_GPL(edac_mc_alloc);


static void edac_mc_free(struct mem_ctl_info *mci)
{
	PDEBUG("%s\n", __func__);

	/* If we're not yet registered with sysfs free only what was allocated
	 * in edac_mc_alloc().
	 */
	if (!device_is_registered(&mci->dev)) {
		_edac_mc_free(mci);
		return;
	}

	/* the mci instance is freed here, when the sysfs object is dropped */
	//edac_unregister_sysfs(mci);
}
//EXPORT_SYMBOL_GPL(edac_mc_free);

static void sbridge_unregister_mci(struct sbridge_dev *sbridge_dev)
{
	struct mem_ctl_info *mci = sbridge_dev->mci;
	struct sbridge_pvt *pvt;

	if (unlikely(!mci || !mci->pvt_info)) {
		PDEBUG("MC: dev = %p\n", &sbridge_dev->pdev[0]->dev);

		PERR("Couldn't find mci handler\n");
		return;
	}

	pvt = mci->pvt_info;

	PDEBUG("MC: mci = %p, dev = %p\n", mci, &sbridge_dev->pdev[0]->dev);

	/* Remove MC sysfs nodes */
	//edac_mc_del_mc(mci->pdev);

	PDEBUG("%s: free mci struct\n", mci->ctl_name);
	kfree(mci->ctl_name);
	edac_mc_free(mci);
	sbridge_dev->mci = NULL;
}

/****************************************************************************
			Memory check routines
 ****************************************************************************/
/*
 * Figure out how big our RAM modules are.
 *
 * The DIMMMTR register in KNL doesn't tell us the size of the DIMMs, so we
 * have to figure this out from the SAD rules, interleave lists, route tables,
 * and TAD rules.
 *
 * SAD rules can have holes in them (e.g. the 3G-4G hole), so we have to
 * inspect the TAD rules to figure out how large the SAD regions really are.
 *
 * When we know the real size of a SAD region and how many ways it's
 * interleaved, we know the individual contribution of each channel to
 * TAD is size/ways.
 *
 * Finally, we have to check whether each channel participates in each SAD
 * region.
 *
 * Fortunately, KNL only supports one DIMM per channel, so once we know how
 * much memory the channel uses, we know the DIMM is at least that large.
 * (The BIOS might possibly choose not to map all available memory, in which
 * case we will underreport the size of the DIMM.)
 *
 * In theory, we could try to determine the EDC sizes as well, but that would
 * only work in flat mode, not in cache mode.
 *
 * @mc_sizes: Output sizes of channels (must have space for KNL_MAX_CHANNELS
 *            elements)
 */
static int knl_get_dimm_capacity(struct sbridge_pvt *pvt, u64 *mc_sizes)
{
	u64 sad_base, sad_size, sad_limit = 0;
	u64 tad_base, tad_size, tad_limit, tad_deadspace, tad_livespace;
	int sad_rule = 0;
	int tad_rule = 0;
	int intrlv_ways, tad_ways;
	u32 first_pkg, pkg;
	int i;
	u64 sad_actual_size[2]; /* sad size accounting for holes, per mc */
	u32 dram_rule, interleave_reg;
	u32 mc_route_reg[KNL_MAX_CHAS];
	u32 edc_route_reg[KNL_MAX_CHAS];
	int edram_only;
	char edc_route_string[KNL_MAX_EDCS*2];
	char mc_route_string[KNL_MAX_CHANNELS*2];
	int cur_reg_start;
	int mc;
	int channel;
	int way;
	int participants[KNL_MAX_CHANNELS];
	int participant_count = 0;

	for (i = 0; i < KNL_MAX_CHANNELS; i++)
		mc_sizes[i] = 0;

	/* Read the EDC route table in each CHA. */
	cur_reg_start = 0;
	for (i = 0; i < KNL_MAX_CHAS; i++) {
		pci_read_config_dword(pvt->knl.pci_cha[i],
				KNL_EDC_ROUTE, &edc_route_reg[i]);

		if (i > 0 && edc_route_reg[i] != edc_route_reg[i-1]) {
			knl_show_edc_route(edc_route_reg[i-1],
					edc_route_string);
			if (cur_reg_start == i-1)
				PDEBUG("edc route table for CHA %d: %s\n",
					cur_reg_start, edc_route_string);
			else
				PDEBUG("edc route table for CHA %d-%d: %s\n",
					cur_reg_start, i-1, edc_route_string);
			cur_reg_start = i;
		}
	}
	knl_show_edc_route(edc_route_reg[i-1], edc_route_string);
	if (cur_reg_start == i-1)
		PDEBUG("edc route table for CHA %d: %s\n",
			cur_reg_start, edc_route_string);
	else
		PDEBUG("edc route table for CHA %d-%d: %s\n",
			cur_reg_start, i-1, edc_route_string);

	/* Read the MC route table in each CHA. */
	cur_reg_start = 0;
	for (i = 0; i < KNL_MAX_CHAS; i++) {
		pci_read_config_dword(pvt->knl.pci_cha[i],
			KNL_MC_ROUTE, &mc_route_reg[i]);

		if (i > 0 && mc_route_reg[i] != mc_route_reg[i-1]) {
			knl_show_mc_route(mc_route_reg[i-1], mc_route_string);
			if (cur_reg_start == i-1)
				PDEBUG("mc route table for CHA %d: %s\n",
					cur_reg_start, mc_route_string);
			else
				PDEBUG("mc route table for CHA %d-%d: %s\n",
					cur_reg_start, i-1, mc_route_string);
			cur_reg_start = i;
		}
	}
	knl_show_mc_route(mc_route_reg[i-1], mc_route_string);
	if (cur_reg_start == i-1)
		PDEBUG("mc route table for CHA %d: %s\n",
			cur_reg_start, mc_route_string);
	else
		PDEBUG("mc route table for CHA %d-%d: %s\n",
			cur_reg_start, i-1, mc_route_string);

	/* Process DRAM rules */
	for (sad_rule = 0; sad_rule < pvt->info.max_sad; sad_rule++) {
		/* previous limit becomes the new base */
		sad_base = sad_limit;

		pci_read_config_dword(pvt->pci_sad0,
			pvt->info.dram_rule[sad_rule], &dram_rule);

		if (!DRAM_RULE_ENABLE(dram_rule))
			break;

		edram_only = KNL_EDRAM_ONLY(dram_rule);

		sad_limit = pvt->info.sad_limit(dram_rule)+1;
		sad_size = sad_limit - sad_base;

		pci_read_config_dword(pvt->pci_sad0,
			pvt->info.interleave_list[sad_rule], &interleave_reg);

		/*
		 * Find out how many ways this dram rule is interleaved.
		 * We stop when we see the first channel again.
		 */
		first_pkg = sad_pkg(pvt->info.interleave_pkg,
						interleave_reg, 0);
		for (intrlv_ways = 1; intrlv_ways < 8; intrlv_ways++) {
			pkg = sad_pkg(pvt->info.interleave_pkg,
						interleave_reg, intrlv_ways);

			if ((pkg & 0x8) == 0) {
				/*
				 * 0 bit means memory is non-local,
				 * which KNL doesn't support
				 */
				PERR("Unexpected interleave target %d\n",
					pkg);
				return -1;
			}

			if (pkg == first_pkg)
				break;
		}
		if (KNL_MOD3(dram_rule))
			intrlv_ways *= 3;

		PINFO("dram rule %d (base 0x%llx, limit 0x%llx), %d way interleave%s\n",
			sad_rule,
			sad_base,
			sad_limit,
			intrlv_ways,
			edram_only ? ", EDRAM" : "");

		/*
		 * Find out how big the SAD region really is by iterating
		 * over TAD tables (SAD regions may contain holes).
		 * Each memory controller might have a different TAD table, so
		 * we have to look at both.
		 *
		 * Livespace is the memory that's mapped in this TAD table,
		 * deadspace is the holes (this could be the MMIO hole, or it
		 * could be memory that's mapped by the other TAD table but
		 * not this one).
		 */
		for (mc = 0; mc < 2; mc++) {
			sad_actual_size[mc] = 0;
			tad_livespace = 0;
			for (tad_rule = 0;
					tad_rule < ARRAY_SIZE(
						knl_tad_dram_limit_lo);
					tad_rule++) {
				if (knl_get_tad(pvt,
						tad_rule,
						mc,
						&tad_deadspace,
						&tad_limit,
						&tad_ways))
					break;

				tad_size = (tad_limit+1) -
					(tad_livespace + tad_deadspace);
				tad_livespace += tad_size;
				tad_base = (tad_limit+1) - tad_size;

				if (tad_base < sad_base) {
					if (tad_limit > sad_base)
						PERR("TAD region overlaps lower SAD boundary -- TAD tables may be configured incorrectly.\n");
				} else if (tad_base < sad_limit) {
					if (tad_limit+1 > sad_limit) {
						PERR("TAD region overlaps upper SAD boundary -- TAD tables may be configured incorrectly.\n");
					} else {
						/* TAD region is completely inside SAD region */
						PERR("TAD region %d 0x%llx - 0x%llx (%lld bytes) table%d\n",
							tad_rule, tad_base,
							tad_limit, tad_size,
							mc);
						sad_actual_size[mc] += tad_size;
					}
				}
				tad_base = tad_limit+1;
			}
		}

		for (mc = 0; mc < 2; mc++) {
			PINFO(" total TAD DRAM footprint in table%d : 0x%llx (%lld bytes)\n",
				mc, sad_actual_size[mc], sad_actual_size[mc]);
		}

		/* Ignore EDRAM rule */
		if (edram_only)
			continue;

		/* Figure out which channels participate in interleave. */
		for (channel = 0; channel < KNL_MAX_CHANNELS; channel++)
			participants[channel] = 0;

		/* For each channel, does at least one CHA have
		 * this channel mapped to the given target?
		 */
		for (channel = 0; channel < KNL_MAX_CHANNELS; channel++) {
			for (way = 0; way < intrlv_ways; way++) {
				int target;
				int cha;

				if (KNL_MOD3(dram_rule))
					target = way;
				else
					target = 0x7 & sad_pkg(
				pvt->info.interleave_pkg, interleave_reg, way);

				for (cha = 0; cha < KNL_MAX_CHAS; cha++) {
					if (knl_get_mc_route(target,
						mc_route_reg[cha]) == channel
						&& participants[channel]) {
						participant_count++;
						participants[channel] = 1;
						break;
					}
				}
			}
		}

		if (participant_count != intrlv_ways)
			PERR("participant_count (%d) != interleave_ways (%d): DIMM size may be incorrect\n",
				participant_count, intrlv_ways);

		for (channel = 0; channel < KNL_MAX_CHANNELS; channel++) {
			mc = knl_channel_mc(channel);
			if (participants[channel]) {
				PERR("mc channel %d contributes %lld bytes via sad entry %d\n",
					channel,
					sad_actual_size[mc]/intrlv_ways,
					sad_rule);
				mc_sizes[channel] +=
					sad_actual_size[mc]/intrlv_ways;
			}
		}
	}

	return 0;
}

/**
 * get_dimm_config - detects the presence of DIMMs in each channel and retrieves
 * the information about each DIMM.
 *
 * DIMM INFO: Total DIMM count in system, DIMM count in each channel,
 * Channel # for each DIMM, DIMM index in each channel, size of DIMM,
 * bank count, rank count, row, col, iowidth, mirror, lockstep, page mode
 **/
static int get_dimm_config(struct mem_ctl_info *mci)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	struct dimm_info *dimm;
	unsigned i, j, banks, ranks, rows, cols, npages;
	u64 size;
	u32 reg;
	enum edac_type mode;
	enum mem_type mtype;
	int channels = pvt->info.type == KNIGHTS_LANDING ?
		KNL_MAX_CHANNELS : NUM_CHANNELS;
	u64 knl_mc_sizes[KNL_MAX_CHANNELS];
//MK1101-begin
	u32 gb, mb;
//MK1101-end

	if (pvt->info.type == HASWELL || pvt->info.type == BROADWELL ||
			pvt->info.type == KNIGHTS_LANDING)
		 /* SAD_Target register is in Cbo SAD 1 */
		pci_read_config_dword(pvt->pci_sad1, SAD_TARGET, &reg);
	else
		pci_read_config_dword(pvt->pci_br0, SAD_TARGET, &reg);

	/*
	 * SourceID — SourceID of the Socket. Programmable by BIOS.
	 * By default, the value should be part of the APICID that
	 * represent the socket.
	 */
	if (pvt->info.type == KNIGHTS_LANDING)
		pvt->sbridge_dev->source_id = SOURCE_ID_KNL(reg);
	else
		pvt->sbridge_dev->source_id = SOURCE_ID(reg);

	pvt->sbridge_dev->node_id = pvt->info.get_node_id(pvt);
	PINFO("mc#%d: Node ID: %d, source ID: %d\n",
		 pvt->sbridge_dev->mc,
		 pvt->sbridge_dev->node_id,
		 pvt->sbridge_dev->source_id);

	/*
	 * KNL doesn't support mirroring or lockstep, and is always closed page
	 */
	if (pvt->info.type == KNIGHTS_LANDING) {
		mode = EDAC_S4ECD4ED;
		pvt->is_mirrored = false;

		if (knl_get_dimm_capacity(pvt, knl_mc_sizes) != 0)
			return -1;
	} else {
		pci_read_config_dword(pvt->pci_ras, RASENABLES, &reg);
		if (IS_MIRROR_ENABLED(reg)) {
			PINFO("Memory mirror is enabled\n");
			pvt->is_mirrored = true;
		} else {
			PINFO("Memory mirror is disabled\n");
			pvt->is_mirrored = false;
		}

		pci_read_config_dword(pvt->pci_ta, MCMTR, &pvt->info.mcmtr);
		if (IS_LOCKSTEP_ENABLED(pvt->info.mcmtr)) {
			PINFO("Lockstep is enabled\n");
			mode = EDAC_S8ECD8ED;
			pvt->is_lockstep = true;
		} else {
			PINFO("Lockstep is disabled\n");
			mode = EDAC_S4ECD4ED;
			pvt->is_lockstep = false;
		}
		if (IS_CLOSE_PG(pvt->info.mcmtr)) {
			PINFO("address map is on closed page mode\n");
			pvt->is_close_pg = true;
		} else {
			PINFO("address map is on open page mode\n");
			pvt->is_close_pg = false;
		}
	}

	mtype = pvt->info.get_memory_type(pvt);
	if (mtype == MEM_RDDR3 || mtype == MEM_RDDR4)
		PINFO("Memory is registered\n");
	else if (mtype == MEM_UNKNOWN)
		PINFO("Cannot determine memory type\n");
	else
		PINFO("Memory is unregistered\n");

	/* Hard coded but technically it can be figured out */
	if (mtype == MEM_DDR4 || mtype == MEM_RDDR4)
		banks = 16;
	else
		banks = 8;

	/*
	 * # of channels was determined in the beginning of this routine. It
	 * includes the channel count for all processors (nodes).
	 */
	for (i = 0; i < channels; i++) {

		u32 mtr;
		int max_dimms_per_channel;

		if (pvt->info.type == KNIGHTS_LANDING) {
			max_dimms_per_channel = 1;
			if (!pvt->knl.pci_channel[i])
				continue;
		} else {
			/*
			 * Each channel has more than one DIMM slot. Each dimmmtr_j
			 * register, where j is 0-based DIMM index, represents a DIMM slot
			 * in a channel.
			 */
			max_dimms_per_channel = ARRAY_SIZE(mtr_regs);

			/* If this channel doesn't exist, move on to the next one */
//MK1101			if (!pvt->pci_tad[i])
//MK1101				continue;
//MK1101-begin
			if (!pvt->pci_tad[i]) {
				continue;
			}
			pci_read_config_dword(pvt->pci_tad[i], 0, &reg);
			PINFO("Channel %d - DID:VID = 0x%.8x\n", i, reg);
//MK1101-end
		}

		/* For each DIMM in the current channel */
		for (j = 0; j < max_dimms_per_channel; j++) {
			dimm = EDAC_DIMM_PTR(mci->layers, mci->dimms, mci->n_layers,
				       i, j, 0);

			/* Read dimmmtr_j reg for the current DIMM slot */
			if (pvt->info.type == KNIGHTS_LANDING) {
				pci_read_config_dword(pvt->knl.pci_channel[i], knl_mtr_reg, &mtr);
			} else {
				/* dimmmtr_j in 1/19,22/2,3,4,5 */
				pci_read_config_dword(pvt->pci_tad[i], mtr_regs[j], &mtr);
			}
			PINFO("Channel #%d  dimmmtr_%d = 0x%.8x\n", i, j, mtr);
			if (IS_DIMM_PRESENT(mtr)) {
				pvt->channel[i].dimms++;
//MK1101-begin
				/* Total # of DIMMs in system */
				hd_desc[0].sys_dimm_count++;

				/* DIMM count in the current channel */
				hd_desc[0].channel[i].ch_dimms++;

				/* ch_dimm_mask indicates the presence of DIMMs in its channel */
				hd_desc[0].channel[i].ch_dimm_mask |= (unsigned char)(1 << j);

				/* Will be used to access JEDEC defined SPD in each DIMM */
				hd_desc[0].channel[i].dimm[j].spd_dimm_id = spd_dimm_id_table[i][j];

				/* This info indicates the presence of DIMMs based on spd_dimm_id */
				hd_desc[0].spd_dimm_mask |= (unsigned int)(1 << hd_desc[0].channel[i].dimm[j].spd_dimm_id);
//MK1101-end

//MK0412-begin
				/* Total number of HybriDIMMs in system */
				if ( check_hdimm(hd_desc[0].channel[i].dimm[j].spd_dimm_id) == 0 ) {
///					hd_desc[0].channel[i].dimm[j].spd_dimm_id = 1;
//MK0508-begin
					hd_desc[0].sys_hdimm_mask |= (unsigned int)(1 << hd_desc[0].channel[i].dimm[j].spd_dimm_id);
//MK0508-end
					hd_desc[0].sys_hdimm_count++;
				}
//MK0412-end

				ranks = numrank(pvt->info.type, mtr);

				if (pvt->info.type == KNIGHTS_LANDING) {
					/* For DDR4, this is fixed. */
					cols = 1 << 10;
					rows = knl_mc_sizes[i] /
						((u64) cols * ranks * banks * 8);
				} else {
					rows = numrow(mtr);
					cols = numcol(mtr);
				}

				/* Calculate size in MB */
				size = ((u64)rows * cols * banks * ranks) >> (20 - 3);
				npages = MiB_TO_PAGES(size);

//MK1101-begin
				gb = div_u64_rem(size, 1024,&mb);

				/* Is CPU source ID == node number??? */
				hd_desc[0].channel[i].dimm[j].node_num = 0;
				hd_desc[0].channel[i].dimm[j].chan_num = i;
				hd_desc[0].channel[i].dimm[j].slot_num = j;
				hd_desc[0].channel[i].dimm[j].banks = banks;
				hd_desc[0].channel[i].dimm[j].ranks = ranks;
				hd_desc[0].channel[i].dimm[j].row = rows;
				hd_desc[0].channel[i].dimm[j].col = cols;
				hd_desc[0].channel[i].dimm[j].dram_size = gb;
				hd_desc[0].channel[i].dimm[j].iowidth = (unsigned char)pvt->info.get_width(pvt, mtr);
//MK1101-end

//MK1101				PINFO("mc#%d: ha %d channel %d, dimm %d, %lld MB (%d pages) "
//MK1101						"bank: %d, rank: %d, row: %#x, col: %#x\n",
//MK1101						pvt->sbridge_dev->mc, i/4, i%4, j, size, npages,
//MK1101						banks, ranks, rows, cols);
//MK1101-begin
				PINFO("mc#%d: ha %d channel %d, dimm %d, %lld MB (%d pages) "
						"bank: %d, rank: %d, row: %#x, col: %#x, iowidth: %d\n",
						pvt->sbridge_dev->mc, i/4, i%4, j, size, npages,
						banks, ranks, rows, cols, pvt->info.get_width(pvt, mtr));
//MK1101-end

				dimm->nr_pages = npages;
				dimm->grain = 32;
				dimm->dtype = pvt->info.get_width(pvt, mtr);
				dimm->mtype = mtype;
				dimm->edac_mode = mode;
				snprintf(dimm->label, sizeof(dimm->label),
					 "CPU_SrcID#%u_Ha#%u_Chan#%u_DIMM#%u",
					 pvt->sbridge_dev->source_id, i/4, i%4, j);
				PINFO("%s\n", dimm->label);
			}
		} // end of for (j = 0;...
//MK1101-begin
		PINFO("DIMM count in Channel #%d = %d\n", i, pvt->channel[i].dimms);
//MK1101-end
	} // end of for (i=0;....

//MK1101-begin
	PINFO("Total DIMM count in system = %d\n", hd_desc[0].sys_hdimm_count);
//MK1101-end
	return 0;
}

/**
 * get_memory_layout -
 *
 * SYSTEM MEMORY INFO: TOLM, TOHM,
 **/
static void get_memory_layout(const struct mem_ctl_info *mci)
{
	struct sbridge_pvt *pvt = mci->pvt_info;
	int i, j, k, n_sads, n_tads, sad_interl;
	u32 reg;
	u64 limit, prv = 0;
	u64 tmp_mb;
	u32 gb, mb;
	u32 rir_way;
	u64 tmp_limit = 0;
	u64 prev_tmp_limit = 0;

	/*
	 * Step 1) Get TOLM/TOHM ranges
	 */
	pvt->tolm = pvt->info.get_tolm(pvt);
	tmp_mb = (1 + pvt->tolm) >> 20;

	gb = div_u64_rem(tmp_mb, 1024, &mb);
	PDEBUG("TOLM: %u.%03u GB (0x%016Lx)\n",	gb, (mb*1000)/1024, (u64)pvt->tolm);
//MK1101-begin
	hd_desc[0].tolm_addr = (u64)pvt->tolm;
//MK1101-end

//MK1101-begin
	/* Calculate size of reserved memory */
	tmp_mb = (TORM - (1 + pvt->tolm)) >> 20;
	gb = div_u64_rem(tmp_mb, 1024, &mb);
	PDEBUG("Reserved memory size: %u.%03u GB (0x%016Lx ~ 0x%016Lx)\n",
			gb, (mb*1000)/1024, (1 + pvt->tolm), (u64)(TORM-1));
	hd_desc[0].sys_rsvd_mem_size = gb;
//MK1101-end

	/* Grab the TOLM for system mmio calculation */
	tolm_upper_gb = gb * 0x40000000;	// * 1 GB

	/* Address range is already 45:25 */
	pvt->tohm = pvt->info.get_tohm(pvt);
	tmp_mb = (1 + pvt->tohm) >> 20;

	gb = div_u64_rem(tmp_mb, 1024, &mb);
	PDEBUG("TOHM: %u.%03u GB (0x%016Lx)\n",	gb, (mb*1000)/1024, (u64)pvt->tohm);
//MK1101-begin
	hd_desc[0].tohm_addr = (u64)pvt->tohm;
	hd_desc[0].sys_mem_size = gb - hd_desc[0].sys_rsvd_mem_size;
//MK1101-end

	/*
	 * Step 2) Get SAD range and SAD Interleave list
	 * TAD registers contain the interleave wayness. However, it
	 * seems simpler to just discover it indirectly, with the
	 * algorithm bellow.
	 */
	prv = 0;
	for (n_sads = 0; n_sads < pvt->info.max_sad; n_sads++) {
		/* SAD_LIMIT Address range is 45:26 */
		pci_read_config_dword(pvt->pci_sad0, pvt->info.dram_rule[n_sads], &reg);
		limit = pvt->info.sad_limit(reg);

		if (!DRAM_RULE_ENABLE(reg))
			continue;

		if (limit <= prv)
			break;

		tmp_mb = (limit + 1) >> 20;
		gb = div_u64_rem(tmp_mb, 1024, &mb);
//MK1101		PINFO("SAD#%d %s up to %u.%03u GB (0x%016Lx) Interleave: %s reg=0x%08x\n",
//MK1101			 n_sads,
//MK1101			 show_dram_attr(pvt->info.dram_attr(reg)),
//MK1101			 gb, (mb*1000)/1024,
//MK1101			 ((u64)tmp_mb) << 20L,
//MK1101			 pvt->info.show_interleave_mode(reg),
//MK1101			 reg);
//MK1101-begin
		hd_desc[0].a7_mode = A7MODE(reg);
		PINFO("SAD#%d %s up to %u.%03u GB (0x%016Lx) Interleave: %s reg=0x%08x "
				"a7mode= %d\n",
				n_sads,
				show_dram_attr(pvt->info.dram_attr(reg)),
				gb, (mb*1000)/1024,
				((u64)tmp_mb) << 20L,
				pvt->info.show_interleave_mode(reg),
				reg, hd_desc[0].a7_mode);
//MK1101-end
		prv = limit;

		pci_read_config_dword(pvt->pci_sad0, pvt->info.interleave_list[n_sads],
				      &reg);
		sad_interl = sad_pkg(pvt->info.interleave_pkg, reg, 0);
		for (j = 0; j < 8; j++) {
			u32 pkg = sad_pkg(pvt->info.interleave_pkg, reg, j);
			if (j > 0 && sad_interl == pkg)
				break;

			PINFO("SAD#%d, interleave #%d: %d\n",
				 n_sads, j, pkg);
		}
	}

	//if (pvt->info.type == KNIGHTS_LANDING)
	//	return;

	/*
	 * Step 3) Get TAD range
	 */
	 PINFO("MAX_TAD=%lu\n", MAX_TAD);
	for (n_tads = 0; n_tads < MAX_TAD; n_tads++) {
//		 PINFO("pvt->pci_ha0 = %.8X\n", pvt->pci_ha0);	//MK debug
		pci_read_config_dword(pvt->pci_ha0, tad_dram_rule[n_tads], &reg);
//MK1101-begin
		hd_desc[0].chan_way = (unsigned char)(1 + (u32)TAD_CH(reg));
//MK1101-end
		limit = TAD_LIMIT(reg);
		if (limit <= prv) {
            PINFO("limit<= prv, limit = %llu, prv = %llu\n", limit, prv);
			//break;
        }
		tmp_mb = (limit + 1) >> 20;
		tmp_limit = tmp_mb << 20L;
		PDEBUG("tmp_limit=%llx\n", tmp_limit);

		gb = div_u64_rem(tmp_mb, 1024, &mb);
		PINFO("TAD#%d: up to %u.%03u GB (0x%016Lx), sck_way=%d, ch_way=%d, TGT: %d, %d, %d, %d, reg=0x%08x\n",
			 n_tads, gb, (mb*1000)/1024,
			 ((u64)tmp_mb) << 20L,
			 1 << (u32)TAD_SOCK(reg),	/* shift 1 from the formula in dimmcfg */
			 1 + (u32)TAD_CH(reg),		/* plus 1 from the formula in dimmcfg */
			 (u32)TAD_TGT0(reg),
			 (u32)TAD_TGT1(reg),
			 (u32)TAD_TGT2(reg),
			 (u32)TAD_TGT3(reg),
			 reg);
		prv = limit;

#if 0	//MK0414 - removed b/c I don't this code works.
		/* Check if the discovered hvdimm is in between TAD# and get the interleave mode */
        if (system_has_hv_only) {
            PDEBUG("%s - hv found in e820, now check if addr in between TADs\n", __func__);
            PDEBUG("hvdimm_discovered[0].mem_start=%llx", hvdimm_discovered[0].mem_start);
            PDEBUG("tmp_limit=%llx\n", tmp_limit);
            PDEBUG("prev_tmp_limit=%llx\n", prev_tmp_limit);

            /* Logic <= is to make sure if hv starts at 0, then it will step into the if/then below */
            if ((hvdimm_discovered[0].mem_start >= prev_tmp_limit ) && (hvdimm_discovered[0].mem_start <= tmp_limit)) {
            	PDEBUG("HV found here in between prev_tmp_mb=%llx and %llx\n", prev_tmp_limit, tmp_limit);
                hv_ch_way = (u8)(1 + (u32)TAD_CH(reg));
                PDEBUG("hv ch_way=%x\n", hv_ch_way);

                /* Check what node the hv is installed and save the info for sysfs */
                PDEBUG("NODE: %d\n", pvt->sbridge_dev->node_id);
                if (pvt->sbridge_dev->node_id == 0)
                    node0_ch_way = (uint)hv_ch_way;
                else if (pvt->sbridge_dev->node_id == 1)
                    node1_ch_way = (uint)hv_ch_way;
                else
                    PERR("ERROR: More than 2 nodes are not supported in this version\n");
            }
        }
        else
        	PDEBUG("%s - no HV found in this TAD#s range\n", __func__);
#endif	//MK0414

		prev_tmp_limit = tmp_limit;
	}

	/*
	 * Step 4) Get TAD offsets, per each channel
	 */
	for (i = 0; i < NUM_CHANNELS; i++) {
		if (!pvt->channel[i].dimms)
			continue;
		for (j = 0; j < n_tads; j++) {
			pci_read_config_dword(pvt->pci_tad[i], tad_ch_nilv_offset[j], &reg);
			tmp_mb = TAD_OFFSET(reg) >> 20;
			gb = div_u64_rem(tmp_mb, 1024, &mb);
			PINFO("TAD CH#%d, offset #%d: %u.%03u GB (0x%016Lx), reg=0x%08x\n",
				 i, j,
				 gb, (mb*1000)/1024,
				 ((u64)tmp_mb) << 20L,
				 reg);
		}
	}

	/*
	 * Step 6) Get RIR Wayness/Limit, per each channel
	 */
	for (i = 0; i < NUM_CHANNELS; i++) {
		if (!pvt->channel[i].dimms)
			continue;
		for (j = 0; j < MAX_RIR_RANGES; j++) {
			pci_read_config_dword(pvt->pci_tad[i], rir_way_limit[j], &reg);

			if (!IS_RIR_VALID(reg))
				continue;

			tmp_mb = pvt->info.rir_limit(reg) >> 20;
			rir_way = 1 << RIR_WAY(reg);
			gb = div_u64_rem(tmp_mb, 1024, &mb);
			PDEBUG("CH#%d RIR#%d, limit: %u.%03u GB (0x%016Lx), way: %d, reg=0x%08x\n",
				 i, j,
				 gb, (mb*1000)/1024,
				 ((u64)tmp_mb) << 20L,
				 rir_way,
				 reg);

			for (k = 0; k < rir_way; k++) {
				pci_read_config_dword(pvt->pci_tad[i], rir_offset[j][k], &reg);
				tmp_mb = RIR_OFFSET(reg) << 6;

				gb = div_u64_rem(tmp_mb, 1024, &mb);
				PDEBUG("CH#%d RIR#%d INTL#%d, offset %u.%03u GB (0x%016Lx), tgt: %d, reg=0x%08x\n",
					 i, j, k,
					 gb, (mb*1000)/1024,
					 ((u64)tmp_mb) << 20L,
					 (u32)RIR_RNK_TGT(reg),
					 reg);
			}
		}
	}
}

#if 0  /* Not needed, but left here if ECC needs to be checked */
static struct pci_dev *get_pdev_same_bus(u8 bus, u32 id)
{
	struct pci_dev *pdev = NULL;

	do {
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL, id, pdev);
		if (pdev && pdev->bus->number == bus)
			break;
	} while (pdev);

	return pdev;
}


/**
 * check_if_ecc_is_active() - Checks if ECC is active
 * @bus:	Device bus
 * @type:	Memory controller type
 * returns: 0 in case ECC is active, -ENODEV if it can't be determined or
 *	    disabled
 */
static int check_if_ecc_is_active(const u8 bus, enum type type)
{
	struct pci_dev *pdev = NULL;
	u32 mcmtr, id;

	switch (type) {
	case IVY_BRIDGE:
		id = PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TA;
		break;
	case HASWELL:
		id = PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0_TA;
		break;
	case SANDY_BRIDGE:
		id = PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_TA;
		break;
	case BROADWELL:
		id = PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0_TA;
		break;
	case KNIGHTS_LANDING:
		/*
		 * KNL doesn't group things by bus the same way
		 * SB/IB/Haswell does.
		 */
		id = PCI_DEVICE_ID_INTEL_KNL_IMC_TA;
		break;
	default:
		return -ENODEV;
	}

	if (type != KNIGHTS_LANDING)
		pdev = get_pdev_same_bus(bus, id);
	else
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL, id, 0);

	if (!pdev) {
		PERR("Couldn't find PCI device "
					"%04x:%04x! on bus %02d\n",
					PCI_VENDOR_ID_INTEL, id, bus);
		return -ENODEV;
	}

	pci_read_config_dword(pdev,
			type == KNIGHTS_LANDING ? KNL_MCMTR : MCMTR, &mcmtr);
	if (!IS_ECC_ENABLED(mcmtr)) {
		PERR("ECC is disabled. Aborting\n");
		return -ENODEV;
	}
	return 0;
}
#endif // #if 0

/* Note: mc# is basically node #. If dual CPU, */
/* then mc#0 is MC for cpu1 and mc#1 is for cpu0 */
static int
sbridge_register_mci(struct sbridge_dev *sbridge_dev, enum type type)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	struct sbridge_pvt *pvt;
	struct pci_dev *pdev = sbridge_dev->pdev[0];
	int rc = 0;

/* Not checking ECC for errors. This module is only for discovery purpose. */
#if 0
	/* Check the number of active and not disabled channels */
	rc = check_if_ecc_is_active(sbridge_dev->bus, type);
	if (unlikely(rc < 0)) {
		PERR("ECC IS DISABLED !!!\n");
		return rc;
	}
#endif

	/* allocate a new MC control structure */
	layers[0].type = EDAC_MC_LAYER_CHANNEL;
	layers[0].size = type == KNIGHTS_LANDING ?
		KNL_MAX_CHANNELS : NUM_CHANNELS;
	layers[0].is_virt_csrow = false;
	layers[1].type = EDAC_MC_LAYER_SLOT;
	layers[1].size = type == KNIGHTS_LANDING ? 1 : MAX_DIMMS;
	layers[1].is_virt_csrow = true;
	mci = edac_mc_alloc(sbridge_dev->mc, ARRAY_SIZE(layers), layers,
			    sizeof(*pvt));

	if (unlikely(!mci))
		return -ENOMEM;

	PDEBUG("MC: mci = %p, dev = %p\n", mci, &pdev->dev);

	pvt = mci->pvt_info;
	memset(pvt, 0, sizeof(*pvt));

	/* Associate sbridge_dev and mci for future usage */
	pvt->sbridge_dev = sbridge_dev;
	sbridge_dev->mci = mci;

	mci->mtype_cap = type == KNIGHTS_LANDING ?
		MEM_FLAG_DDR4 : MEM_FLAG_DDR3;
	mci->edac_ctl_cap = EDAC_FLAG_NONE;
	mci->edac_cap = EDAC_FLAG_NONE;
	mci->mod_name = "sbridge_edac.c";
	mci->mod_ver = SBRIDGE_REVISION;
	mci->dev_name = pci_name(pdev);
	mci->ctl_page_to_phys = NULL;

	/* Set the function pointer to an actual operation function */
	//mci->edac_check = sbridge_check_error;

	pvt->info.type = type;
	switch (type) {
	case IVY_BRIDGE:
		/* NOTE: We don't support ivy bridge */
		PERR("No support for Ivy Bridge");
		goto fail0;
		break;
	case SANDY_BRIDGE:
		pvt->info.rankcfgr = SB_RANK_CFG_A;
		pvt->info.get_tolm = sbridge_get_tolm;
		pvt->info.get_tohm = sbridge_get_tohm;
		pvt->info.dram_rule = sbridge_dram_rule;
		pvt->info.get_memory_type = get_memory_type;
		pvt->info.get_node_id = get_node_id;
		pvt->info.rir_limit = rir_limit;
		pvt->info.sad_limit = sad_limit;
		pvt->info.interleave_mode = interleave_mode;
		pvt->info.show_interleave_mode = show_interleave_mode;
		pvt->info.dram_attr = dram_attr;
		pvt->info.max_sad = ARRAY_SIZE(sbridge_dram_rule);
		pvt->info.interleave_list = sbridge_interleave_list;
		pvt->info.max_interleave = ARRAY_SIZE(sbridge_interleave_list);
		pvt->info.interleave_pkg = sbridge_interleave_pkg;
		pvt->info.get_width = sbridge_get_width;
		mci->ctl_name = kasprintf(GFP_KERNEL, "Sandy Bridge Socket#%d", mci->mc_idx);

		/* Store pci devices at mci for faster access */
		rc = sbridge_mci_bind_devs(mci, sbridge_dev);
		if (unlikely(rc < 0))
			goto fail0;
		break;
	case HASWELL:
		/* rankcfgr isn't used */
		pvt->info.get_tolm = haswell_get_tolm;
		pvt->info.get_tohm = haswell_get_tohm;
		pvt->info.dram_rule = ibridge_dram_rule;
		pvt->info.get_memory_type = haswell_get_memory_type;
		pvt->info.get_node_id = haswell_get_node_id;
		pvt->info.rir_limit = haswell_rir_limit;
		pvt->info.sad_limit = sad_limit;
		pvt->info.interleave_mode = interleave_mode;
		pvt->info.show_interleave_mode = show_interleave_mode;
		pvt->info.dram_attr = dram_attr;
		pvt->info.max_sad = ARRAY_SIZE(ibridge_dram_rule);
		pvt->info.interleave_list = ibridge_interleave_list;
		pvt->info.max_interleave = ARRAY_SIZE(ibridge_interleave_list);
		pvt->info.interleave_pkg = ibridge_interleave_pkg;
		pvt->info.get_width = ibridge_get_width;
		mci->ctl_name = kasprintf(GFP_KERNEL, "Haswell Socket#%d", mci->mc_idx);

		/* Store pci devices at mci for faster access */
		rc = haswell_mci_bind_devs(mci, sbridge_dev);
		if (unlikely(rc < 0))
			goto fail0;
		PDEBUG("%s", mci->ctl_name);
		break;
	case BROADWELL:
		/* rankcfgr isn't used */
		pvt->info.get_tolm = haswell_get_tolm;
		pvt->info.get_tohm = haswell_get_tohm;
		pvt->info.dram_rule = ibridge_dram_rule;
		pvt->info.get_memory_type = haswell_get_memory_type;
		pvt->info.get_node_id = haswell_get_node_id;
		pvt->info.rir_limit = haswell_rir_limit;
		pvt->info.sad_limit = sad_limit;
		pvt->info.interleave_mode = interleave_mode;
		pvt->info.show_interleave_mode = show_interleave_mode;
		pvt->info.dram_attr = dram_attr;
		pvt->info.max_sad = ARRAY_SIZE(ibridge_dram_rule);
		pvt->info.interleave_list = ibridge_interleave_list;
		pvt->info.max_interleave = ARRAY_SIZE(ibridge_interleave_list);
		pvt->info.interleave_pkg = ibridge_interleave_pkg;
		pvt->info.get_width = broadwell_get_width;
		mci->ctl_name = kasprintf(GFP_KERNEL, "Broadwell Socket#%d", mci->mc_idx);

		/* Store pci devices at mci for faster access */
		rc = broadwell_mci_bind_devs(mci, sbridge_dev);
		if (unlikely(rc < 0))
			goto fail0;

		PDEBUG("%s", mci->ctl_name);
		break;
	case KNIGHTS_LANDING:
		/* pvt->info.rankcfgr == ??? */
		pvt->info.get_tolm = knl_get_tolm;
		pvt->info.get_tohm = knl_get_tohm;
		pvt->info.dram_rule = knl_dram_rule;
		pvt->info.get_memory_type = knl_get_memory_type;
		pvt->info.get_node_id = knl_get_node_id;
		pvt->info.rir_limit = NULL;
		pvt->info.sad_limit = knl_sad_limit;
		pvt->info.interleave_mode = knl_interleave_mode;
		pvt->info.show_interleave_mode = knl_show_interleave_mode;
		pvt->info.dram_attr = dram_attr_knl;
		pvt->info.max_sad = ARRAY_SIZE(knl_dram_rule);
		pvt->info.interleave_list = knl_interleave_list;
		pvt->info.max_interleave = ARRAY_SIZE(knl_interleave_list);
		pvt->info.interleave_pkg = ibridge_interleave_pkg;
		pvt->info.get_width = knl_get_width;
		mci->ctl_name = kasprintf(GFP_KERNEL,
			"Knights Landing Socket#%d", mci->mc_idx);

		rc = knl_mci_bind_devs(mci, sbridge_dev);
		if (unlikely(rc < 0))
			goto fail0;
		PDEBUG("%s", mci->ctl_name);
		break;
	}

	/* Get dimm basic config and the memory layout */
	get_dimm_config(mci);

	get_memory_layout(mci);

	/* record ptr to the generic device */
	mci->pdev = &pdev->dev;

	/* add this new MC control structure to EDAC's list of MCs - */
	/* NO NEED for discovery */
	//if (unlikely(edac_mc_add_mc(mci))) {
	//	PDEBUG("MC: failed edac_mc_add_mc()\n");
	//	rc = -EINVAL;
	//	goto fail0;
	//}

	return 0;

fail0:
	kfree(mci->ctl_name);
	edac_mc_free(mci);
	sbridge_dev->mci = NULL;
	return rc;
}

static int hv_discovery_probe(
	struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct pci_private *priv;
	int rc = -ENODEV;
	u8 mc, num_mc = 0;
	struct sbridge_dev *sbridge_dev;
	enum type type = SANDY_BRIDGE;

	PDEBUG("In pci probe() function\n");

//MK1101-begin
	/* Clear memory info */
	memset(hd_desc, 0, sizeof(hd_desc));
//MK1101-end

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {

		dev_err(&pdev->dev, "Unable to allocate memory for pci private\n");
		return -ENOMEM;
	}

	priv->pdev = pdev;
	pci_set_drvdata(pdev, priv);

	spin_lock_init(&priv->hv_discovery_spinlock);

	/* sbac probe starts here......... */

	/* get the pci devices we want to reserve for our use */
	mutex_lock(&sbridge_edac_lock);

	//PERR("pdev->device=%x\n", (pdev->device));

	/*
	 * All memory controllers are allocated at the first pass.
	*/
	if (unlikely(probed >= 1)) {
		PDEBUG("Already probed - exit probe function");
		mutex_unlock(&sbridge_edac_lock);
		return -ENODEV;
	}

	probed++;

	/* Find installed modules, either hv only or mixed with LRDIMM */
	find_installed_memory_types();

	//PERR("pdev->device=%x\n", (pdev->device));

	switch (pdev->device) {
		case PCI_DEVICE_ID_INTEL_IBRIDGE_IMC_HA0_TA:
			rc = sbridge_get_all_devices(&num_mc,
						pci_dev_descr_ibridge_table);
			type = IVY_BRIDGE;
			break;
		case PCI_DEVICE_ID_INTEL_SBRIDGE_IMC_HA0:
			rc = sbridge_get_all_devices(&num_mc,
						pci_dev_descr_sbridge_table);
			type = SANDY_BRIDGE;
			break;
		case PCI_DEVICE_ID_INTEL_HASWELL_IMC_HA0:
			rc = sbridge_get_all_devices(&num_mc,
						pci_dev_descr_haswell_table);
			type = HASWELL;
			break;
		case PCI_DEVICE_ID_INTEL_BROADWELL_IMC_HA0:
			rc = sbridge_get_all_devices(&num_mc,
						pci_dev_descr_broadwell_table);
			type = BROADWELL;
		    break;
		case PCI_DEVICE_ID_INTEL_KNL_IMC_SAD0:
			rc = sbridge_get_all_devices_knl(&num_mc,
						pci_dev_descr_knl_table);
			type = KNIGHTS_LANDING;
			break;
		default:
			PERR("MEMORY CONTROLLER TYPE IS NOT SUPPORTED");
			break;
	}

	if (unlikely(rc < 0)) {
		PERR("couldn't get all devices for 0x%x\n", pdev->device);
		goto fail0;
	}
	else {
		PINFO("Discovered architecture = %s", type_name[type]);
	}

	mc = 0;

	list_for_each_entry(sbridge_dev, &sbridge_edac_list, list) {
		PINFO("Registering MC#%d (%d of %d)\n",
			 mc, mc + 1, num_mc);

		sbridge_dev->mc = mc++;
		rc = sbridge_register_mci(sbridge_dev, type);
		if (unlikely(rc < 0)) {
			PERR("sbridge_register_mci FAILED\n");
			goto fail1;
		}
	}

	/* Run main algorithm to discover hvdimms - get_hvdimmm_config and layout inside func below */
	/* at this moment, we already know the ch_way and e820 info. If no hv, then already exit at e820 */
	discover_hvdimm();

    /* Populate hv bsm/mmls data structure to use */
    populate_hvdata();

//MK0417-begin
	PDEBUG(">> testing the following calls <<\n");
	checkPopDimm();
	get_memory_layout2();
//MK0417-end
	mutex_unlock(&sbridge_edac_lock);

	return 0;

fail1:
	list_for_each_entry(sbridge_dev, &sbridge_edac_list, list)
		sbridge_unregister_mci(sbridge_dev);

	sbridge_put_all_devices();
fail0:
		mutex_unlock(&sbridge_edac_lock);
		return rc;
}

static void hv_discovery_remove(struct pci_dev *pdev)
{
	struct pci_private *priv = pci_get_drvdata(pdev);
	PDEBUG("In pci remove() function\n");

	mutex_lock(&sbridge_edac_lock);

	if (unlikely(!probed)) {
		mutex_unlock(&sbridge_edac_lock);
		return;
	}

	sbridge_put_all_devices();

	kfree(priv);
	probed--;

	mutex_unlock(&sbridge_edac_lock);

}

static struct pci_driver hv_discovery_pci_driver ={
	.name		= DRIVER_NAME,
	.probe		= hv_discovery_probe,
	.remove		= hv_discovery_remove,
	.id_table	= sbridge_pci_tbl,
};

static int __init hv_discovery_init(void)
{
	int res;

	res = pci_register_driver(&hv_discovery_pci_driver);
	if(res<0) {
		PERR("Adding driver to pci core failed\n");
		return res;
	}

	PINFO("INIT successful\n");
	PINFO("note: If probe() is not called, then mem controller is not supported\n");

	return 0;
}

static void __exit hv_discovery_exit(void)
{
	PINFO("EXIT\n");

	pci_unregister_driver(&hv_discovery_pci_driver);
}

module_init(hv_discovery_init);
module_exit(hv_discovery_exit);

module_param(num_hv, uint, S_IRUGO);
module_param(num_nodes, uint, S_IRUGO);
module_param(node0_ch_way, uint, S_IRUGO);
module_param(node1_ch_way, uint, S_IRUGO);
module_param(hv_start_addr0, ulong, S_IRUGO);
module_param(hv_start_addr1, ulong, S_IRUGO);
module_param(hv_mmio_start0, ulong, S_IRUGO);
module_param(hv_mmio_start1, ulong, S_IRUGO);
module_param(hv_mem_size0, ulong, S_IRUGO);
module_param(hv_mem_size1, ulong, S_IRUGO);
module_param(hv_mmio_size0, ulong, S_IRUGO);
module_param(hv_mmio_size1, ulong, S_IRUGO);

EXPORT_SYMBOL(num_hv);              /* number of hvdimms installed */
EXPORT_SYMBOL(num_nodes);           /* number of nodes (cpu) used */
EXPORT_SYMBOL(node0_ch_way);        /* ch_way interleave mode of node 0 */
EXPORT_SYMBOL(node1_ch_way);        /* ch_way interleave mode of node 1 */
EXPORT_SYMBOL(hv_start_addr0);      /* hv start address at node 0 */
EXPORT_SYMBOL(hv_start_addr1);      /* hv start address at node 1 */
EXPORT_SYMBOL(hv_mmio_start0);      /* mmio start address at node 0 */
EXPORT_SYMBOL(hv_mmio_start1);      /* mmio start address at node 1 */
EXPORT_SYMBOL(hv_mem_size0);        /* hv ddr mem size at node 0 */
EXPORT_SYMBOL(hv_mem_size1);        /* hv ddr mem size at node 1 */
EXPORT_SYMBOL(hv_mmio_size0);       /* mmio size at node 0 */
EXPORT_SYMBOL(hv_mmio_size1);       /* mmio size at node 1 */
