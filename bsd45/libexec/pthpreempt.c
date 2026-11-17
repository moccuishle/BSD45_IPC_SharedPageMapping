/*
 * pthpreempt.c - manual demo of kernel-mode thread preemption (KLT).
 *
 * Two threads in the SAME process burn CPU and print their letter.  Since
 * both share one address space but have separate TCBs/kern stacks, the PIT
 * preempts between them: output interleaves A/B instead of AAAA...BBBB.
 * Also prints each thread's effective priority via syscall 447.
 */
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static volatile int run_a, run_b;

static void
burn(void)
{
	volatile unsigned i;

	for (i = 0; i < 200000; i++)
		;
}

static void *
worker_a(void *arg)
{
	int i, p;

	(void)arg;
	p = sched_getprio();
	printf("A: start prio %d\n", p);
	for (i = 0; i < 12; i++) {
		burn();
		write(1, "A", 1);
	}
	run_a = 1;
	printf("\nA: done\n");
	return (0);
}

static void *
worker_b(void *arg)
{
	int i, p;

	(void)arg;
	p = sched_getprio();
	printf("B: start prio %d\n", p);
	for (i = 0; i < 12; i++) {
		burn();
		write(1, "B", 1);
	}
	run_b = 1;
	printf("\nB: done\n");
	return (0);
}

int
main(void)
{
	pthread_t a, b;
	int ok = 1;

	printf("pthpreempt: two threads, one process\n");
	printf("pthpreempt: main prio %d\n", sched_getprio());
	if (pthread_create(&a, 0, worker_a, 0) < 0 ||
	    pthread_create(&b, 0, worker_b, 0) < 0) {
		printf("pthpreempt: create FAIL\n");
		return (1);
	}
	/* main waits: KLT should still interleave A/B while we are blocked */
	pthread_join(a, 0);
	pthread_join(b, 0);
	printf("pthpreempt: A=%d B=%d\n", run_a, run_b);
	if (!run_a || !run_b)
		ok = 0;
	printf("pthpreempt: %s\n", ok ? "PASS" : "FAIL");
	return (ok ? 0 : 1);
}
