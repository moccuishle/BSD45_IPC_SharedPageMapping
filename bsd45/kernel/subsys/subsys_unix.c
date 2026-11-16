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

int
unix_dispatch(struct sysregs *r)
{

	(void)bsd45_syscall(r);
	return (0);
}
