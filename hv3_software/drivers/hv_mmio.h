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
#ifndef _HV_MULTI_H_
#define _HV_MULTI_H_

//MK0105-begin
/*
 * When set to 1 - enable BCOM before fake-write & disable after fake-write
 * operation. Will not enable BCOM when loading the command driver.
 * When set to 0 - command driver will not control BCOM at all. It should be
 * enabled by another SW before loading the driver.
 */
#define	DEBUG_FEAT_BCOM_ONOFF				0
//MK0105-end

//MK0202-begin
/*
 * Will not send a query command and check query status during BSM_WRITE
 * when set to 1
 */
#define DEBUG_FEAT_SKIP_QUERY_BSM_WRITE		0
//MK0202-end

//MK-begin
/*temporarily brought these switches here from hv_cmd.c */
/* comment out these lines on target HW */
//#define SW_SIM
//#define STS_TST
//MK-end

/* enable hv_mcpy() logging, default disabled */
#define MMIO_LOGGING	//MK

/* REV B board support */
#define REV_B_MM
// #define MMLS_16K_ALIGNMENT

/* use code that supports MULTI DIMM, default disabled for bring-up */
/* single dimm code would be deprecated after multi dimm code verification */
// #define MULTI_DIMM

/* like MMLS, interleave BSM data to multiple DIMM, default enabled */
#define INTERLEAVE_BSM

/* RDIMM populated together w HVDIMM, default disabled */
// Better to have a logic to check if the address is in HVDIMM or not
// #define RDIMM_POPULATED

/* no cache flush period for accessing command/data/status operation, default disabled */
// #define NO_CACHE_FLUSH_PERIOD

/* HVDIMM HW suspports 1KB block */
#define MIN_1KBLOCK_SUPPORT

#define MAX_HV_GROUP		1	/* one BSM and MMLS storage only */
//MK#define MAX_HV_INTV		2	/* for CPU0 and CPU1 */
//MK-begin
#define MAX_HV_INTV		1	/* CPU0 only for development */
//MK-end
#define MAX_HV_DIMM		8	/* 4 HVDIMM on each CPU */

#define GROUP_BSM		1
#define GROUP_MMLS		2

#define DEV_BLOCK		1
#define	DEV_CHAR		2

#define HV_WRITE		1
#define	HV_READ			2

#define MEM_BURST_SIZE		64
#define HV_BLOCK_SIZE		512	// should be renamed to HV_SECTOR_SIZE
//MK#define FS_BLOCK_SIZE		4096	// real block size of HVDIMM
//MK-begin
#ifdef SW_SIM
#define FS_BLOCK_SIZE		512
#else
#define FS_BLOCK_SIZE		4096	// real block size of HVDIMM
#endif
//MK-end

#define INVALID_GROUP		0xff

#define	CMD_BUFFER_SIZE		64
#define	STATUS_BUFFER_SIZE	64
#define DATA_BUFFER_SIZE	4096
#define DATA_BUFFER_NUM		4
//MK-begin
#define ECC_TABLE_ENTRY_SIZE	64	// 64 bytes per entry
#define ECC_TABLE_TOTAL_ENTRY	256	// 256 entries
//MK-end


/*
 * HV Description Table
 *
 * Each entry on this table carries description of a HVDIMM modulue
 * For now eMMC capacity is hardcoded to be 256G and DRAM 16G
 * The usage of slot info is not decided yet. It's expected the info
 * can be used to decide processor locality in NUMA 
 */
struct hv_description_tbl {
	unsigned int hid;
	unsigned int emmc_cap;		/* in GB */
	unsigned int dram_cap;		/* in GB */
	unsigned int slot;		
};

//MK1104-begin
#define SPD_ID			0x0A
#define TSOD_ID			0x03		// For DRAM temperature info
#define FPGA1_ID		0x05
#define FPGA2_ID		0x07
#define PAGE_ID			0x06
//MK1104-end

//MK1101-begin
#define MAX_NPS			1			// Max number of nodes per system
#define	MAX_CPN			8			// Max number of channels per node
#define	MAX_DPC			3			// Max number of dimms per channel

