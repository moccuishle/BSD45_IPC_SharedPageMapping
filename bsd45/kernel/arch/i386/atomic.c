/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * atomic.c - i386 atomic operations and memory barriers.
 *
 * This is the ONLY place in the BSD45 kernel (outside arch/) that contains
 * the lock-prefixed instructions used by the synchronization layer.  Keeping
 * them here satisfies the asmble.md rule: assembly belongs under arch/<arch>/.
 */
#include "arch/i386/atomic.h"

void
atomic_set(atomic_t *v, int x)
{

	*v = x;
}

int
atomic_read(atomic_t *v)
{

	return (*v);
}

void
atomic_inc(atomic_t *v)
{

	__asm __volatile("lock incl %0" : "+m" (*v));
}

void
atomic_dec(atomic_t *v)
{

	__asm __volatile("lock decl %0" : "+m" (*v));
}

int
atomic_add(atomic_t *v, int x)
{
	int r;

	__asm __volatile("lock xaddl %0,%1"
	    : "=r" (r) : "m" (*v), "0" (x) : "memory");
	return (r + x);
}

int
atomic_cmpxchg(atomic_t *v, int old, int new)
{
	int prev;

	__asm __volatile("lock cmpxchgl %2,%1"
	    : "=a" (prev), "+m" (*v)
	    : "r" (new), "0" (old) : "memory");
	return (prev);
}

int
atomic_test_and_set(atomic_t *v)
{
	int prev = 1;

	__asm __volatile("xchgl %0,%1"
	    : "=r" (prev), "+m" (*v) : "0" (1) : "memory");
	return (prev);
}

void
sync_mb(void)
{

	__asm __volatile("mfence" ::: "memory");
}

void
sync_rmb(void)
{

	__asm __volatile("lfence" ::: "memory");
}

void
sync_wmb(void)
{

	__asm __volatile("sfence" ::: "memory");
}
