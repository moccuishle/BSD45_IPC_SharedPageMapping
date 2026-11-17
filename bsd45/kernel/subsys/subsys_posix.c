/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * subsys_posix.c - POSIX personality (S20).
 *
 * POSIX does not standardise syscall numbers; this personality reuses the
 * native numbering but only admits the POSIX surface (open/read/write/fork/
 * exec/wait/stat/pthread-supporting calls).  Non-POSIX calls such as vfork
 * are rejected, which makes the filtering visible to tests.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "subsys/subsys.h"

/* 中文：判断系统调用号是否属于允许的 POSIX 接口集合，属于则允许否则拒绝 */
/* EN: Test whether a syscall number belongs to the allowed set of POSIX interfaces, admitting POSIX calls and rejecting others */
static int
posix_allowed(int nr)
{

	switch (nr) {
	case 1: case 2: case 3: case 4: case 5: case 6: case 7:
	case 9: case 10: case 12:
	case 20: case 24: case 25: case 33: case 37:
	case 41: case 42: case 43: case 46: case 47: case 48:
	case 54: case 59: case 60:
	case 65: case 73: case 74:
	case 81: case 82: case 89: case 90: case 92:
	case 116: case 128:
	case 136: case 137: case 164:
	case 188: case 189: case 190: case 196: case 197: case 199:
	case 326:
		return (1);
	default:
		return (0);
	}
}

/* 中文：POSIX personality 的系统调用分派：非 POSIX 调用直接返回 ENOSYS，否则转交 bsd45_syscall 执行 */
/* EN: System-call dispatch for the POSIX personality: return ENOSYS for non-POSIX calls, otherwise hand off to bsd45_syscall */
int
posix_dispatch(struct sysregs *r)
{

	if (!posix_allowed((int)r->eax)) {
		r->eax = (unsigned)-78;		/* ENOSYS */
		return (0);
	}
	(void)bsd45_syscall(r);
	return (0);
}