//MK0508-begin
 struct nl_spd_info {
 	unsigned int node_num;
 	unsigned int chan_num;
 	unsigned int slot_num;
 	unsigned int mmio_index;
 };

struct nl_mmio_info {
	unsigned int node_num;
 	unsigned int chan_num;
 	unsigned int slot_num;
 	unsigned int spd_num;
 	unsigned long pa;
 	void *gws;
 	void *grs;
 	void *qcs;
 	void *reset;
 	void *hc;
 	void *term;
 	void *bcom_sw_en;
 	void *bcom_sw_dis;
 	void *ecc;
};
//MK0508-end

/* Each DIMM info */
struct nl_dimm_info {
	unsigned char node_num;			// 0-based node index
	unsigned char chan_num;			// 0-based channel number
	unsigned char slot_num;			// 0-based slot number
	unsigned char banks;			// # of banks
	unsigned char ranks;			// # of ranks
	unsigned char iowidth;			// data width
	unsigned int row;				//
	unsigned int col;				//
	unsigned int freq;				//
	unsigned int dram_size;			// in GB
	unsigned int nv_size;			// in GB
	unsigned int spd_dimm_id;		// DIMM ID used to access SPD
//MK0412-begin
	unsigned int hdimm_flag;		// 0 = other, 1 = HybriDIMM
	unsigned long mmio_pa;			// MMIO physical base address
//MK0412-end
//MK0508-begin
	/* For non_itlv use only.
	 * It is used to find mmio index for a selected HDIMM.
	 */
	unsigned int mmio_index;
//MK0508-end
};

/* Each memory channel info */
struct nl_channel_info {
	unsigned char ch_dimms;			// Number of dimms in a channel
	unsigned char ch_dimm_mask;		// indicates the presence of DIMMs in
									//  its channel
	struct nl_dimm_info dimm[MAX_DPC];
};

/* Info about memory installed in system */
struct hdimm_info {
	unsigned long tolm_addr;		// Top of low memory address
	unsigned long tohm_addr;		// Top of high memory address
	unsigned long sys_rsvd_mem_size;// Size of system reserved memory in GB
	unsigned long sys_mem_size;		// Size of system memory in GB
									//  = tohm - sys_rsvd_mem_size
	unsigned int spd_dimm_mask;		// indicates the presence of DIMMs based on
									//  spd_dimm_id
	unsigned char sys_dimm_count;	// Total # of DIMMs in system
	unsigned char sys_hdimm_count;	// Total # of HybriDIMMs in system
	unsigned char a7_mode;			// A7 mode enable/disable
	unsigned char sock_way;
	unsigned char chan_way;
	unsigned char rank_way;
	struct nl_channel_info channel[MAX_CPN];
//MK0508-begin
	unsigned int sys_hdimm_mask;	// Each bit# represents HDIMM's SPD ID
	unsigned int mmio_mask;			// Each bit# represents mmio index #
	struct nl_mmio_info mmio[MAX_NPS*MAX_CPN*MAX_DPC];	// Only for HDIMM
	struct nl_spd_info spd[MAX_NPS*MAX_CPN*MAX_DPC];	// Only for HDIMM
//MK0508-end
};
//MK1101-end

/*
 *
 * HV Group Tablec
 *
 * Each entry on this table describes the composition of a BSM and MMLS group.
 * Each group is seen by upper layer as a BSM and MMLS logical storage and
 * can be mounted with a block or char device. Physical storage of a group 
 * can come from multiple HVDIMMM(s). 
 *
 *     "emmc[]" describes size and order of HVDIMM(s) contribution
 *     "intv" describes interleaving rule of commnad/data/status/HV-DRAM. 
 *     "mem[]" describes starting address of DRAM and MMIO areas in this group.
 *
 * For 1-way interleaving, the size of emmc[] is equal to the size of mem[] 
 * since each HVDIMM has its own DRAM/MMIO. Otherwise, the size of mem[]
 * is emmc[] divided by "ways" of interleaving.
 *
 */

struct emmc_alloc {
	unsigned long b_start;		/* starting sector */
	unsigned long b_size;		/* number of sectors */
	unsigned long m_start;		/* starting sector */
	unsigned long m_size;		/* number of sectors */
};

