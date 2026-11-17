/*
 * schedtest.c - verify the 20-level priority scheduler.
 *
 * Priority scale: 0 lowest, 19 highest.  Higher preempts lower.
 * Tests:
 *   1. sched_setprio/getprio round-trip
 *   2. priority inheritance: a high-prio thread joins a low-prio thread;
 *      while the joiner waits, the joinee should run at the donated level.
 */
#include <stdio.h>
#include <pthread.h>

static volatile int order[8];
static volatile int order_n;

static volatile int observed_prio;

static void *
low_worker(void *arg)
{
	int i, p;

	(void)arg;
	/* Busy-yield a few times so the joiner has a chance to join (and
	 * donate its priority) before we sample our effective priority. */
	for (i = 0; i < 5000; i++) {
		p = sched_getprio();
		if (p > observed_prio)
			observed_prio = p;	/* keep the highest seen */
		pthread_yield();
	}
	return ((void *)observed_prio);
}

int
main(void)
{
	pthread_t lo;
	void *ret;
	int self, before, ok = 1;

	/* --- test 1: set/get priority --- */
	before = sched_getprio();
	if (sched_setprio(3) < 0 || sched_getprio() != 3) {
		printf("schedtest: setprio FAIL\n");
		return (1);
	}
	printf("schedtest: setprio ok (%d)\n", sched_getprio());

	/* --- test 2: priority inheritance via join ---
	 * spawn a low-priority worker at normal priority, let it start, then
	 * raise our priority and join: the worker should inherit our level
	 * while we wait, so it finishes quickly and we get a fast response. */
	sched_setprio(10);		/* normal: let worker get CPU first */
	order_n = 0;
	if (pthread_create(&lo, 0, low_worker, 0) < 0) {
		printf("schedtest: create FAIL\n");
		return (1);
	}
	pthread_yield();			/* let the worker start */
	sched_setprio(18);			/* now we outrank the worker */
	self = sched_getprio();
	if (pthread_join(lo, &ret) < 0) {
		printf("schedtest: join FAIL\n");
		return (1);
	}
	printf("schedtest: worker saw prio %d (joiner %d)\n", (int)ret, self);
	/* inherited priority must be at least the joiner's level */
	if ((int)ret < self)
		ok = 0;

	/* after join, our priority is restored to what we set (18) */
	if (sched_getprio() != 18)
		ok = 0;

	(void)before;
	printf("schedtest: %s\n", ok ? "PASS" : "FAIL");
	return (ok ? 0 : 1);
}
