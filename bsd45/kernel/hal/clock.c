/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * clock.c - PIT channel 0 at 100 Hz, delivered through the IPL layer.
 *
 * This is the hardware event that makes the scheduler preemptive: the IRQ
 * handler enters at IPL_CLOCK, accounts the tick, sends EOI, and asks
 * sched_tick() to switch user threads when the quantum expired.  Because
 * the interrupt gate cleared IF, the handler itself cannot be preempted.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "hal/ipl.h"
#include "hal/clock.h"
#include "subsys/subsys.h"

extern void sched_tick(struct sysregs *r);
extern int need_resched;
extern unsigned sched_switches;

#define	CLOCK_HZ	100

static unsigned	clock_ticks;
static unsigned	clock_quantum = 2;	/* ticks per scheduling slice */
static unsigned	lat_last, lat_min = ~0u, lat_max, lat_sum, lat_n;

/*
 * Tick-to-tick TSC jitter.  This is not a hardware IRQ latency probe, but it
 * exposes the cost of masking the PIT at higher IPLs and of long kernel
 * sections; the caller prints min/max/average cycles.
 * 相邻时钟节拍的 TSC 抖动。它不是硬件中断延迟探针，但能暴露高 IPL 屏蔽
 * PIT 以及长内核区间带来的代价；调用者打印最小/最大/平均周期数。
 */
/* 中文：打印相邻时钟节拍的 TSC 抖动统计（最小/最大/平均周期数）；这不是硬件中断延迟探针，但能反映高 IPL 屏蔽 PIT 与长内核区间带来的代价。 */
/* EN: Print TSC jitter statistics between consecutive clock ticks (min/max/avg cycles); not a hardware IRQ latency probe, but it exposes the cost of masking the PIT at high IPLs and of long kernel sections. */
void
clock_lat_print(void)
{

	printf("lat: samples %d min %d max %d avg %d cycles\n",
	    lat_n, lat_n ? lat_min : 0, lat_max,
	    lat_n ? lat_sum / lat_n : 0);
}

/* EN: Return the cumulative 100 Hz PIT tick counter that clock_irq() increments on every IRQ0. */
/* 中文：返回 clock_irq() 在每次 IRQ0 时递增的 100 Hz 累计时钟节拍数。 */
unsigned
clock_get_ticks(void)
{

	return (clock_ticks);
}

/* EN: Return the scheduling quantum in ticks (default 2) that clock_irq() uses to decide when to request a reschedule. */
/* 中文：返回调度时间片的节拍数（默认 2），clock_irq() 据此判断何时请求重新调度。 */
unsigned
clock_quantum_get(void)
{

	return (clock_quantum);
}

/*
 * IRQ0.  Called from irq0_entry() with the interrupted context as a
 * struct sysregs on the kernel stack.
 */
/* EN: IRQ0: raise IPL_CLOCK, bump tick and hard-event counters, set need_resched on quantum expiry, send PIC EOI, then sched_tick() if user mode. */
/* 中文：IRQ0：提升到 IPL_CLOCK，累加节拍与硬中断计数，时间片到期时置 need_resched，先发 PIC EOI，再对用户态上下文调用 sched_tick()。 */
void
clock_irq(struct sysregs *r)
{
	int old;

	old = ipl_enter(IPL_CLOCK);
	clock_ticks++;
	ipl_hard_events++;
	{
		unsigned now = hal_rdtsc();

		if (lat_last != 0) {
			unsigned d = now - lat_last;

			if (d < lat_min)
				lat_min = d;
			if (d > lat_max)
				lat_max = d;
			lat_sum += d;
			lat_n++;
		}
		lat_last = now;
	}

	if (clock_quantum == 0 || (clock_ticks % clock_quantum) == 0)
		need_resched = 1;

	/* end of interrupt before we possibly switch address spaces */
	hal_clock_eoi();

	if (r != 0 && (r->cs & 3) == 3)
		sched_tick(r);
	ipl_exit(old);
}

/* dispatch entry for ipl_print()/softirq bookkeeping; IRQ0 bypasses it */
/* EN: Empty stub registered as the IPL_CLOCK action for ipl_print()/softirq bookkeeping; the IRQ0 path calls clock_irq() and bypasses it. */
/* 中文：注册为 IPL_CLOCK 的空动作桩，仅供 ipl_print()/软中断统计使用；IRQ0 路径直接调用 clock_irq() 而绕过它。 */
static void
clock_action(void *arg)
{

	(void)arg;
}

/* EN: Register the IPL_CLOCK action and let the arch backend install/program the timer. */
/* 中文：注册 IPL_CLOCK 动作，并由架构后端安装/编程定时器。 */
void
clock_init(void)
{

	ipl_register(IPL_CLOCK, clock_action, 0, "clock");
	hal_clock_init();
	printf("clock: %d Hz, IRQ0 -> IPL %d, quantum %d ticks\n",
	    CLOCK_HZ, IPL_CLOCK, clock_quantum);
}

/* EN: Print tick counter, PIT rate, quantum and switch count, then the IRQ0 handler run count from ipl_runs(IPL_CLOCK). */
/* 中文：打印节拍计数、PIT 频率、时间片与切换次数，再用 ipl_runs(IPL_CLOCK) 打印 IRQ0 处理函数的运行次数。 */
void
clock_print(void)
{

	printf("clock: ticks %d hz %d quantum %d switches %d\n",
	    clock_ticks, CLOCK_HZ, clock_quantum, sched_switches);
	printf("clock: irq0 runs %d\n", ipl_runs(IPL_CLOCK));
}
