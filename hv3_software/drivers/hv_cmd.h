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

#define QUEUE_DEPTH	16	//64

/* data commands */
//MK1006#define MMLS_READ	0x10
//MK1006#define MMLS_WRITE	0x20
//MK1006-begin
// Temp change to test the two new commands: TBM_READ & TBM_WRITE

#if 0 //1: TBM READ/WRITE, 0:eMMC READ/WRITE
#define MMLS_READ	0x90	// TBM_READ
#define MMLS_WRITE	0x80	// TBM_WRITE
#else
#define MMLS_READ	0x10	//MK0324 - FPGA expects this command for eMMC operation
#define MMLS_WRITE	0x20	//MK0324 - FPGA expects this command for eMMC operation
#endif

//MK1006-end
#define PAGE_SWAP	0x50
#define BSM_BACKUP	0x60
#define BSM_RESTORE	0x61
#define BSM_WRITE	MMLS_WRITE
#define BSM_READ	MMLS_READ
#define BSM_QWRITE	0x41
#define	BSM_QREAD	0x31
#define QUERY		0x70
//MK1201-begin
#define GET_VID		0x70
//MK1201-end

/* control commands */
#define NOP			0x00
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
#define	PROGRESS_STATUS		0x0C
					/* 00: no cmd*/
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

//MK-begin
typedef enum {
	FAKE_READ,
	FAKE_WRITE
} fake_type_t ;
//MK-end

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
//MK-begin
struct hv_rw_cmd_field {
	unsigned char cmd;
	unsigned char tag;
	unsigned char reserve1[2];
};

union hv_rw_cmd_field_union {
	struct hv_rw_cmd_field cmd_field;
	unsigned int cmd_field_32b;
};

struct hv_rw_cmd {
	union hv_rw_cmd_field_union command[2];
	unsigned int sector[2];
	unsigned int lba[2];
	unsigned int dram_addr[2];
	unsigned int checksum[2];
	unsigned int reserve1[6];
};

struct hv_query_cmd_field {
	unsigned char cmd;
	unsigned char query_tag;
	unsigned char cmd_tag;
	unsigned char reserve1;
};

union hv_query_cmd_field_union {
	struct hv_query_cmd_field cmd_field;
	unsigned int cmd_field_32b;
};

struct hv_query_cmd {
	union hv_query_cmd_field_union command[2];
	unsigned int reserve1[6];
	unsigned int checksum[2];
	unsigned int reserve2[6];
};

//MK-end

//MK0307-begin
struct hv_query_cmd_status {
	unsigned char master_status;
	unsigned char reserve1[3];
	unsigned char slave_status;
	unsigned char reserve2[3];
//MK0425	unsigned int master_checksum1;
//MK0425	unsigned int slave_checksum1;
//MK0425	unsigned int master_checksum2;
//MK0425	unsigned int slave_checksum2;
//MK0425	unsigned int master_popcount;
//MK0425	unsigned int slave_popcount;
//MK0425	unsigned long reserve3[4];
//MK0425-begin
	unsigned int fw_m_cs1;		// fake-write master checksum 1
	unsigned int fw_s_cs1;		// fake-write slave checksum 1
	unsigned int fw_m_cs2;		// fake-write master checksum 2
	unsigned int fw_s_cs2;		// fake-write slave checksum 2
	unsigned int fw_m_pc;		// fake-write master popcount
	unsigned int fw_s_pc;		// fake-write slave popcount
	unsigned int fr_m_cs1;		// fake-read master checksum 1
	unsigned int fr_s_cs1;		// fake-read slave checksum 1
	unsigned int fr_m_cs2;		// fake-read master checksum 2
	unsigned int fr_s_cs2;		// fake-read slave checksum 2
	unsigned int fr_m_pc;		// fake-read master popcount
	unsigned int fr_s_pc;		// fake-read slave popcount
	unsigned long reserve3;
//MK0425-end
};
//MK0307-end

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

//MK1201-begin
struct hd_vid_t {
	unsigned int spd_dimm_id;		// DIMM ID used to access SPD
	unsigned int vid;
	int error_code;
};

int get_vid_command(void *cmd_desc);
//MK1201-end

int reset_command(unsigned int tag);
int bsm_query_command(unsigned short tag, unsigned short tag_id, int type, unsigned int lba);
int mmls_query_command(unsigned short tag, unsigned short tag_id, int type, unsigned int lba);
int ecc_train_command(void);
int inquiry_command(unsigned int tag);
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
int page_swap_command(unsigned int tag,
					   unsigned int o_sector,
					   unsigned int i_sector,
					   unsigned int o_lba,
					   unsigned int i_lba,
					   unsigned int o_mm_addr,
					   unsigned int i_mm_addr);
