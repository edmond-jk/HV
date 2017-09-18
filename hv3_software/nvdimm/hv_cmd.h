/*
 *
 *  HVDIMM command driver header file for BSM/MMLS.
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

/////////////////////////////////////////////////////// copied from hv_mmio.h
/* enable hv_mcpy() logging, default disabled */
// #define MMIO_LOGGING

/* REV B board support */
#define REV_B_MM
//#define MMLS_16K_ALIGNMENT

/* use code that supports MULTI DIMM, default disabled for bring-up */
/* single dimm code would be deprecated after multi dimm code verification */
// #define MULTI_DIMM

/* like MMLS, interleave BSM data to multiple DIMM, default enabled */
#define INTERLEAVE_BSM

/* RDIMM populated together w HVDIMM, default disabled */
//Better to have a logic to check if the address is in HVDIMM or not
#ifndef SIMULATION_TB
// #define RDIMM_POPULATED
#else
/* always use an intermediate non-cacheable buffer for MMLS operation */
#define RDIMM_POPULATED
#endif

/* no cache flush period for accessing command/data/status operation, default disabled */
// #define NO_CACHE_FLUSH_PERIOD

/* HVDIMM HW suspports 1KB block */
#define MIN_1KBLOCK_SUPPORT

#define MAX_HV_GROUP		1	/* one BSM and MMLS storage only */
#define MAX_HV_INTV		2	/* for CPU0 and CPU1 */
#define MAX_HV_DIMM		8	/* 4 HVDIMM on each CPU */

#define GROUP_BSM		1
#define GROUP_MMLS		2

#define DEV_BLOCK		1
#define	DEV_CHAR		2

#define HV_WRITE		1
#define	HV_READ			2

#define MEM_BURST_SIZE		64
#define HV_BLOCK_SIZE		512	// should be renamed to HV_SECTOR_SIZE
#define FS_BLOCK_SIZE		4096	// real block size of HVDIMM

#define INVALID_GROUP		0xff

#define	CMD_BUFFER_SIZE		64
#define	STATUS_BUFFER_SIZE	64
#define DATA_BUFFER_SIZE	4096
#define DATA_BUFFER_NUM		4



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

//extern struct hv_group_tbl hv_group[MAX_HV_GROUP];
extern struct hv_description_tbl hv_desc[MAX_HV_DIMM];

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

// Size of DRAM in one HVDIMM, 8G
#define HV_DRAM_SIZE		0x200000000
// Size of temporary space to handle RDIMM_POPULATED and etc,
// 32M (16M for BSM and 16M for MMLS)
#define HV_MMLS_DRAM_SIZE	0x02000000
// Size of HV MMIO window, 1M
#define HV_MMIO_SIZE		0x00100000
// MMLS address alignment, 16K
#ifdef MMLS_16K_ALIGNMENT
#define MMLS_ALIGNMENT_SIZE	0x00004000
#endif

#ifdef MULTI_DIMM
/* MMIO region offset relative to v_mmio */
#define WRITE_STATUS_OFF	0x00000000		// write buffer status, 64 bytes
#define READ_STATUS_OFF		0x00004000		// read buffer status, 64 bytes
#define QUERY_STATUS_OFF	0x00008000		// command status, 64 bytes
#define CMD_OFF				0x00010000		// host command, 64 bytes
#define TERM_OFF			0x00014000		// temination command, 64 bytes
#define BCOM_OFF			0x00018000		// bcom mux control, 64 bytes
#define ECC_OFF				0x00020000		// ecc training, 256*64 bytes

#define MMLS_DRAM_OFF		0x00000000
#define BSM_DRAM_OFF		HV_MMLS_DRAM_SIZE/2

unsigned char hv_read_status (int gid, int type, long lba, long offset);
void hv_write_cmd (int gid, int type, long lba, void *cmd, long offset );
unsigned char hv_query_status (int gid, int type, long lba, unsigned char status);
void hv_write_ecc (int gid, int lba, void *cmd);

#else
/* MMIO region address relative to v_mmio */
#if 0
#define WRITE_STATUS_OFF	(hv_group[0].mem[0].v_mmio+0x00000000)
#define READ_STATUS_OFF		(hv_group[0].mem[0].v_mmio+0x00004000)
#define QUERY_STATUS_OFF	(hv_group[0].mem[0].v_mmio+0x00008000)
#define CMD_OFF				(hv_group[0].mem[0].v_mmio+0x00010000)
#define TERM_OFF			(hv_group[0].mem[0].v_mmio+0x00014000)
#define BCOM_OFF			(hv_group[0].mem[0].v_mmio+0x00018000)
#define ECC_OFF				(hv_group[0].mem[0].v_mmio+0x00020000)

#define MMLS_DRAM_OFF		(hv_group[0].mem[0].v_dram+0x00000000)
#define BSM_DRAM_OFF		(hv_group[0].mem[0].v_dram+HV_MMLS_DRAM_SIZE/2)
#endif
static unsigned char hv_read_status (int gid, int type, long lba, void *addr );
static void hv_write_cmd (int gid, int type, long lba, void *cmd, void *addr );

