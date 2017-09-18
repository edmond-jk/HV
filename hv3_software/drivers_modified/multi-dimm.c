
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

//MK1101-begin
#define MAX_NPS			1			// Max number of nodes per system
#define	MAX_CPN			8			// Max number of channels per node
#define	MAX_DPC			3			// Max number of dimms per channel

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


const unsigned int MultiplyDeBruijnBitPosition[32] =
{
  // precomputed lookup table
  0,  1, 28,  2, 29, 14, 24,  3, 30, 22, 20, 15, 25, 17,  4,  8,
  31, 27, 13, 23, 21, 19, 16,  7, 26, 12, 18,  6, 11,  5, 10,  9
};

unsigned int get_lowest_bit_set(unsigned int x)
{
	/* only lowest bit will remain 1, all others become 0 */
	x  &= -(int)(x);

	/* DeBruijn constant */
	x *= 0x077CB531;

	/* the upper 5 bits are unique, skip the rest (32 - 5 = 27) */
	x >>= 27;

	/* convert to actual position */
	return MultiplyDeBruijnBitPosition[x];
}


void allocate_mmio_space_non_itlv(void)
{
	unsigned int mmio_index=0;
	unsigned int i, j, k;


	for (i=0; i < MAX_NPS; i++)
	{
		/* Clear the structure first before filling in */
		memset(hd_desc[i].mmio, 0, sizeof(hd_desc[i].mmio));

		/* Initialize the spd structure first before filling in */
		memset(hd_desc[i].spd, 0xFF, sizeof(hd_desc[i].spd));

		for (j=0; j < MAX_CPN; j++)
		{
			for (k=0; k < MAX_DPC; k++)
			{
				if ( hd_desc[i].channel[j].dimm[k].hdimm_flag == 1 ) {
					/* Each bit # represets SPD ID for this HDIMM */
//					hd_desc[i].sys_hdimm_mask |= (1 << hd_desc[i].channel[j].dimm[k].spd_dimm_id);

					/* Get IO memory from OS */
					hv_request_mem(hd_desc[i].channel[j].dimm[k].mmio_pa,
									(unsigned long *) &hd_desc[i].mmio[mmio_index].gws,
									HV_MMIO_SIZE,
									0,
									"cmd");

					/* Save other useful info */
					hd_desc[i].mmio[mmio_index].node_num = i;
					hd_desc[i].mmio[mmio_index].chan_num = j;
					hd_desc[i].mmio[mmio_index].slot_num = k;
					hd_desc[i].mmio[mmio_index].spd_num = hd_desc[i].channel[j].dimm[k].spd_dimm_id;	// may not ne needed
					hd_desc[i].mmio[mmio_index].pa = hd_desc[i].channel[j].dimm[k].mmio_pa;				// may not be needed
					hd_desc[i].channel[j].dimm[k].mmio_index = mmio_index;

					hd_desc[i].spd[hd_desc[i].channel[j].dimm[k].spd_dimm_id].node_num = i;
					hd_desc[i].spd[hd_desc[i].channel[j].dimm[k].spd_dimm_id].chan_num = j;
					hd_desc[i].spd[hd_desc[i].channel[j].dimm[k].spd_dimm_id].slot_num = k;
					hd_desc[i].spd[hd_desc[i].channel[j].dimm[k].spd_dimm_id].mmio_index = mmio_index;	// may not be needed

					mmio_index++;
				}
			} // end of for (k=0
		} // end of for (j=0
	} // end of for (i=0

	/* mmio_index should be equal to hd_desc[0].sys_hdimm_count at this point */
	/* Configure MMIO regions for the rest of HDIMMs */
	for (i=0; i < hd_desc[0].sys_hdimm_count; i++)
	{
		/* MMIO region for each HDIMM is not interleaved. */
		hd_desc[0].mmio[i].grs   = hd_desc[0].mmio[i].gws + 0x4000;
		hd_desc[0].mmio[i].qcs   = hd_desc[0].mmio[i].qrs + 0x4000;
		hd_desc[0].mmio[i].reset = hd_desc[0].mmio[i].qcs + 0x4000;
		hd_desc[0].mmio[i].hc    = hd_desc[0].mmio[i].reset + 0x4000;
		hd_desc[0].mmio[i].term  = hd_desc[0].mmio[i].hc + 0x4000;
		hd_desc[0].mmio[i].bcom_sw_en  = hd_desc[0].mmio[i].term + 0x4000;
		hd_desc[0].mmio[i].bcom_sw_dis = hd_desc[0].mmio[i].bcom_sw_en + 0x4000;
		hd_desc[0].mmio[i].ecc         = hd_desc[0].mmio[i].bcom_sw_dis + 0x4000;
	}

}