//MK0207int bsm_read_command(unsigned int tag,
//MK0207					unsigned int sector,
//MK0207					unsigned int lba,
//MK0207					unsigned char *buf,
//MK0207					unsigned char async,
//MK0207					void *call_back);
//MK0207-begin
int bsm_read_command(unsigned int tag, unsigned int sector, unsigned int lba,
		unsigned char *buf, unsigned char async, void *call_back, unsigned char max_retry_count);
//MK0207-end

//MK0817-begin - test only
//MK1118int bsm_read_command_2(unsigned int tag, unsigned int sector, unsigned int lba,
//MK1118		unsigned char *buf, unsigned char async, void *call_back);
//MK0120//MK1118-begin
//MK0120int bsm_read_command_2(unsigned int tag, unsigned int sector, unsigned int lba,
//MK0120		unsigned char *buf, unsigned char *vbuf, unsigned char async, void *call_back);
//MK0120//MK1118-end
//MK0817-end

//MK0201//MK0120-begin
//MK0201int bsm_read_command_2(unsigned int tag, unsigned int sector, unsigned int lba,
//MK0201		unsigned char *buf, unsigned char *vbuf, unsigned char async, void *call_back, unsigned char *checksum);
//MK0201//MK0120-end
//MK0201-begin
int bsm_read_command_2(unsigned int tag, unsigned int sector, unsigned int lba,
		unsigned char *buf, unsigned char *vbuf, unsigned char async, void *call_back, unsigned char max_retry_count);
//MK0201-end

//MK0213-begin
int bsm_read_command_3(unsigned int tag, unsigned int sector, unsigned int lba,
			unsigned char *buf,	unsigned char *vbuf, unsigned char async,
			void *callback_func, unsigned char max_retry_count);
//MK0213-end

int bsm_write_command(unsigned int tag,
					unsigned int sector,
					unsigned int lba,
					unsigned char *buf,
					unsigned char async,
					void *call_back);

//MK0724-begin - test only
//MK0201//MK1118int bsm_write_command_2(unsigned int tag, unsigned int sector, unsigned int lba,
//MK0201//MK1118			unsigned char *buf,	unsigned char async, void *callback_func);
//MK0201//MK1118-begin
//MK0201int bsm_write_command_2(unsigned int tag, unsigned int sector, unsigned int lba,
//MK0201			unsigned char *buf,	unsigned char *vbuf, unsigned char async, void *callback_func);
//MK0201//MK1118-end
//MK0201//MK0724-end
//MK0201-begin
int bsm_write_command_2(unsigned int tag, unsigned int sector, unsigned int lba,
			unsigned char *buf,	unsigned char *vbuf, unsigned char async, void *callback_func, unsigned char max_retry_count);
//MK0201-end

//MK0201-begin
int bsm_write_command_3(unsigned int tag, unsigned int sector, unsigned int lba,
		unsigned char *buf, unsigned char *vbuf, unsigned char async,
		void *callback_func, unsigned char max_retry_count);
//MK0201-end

int bsm_qread_command(unsigned int tag,
					   unsigned int sector,
					   unsigned int lba,
					   unsigned char *buf);
int bsm_qwrite_command(unsigned int tag,
					    unsigned int sector,
					    unsigned int lba,
						unsigned char *buf);
int bsm_backup_command(unsigned int tag,
					    unsigned int sector,
					    unsigned int lba);
int bsm_restore_command(unsigned int tag,
					     unsigned int sector,
					     unsigned int lba);
//MK0123-begin
int bsm_terminate_command(void);
//MK0123-end

//MK0125-begin
int bsm_write_command_master(unsigned int tag, unsigned int sector, unsigned int lba,
		unsigned char *buf, unsigned char *vbuf, unsigned char async, void *callback_func);
int bsm_read_command_master(unsigned int tag, unsigned int sector, unsigned int lba,
		unsigned char *buf,	unsigned char *vbuf, unsigned char async,
		void *callback_func);
//MK0125-end

//MK0324-begin
int bsm_write_command_emmc(unsigned int tag, unsigned int sector, unsigned int lba,
		unsigned char *buf, unsigned char *vbuf, unsigned char async, void *callback_func);
int bsm_read_command_emmc(unsigned int tag, unsigned int sector, unsigned int lba,
		unsigned char *buf,	unsigned char *vbuf, unsigned char async, void *callback_func);
//MK0324-end

//MK0410-begin
int bsm_read_command_3_cmd_only(unsigned int tag, unsigned int sector, unsigned int lba,
			unsigned char *buf,	unsigned char *vbuf, unsigned char async,
			void *callback_func, unsigned char max_retry_count);
//MK0410-end

int single_cmd_init(void);
int single_cmd_exit(void);
void spin_for_cmd_init(void);
unsigned long hv_nstimeofday(void);
#ifndef SIMULATION_TB
int hv_cmd_stress_init(struct dentry *dirret);
#endif

//MK0725-begin - not yet tested
void td_memcpy_8x8_movnti(void *dst, const void *src, unsigned int len);
//MK0725-end