struct interleave {
	unsigned int chnl_way;		/* ways of channel interleaving	*/
	unsigned int chnl_size;		/* for now 64 bytes */	
	unsigned int rank_way;		/* not supported */
};

struct mem_region {
	unsigned long p_dram;		/* DRAM physical address */
	void  *v_dram;			/* DRAM virtual address */
	unsigned long p_mmio;		/* MMIO physical address */
	void  *v_mmio;			/* base MMIO virtual address */
#ifndef REV_B_MM		
	void  *v_bsm_w_mmio;		/* BSM write MMIO virtual address */
	void  *v_bsm_r_mmio;		/* BSM read MMIO virtual address */
	void  *v_other_mmio;		/* other MMIO virtual address */
#endif	
#ifdef CONFIG_NUMA_NLST
	int numa_node;                  /* node this region belongs to */
#endif
};

struct hv_group_tbl {
	unsigned int gid;		/* assigned by HV SW, 0-based */
	unsigned long bsm_size;		/* total BSM sectors */
	unsigned long mmls_size;	/* total MMLS sectors */
	unsigned int bsm_device;	/* block or char device */
	unsigned int mmls_device;	/* block or char device */
	unsigned int num_hv;		/* number of HVDIMM in this group */
	struct emmc_alloc emmc[MAX_HV_DIMM];			
	struct interleave intv;		/* interleave rule */
	struct mem_region mem[MAX_HV_INTV];		
};

struct HV_BSM_IO_t {
	long b_size;
	void  *b_iomem;
	unsigned long phys_start;		/* phys addr of bsm start */
};

struct HV_MMLS_IO_t {
	long m_size;
	void  *m_iomem;
	unsigned long phys_start;		/* phys addr of mmls start */
};

//SJ0313//MK0126-begin
//SJ0313struct block_checksum_t {
//SJ0313	unsigned char sub_cs[4];
//SJ0313};
//SJ0313//MK0126-end
//SJ0313-begin
struct block_checksum_t {
	union {
		unsigned int cs;
		unsigned char sub_cs[4];
	};
};
//SJ0313-end

//MK0202-begin
struct fpga_debug_info_t {
	unsigned int lba;
	unsigned int lba2;
	unsigned int fr_buff_addr;
	unsigned int mmio_cmd_checksum;
};
//MK0202-end

extern struct hv_group_tbl hv_group[MAX_HV_GROUP];
extern struct hv_description_tbl hv_desc[MAX_HV_DIMM];
//MK1101-begin
extern struct hdimm_info hd_desc[MAX_NPS];
//MK1101-end

//MK0301-begin
#define DEFAULT_PATTERN_MASK	0xFFFFFFFFFFFFFFFF
//MK0301-end

#ifndef REV_B_MM
/* have to reserve HV DRAM space in case RDIMM is populated */
#define HV_DRAM_SIZE		0x40000000	
#define HV_MMIO_SIZE		0x40000000

#define MMIO_CMD_OFF		0x00000000
#define MMIO_BSM_W_OFF		0x00008000
#define MMIO_BSM_R_OFF		0x0000C000
#define MMIO_OTHER_OFF		0x00010000
#define MMIO_CMD_SIZE		(MMIO_BSM_W_OFF-MMIO_CMD_OFF)
#define MMIO_BSM_W_SIZE		(MMIO_BSM_R_OFF-MMIO_BSM_W_OFF)
#define MMIO_BSM_R_SIZE		(MMIO_OTHER_OFF-MMIO_BSM_R_OFF)
#define MMIO_OTHER_SIZE		(HV_MMIO_SIZE-MMIO_OTHER_OFF)

