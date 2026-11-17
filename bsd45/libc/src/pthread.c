/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * pthread.c - pthread_create/pthread_join on top of BSD45 user threads.
 *
 * The kernel ABI is:
 *	440 thread_create(entry, arg, stack) -> tid
 *	441 thread_exit(code)
 *	442 thread_join(tid) -> code
 *	443 thread_self() -> tid
 *	444 thread_yield()
 *
 * Each new thread gets a static user stack and a small start record; the
 * kernel enters at pthread_tramp with eax = &start, so the trampoline calls
 * start->fn(start->arg) and then thread_exit().
 */
#include <pthread.h>
#include <syscall_arch.h>

#define	NTHREADS	4
#define	sc(n, a, b, c)	__syscall3((n), (a), (b), (c))

static int	used[NTHREADS];
static pthread_t tids[NTHREADS];
static volatile int done[NTHREADS];
static void	*retv[NTHREADS];
static int	detached[NTHREADS];

struct pstart {
	void	*(*fn)(void *);
	void	*arg;
};

static struct pstart pss[NTHREADS];

extern void pthread_tramp(void);

static int
stack_alloc(void)
{
	int i;

	for (i = 0; i < NTHREADS; i++)
		if (!used[i]) {
			used[i] = 1;
			return (i);
		}
	return (-1);
}

int
pthread_create(pthread_t *tid, const pthread_attr_t *attr,
    void *(*start)(void *), void *arg)
{
	struct pstart *ps;
	int i, r;

	(void)attr;
	if (tid == 0 || start == 0)
		return (-1);
	i = stack_alloc();
	if (i < 0)
		return (-1);
	ps = &pss[i];
	ps->fn = start;
	ps->arg = arg;
	r = sc(440, (int)pthread_tramp, (int)ps, 0);
	if (r < 0) {
		used[i] = 0;
		return (-1);
	}
	tids[i] = r;
	*tid = r;
	return (0);
}

int
pthread_join(pthread_t tid, void **retval)
{
	int i;

	for (i = 0; i < NTHREADS; i++)
		if (tids[i] == tid)
			break;
	if (i == NTHREADS)
		return (-1);
	while (!done[i])
		pthread_yield();
	if (retval != 0)
		*retval = retv[i];
	used[i] = 0;
	tids[i] = 0;
	done[i] = 0;
	retv[i] = 0;
	return (0);
}

int
pthread_detach(pthread_t tid)
{
	int i;

	for (i = 0; i < NTHREADS; i++)
		if (tids[i] == tid)
			break;
	if (i == NTHREADS)
		return (-1);
	detached[i] = 1;
	(void)sc(445, tid, 0, 0);
	return (0);
}

pthread_t
pthread_self(void)
{

	return (sc(443, 0, 0, 0));
}

void
pthread_exit(void *retval)
{
	pthread_t self = pthread_self();
	int i;

	for (i = 0; i < NTHREADS; i++)
		if (tids[i] == self) {
			retv[i] = retval;
			done[i] = 1;
			if (detached[i])
				used[i] = 0;
			break;
		}
	sc(441, (int)retval, 0, 0);
	for (;;)
		;
}

int
pthread_yield(void)
{

	return (sc(444, 0, 0, 0));
}

int
sched_setprio(int prio)
{

	return (sc(446, prio, 0, 0));
}

int
sched_getprio(void)
{

	return (sc(447, 0, 0, 0));
}