void deallocate_mmio_space_non_itlv(void)
{
	unsigned int i;


	for (i=0; i < hd_desc[0].sys_hdimm_count; i++)
	{
		if ( hd_desc[0].mmio[i].gws != 0 ) {
			iounmap((void *)hd_desc[0].mmio[i].gws);
			release_mem_region(hd_desc[0].mmio[i].pa, HV_MMIO_SIZE);
		}
	} // end of for (i=0
}


void allocate_mmio_space_itlv(void)
{
	unsigned int i, j, k, mmio_index=0;
	unsigned long lowest_mmio_va, lowest_mmio_pa=hd_desc[0].tohm_addr, delta;


	/* Find an HybriDIMM with the lowest MMIO physical base address */
	for (i=0; i < MAX_NPS; i++)
	{
		for (j=0; j < MAX_CPN; j++)
		{
			for (k=0; k < MAX_DPC; k++)
			{
				/* Check if the current DIMM is HybriDIMM */
				if ( hd_desc[i].channel[j].dimm[k].hdimm_flag == 1 ) {

					/* Is its MMIO base addr lower than the current lowest? */
					if ( hd_desc[i].channel[j].dimm[k].mmio_pa != 0 &&
						hd_desc[i].channel[j].dimm[k].mmio_pa < lowest_mmio_pa ) {
						/* We just need to know the address. We don't care
						 * which HDIMM it belongs to.
						 */
						lowest_mmio_pa = hd_desc[i].channel[j].dimm[k].mmio_pa;
					}
				}
			} // end of for (k=0
		} // end of for (j=0
	} // end of for (i=0

	/* Get IO memory from OS - we are assuming that it will be 3-way channel
	 * interleaving if there are three HDIMMs, etc. For example, since MMIO
	 * space for each HDIMM takes 1MB, we will allocate 3MB of MMIO space for
	 * 3-way channel interleaving.
	 */
	hv_request_mem(lowest_mmio_pa,
					(unsigned long *) &lowest_mmio_va,
					HV_MMIO_SIZE * hd_desc[0].sys_hdimm_count,
					0,
					"cmd");

	/* Fill out hd_desc[].mmio structure */
	for (i=0; i < MAX_NPS; i++)
	{
		/* Clear the mmio structure first before filling in */
		memset(hd_desc[i].mmio, 0, sizeof(hd_desc[i].mmio));

		/* Initialize the spd structure first before filling in */
		memset(hd_desc[i].spd, 0xFF, sizeof(hd_desc[i].spd));

		for (j=0; j < MAX_CPN; j++)
		{
			for (k=0; k < MAX_DPC; k++)
			{
				/* Check if the current DIMM is HybriDIMM */
				if ( hd_desc[i].channel[j].dimm[k].hdimm_flag == 1 ) {
					/* Each bit # represets SPD ID for this HDIMM */
//					hd_desc[i].sys_hdimm_mask |= (1 << hd_desc[i].channel[j].dimm[k].spd_dimm_id);

					/* Save other useful info */
					hd_desc[i].mmio[mmio_index].node_num = i;
					hd_desc[i].mmio[mmio_index].chan_num = j;
					hd_desc[i].mmio[mmio_index].slot_num = k;
					hd_desc[i].mmio[mmio_index].spd_num = hd_desc[i].channel[j].dimm[k].spd_dimm_id;	// may not be needed
					hd_desc[i].mmio[mmio_index].pa = hd_desc[i].channel[j].dimm[k].mmio_pa;				// may not be needed
					hd_desc[i].channel[j].dimm[k].mmio_index = mmio_index;

					hd_desc[i].spd[hd_desc[i].channel[j].dimm[k].spd_dimm_id].node_num = i;
					hd_desc[i].spd[hd_desc[i].channel[j].dimm[k].spd_dimm_id].chan_num = j;
					hd_desc[i].spd[hd_desc[i].channel[j].dimm[k].spd_dimm_id].slot_num = k;
					hd_desc[i].spd[hd_desc[i].channel[j].dimm[k].spd_dimm_id].mmio_index = mmio_index;	// may not be needed

					/* Calculate VA of each HDIMM's base, which is GWS */
					hd_desc[i].mmio[mmio_index].gws = lowest_mmio_va + (hd_desc[i].channel[j].dimm[k].mmio_pa - lowest_mmio_pa);
					mmio_index++;
				}
			} // end of for (k=0
		} // end of for (j=0
	} // end of for (i=0

	/* mmio_index should be equal to hd_desc[0].sys_hdimm_count at this point */
	/* Setup all other MMIO regions for HDIMMs */
	delta = 0x4000 * hd_desc[0].sys_hdimm_count;
	for (i=0; i < hd_desc[0].sys_hdimm_count; i++)
	{
		hd_desc[0].mmio[i].grs   = hd_desc[0].mmio[i].gws + delta;
		hd_desc[0].mmio[i].qcs   = hd_desc[0].mmio[i].grs + delta;
		hd_desc[0].mmio[i].reset = hd_desc[0].mmio[i].qcs + delta;
		hd_desc[0].mmio[i].hc    = hd_desc[0].mmio[i].reset + delta;
		hd_desc[0].mmio[i].term  = hd_desc[0].mmio[i].hc + delta;
		hd_desc[0].mmio[i].bcom_sw_en  = hd_desc[0].mmio[i].term + delta;
		hd_desc[0].mmio[i].bcom_sw_dis = hd_desc[0].mmio[i].bcom_sw_en + delta;
		hd_desc[0].mmio[i].ecc         = hd_desc[0].mmio[i].bcom_sw_dis + delta;
	}

}