#ifdef MULTI_DIMM
#define CMD_OFF			0x00000000	/* relative to v_mmio */
#define QUERY_STATUS_OFF	0x00004000	
#define BSM_WRITE_OFF		0x00000000	/* relative to v_bsm_w_mmio */	
#define BSM_READ_OFF		0x00000000	/* relative to v_bsm_r_mmio */
#define ECC_OFF			0x00000000	/* relative to v_other_mmio */
#define TERM_OFF		0x00004000
#define READ_STATUS_OFF		0x00018000
#define WRITE_STATUS_OFF	0x00019000
#define MMLS_DRAM_OFF		0x00000000
#else
#define CMD_OFF			(hv_group[0].mem[0].v_mmio+0x00000000)	
#define QUERY_STATUS_OFF	(hv_group[0].mem[0].v_mmio+0x00004000)	
#define BSM_WRITE_OFF		(hv_group[0].mem[0].v_bsm_w_mmio)	
#define BSM_READ_OFF		(hv_group[0].mem[0].v_bsm_r_mmio)	
#define ECC_OFF			(hv_group[0].mem[0].v_other_mmio)
#define TERM_OFF		(hv_group[0].mem[0].v_other_mmio+0x00004000)
#define READ_STATUS_OFF		(hv_group[0].mem[0].v_other_mmio+0x00018000)
#define WRITE_STATUS_OFF	(hv_group[0].mem[0].v_other_mmio+0x00019000)
#define MMLS_DRAM_OFF		(hv_group[0].mem[0].v_dram)
#endif

void hv_write_command (int gid, int type, int lba, void *cmd);
void hv_write_termination (int gid, int type, int lba, void *cmd);
void hv_write_ecc (int gid, int lba, void *cmd);
unsigned char hv_query_status (int gid, int type, long lba, unsigned char status);
unsigned char hv_read_buf_status (int gid, int type, long lba);
unsigned char hv_write_buf_status (int gid, int type, long lba);

#else	// REV_B_MM

//MK0412-begin
#define	SPD_MFID_OFFSET		320
#define	SPD_MFID_LEN		2
#define	SPD_MFID_NETLIST	0x1683

#define	SPD_MPN_OFFSET		329
#define	SPD_MPN_LEN			20
#define	SPD_MPN_HDIMM		"NV3-25082-1SDT201000"
//MK0412-end

/* Size of HV MMIO window */
#define HV_MMIO_SIZE			0x00100000		// 1MB

/*
 * Size of temporary space to handle RDIMM_POPULATED and etc,
 * 96M (16M each for BSM/MMLS temp buffer, 32M each for BSM/MMLS cache)
 */
#define HV_MMLS_DRAM_SIZE		0x06000000		// 96MB

//MK0727-begin
/*
 * Size of buffer allocated from HVDIMM, this buffer is used for fake read
 * operation
 */
//MK0205#define HV_BUFFER_SIZE			0x1000000		// 16MB
//MK0205-begin
#define HV_BUFFER_SIZE			0x40000000		// 1GB
//MK0205-end

/*
 * 26GB = 16GB LRDIMM + 8GB HVDIMM + 2GB (reserved mem by the system)
 */
#define	LRDIMM_SIZE				0	//0x400000000		// 16GB -> 10.5.3.187/137/186
#define	SYS_RESERVED_MEM_SIZE	0x80000000		// 2GB
#define HV_DRAM_SIZE			0x200000000		// 8GB (size of DRAM on one HVDIMM)
#define	TOP_OF_SYS_MEM			LRDIMM_SIZE + SYS_RESERVED_MEM_SIZE + HV_DRAM_SIZE

/* System physical address of a buffer for the fake read/write operation */
#define	FAKE_BUFF_SYS_PADDR		TOP_OF_SYS_MEM - HV_MMIO_SIZE - HV_MMLS_DRAM_SIZE - HV_BUFFER_SIZE
//MK0727-end

//MK1024-begin
#define	MMLS_WRITE_BUFF_SIZE		0x00100000				// 1MB
#define	MMLS_WRITE_BUFF_SYS_PADDR	FAKE_BUFF_SYS_PADDR
#define	MMLS_READ_BUFF_SYS_PADDR	MMLS_WRITE_BUFF_SYS_PADDR + MMLS_WRITE_BUFF_SIZE
//MK1024-end

// MMLS address alignment, 16K
#ifdef MMLS_16K_ALIGNMENT
#define MMLS_ALIGNMENT_SIZE	0x00004000
#endif

#ifdef MULTI_DIMM
/* MMIO region offset relative to v_mmio */
#define WRITE_STATUS_OFF	0x00000000		// write buffer status, 64 bytes
#define READ_STATUS_OFF		0x00004000		// read buffer status, 64 bytes		
#define QUERY_STATUS_OFF	0x00008000		// command status, 64 bytes
#define CMD_OFF			0x00010000		// host command, 64 bytes
#define TERM_OFF		0x00014000		// temination command, 64 bytes
#define BCOM_OFF		0x00018000		// bcom mux control, 64 bytes
#define ECC_OFF			0x00020000		// ecc training, 256*64 bytes

