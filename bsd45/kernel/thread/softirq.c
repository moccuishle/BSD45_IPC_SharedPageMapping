/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * softirq.c - deferred work queue (replaces the old IPL-based softirq).
 *
 * A small fixed queue of (fn,arg) callbacks plus per-id soft handlers.  A
 * kernel thread drains the queue on its own stack; syscall exit also calls
 * softirq_dispatch() as a safe point.  No interrupt priority levels.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "sys/platform.h"
#include "thread/kthread.h"

#define	NDEFER	64

static void		(*soft_handlers[NSOFT])(void);
static unsigned		soft_runs[NSOFT];
static struct kthread	*soft_thread;
static unsigned		soft_total;

/* deferred (fn,arg) queue */
static void		(*defer_fn[NDEFER])(void *);
static void		*defer_arg[NDEFER];
static unsigned	defer_head, defer_tail, defer_pending;

/* EN: Run one deferred callback. */
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

/* EN: Drain all deferred callbacks and run pending soft handlers. */
void
softirq_dispatch(void)
{
	void (*fn)(void *);
	void *arg;

	while (defer_head != defer_tail) {
		fn = defer_fn[defer_head];
		arg = defer_arg[defer_head];
		defer_fn[defer_head] = 0;
		defer_arg[defer_head] = 0;
		defer_head = (defer_head + 1) % NDEFER;
		defer_pending--;
		if (fn != 0)
			fn(arg);
	}
}

/* EN: Softirq kernel thread: dispatch deferred work, else block. */
static void
softirq_thread(void *arg)
{

	(void)arg;
	for (;;) {
		if (defer_pending != 0)
			softirq_dispatch();
		else
			kthread_block();
	}
}

/* EN: Page-reclaim softirq handler: reclaim 8 pages from the page cache. */
static void
soft_reclaim(void)
{

	(void)pagecache_reclaim(8);
}

/* EN: Initialize softirqs: clear tables and spawn the softirq kernel thread. */
void
softirq_init(void)
{

	soft_total = 0;
	defer_head = defer_tail = defer_pending = 0;
	bzero(soft_handlers, sizeof(soft_handlers));
	bzero(soft_runs, sizeof(soft_runs));
	bzero(defer_fn, sizeof(defer_fn));
	bzero(defer_arg, sizeof(defer_arg));
	soft_handlers[SOFT_RECLAIM] = soft_reclaim;
	soft_thread = kthread_spawn("softirq", softirq_thread, 0);
	if (soft_thread == 0)
		printf("softirq: no kernel thread\n");
}

/* EN: Queue a deferred callback (fn,arg); wakes the softirq thread. */
void
softirq_defer(void (*fn)(void *), void *arg)
{
	unsigned next;

	if (fn == 0)
		return;
	next = (defer_tail + 1) % NDEFER;
	if (next == defer_head)		/* queue full: drop */
		return;
	defer_fn[defer_tail] = fn;
	defer_arg[defer_tail] = arg;
	defer_tail = next;
	defer_pending++;
	if (soft_thread != 0)
		kthread_wake(soft_thread);
}

/* EN: Register handler fn for softirq id, ignoring out-of-range ids. */
void
softint_register(int id, void (*fn)(void))
{

	if (id >= 0 && id < NSOFT)
		soft_handlers[id] = fn;
}

/* EN: Schedule softirq id by queuing softirq_run(id) and waking the thread. */
void
softint_schedule(int id)
{

	if (id < 0 || id >= NSOFT)
		return;
	softirq_defer(softirq_run, (void *)id);
}

/* EN: Print softirq statistics. */
void
softirq_print(void)
{
	int i;

	printf("softirq: total %d pending %d thread %s\n", soft_total,
	    defer_pending, soft_thread ? "up" : "down");
	for (i = 0; i < NSOFT; i++)
		printf("softirq: %d runs %d handler %s\n", i, soft_runs[i],
		    soft_handlers[i] ? "yes" : "no");
}

/* EN: Return the total number of softirq executions. */
unsigned
softirq_count(void)
{

	return (soft_total);
}

/* ---------------------------------------------------------------- test */

static volatile unsigned soft_test_count;

/* EN: Softirq handler for self-test: increment soft_test_count. */
static void
soft_test_handler(void)
{

	soft_test_count++;
}

/* EN: Softirq self-test: schedule SOFT_TEST and spin until it fires. */
int
softirq_selftest(void)
{
	int i;

	soft_test_count = 0;
	softint_register(SOFT_TEST, soft_test_handler);
	softint_schedule(SOFT_TEST);
	for (i = 0; i < 100 && soft_test_count == 0; i++) {
		softirq_dispatch();
		kthread_run();
	}
	softint_register(SOFT_TEST, 0);
	return ((int)soft_test_count);
}
