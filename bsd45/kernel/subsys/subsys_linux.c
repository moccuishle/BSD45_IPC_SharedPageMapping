/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * subsys_linux.c - Linux i386 personality (S20).
 *
 * Translates Linux int 0x80 call numbers into the native 4.5BSD service ABI.
 * The arguments in ebx/ecx/edx/esi/edi are already in the same registers, so
 * most translations are a single number rewrite; exit_group is folded into
 * exit and mmap2 into mmap (the port has no page-offset mappings anyway).
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "subsys/subsys.h"

struct linux_map_entry {
	int	l_nr;
	int	l_bsd;
};

static const struct linux_map_entry linux_map_tab[] = {
	{ 1, 1 },	/* exit */
	{ 2, 2 },	/* fork */
	{ 3, 3 },	/* read */
	{ 4, 4 },	/* write */
	{ 5, 5 },	/* open */
	{ 6, 6 },	/* close */
	{ 9, 9 },	/* link */
	{ 10, 10 },	/* unlink */
	{ 11, 59 },	/* execve */
	{ 12, 12 },	/* chdir */
	{ 19, 199 },	/* lseek */
	{ 20, 20 },	/* getpid */
	{ 33, 33 },	/* access */
	{ 37, 37 },	/* kill */
	{ 39, 136 },	/* mkdir */
	{ 40, 137 },	/* rmdir */
	{ 41, 41 },	/* dup */
	{ 42, 42 },	/* pipe */
	{ 45, 17 },	/* brk */
	{ 54, 54 },	/* ioctl */
	{ 55, 92 },	/* fcntl */
	{ 57, 82 },	/* setpgid */
	{ 60, 60 },	/* umask */
	{ 63, 90 },	/* dup2 */
	{ 65, 81 },	/* getpgrp */
	{ 78, 116 },	/* gettimeofday */
	{ 91, 73 },	/* munmap */
	{ 114, 7 },	/* wait4 */
	{ 122, 164 },	/* uname */
	{ 125, 74 },	/* mprotect */
	{ 192, 197 },	/* mmap2 */
	{ 252, 1 },	/* exit_group -> exit */
};

int
linux_map(int linux_nr)
{
	unsigned i;

	for (i = 0; i < sizeof(linux_map_tab) / sizeof(linux_map_tab[0]); i++)
		if (linux_map_tab[i].l_nr == linux_nr)
			return (linux_map_tab[i].l_bsd);
	return (-1);
}

int
linux_dispatch(struct sysregs *r)
{
	int bsd = linux_map((int)r->eax);

	if (bsd < 0) {
		r->eax = (unsigned)-38;		/* ENOSYS */
		return (0);
	}
	r->eax = (unsigned)bsd;
	(void)bsd45_syscall(r);
	return (0);
}