#define MMLS_DRAM_OFF		0x00000000	
#define BSM_DRAM_OFF		0x01000000
#define MMLS_CACHE_OFF		0x02000000
#define BSM_CACHE_OFF		0x04000000
#define CACHE_MEM_SIZE		0x02000000

unsigned char hv_read_status (int gid, int type, long lba, long offset);
void hv_write_cmd (int gid, int type, long lba, void *cmd, long offset );
unsigned char hv_query_status (int gid, int type, long lba, unsigned char status);
void hv_write_ecc (int gid, int lba, void *cmd);

#else
/* MMIO region address relative to v_mmio */
#define WRITE_STATUS_OFF	(hv_group[0].mem[0].v_mmio+0x00000000)
#define READ_STATUS_OFF		(hv_group[0].mem[0].v_mmio+0x00004000)
#define QUERY_STATUS_OFF	(hv_group[0].mem[0].v_mmio+0x00008000)
//MK0321-begin
#define RESET_OFF			(hv_group[0].mem[0].v_mmio+0x0000C000)
//MK0321-end
#define CMD_OFF			(hv_group[0].mem[0].v_mmio+0x00010000)
#define TERM_OFF		(hv_group[0].mem[0].v_mmio+0x00014000)
#define BCOM_OFF		(hv_group[0].mem[0].v_mmio+0x00018000)
//MK0217-begin
#define BCOM_DIS_OFF	(hv_group[0].mem[0].v_mmio+0x0001C000)
//MK0215-end
#define ECC_OFF			(hv_group[0].mem[0].v_mmio+0x00020000)

#define MMLS_DRAM_OFF		(hv_group[0].mem[0].v_dram+0x00000000)
#define BSM_DRAM_OFF		(hv_group[0].mem[0].v_dram+0x01000000)
#define MMLS_CACHE_OFF		(hv_group[0].mem[0].v_dram+0x02000000)
#define BSM_CACHE_OFF		(hv_group[0].mem[0].v_dram+0x04000000)
#define CACHE_MEM_SIZE		0x02000000

//MK0324-begin
#define	EMMC_CH_CNT		8	//4
//MK0324-end

unsigned char hv_read_status (int gid, int type, long lba, void *addr );
void hv_write_cmd (int gid, int type, long lba, void *cmd, void *addr );

#define hv_query_status(gid,type,lba,status) hv_read_status(gid, type, lba, QUERY_STATUS_OFF)
#define hv_write_ecc(gid,lba,cmd) hv_write_cmd(gid, 0, lba, cmd, ECC_OFF)

#endif	// MULTI_DIMM

#define hv_write_command(gid,type,lba,cmd) hv_write_cmd(gid, type, lba, cmd, CMD_OFF)
//MK#define hv_write_termination(gid,type,lba,cmd) hv_write_cmd(gid, type, lba, cmd, TERM_OFF)
#define hv_read_buf_status(gid,type,lba) hv_read_status(gid, type, lba, READ_STATUS_OFF)
#define hv_write_buf_status(gid,type,lba) hv_read_status(gid, type, lba, WRITE_STATUS_OFF)
unsigned long hv_get_dram_addr (int type, void *addr);

#endif	// REV_B_MM

void hv_write_bsm_data (int gid, long lba, int index, void *data, long size);
void hv_read_bsm_data (int gid, long lba, int index, void *data, long size);
void hv_mmls_fake_read (int gid, long lba, int index, void *data, long size);
void hv_mmls_fake_write (int gid, long lba, int index, void *data, long size);
//MK0728-begin
void hv_mmls_fake_read_2(int gid, long lba, int index, void *pdata, long size);
void hv_mmls_fake_read_3(int gid, long lba, int index, void *pdata, long size);
//MK0728-end
//MK1103-begin
void memcpy_64B_movnti(void *dst, void *src);
void memcpy_64B_movnti_2(void *dst, void *src, unsigned int len);
//MK1103-end