void deallocate_mmio_space_itlv(unsigned long pa, void *va, unsigned long size)
{
//	iounmap(va);
//	release_mem_region(pa, size);
	iounmap((void *)hd_desc[0].mmio[0].gws);
	release_mem_region(hd_desc[0].mmio[0].pa, HV_MMIO_SIZE * hd_desc[0].sys_hdimm_count);
}


//void hv_write_cmd (int gid, int type, long lba, void *cmd, void *addr)
void hv_write_cmd (void *cmd, unsigned int spd_dimm_mask)
{
	unsigned int i, m=spd_dimm_mask, spd_id;

	if(!cmd)
		cmd = cmd_burst_buf;

	while ( m > 0 ) {
		spd_id = get_lowest_bit_set(m);
		m &= ~(1 << spd_id);
		i = hd_desc[0].spd[spd_id].mmio_index;

		if (i > hd_desc[0].sys_hdimm_count) {
			break;
		}

		/* Write a 64-byte command descriptor to MMIO Command Region */
		memcpy_64B_movnti(hd_desc[0].mmio[i].hc, cmd);

		/* Fake-read to write data to FPGA */
		if (cmd_status_use_cache) {
			clflush_cache_range(hd_desc[0].mmio[i].hc, CMD_BUFFER_SIZE << hd_desc[0].a7_mode);
		}
	}
}


//unsigned char hv_read_status (int gid, int type, long lba, void *addr)
unsigned int hv_read_gws(unsigned int spd_dimm_mask, unsigned char block_size)
{
	unsigned int i, m=spd_dimm_mask, spd_id, hdimm_gws=0;


	/* If BCOM toggle is enabled, enable BCOM now */
	if ( bcom_toggle_enabled() ) {
		hv_enable_bcom();
	} else {
		hv_delay_us(get_user_defined_delay());
	}

	while ( m > 0 ) {
		spd_id = get_lowest_bit_set(m);
		m &= ~(1 << spd_id);
		i = hd_desc[0].spd[spd_id].mmio_index;

		if (i > hd_desc[0].sys_hdimm_count) {
			break;
		}

		/* Presight-write dummy data to the G.W.S. of each HDIMM */
		/* >>>>>>>>>>>>>>> MAKE SURE fake_mmls_buf is 128 bytes <<<<<<<<<<<<<<< */
		memcpy_64B_movnti_3(hd_desc[0].mmio[i].gws, (void*)fake_mmls_buf, MEM_BURST_SIZE << hd_desc[0].a7_mode);

		/* Make sure what we just wrote reaches DRAM/FGPA */
		if (cmd_status_use_cache) {
			clflush_cache_range(hd_desc[0].mmio[i].gws, MEM_BURST_SIZE << hd_desc[0].a7_mode);
		}
	}

	/* If BCOM toggle is enabled, disable BCOM now */
	if ( bcom_toggle_enabled() ) {
		hv_disable_bcom();
	} else {
		hv_delay_us(get_user_defined_delay());
	}

	/* Read status bytes */
	/* >>>>>>>>>>>>>>> MAKE SURE cmd_burst__buf is 128 bytes <<<<<<<<<<<<<<< */
	m = spd_dimm_mask;
	while ( m > 0 ) {
		spd_id = get_lowest_bit_set(m);
		m &= ~(1 << spd_id);
		i = hd_desc[0].spd[spd_id].mmio_index;

		if (i > hd_desc[0].sys_hdimm_count) {
			break;
		}

		clflush_cache_range((void *)cmd_burst_buf, CMD_BUFFER_SIZE << hd_desc[0].a7_mode);
		hv_mcpy((void *)cmd_burst_buf, (void *)hd_desc[0].mmio[i].gws, CMD_BUFFER_SIZE << hd_desc[0].a7_mode);

		if ( (cmd_burst_buf[0] & 0x07) >= block_size ) {
			hdimm_gws |= (1 << hd_desc[0].mmio[i].spd_num);
		}

		/* >>>>> Do we need some delay at this point??? <<<<< */
	}

	/* Each bit represents SPD IDs of DIMMs that returned CMD_DONE status */
	return hdimm_gws;
}


