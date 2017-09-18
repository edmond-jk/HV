

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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/atomic.h>

#include "hv_params.h"
#include "hvdimm.h"

static int hv_cmdq_cur_tag;		/* current tag */
static atomic_t hv_cmdq_len;		/* # of cmds in queue */

/* "hv_cmdq" holds outstanding commands */
static struct {
	unsigned long bio;
	unsigned char is_last_segment;
} hv_cmdq[HV_Q_DEPTH];


int hv_next_cmdq_tag(void)
{
	int tag;

	tag = hv_cmdq_cur_tag;
	hv_cmdq_cur_tag++;

	if (hv_cmdq_cur_tag == get_queue_size())
		hv_cmdq_cur_tag = 0;

	return tag;
}

int hv_prev_cmdq_tag(void)
{
	int tag;

	tag = hv_cmdq_cur_tag;
	if (tag == 0)
		tag = get_queue_size()-1;
	else
		tag--;

	return tag;
}

/* ----- Async Mode Fucntions ---- */

/**	
 * 	NOTE: hv_lock spinlock taken by caller
 **/
void hv_queue_cmdq(unsigned long bio, unsigned char is_last_segment)
{
		hv_log("%s  (hv_cmdq_len %d tag %d\n",
			__func__, atomic_read(&hv_cmdq_len), hv_prev_cmdq_tag());

		/* All bio segments are tracked as individual cmds to the hardware */
		atomic_inc(&hv_cmdq_len);
		
		hv_cmdq[hv_prev_cmdq_tag()].is_last_segment = is_last_segment;
		hv_cmdq[hv_prev_cmdq_tag()].bio = bio;
}

/**	
 * 	NOTE: hv_lock spinlock taken by caller
 **/
unsigned long hv_dequeue_cmdq(int tag, unsigned char *is_last_segment)
{
		unsigned long bio = 0;

		bio = hv_cmdq[tag].bio;
		*is_last_segment = hv_cmdq[tag].is_last_segment;
		hv_cmdq[tag].bio = 0;
		hv_cmdq[tag].is_last_segment = 0;
		atomic_dec(&hv_cmdq_len);

//MK		hv_log("%s: (hv_cmdq_len %d, tag %d bio %lx, last %d)\n",
//MK			__func__, hv_cmdq_len, tag, bio, *is_last_segment);
//MK-begin
		hv_log("%s: (hv_cmdq_len %d, tag %d bio %lx, last %d)\n",
			__func__, hv_cmdq_len.counter, tag, bio, *is_last_segment);
//MK-end
		return bio;
}

void hv_cmdq_queue_full_wait(void)
{
		int err_cnt;

		err_cnt = 0;
		while (atomic_read(&hv_cmdq_len) >= get_queue_size()) {
			udelay(100);
			pr_notice("%s: waiting for queue flushed\n", __func__);
		}
}