int interleave_ways (int gid);
int hv_io_init(void);
void hv_io_release(void);
void get_bsm_iodata(struct HV_BSM_IO_t *p_bio_data);
void get_mmls_iodata(struct HV_MMLS_IO_t *p_mio_data);
//MK-begin
void hv_open_sesame(void);
void hv_write_termination(int gid, int type, int lba, void *cmd);
int hv_train_ecc_table(void);
void show_pci_config_regs(int bus, int dev, int func);
void show_fpga_i2c_reg(unsigned int spd_dimm_id, unsigned int fpga_id,
						unsigned int start_addr, unsigned int end_addr);
void show_dimm_spd(unsigned int spd_dimm_id);
int get_dimm_temp(unsigned int spd_dimm_id);
int set_i2c_page_address(unsigned int dimm, int page);
int read_smbus(unsigned int dimm, unsigned int smb_dti, unsigned int off);
int write_smbus(unsigned int dimm, unsigned int smb_dti, unsigned int off, unsigned int data);
//MK-end
//MK1019-begin
void clear_fake_mmls_buf(void);
//MK1019-end
//MK1215-begin
void display_buffer(unsigned long *pbuff, unsigned long qwcount, unsigned long qwmask);
//MK1215-end
//MK0105-begin
void hv_enable_bcom(void);
//MK0302#if (DEBUG_FEAT_BCOM_ONOFF == 1)
void hv_disable_bcom(void);
//MK0302#endif
//MK0105-end

//MK0120-begin
//unsigned char hv_read_checksum(void *addr);
void hv_read_fake_buffer_checksum(unsigned char fake_rw_flag, struct block_checksum_t *pcs);
//MK0120-end
//MK0214-begin
void hv_read_fake_buffer_checksum_2(unsigned char fake_rw_flag, struct block_checksum_t *pcs);
//MK0214-end

//MK0207-begin
void hv_read_mmio_command_checksum(struct fpga_debug_info_t *pdebuginfo);
void hv_display_mmio_command_slaveio(void);
void hv_reset_internal_state_machine(void);
//MK0207-end

//SJ0313-begin
void hv_reset_bcom_control(void);
//SJ0313-end

//MK0126-begin
void calculate_checksum(void *ba, struct block_checksum_t *pcksm);
//MK0126-end
//MK0214-begin
void calculate_checksum_2(void *ba, struct block_checksum_t *pcksm);
//MK0214-end

//MK0307-begin
unsigned char data_cs_comp(struct block_checksum_t *pcs1, struct block_checksum_t *pcs2);
//MK0307-end

//MK0202-begin
void hv_read_fpga_debug_info(struct fpga_debug_info_t *pdebuginfo);
//MK0202-end

//MK0209-begin
void clear_command_tag(void);
void inc_command_tag(void);
unsigned char get_command_tag(void);
//MK0209-end

//MK0223-begin
void set_debug_feat_flags(unsigned int flag);
void set_debug_feat_bsm_wrt_flags(unsigned int flag);
void set_debug_feat_bsm_rd_flags(unsigned int flag);
void hv_config_bcom_ctrl(unsigned char sw);

void init_bcom_ctrl_method(void);
unsigned char get_bcom_ctrl_method(void);
//MK0302-begin
unsigned char bcom_toggle_enabled(void);
//MK0302-end
//MK0307-begin
unsigned char slave_data_cs_enabled(void);
unsigned char slave_cmd_done_check_enabled(void);
unsigned char get_data_cs_location(void);
//MK0307-end

unsigned char bsm_wrt_cmd_checksum_verification_enabled(void);
unsigned char get_bsm_wrt_cmd_checksum_max_retry_count(void);
unsigned char bsm_wrt_data_checksum_verification_enabled(void);
unsigned char get_bsm_wrt_data_checksum_max_retry_count(void);
unsigned char bsm_wrt_fr_retry_enabled(void);
unsigned char get_bsm_wrt_fr_max_retry_count(void);
unsigned char bsm_wrt_qc_retry_enabled(void);
unsigned char get_bsm_wrt_qc_max_retry_count(void);
unsigned char bsm_wrt_skip_query_command_enabled(void);
unsigned char bsm_wrt_skip_gws_enabled(void);
//MK0301//MK0224-begin
//MK0301unsigned char bsm_wrt_skip_fr_on_lba_enabled(void);
//MK0301void set_bsm_wrt_skipping_lba(unsigned int lba);
//MK0301unsigned int get_bsm_wrt_skipping_lba(void);
//MK0301//MK0224-end
//MK0301-begin
unsigned char bsm_wrt_skip_termination_enabled(void);

