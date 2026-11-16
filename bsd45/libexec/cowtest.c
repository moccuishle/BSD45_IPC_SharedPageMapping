/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * cowtest.c - os.md mechanism 2/3 acceptance: fork() is copy-on-write.
 * The child changes a variable on its stack; the parent must still see the
 * old value, and the kernel's COW counters must show at least one fault.
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

static void
getcow(unsigned *v)
{

	__asm__ volatile("int $0x80" :: "a"(431), "b"(v) : "memory");
}

int
main(void)
{
	volatile int shared = 42;
	unsigned v[3];
	pid_t p;
	int st;

	p = fork();
	if (p < 0) {
		printf("cowtest: fork FAIL\n");
		return (1);
	}
	if (p == 0) {
		shared = 99;
		printf("cowtest: child x=%d\n", (int)shared);
		_exit(0);
	}
	wait4(-1, &st, 0, (struct rusage *)0);
	getcow(v);
	printf("cowtest: parent x=%d\n", (int)shared);
	printf("cow: shared %d faults %d copies %d\n",
	    (int)v[0], (int)v[1], (int)v[2]);
	if (shared != 42) {
		printf("cowtest: FAIL (parent page changed)\n");
		return (1);
	}
	if (v[1] == 0 || v[2] == 0) {
		printf("cowtest: FAIL (no COW fault/copy)\n");
		return (1);
	}
	printf("cowtest: PASS\n");
	return (0);
}
