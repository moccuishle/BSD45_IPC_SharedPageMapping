/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * softirq.c - deferred work on the unified IPL bitmap (S21 + os.md M4).
 *
 * Devices and the VM layer only mark an IPL bit (softirq_raise); the same
 * ipl_dispatch() loop that serves hardware events serves them, high to low.
 * A kernel thread is still the preferred carrier: it runs the dispatcher on
 * its own stack, so page reclaim does not happen on a user thread's stack.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "hal/ipl.h"
#include "thread/kthread.h"

#define	SOFT_MASK	((1u << IPL_SOFT0) | (1u << IPL_SOFT1) | \
			 (1u << IPL_SOFT2) | (1u << IPL_SOFT3))

static void		(*soft_handlers[NSOFT])(void);
static unsigned		soft_runs[NSOFT];
static struct kthread	*soft_thread;
static unsigned		soft_total;

/* 中文：软中断执行体：校验 id 范围、递增总/分项运行计数，并调用 id 对应的已注册处理函数 */
/* EN: Softirq runner: validate the id range, bump total/per-id run counters and call the registered handler for id */
static void
softirq_run(void *arg)
{
	int id = (int)arg;

	if (id < 0 || id >= NSOFT)
		return;
	soft_total++;
	soft_runs[id]++;
	if (soft_handlers[id] != 0)
		soft_handlers[id]();
}

/* 中文：软中断内核线程：循环检查 pending 的软 IPL 位，有则调用 ipl_dispatch 处理，否则阻塞等待 */
/* EN: Softirq kernel thread: loop checking for pending soft IPL bits, dispatch via ipl_dispatch when present, else block waiting */
static void
softirq_thread(void *arg)
{

	(void)arg;
	for (;;) {
		if ((ipl_pending() & SOFT_MASK) != 0)
			(void)ipl_dispatch();
		else
			kthread_block();
	}
}

/* 中文：页回收软中断处理：调用 pagecache_reclaim 回收 8 页 */
/* EN: Page-reclaim softirq handler: call pagecache_reclaim to reclaim 8 pages */
static void
soft_reclaim(void)
{

	(void)pagecache_reclaim(8);
}

/* 中文：初始化软中断：清零处理函数表与计数、注册页回收处理函数，并生成软中断内核线程 */
/* EN: Initialize softirqs: clear the handler table and counters, register the page-reclaim handler and spawn the softirq kernel thread */
void
softirq_init(void)
{

	soft_total = 0;
	bzero(soft_handlers, sizeof(soft_handlers));
	bzero(soft_runs, sizeof(soft_runs));
	soft_handlers[SOFT_RECLAIM] = soft_reclaim;
	soft_thread = kthread_spawn("softirq", softirq_thread, 0);
	if (soft_thread == 0)
		printf("softirq: no kernel thread\n");
}

/* 中文：注册软中断 id 的处理函数 fn，id 越界则忽略 */
/* EN: Register handler fn for softirq id, ignoring out-of-range ids */
void
softint_register(int id, void (*fn)(void))
{

	if (id >= 0 && id < NSOFT)
		soft_handlers[id] = fn;
}

/* 中文：调度软中断 id：以 softirq_run 为回调 raise 对应的软 IPL 位，并唤醒软中断线程 */
/* EN: Schedule softirq id: raise its soft IPL bit with softirq_run as callback and wake the softirq thread */
void
softint_schedule(int id)
{

	if (id < 0 || id >= NSOFT)
		return;
	softirq_raise(IPL_SOFT0 + id, softirq_run, (void *)id);
	if (soft_thread != 0)
		kthread_wake(soft_thread);
}

/* 中文：打印软中断统计：总数、pending 掩码、线程状态以及各 id 的运行次数与是否有处理函数 */
/* EN: Print softirq statistics: total, pending mask, thread state and per-id run counts and handler presence */
void
softirq_print(void)
{
	int i;

	printf("softirq: total %d pending %x thread %s\n", soft_total,
	    ipl_pending() & SOFT_MASK, soft_thread ? "up" : "down");
	for (i = 0; i < NSOFT; i++)
		printf("softirq: %d runs %d handler %s\n", i, soft_runs[i],
		    soft_handlers[i] ? "yes" : "no");
}

/* 中文：返回软中断累计执行次数 total */
/* EN: Return the total number of softirq executions */
unsigned
softirq_count(void)
{

	return (soft_total);
}

/* ---------------------------------------------------------------- test */

static volatile unsigned soft_test_count;

/* 中文：自测用软中断处理函数：递增 soft_test_count */
/* EN: Softirq handler for self-test: increment soft_test_count */
static void
soft_test_handler(void)
{

	soft_test_count++;
}

/* 中文：软中断自测：注册并调度 SOFT_TEST，最多循环 100 次 kthread_run 等待其执行，注销后返回计数 */
/* EN: Softirq self-test: register and schedule SOFT_TEST, run kthread_run up to 100 times until it fires, unregister and return the count */
int
softirq_selftest(void)
{
	int i;

	soft_test_count = 0;
	softint_register(SOFT_TEST, soft_test_handler);
	softint_schedule(SOFT_TEST);
	for (i = 0; i < 100 && soft_test_count == 0; i++)
		kthread_run();
	softint_register(SOFT_TEST, 0);
	return ((int)soft_test_count);
}