#define hv_query_status(gid,type,lba,status) hv_read_status(gid, type, lba, QUERY_STATUS_OFF)
#define hv_write_ecc(gid,lba,cmd) hv_write_cmd(gid, 0, lba, cmd, ECC_OFF)

#endif	// MULTI_DIMM

#define hv_write_command(gid,type,lba,cmd) hv_write_cmd(gid, type, lba, cmd, CMD_OFF)
#define hv_write_termination(gid,type,lba,cmd) hv_write_cmd(gid, type, lba, cmd, TERM_OFF)
#define hv_read_buf_status(gid,type,lba) hv_read_status(gid, type, lba, READ_STATUS_OFF)
#define hv_write_buf_status(gid,type,lba) hv_read_status(gid, type, lba, WRITE_STATUS_OFF)

static unsigned long hv_get_dram_addr (int type, void *addr);

#endif	// REV_B_MM

static void hv_write_bsm_data (int gid, long lba, int index, void *data, long size);
static void hv_read_bsm_data (int gid, long lba, int index, void *data, long size);
static void hv_mmls_fake_read (int gid, long lba, int index, void *data, long size);
static void hv_mmls_fake_write (int gid, long lba, int index, void *data, long size);

static int interleave_ways (int gid);
static int hv_io_init(void);
static void hv_io_release(void);
static void get_bsm_iodata(struct HV_BSM_IO_t *p_bio_data);
static void get_mmls_iodata(struct HV_MMLS_IO_t *p_mio_data);

static long bsm_start(void);
static long bsm_size(void);
static long mmls_start(void);
static long mmls_size(void);
static long mmls_cdev(void);
static long bsm_cdev(void);


////////////////////////////////////////////////////////////////



#define QUEUE_DEPTH	16	//64

/* data commands */
#define MMLS_READ	0x10
#define MMLS_WRITE	0x20
#define PAGE_SWAP	0x50
#define BSM_BACKUP	0x60
#define BSM_RESTORE	0x61
#define BSM_WRITE	0x40
#define BSM_READ	0x30
#define BSM_QWRITE	0x41
#define	BSM_QREAD	0x31
#define QUERY		0x70

/* control commands */
#define NOP		0x00
#define RESET		0xE0
#define CONFIG		0x90
#define INQUIRY		0x91
#define TRIM		0x80
#define FW_UPDATE	0xF0
#define ABORT		0xE1

/* general status definitions */
/* #define BSM_WRITE_READY		0x10
#define MMLS_WRITE_READY	0x10
#define BSM_READ_READY		0x20
#define MMLS_READ_READY		0x20
#define	DEVICE_ERROR		0x40 */
#define G_STS_CNT_MASK	0x07


/* query status definitions */
#define	CMD_SYNC_COUNTER	0x03
#define	PROGRESS_STATUS		0x0C	/* 00: no cmd*/
					/* 01: cmd in FIFO */
					/* 10: cmd being processed */
					/* 11: cmd done */
#define CMD_BEING_PROCESSED	0x08
#define CMD_DONE		0x06	/* 0x0C */
#define DEVICE_SUCCESS		0x40

/* Waiting time from HW */
#define READ_TIME		3000		/* unit: ns */
#define WRITE_TIME		1000		/* 1 usec */
#define WRITE_TO_FLASH_TIME	6000

/* ECC training constants */
#define ECC_ADR_SHFT			0
#define ECC_CMDS_NUM			128
#define ECC_REPEAT_NUM			8

/* Latency test mode */
#define BSM_R_LTNCY	1
#define BSM_W_LTNCY	2
#define BSM_RW_LTNCY	3
#define MMLS_R_LTNCY	4
#define MMLS_W_LTNCY	5
#define MMLS_RW_LTNCY	6
#define ALL_RW_LTNCY	7


struct HV_CMD_MMLS_t {
	unsigned char cmd;
	unsigned char tag[2];
	unsigned char reserve1[5];
	unsigned char sector[4];
	unsigned char reserve2[4];
	unsigned char lba[4];
	unsigned char reserve3[4];
	unsigned char mm_addr[8];
	unsigned char more_data;	/* byte 32 */
	unsigned char reserve4[31];
};

struct HV_CMD_SWAP_t {
	unsigned char cmd;
	unsigned char tag[2];
	unsigned char reserve1[5];
	unsigned char page_out_sector[4];
	unsigned char page_in_sector[4];
	unsigned char page_out_lba[4];
	unsigned char page_in_lba[4];
	unsigned char mm_addr_out[4];
	unsigned char mm_addr_in[4];
	unsigned char reserve2[32];
};

struct HV_CMD_BSM_t {
	unsigned char cmd;
	unsigned char tag[2];
	unsigned char reserve1[5];
	unsigned char sector[4];
	unsigned char reserve2[4];
	unsigned char lba[4];		/* byte 16 */
	unsigned char reserve3[4];
	unsigned char mm_addr[8];
	unsigned char more_data;	/* byte 32 */
	unsigned char reserve4[31];
};

