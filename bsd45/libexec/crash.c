/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * crash.c - os.md mechanism 1 acceptance: a user program that dereferences
 * NULL must die with SIGSEGV while the kernel (and the shell) survives.
 */
#include <stdio.h>

int
main(void)
{

	printf("crash: about to fault\n");
	*(volatile int *)0 = 1;
	printf("crash: survived the fault (FAIL)\n");
	return (0);
}
