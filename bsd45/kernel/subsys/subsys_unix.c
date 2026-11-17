/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * subsys_unix.c - the native 4.5BSD ABI personality (S20).
 *
 * This is the default personality and the reference service ABI: it simply
 * forwards to bsd45_syscall(), which is the kernel's internal syscall switch.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "subsys/subsys.h"

/* 中文：原生 4.5BSD ABI personality 的系统调用分发入口，直接转发到内核内部的 bsd45_syscall 开关 */
/* EN: Native 4.5BSD ABI syscall dispatch entry that simply forwards to the kernel's internal bsd45_syscall switch */
int
unix_dispatch(struct sysregs *r)
{

	(void)bsd45_syscall(r);
	return (0);
}
