/*
 *
 *  HVDIMM Command driver for BSM/MMLS.
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

#include <linux/ktime.h>
#include <linux/spinlock.h>

#include "hv_profile.h"


typedef struct hv_profile_t {

	spinlock_t lock;
	ktime_t time_spent;
	unsigned long data_size;
	unsigned long count;

} HV_PROFILE_t;


static HV_PROFILE_t	profile_data[MAX_NUM_PROFILE_SET];



int hv_profile_init(void) 
{

	int i;
	pr_notice("%s: entered\n",__func__);

	for(i=0;i<MAX_NUM_PROFILE_SET;i++){
		profile_data[i].time_spent = ktime_set(0,0);
		spin_lock_init(&profile_data[i].lock);
		profile_data[i].data_size = 0;
		profile_data[i].count = 0;
	}

	return 0;
}

int hv_profile_add(unsigned int id, ktime_t time, unsigned int size)
{
	
	unsigned long flags;

	if (id >= MAX_NUM_PROFILE_SET )
		return -1;
	spin_lock_irqsave(&profile_data[id].lock, flags);

	profile_data[id].time_spent = ktime_add(profile_data[id].time_spent , time);
	profile_data[id].data_size += size;
	profile_data[id].count++;
	spin_unlock_irqrestore(&profile_data[id].lock, flags);
	return 0;
}

int hv_profile_print(void)
{
	int i=0;
	signed long long ms;
	signed long avg=0;
	
	for(i=0;i<MAX_NUM_PROFILE_SET;i++){
		ms = ktime_to_ms(profile_data[i].time_spent);
		if (profile_data[i].count) {
			avg = ktime_to_ns(profile_data[i].time_spent)/profile_data[i].count;
			pr_info("P Id: %d, time ,%lld, msec, data ,%ld, sectors, count ,%ld, avg ,%ld\n",
				i , ms, profile_data[i].data_size, profile_data[i].count, avg);
		}
	}
	return 0;
}