unsigned char bsm_wrt_send_dummy_command_enabled(void);
void set_bsm_wrt_dummy_command_lba(unsigned int lba);
unsigned int get_bsm_wrt_dummy_command_lba(void);

unsigned char bsm_wrt_do_dummy_read_enabled(void);
void set_bsm_wrt_dummy_read_addr(unsigned long va);
unsigned long *get_bsm_wrt_dummy_read_addr(void);
//MK0301-end
//MK0307-begin
unsigned char bsm_wrt_popcnt_enabled(void);
//MK0307-end

//MK0321-begin
void hv_config_fpga_reset_ctrl(unsigned char sw);
void init_fpga_reset_ctrl_method(void);
unsigned char get_fpga_reset_ctrl_method(void);
//MK0321-end

unsigned char bsm_rd_cmd_checksum_verification_enabled(void);
unsigned char get_bsm_rd_cmd_checksum_max_retry_count(void);
unsigned char bsm_rd_data_checksum_verification_enabled(void);
unsigned char get_bsm_rd_data_checksum_max_retry_count(void);
unsigned char bsm_rd_fw_retry_enabled(void);
unsigned char get_bsm_rd_fw_max_retry_count(void);
unsigned char bsm_rd_qc_retry_enabled(void);
unsigned char get_bsm_rd_qc_max_retry_count(void);
unsigned char bsm_rd_skip_query_command_enabled(void);
unsigned char bsm_rd_skip_grs_enabled(void);
//MK0301-begin
unsigned char bsm_rd_skip_termination_enabled(void);

unsigned char bsm_rd_send_dummy_command_enabled(void);
void set_bsm_rd_dummy_command_lba(unsigned int lba);
unsigned int get_bsm_rd_dummy_command_lba(void);

unsigned char bsm_rd_do_dummy_read_enabled(void);
void set_bsm_rd_dummy_read_addr(unsigned long va);
unsigned long *get_bsm_rd_dummy_read_addr(void);

void set_pattern_mask(unsigned long mask);
unsigned long get_pattern_mask(void);
//MK0301-end
//MK0307-begin
unsigned char bsm_rd_popcnt_enabled(void);
//MK0307-end

//MK0605void set_user_defined_delay(unsigned int usdelay);
//MK0605unsigned int get_user_defined_delay(void);
void hv_delay_us(unsigned int delay_us);
//MK0223-end

//MK0224-begin
void set_bsm_wrt_qc_status_delay(unsigned long delay);
unsigned long get_bsm_wrt_qc_status_delay(void);
void set_bsm_rd_qc_status_delay(unsigned long delay);
unsigned long get_bsm_rd_qc_status_delay(void);
//MK0224-end

//MK0307-begin
void save_query_status(void);
int sw_popcnt_32(int i);
int sw_popcnt_64(long i);
int calculate_popcnt(void *pbuff, unsigned long bytecnt, unsigned char type);
//MK0418int hv_read_fake_buffer_popcnt(unsigned char fake_rw_flag);
//MK0418-begin
short int hv_read_fake_buffer_popcnt(unsigned char fake_rw_flag);
//MK0418-end
//MK0307-end

//MK0405-begin
void set_user_defined_delay_ms(unsigned char idx, unsigned int msdelay);
unsigned int get_user_defined_delay_ms(unsigned char idx);
//MK0405-end
//MK0605-begin
void set_user_defined_delay_us(unsigned char idx, unsigned int usdelay);
unsigned int get_user_defined_delay_us(unsigned char idx);
//MK0605-end

long bsm_start(void);
long bsm_size(void);
long mmls_start(void);
long mmls_size(void);
long mmls_cdev(void);
long bsm_cdev(void);

#endif  /* _HV_MULTI_H_ */

