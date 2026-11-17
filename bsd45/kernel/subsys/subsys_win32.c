/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * subsys_win32.c - minimal Win32 personality (S20).
 *
 * The Win32 API is not a syscall table: the DLLs normally translate
 * CreateFile/ReadFile/... into Nt* calls.  This personality is the kernel
 * side of a tiny Win32 surface used for bring-up and tests:
 *
 *	0x1001 GetCurrentProcessId
 *	0x1002 GetCurrentThreadId
 *	0x1003 CreateFileA(path, access, share, sa, disposition, flags, tmpl)
 *	0x1004 ReadFile(handle, buf, len, &read, ov)
 *	0x1005 WriteFile(handle, buf, len, &written, ov)
 *	0x1006 CloseHandle(handle)
 *	0x1007 Sleep(ms)
 *
 * Handles are the native fd plus 0x100 so they cannot be confused with the
 * UNIX personality's descriptors.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "subsys/subsys.h"

#define	WIN_HANDLE_BASE	0x100

/* 中文：以四个参数组装 sysregs 寄存器结构，调用底层 BSD45 系统调用并返回 eax 中的结果 */
/* EN: Fill a sysregs structure with four arguments, invoke the underlying BSD45 syscall and return the result in eax */
static int
win_bsd(unsigned nr, unsigned a, unsigned b, unsigned c)
{
	struct sysregs t;

	bzero(&t, sizeof(t));
	t.eax = nr;
	t.ebx = a;
	t.ecx = b;
	t.edx = c;
	(void)bsd45_syscall(&t);
	return ((int)t.eax);
}

/* 中文：Win32 personality 的入口分派器，把 GetCurrentProcessId/CreateFileA/ReadFile 等 API 号映射到对应的 BSD 系统调用 */
/* EN: Win32 personality entry dispatcher that maps API numbers such as GetCurrentProcessId/CreateFileA/ReadFile to BSD syscalls */
int
win32_dispatch(struct sysregs *r)
{
	unsigned api = r->eax;

	if (api < 0x1000)
		return (-1);
	switch (api) {
	case 0x1001:			/* GetCurrentProcessId */
	case 0x1002: {			/* GetCurrentThreadId */
		int pid = win_bsd(20, 0, 0, 0);

		if (pid < 0) {
			r->eax = 0;
			return (0);
		}
		r->eax = (unsigned)pid;
		return (0);
	}
	case 0x1003: {			/* CreateFileA */
		unsigned path = r->ebx, access = r->ecx;
		unsigned disp = r->esi;
		int flags = 0, fd;

		if ((access & 0x80000000u) && (access & 0x40000000u))
			flags = 2;		/* O_RDWR */
		else if (access & 0x40000000u)
			flags = 1;		/* O_WRONLY */
		else
			flags = 0;		/* O_RDONLY */
		if (disp == 2)			/* CREATE_ALWAYS */
			flags |= 0x600;		/* O_CREAT|O_TRUNC */
		else if (disp == 4)		/* OPEN_ALWAYS */
			flags |= 0x200;		/* O_CREAT */
		fd = win_bsd(5, path, (unsigned)flags, 0644);
		r->eax = (fd < 0) ? 0xffffffffu :
		    (unsigned)(fd + WIN_HANDLE_BASE);
		return (0);
	}
	case 0x1004: {			/* ReadFile */
		unsigned h = r->ebx, buf = r->ecx, len = r->edx;
		unsigned readp = r->edi;
		int n, fd = (int)h - WIN_HANDLE_BASE;

		n = (fd < 0) ? -1 : win_bsd(3, (unsigned)fd, buf, len);
		if (n < 0) {
			r->eax = 0;
			return (0);
		}
		if (readp != 0)
			*(unsigned *)readp = (unsigned)n;
		r->eax = 1;
		return (0);
	}
	case 0x1005: {			/* WriteFile */
		unsigned h = r->ebx, buf = r->ecx, len = r->edx;
		unsigned writep = r->edi;
		int n, fd = (int)h - WIN_HANDLE_BASE;

		n = (fd < 0) ? -1 : win_bsd(4, (unsigned)fd, buf, len);
		if (n < 0) {
			r->eax = 0;
			return (0);
		}
		if (writep != 0)
			*(unsigned *)writep = (unsigned)n;
		r->eax = 1;
		return (0);
	}
	case 0x1006: {			/* CloseHandle */
		int fd = (int)r->ebx - WIN_HANDLE_BASE;

		if (fd < 0) {
			r->eax = 0;
			return (0);
		}
		r->eax = (win_bsd(6, (unsigned)fd, 0, 0) == 0) ? 1 : 0;
		return (0);
	}
	case 0x1007: {			/* Sleep(ms) */
		unsigned ms = r->ebx, i;

		for (i = 0; i < ms * 1000u; i++)
			;
		r->eax = 0;
		return (0);
	}
	default:
		return (-1);
	}
}
