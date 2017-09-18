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
#ifndef _HV_TIMER_H_
#define _HV_TIMER_H_

#define USE_HRTIMER   1	/* 1=hrtimer, 0=cmd timer */

#if USE_HRTIMER
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#else
#include <linux/timer.h>
#endif

struct hv_timer_operations {
	void (*init) (void);
	void (*set_timer_data)(void __iomem *dst, void *src, int count, int async,
		void (*callback)(int tag, int err), int tag);
#if USE_HRTIMER
	enum hrtimer_restart (*memcpy_toio_callback)(struct hrtimer *);
	enum hrtimer_restart (*memcpy_fromio_callback)(struct hrtimer *);
	void (*start)(int, enum hrtimer_restart (*timer_callback_fn)(struct hrtimer *));
#else
	void (*memcpy_toio_callback)(unsigned long);
	void (*memcpy_fromio_callback)(unsigned long);
	void (*start)(int, void (*timer_callback_fn)(unsigned long));
#endif
};

extern const struct hv_timer_operations hv_timer_ops;

#endif  /* _HV_TIMER_H_ */