//unsigned char hv_read_status (int gid, int type, long lba, void *addr)
unsigned int hv_read_grs(unsigned int spd_dimm_mask, unsigned char block_size)
{
	unsigned int i, m=spd_dimm_mask, spd_id, hdimm_grs=0;


	/* If BCOM toggle is enabled, enable BCOM now */
	if ( bcom_toggle_enabled() ) {
		hv_enable_bcom();
	} else {
		hv_delay_us(get_user_defined_delay());
	}

	while ( m > 0 ) {
		spd_id = get_lowest_bit_set(m);
		m &= ~(1 << spd_id);
		i = hd_desc[0].spd[spd_id].mmio_index;

		if (i > hd_desc[0].sys_hdimm_count) {
			break;
		}

		/* Presight-write dummy data to the G.R.S. of each HDIMM */
		/* >>>>>>>>>>>>>>> MAKE SURE fake_mmls_buf is 128 bytes <<<<<<<<<<<<<<< */
		memcpy_64B_movnti_3(hd_desc[0].mmio[i].grs, (void*)fake_mmls_buf, MEM_BURST_SIZE << hd_desc[0].a7_mode);

		/* Make sure what we just wrote reaches DRAM/FGPA */
		if (cmd_status_use_cache) {
			clflush_cache_range(hd_desc[0].mmio[i].grs, MEM_BURST_SIZE << hd_desc[0].a7_mode);
		}
	}

	/* If BCOM toggle is enabled, disable BCOM now */
	if ( bcom_toggle_enabled() ) {
		hv_disable_bcom();
	} else {
		hv_delay_us(get_user_defined_delay());
	}

	/* Read status bytes */
	/* >>>>>>>>>>>>>>> MAKE SURE cmd_burst__buf is 128 bytes <<<<<<<<<<<<<<< */
	m = spd_dimm_mask;
	while ( m > 0 ) {
		spd_id = get_lowest_bit_set(m);
		m &= ~(1 << spd_id);
		i = hd_desc[0].spd[spd_id].mmio_index;

		if (i > hd_desc[0].sys_hdimm_count) {
			break;
		}

		clflush_cache_range((void *)cmd_burst_buf, CMD_BUFFER_SIZE << hd_desc[0].a7_mode);
		hv_mcpy((void *)cmd_burst_buf, (void *)hd_desc[0].mmio[i].grs, CMD_BUFFER_SIZE << hd_desc[0].a7_mode);

		if ( (cmd_burst_buf[0] & 0x07) >= block_size ) {
			hdimm_grs |= (1 << hd_desc[0].mmio[i].spd_num);
		}

		/* >>>>> Do we need some delay at this point??? <<<<< */
	}

	/* Each bit represents SPD IDs of DIMMs that returned CMD_DONE status */
	return hdimm_grs;
}


