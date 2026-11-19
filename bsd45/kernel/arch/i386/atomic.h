/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * atomic.h - i386 atomic operations (arch-private header).
 *
 * Per the kernel assembly rule (asmble.md), all inline assembly lives under
 * kernel/arch/<arch>/.  This header declares the implementations in atomic.c;
 * generic code (e.g. sys/sync.h) includes this header and calls the functions.
 */
#ifndef _ARCH_I386_ATOMIC_H_
#define _ARCH_I386_ATOMIC_H_

typedef volatile int	atomic_t;

void	atomic_set(atomic_t *v, int x);
int	atomic_read(atomic_t *v);
void	atomic_inc(atomic_t *v);
void	atomic_dec(atomic_t *v);
int	atomic_add(atomic_t *v, int x);		/* returns new value */
int	atomic_cmpxchg(atomic_t *v, int old, int new); /* returns old value */
int	atomic_test_and_set(atomic_t *v);	/* returns previous value */

void	sync_mb(void);				/* mfence */
void	sync_rmb(void);				/* lfence */
void	sync_wmb(void);				/* sfence */

#endif /* _ARCH_I386_ATOMIC_H_ */
