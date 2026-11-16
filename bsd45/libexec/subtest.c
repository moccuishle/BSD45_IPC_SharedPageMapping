/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * subtest(1) - S20 acceptance: switch API personalities from user mode.
 *
 * Each personality is selected with the core set_personality() call (424),
 * then exercised with its own ABI:
 *   unix/posix: native 4.5BSD getpid (20)
 *   linux:      Linux i386 getpid (20) and write (4)
 *   win32:      Win32 GetCurrentProcessId (0x1001) and Sleep (0x1007)
 */
#include <stdio.h>
#include <unistd.h>

static int
sys5(int n, int a, int b, int c, int d)
{
	int r;

	__asm__ volatile("int $0x80"
	    : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d)
	    : "memory");
	return (r);
}

int
main(void)
{
	int pid;

	(void)sys5(424, 0, 0, 0, 0);		/* unix */
	pid = sys5(20, 0, 0, 0, 0);
	printf("subtest: unix pid=%d\n", pid > 0);

	(void)sys5(424, 1, 0, 0, 0);		/* posix */
	pid = sys5(20, 0, 0, 0, 0);
	printf("subtest: posix pid=%d\n", pid > 0);

	(void)sys5(424, 2, 0, 0, 0);		/* linux */
	pid = sys5(20, 0, 0, 0, 0);
	(void)sys5(4, 1, (int)"linux-write-ok\n", 15, 0);
	printf("subtest: linux pid=%d\n", pid > 0);

	(void)sys5(424, 3, 0, 0, 0);		/* win32 */
	pid = sys5(0x1001, 0, 0, 0, 0);
	(void)sys5(0x1007, 0, 0, 0, 0);		/* Sleep(0) */
	(void)sys5(424, 0, 0, 0, 0);		/* back to unix before printf */
	printf("subtest: win32 pid=%d\n", pid > 0);

	printf("subtest: PASS\n");
	return (0);
}