//unsigned char hv_read_status (int gid, int type, long lba, void *addr)
unsigned int hv_read_qcs(unsigned int spd_dimm_mask)
{
	unsigned int i, mask=spd_dimm_mask, spd_id, hdimm_qcs=0;


	/* If BCOM toggle is enabled, enable BCOM now */
	if ( bcom_toggle_enabled() ) {
		hv_enable_bcom();
	} else {
		hv_delay_us(get_user_defined_delay());
	}

	while ( mask > 0 ) {
		spd_id = get_lowest_bit_set(mask);
		mask &= ~(1 << spd_id);
		i = hd_desc[0].spd[spd_id].mmio_index;

		if (i > hd_desc[0].sys_hdimm_count) {
			break;
		}

		/* Presight-write dummy data to the query command status of each HDIMM */
		/* >>>>>>>>>>>>>>> MAKE SURE fake_mmls_buf is 128 bytes <<<<<<<<<<<<<<< */
		memcpy_64B_movnti_3(hd_desc[0].mmio[i].qcs, (void*)fake_mmls_buf, MEM_BURST_SIZE << hd_desc[0].a7_mode);

		/* Make sure what we just wrote reaches DRAM/FGPA */
		if (cmd_status_use_cache) {
			clflush_cache_range(hd_desc[0].mmio[i].qcs, MEM_BURST_SIZE << hd_desc[0].a7_mode);
		}
	}

	/* If BCOM toggle is enabled, disable BCOM now */
	if ( bcom_toggle_enabled() ) {
		hv_disable_bcom();
	} else {
		hv_delay_us(get_user_defined_delay());
	}

	/* Read status bytes */
	/* >>>>>>>>>>>>>>> MAKE SURE cmd_burst__buf is 128 bytes <<<<<<<<<<<<<<< */
	mask = spd_dimm_mask;
	while ( mask > 0 ) {
		spd_id = get_lowest_bit_set(mask);
		mask &= ~(1 << spd_id);
		i = hd_desc[0].spd[spd_id].mmio_index;

		if (i > hd_desc[0].sys_hdimm_count) {
			break;
		}

		clflush_cache_range((void *)cmd_burst_buf, CMD_BUFFER_SIZE << hd_desc[0].a7_mode);
		hv_mcpy((void *)cmd_burst_buf, (void *)hd_desc[0].mmio[i].qcs, CMD_BUFFER_SIZE << hd_desc[0].a7_mode);

		if ( slave_cmd_done_check_enabled() ) {
			if ( ((cmd_burst_buf[4] & 0x0C) | (cmd_burst_buf[0] & 0x0C) >> 2) == 0x0F ) {
				hdimm_qcs |= (1 << hd_desc[0].mmio[i].spd_num);
			}
		} else {
			if ( (cmd_burst_buf[0] & 0x0C) == 0x0C ) {
				hdimm_qcs |= (1 << hd_desc[0].mmio[i].spd_num);
			}
		}

		/* >>>>> Do we need some delay at this point??? <<<<< */
	}

	/* Each bit represents SPD IDs of DIMMs that returned CMD_DONE status */
	return hdimm_qcs;
}


//static int wait_for_cmd_done(unsigned int tag, int type, unsigned int lba, unsigned long delay)
static int wait_for_cmd_done(unsigned int tag, int type, unsigned int lba, unsigned long delay, unsigned int hdimm_mask)
{	
	int error_code=-1;
	unsigned long ts, elapsed_time=0;
//MK0508	unsigned char query_status=0;
//MK0508-begin
	unsigned int query_status=0, qcs_mask=hdimm_mask;
//MK0508-end
	unsigned int dbg_query_status_count=0;

	pr_debug("[%s]: entered\n", __func__);

	ts = hv_nstimeofday();
	while(elapsed_time < QUERY_TIMEOUT) {
		/* For debugging - give some time delay before starting to check the status */
		ndelay(delay);

//MK0508		query_status = hv_query_status(0, type, lba, 0);
//MK0508-begin
		query_status = hv_read_qcs(qcs_mask);
//MK0508-end

		/* Elapsed time since the first query command */
		elapsed_time = hv_nstimeofday() - ts;
		dbg_query_status_count++;

#ifdef SW_SIM
		return 0;
#endif

//MK0508		if ( slave_cmd_done_check_enabled() ) {
//MK0508			/* Check for command done for both FPGAs */
//MK0508			if ( query_status == 0x0F ) {
//MK0508				/* Detected command done, exit the loop */
//MK0508				error_code = 0;
//MK0508				break;
//MK0508			}
//MK0508		} else {
//MK0508			/* Check for command done */
//MK0508			if (((query_status & QUERY_PROGRESS_STATUS_MASK)  >> 2) == 3) {
//MK0508				/* Detected command done, exit the loop */
//MK0508				error_code = 0;
//MK0508				break;
//MK0508			}
//MK0508		}
//MK0508-begin
		if ( (hdimm_mask ^ query_status) == 0 ) {
			error_code = 0;
			break;
		} else {
			qcs_mask = hdimm_mask ^ query_status;
		}
//MK0508-end
	} // end of while

	/* Copy the entire burst to access later */
	if (error_code == 0) {
		save_query_status();
	}
	pr_debug("[%s]: %s: query_status=0x%.2x dbg_query_status_count=%d delay=%lu ns elapsed time=%lu ns\n",
			__func__, (error_code == 0) ? "PASS" : "FAIL", (unsigned int)query_status, dbg_query_status_count, delay, elapsed_time);

	return error_code;
}

