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

#define HV_PROFILE_BR 0
#define HV_PROFILE_BW 1
#define HV_PROFILE_MR 2
#define HV_PROFILE_MW 3
#define HV_PROFILE_TEMP1	4
#define HV_PROFILE_TEMP2	5
#define HV_PROFILE_TEMP3	6
#define HV_PROFILE_TEMP4	7
#define HV_PROFILE_TEMP5	8

#define MAX_NUM_PROFILE_SET 9


int hv_profile_add(unsigned int id, ktime_t time, unsigned int size);
int hv_profile_init(void);
int hv_profile_print(void);

#define HV_PROFILE_START() ktime_t hv_profile_time_start = ktime_get()
#define HV_PROFILE_RESET() hv_profile_time_start = ktime_get()
#define HV_PROFILE_END(a,b) hv_profile_add(a, ktime_sub(ktime_get(),hv_profile_time_start), b) // a: id, b: data size
#define HV_PROFILE_PRINT() hv_profile_print()