struct HV_BSM_STATUS_t {
	unsigned char cmd_status;
	unsigned char error_code;
	unsigned char tag[2];
	unsigned char reserve1[12];
	unsigned char remaining_sector_size[4];
	unsigned char remaining_size_swap[4];
	unsigned char next_lba[4];
	unsigned char next_lba_swap[4];
	unsigned char reserve2[32];
};

struct HV_MMLS_STATUS_t {
	unsigned char cmd_status;
	unsigned char error_code;
	unsigned char tag[2];
	unsigned char reserve1[4];
	unsigned char current_counter;
	unsigned char reserve2[7];
	unsigned char remaining_sector_size[4];
	unsigned char remaining_size_swap[4];
	unsigned char next_lba[4];
	unsigned char next_lba_swap[4];
	unsigned char reserve3[32];
};

struct HV_CMD_RESET_t {
	unsigned char cmd;
	unsigned char tag[2];
	unsigned char reserve1[61];
};

struct HV_CMD_QUERY_t {
	unsigned char cmd;
	unsigned char tag[2];
	unsigned char tag_id[2];
	unsigned char reserve1[61];
};

struct HV_CMD_ABORT_t {
	unsigned char cmd;
	unsigned char tag[2];
	unsigned char tag_id[2];
	unsigned char reserve1[59];
};

struct HV_CMD_FW_UPDATE_t {
	unsigned char cmd;
	unsigned char tag[2];
	unsigned char tag_id[2];
	unsigned char reserve1[59];
};

struct HV_CMD_TRIM_t {
	unsigned char cmd;
	unsigned char tag[2];
	unsigned char reserve1[5];
	unsigned char sector[4];
	unsigned char reserve2[4];
	unsigned char lba[4];
	unsigned char reserve3[44];
};

struct HV_CMD_CONFIG_t {
	unsigned char cmd;
	unsigned char tag[2];
	unsigned char reserve1;
	unsigned char size_of_emmc[4];
	unsigned char size_of_rdimm[4];
	unsigned char size_of_mmls[4];
	unsigned char size_of_bsm[4];
	unsigned char size_of_nvdimm[4];
	unsigned char timeout_emmc[4];
	unsigned char timeout_rdimm[4];
	unsigned char timeout_mmls[4];
	unsigned char timeout_bsm[4];
	unsigned char timeout_nvdimm[4];
	unsigned char reserve2[20];
};

struct HV_CMD_INQUIRY_t {
	unsigned char cmd;
	unsigned char tag[2];
	unsigned char reserve1[61];
};


struct HV_INQUIRY_STATUS_t {
	unsigned char cmd_status;
	unsigned char tag[2];
	unsigned char reserve1;
	unsigned char size_of_emmc[4];
	unsigned char size_of_rdimm[4];
	unsigned char size_of_mmls[4];
	unsigned char size_of_bsm[4];
	unsigned char size_of_nvdimm[4];
	unsigned char timeout_emmc[4];
	unsigned char timeout_rdimm[4];
	unsigned char timeout_mmls[4];
	unsigned char timeout_bsm[4];
	unsigned char timeout_nvdimm[4];
	unsigned char reserve2[20];
};

static int reset_command(unsigned int tag);
static int bsm_query_command(unsigned short tag, unsigned short tag_id, int type, unsigned int lba);
static int mmls_query_command(unsigned short tag, unsigned short tag_id, int type, unsigned int lba);
static int ecc_train_command(void);
static int inquiry_command(unsigned int tag);
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
					unsigned int to_nvdimm);
int mmls_read_command(unsigned int tag,
				unsigned int sector,
				unsigned int lba,
				unsigned long mm_addr,
				unsigned char async,
				void *callback_func);
int mmls_write_command(unsigned int tag,
				unsigned int sector,
				unsigned int lba,
				unsigned long mm_addr,
				unsigned char async,
				void *callback_func);
static int page_swap_command(unsigned int tag,
					   unsigned int o_sector,
					   unsigned int i_sector,
					   unsigned int o_lba,
					   unsigned int i_lba,
					   unsigned int o_mm_addr,
					   unsigned int i_mm_addr);
int bsm_read_command(unsigned int tag,
					unsigned long sector,
					unsigned long long lba,
					unsigned char *buf,
					unsigned char async,
					void *call_back);
int bsm_write_command(unsigned int tag,
					unsigned long sector,
					unsigned long long lba,
					unsigned char *buf,
					unsigned char async,
					void *call_back);
static int bsm_qread_command(unsigned int tag,
					   unsigned int sector,
					   unsigned int lba,
					   unsigned char *buf);
static int bsm_qwrite_command(unsigned int tag,
					    unsigned int sector,
					    unsigned int lba,
						unsigned char *buf);
static int bsm_backup_command(unsigned int tag,
					    unsigned int sector,
					    unsigned int lba);
static int bsm_restore_command(unsigned int tag,
					     unsigned int sector,
					     unsigned int lba);

static int single_cmd_init(void);
static int single_cmd_exit(void);
static void spin_for_cmd_init(void);
static unsigned long hv_nstimeofday(void);
#ifndef SIMULATION_TB
static int hv_cmd_stress_init(struct dentry *dirret);
#endif


