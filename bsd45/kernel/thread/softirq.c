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

static void
soft_reclaim(void)
{

	(void)pagecache_reclaim(8);
}

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

void
softint_register(int id, void (*fn)(void))
{

	if (id >= 0 && id < NSOFT)
		soft_handlers[id] = fn;
}

void
softint_schedule(int id)
{

	if (id < 0 || id >= NSOFT)
		return;
	softirq_raise(IPL_SOFT0 + id, softirq_run, (void *)id);
	if (soft_thread != 0)
		kthread_wake(soft_thread);
}

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

unsigned
softirq_count(void)
{

	return (soft_total);
}

/* ---------------------------------------------------------------- test */

static volatile unsigned soft_test_count;

static void
soft_test_handler(void)
{

	soft_test_count++;
}

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
