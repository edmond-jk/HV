
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
#include "hvdimm.h"
#include "hv_timer.h"

/* -----  Function Declatations ---- */

static void hv_init_timer(void);
static inline void hv_set_timer_data(void __iomem *dst, void *src, int count, int async, void (*callback)(int tag, int err), int tag);
#if USE_HRTIMER
enum hrtimer_restart hv_memcpy_toio_callback(struct hrtimer *my_timer);
enum hrtimer_restart hv_memcpy_fromio_callback(struct hrtimer *my_timer);
static void hv_start_timer(int tag, enum hrtimer_restart (*timer_callback_fn)(struct hrtimer *));
#else
void hv_memcpy_toio_callback(unsigned long hv_timer_data_p);
void hv_memcpy_fromio_callback(unsigned long hv_timer_data_p);
static void hv_start_timer(int tag, void (*timer_callback_fn)(unsigned long));
#endif

/* -----  Structure Definitions ---- */

struct hv_timer_data_t {
#if USE_HRTIMER
	struct hrtimer timer;
#endif
	int tag;
	void *dst;
	void *src;
	int count;
	void (*callback)(int tag, int err);
} hv_timer_data[HV_Q_DEPTH];

const struct hv_timer_operations hv_timer_ops = {
       .init 			= hv_init_timer,
       .set_timer_data 	= hv_set_timer_data,
       .start 			= hv_start_timer,
       .memcpy_toio_callback   =  hv_memcpy_toio_callback,
       .memcpy_fromio_callback =  hv_memcpy_fromio_callback
};

/* -----  Timer Functions  ---- */

static inline void hv_set_timer_data(void __iomem *dst, void *src, int count,
	int async, void (*callback)(int tag, int err), int tag)
{
	hv_timer_data[tag].tag = tag;
	hv_timer_data[tag].dst = dst;
	hv_timer_data[tag].src = src;
	hv_timer_data[tag].count = count;
	hv_timer_data[tag].callback = callback;
}

#if USE_HRTIMER  /* -- hrtimer functions -- */
static void hv_init_timer(void)
{
	int i;

	for (i = 0; i < HV_Q_DEPTH; i++)
		hrtimer_init(&hv_timer_data[i].timer, CLOCK_REALTIME,
				HRTIMER_MODE_REL);
}

enum hrtimer_restart hv_memcpy_toio_callback(struct hrtimer *my_timer)
{
	struct hv_timer_data_t *hv_p = container_of(my_timer,
					struct hv_timer_data_t, timer);

	memcpy_toio(hv_p->dst, hv_p->src, hv_p->count);
	(hv_p->callback)(hv_p->tag, 0);
	return HRTIMER_NORESTART;
}

enum hrtimer_restart hv_memcpy_fromio_callback(struct hrtimer *my_timer)
{
	struct hv_timer_data_t *hv_p = container_of(my_timer,
				struct hv_timer_data_t, timer);

	memcpy_fromio(hv_p->dst, hv_p->src, hv_p->count);
	(hv_p->callback)(hv_p->tag, 0);

	return HRTIMER_NORESTART;
}

static void hv_start_timer(int tag, enum hrtimer_restart (*timer_callback_fn)(struct hrtimer *))
{
	hrtimer_init(&hv_timer_data[tag].timer,
		CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hv_timer_data[tag].timer.function = timer_callback_fn;
	hrtimer_start(&hv_timer_data[tag].timer,
		ktime_set(0, 1000), HRTIMER_MODE_REL);
}

#else /* -- cmd timer functions -- */

static int delay = 1;
static struct timer_list bsm_timer[HV_Q_DEPTH];

static void hv_init_timer(void)
{
	int i;

	for (i = 0; i < HV_Q_DEPTH; i++)
		init_timer(&bsm_timer[i]);
}

void hv_memcpy_toio_callback(unsigned long hv_timer_data_p)
{
	struct hv_timer_data_t *hv_p = (struct hv_timer_data_t *)hv_timer_data_p;

	memcpy_toio(hv_p->dst, hv_p->src, hv_p->count);
	(hv_p->callback)(hv_p->tag, 0);
}

void hv_memcpy_fromio_callback(unsigned long hv_timer_data_p)
{
	struct hv_timer_data_t *hv_p = (struct hv_timer_data_t *)hv_timer_data_p;

	memcpy_fromio(hv_p->dst, hv_p->src, hv_p->count);
	(hv_p->callback)(hv_p->tag, 0);
}

static void hv_start_timer(int tag, void (*timer_callback_fn)(unsigned long))
{

	init_timer(&bsm_timer[tag]);
	bsm_timer[tag].function = timer_callback_fn;
	bsm_timer[tag].expires = jiffies+delay;
	bsm_timer[tag].data = (unsigned long)&hv_timer_data[tag];
	add_timer(&bsm_timer[tag]);
}
#endif

