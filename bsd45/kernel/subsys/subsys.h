/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * subsys.h - API subsystems (personalities), decoupled from the kernel core.
 *
 * The kernel exports one internal service ABI (the historical 4.5BSD numbers
 * in bsd45_syscall()).  A personality is a small module that translates an
 * external API (UNIX, POSIX, Linux i386, Win32) into that service ABI:
 *
 *	unix   - the native 4.5BSD ABI (default)
 *	posix  - the POSIX subset, with explicit call filtering
 *	linux  - Linux i386 int 0x80 numbering, translated to the native ABI
 *	win32  - a minimal Win32 handle API (CreateFile/ReadFile/...)
 *
 * Personalities know nothing about kernel internals; they only rewrite the
 * syscall number/arguments and call bsd45_syscall().  New personalities are
 * added by subsys_register() without touching the trap entry or the VFS.
 */
#ifndef _SUBSYS_H_
#define _SUBSYS_H_

/* Saved user context, filled by syscall_entry/resume_user. */
struct sysregs {
	unsigned ds, es, edi, esi, ebp, esp, ebx, edx, ecx, eax;
	unsigned eip, cs, eflags, uesp, ss;
};

struct subsys {
	const char	*s_name;	/* ASCII registration name */
	int		(*s_dispatch)(struct sysregs *);
	unsigned	 s_calls;
};

void	subsys_init(void);
int	subsys_register(struct subsys *s);
int	subsys_dispatch(struct sysregs *r);
int	subsys_set(int idx);
int	subsys_active(void);
int	subsys_count(void);
const char *subsys_name(int idx);
void	subsys_print(void);
int	subsys_selftest(void);

/* the kernel service ABI every personality ultimately calls */
int	bsd45_syscall(struct sysregs *r);

/* personality dispatchers */
int	unix_dispatch(struct sysregs *r);
int	posix_dispatch(struct sysregs *r);
int	linux_dispatch(struct sysregs *r);
int	linux_map(int linux_nr);
int	win32_dispatch(struct sysregs *r);

#endif
