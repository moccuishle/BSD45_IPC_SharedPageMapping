/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * badptr.c - os.md mechanism 1 negative test: a user pointer that is inside
 * the user address window but not mapped (or mapped read-only for output)
 * must return -EFAULT instead of faulting in the kernel.
 */
#include <stdio.h>

#define	EFAULT	14

static int
sys_write(int fd, const char *b, unsigned n)
{
	int r;

	__asm__ volatile("int $0x80" : "=a"(r)
	    : "a"(4), "b"(fd), "c"(b), "d"(n) : "memory");
	return (r);
}

static int
sys_read(int fd, char *b, unsigned n)
{
	int r;

	__asm__ volatile("int $0x80" : "=a"(r)
	    : "a"(3), "b"(fd), "c"(b), "d"(n) : "memory");
	return (r);
}

int
main(void)
{
	int ok = 1, r;

	/* 0x70000000 is in the user window but has no page table entry */
	r = sys_write(1, (const char *)0x70000000, 16);
	printf("badptr: unmapped write -> %d\n", r);
	if (r != -EFAULT)
		ok = 0;

	/* the text page is mapped read-only: output to it must fail */
	r = sys_read(0, (char *)0x08048000, 4);
	printf("badptr: read into text -> %d\n", r);
	if (r != -EFAULT)
		ok = 0;

	printf("badptr: %s\n", ok ? "PASS" : "FAIL");
	return (ok ? 0 : 1);
}
