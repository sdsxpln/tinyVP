/*
 * Copyright (c) 2016 Leonid Yegoshin
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _TIME_H
#define _TIME_H

#define TIME_SECOND         (1000000000ULL)

#define _TIMER_IRQ  0
//#define _TIMER_PRIO 0x1F
#define _TIMER_PRIO 0x1B

struct timer {
	unsigned long long  time;
	unsigned long long  lcount;
	unsigned long       cnt;
	struct timer        *prev;
	struct timer        *next;
	unsigned int        flag;
};

#define TIMER_FLAG_TICK         0x1
#define TIMER_FLAG_ACTIVE       0x2
#define TIMER_FLAG_FUNC         0x4
#define TIMER_FLAG_TIMER_IRQ    0x8
#define TIMER_FLAG_COUNT        0x10

#define TIMER_TICKS_SECOND      (1)
#define TIMER_TICK              (TIME_SECOND/TIMER_TICKS_SECOND)

static unsigned long long time_extend_count(unsigned long long base,
			unsigned int count)
{
    if (count >= (unsigned int)(base & 0xffffffffULL))
	return count | (base & ~0xffffffffULL);
    return count | ((base & ~0xffffffffULL) + 0x100000000LL);
}

void timer_request(struct timer *timer, unsigned long long delay,
	   void (*func)(void));
void timer_g_irq_reschedule(unsigned int timerno, unsigned long long clock);
void time_update_wall_time(void);

extern volatile unsigned long long current_lcount;
extern int need_time_update;

static inline unsigned long long get_wall_time(void)
{
	extern volatile unsigned long long current_wall_time;

	if (need_time_update)
		time_update_wall_time();
	return current_wall_time;
}


#endif
