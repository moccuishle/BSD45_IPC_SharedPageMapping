/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * subsys.c - personality registry and dispatch (S20).
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "subsys/subsys.h"

extern int strcmp(const char *, const char *);

#define	NSUBSYS	8

static struct subsys	*subsys_tab[NSUBSYS];
static int		 nsubsys;
static int		 subsys_cur;

void
subsys_init(void)
{
	static int inited;
	static struct subsys unix_s = { "unix", unix_dispatch, 0 };
	static struct subsys posix_s = { "posix", posix_dispatch, 0 };
	static struct subsys linux_s = { "linux", linux_dispatch, 0 };
	static struct subsys win32_s = { "win32", win32_dispatch, 0 };

	if (inited)
		return;
	inited = 1;
	nsubsys = 0;
	subsys_cur = 0;
	(void)subsys_register(&unix_s);
	(void)subsys_register(&posix_s);
	(void)subsys_register(&linux_s);
	(void)subsys_register(&win32_s);
}

int
subsys_register(struct subsys *s)
{
	int i;

	if (s == 0 || s->s_name == 0 || s->s_dispatch == 0)
		return (-1);
	for (i = 0; i < nsubsys; i++)
		if (strcmp(subsys_tab[i]->s_name, s->s_name) == 0)
			return (-1);
	if (nsubsys >= NSUBSYS)
		return (-1);
	subsys_tab[nsubsys++] = s;
	return (nsubsys - 1);
}

int
subsys_set(int idx)
{

	if (idx < 0 || idx >= nsubsys)
		return (-1);
	subsys_cur = idx;
	return (0);
}

int
subsys_active(void)
{

	return (subsys_cur);
}

int
subsys_count(void)
{

	return (nsubsys);
}

const char *
subsys_name(int idx)
{

	if (idx < 0 || idx >= nsubsys)
		return ("?");
	return (subsys_tab[idx]->s_name);
}

/*
 * Syscall entry.  Personality selection (424/set, 425/get) is core policy;
 * everything else goes to the active personality.  A personality returns 0
 * if it handled the call, or -1 to get -ENOSYS.
 */
int
subsys_dispatch(struct sysregs *r)
{
	struct subsys *s;
	unsigned nr = r->eax;

	if (nr == 424) {			/* set_personality(idx) */
		int old = subsys_cur;

		if ((int)r->ebx >= 0 && (int)r->ebx < nsubsys)
			subsys_cur = (int)r->ebx;
		else {
			r->eax = (unsigned)-22;	/* EINVAL */
			return (0);
		}
		r->eax = (unsigned)old;
		return (0);
	}
	if (nr == 425) {			/* get_personality() */
		r->eax = (unsigned)subsys_cur;
		return (0);
	}
	s = (subsys_cur >= 0 && subsys_cur < nsubsys) ?
	    subsys_tab[subsys_cur] : 0;
	if (s == 0) {
		r->eax = (unsigned)-78;		/* ENOSYS */
		return (0);
	}
	s->s_calls++;
	if (s->s_dispatch(r) < 0)
		r->eax = (unsigned)-78;
	return (0);
}

void
subsys_print(void)
{
	int i;

	printf("subsys: %d personalities, active %s\n", nsubsys,
	    subsys_name(subsys_cur));
	for (i = 0; i < nsubsys; i++)
		printf("subsys: %d %s calls %d\n", i, subsys_tab[i]->s_name,
		    subsys_tab[i]->s_calls);
}

int
subsys_selftest(void)
{
	struct sysregs r;

	bzero(&r, sizeof(r));
	if (subsys_count() != 4) {
		printf("subsystest: expected 4 personalities, got %d\n",
		    subsys_count());
		return (-1);
	}
	/* umask() is process-independent, so it is safe before pid 1 exists */
	if (subsys_set(0) < 0) {
		printf("subsystest: set unix FAIL\n");
		return (-1);
	}
	r.eax = 60;			/* umask */
	r.ebx = 022;
	(void)subsys_dispatch(&r);
	if ((int)r.eax < 0) {
		printf("subsystest: unix umask FAIL\n");
		return (-1);
	}
	/* POSIX personality admits umask and rejects vfork (66) */
	if (subsys_set(1) < 0) {
		printf("subsystest: set posix FAIL\n");
		return (-1);
	}
	r.eax = 60;
	(void)subsys_dispatch(&r);
	if ((int)r.eax < 0) {
		printf("subsystest: posix umask FAIL\n");
		return (-1);
	}
	r.eax = 66;			/* vfork is not POSIX */
	(void)subsys_dispatch(&r);
	if ((int)r.eax != -78) {
		printf("subsystest: posix filter FAIL\n");
		return (-1);
	}
	/* Linux numbering translation */
	if (subsys_set(2) < 0) {
		printf("subsystest: set linux FAIL\n");
		return (-1);
	}
	if (linux_map(39) != 136 || linux_map(114) != 7 ||
	    linux_map(192) != 197) {
		printf("subsystest: linux ABI map FAIL\n");
		return (-1);
	}
	r.eax = 60;			/* Linux umask is also 60 */
	(void)subsys_dispatch(&r);
	if ((int)r.eax < 0) {
		printf("subsystest: linux umask FAIL\n");
		return (-1);
	}
	/* Win32 Sleep(0) */
	if (subsys_set(3) < 0) {
		printf("subsystest: set win32 FAIL\n");
		return (-1);
	}
	r.eax = 0x1007;
	r.ebx = 0;
	(void)subsys_dispatch(&r);
	if ((int)r.eax < 0) {
		printf("subsystest: win32 Sleep FAIL\n");
		return (-1);
	}
	(void)subsys_set(0);
	printf("subsystest: 4 personalities ok (unix/posix/linux/win32)\n");
	printf("subsystest: PASS\n");
	return (0);
}
