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
#ifndef _HV_QUEUE_H_
#define _HV_QUEUE_H_

extern int hv_next_cmdq_tag(void);
extern int hv_prev_cmdq_tag(void);
extern void hv_queue_cmdq(unsigned long bio, unsigned char is_last_segment);
extern unsigned long hv_dequeue_cmdq(int tag, unsigned char *is_last_segment);
extern void hv_cmdq_queue_full_wait(void);

#endif  /* _HV_QUEUE_H_ */
