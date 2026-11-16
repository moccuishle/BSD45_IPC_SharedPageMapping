/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * spin.c - os.md mechanism 4 acceptance: two CPU-bound user processes must
 * interleave under the PIT-driven preemptive scheduler.  Each process burns
 * CPU without making syscalls except one write per round, so only the clock
 * interrupt can switch between them.  The output letters are the evidence:
 * without preemption it would be all A's followed by all B's.
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

static unsigned
getticks(void)
{
	unsigned r;

	__asm__ volatile("int $0x80" : "=a"(r) : "a"(430) : "memory");
	return (r);
}

static void
burn(void)
{
	volatile unsigned i;

	for (i = 0; i < 300000; i++)
		;
}

static void
letter(int c)
{

	write(1, &c, 1);
}

int
main(void)
{
	unsigned t0, t1;
	pid_t p;
	int i, st;

	t0 = getticks();
	p = fork();
	if (p < 0) {
		printf("spin: fork FAIL\n");
		return (1);
	}
	if (p == 0) {
		for (i = 0; i < 30; i++) {
			burn();
			letter('A');
		}
		_exit(0);
	}
	for (i = 0; i < 30; i++) {
		burn();
		letter('B');
	}
	wait4(-1, &st, 0, (struct rusage *)0);
	t1 = getticks();
	printf("\nspin: ticks %d -> %d\n", (int)t0, (int)t1);
	printf("spin: timer %s\n", t1 > t0 ? "ok" : "FAIL");
	printf("spin: %s\n", t1 > t0 ? "PASS" : "FAIL");
	return (t1 > t0 ? 0 : 1);
}
