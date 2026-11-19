/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * sync.h - kernel synchronization primitives for BSD45 (S200).
 *
 * This layer sits on top of the existing kthread block/wake machinery and
 * the i386 atomic instructions.  It is deliberately self-contained: enabling
 * it does not change any existing scheduler behaviour.  On a uniprocessor
 * build (the default) spinlocks degenerate to preemption disable, while
 * mutex/semaphore/condvar still work via the blocking path.
 *
 * Lock ordering (acquire in this order, release in reverse):
 *   spinlock  ->  mutex  ->  semaphore/condvar
 */
#ifndef _SYS_SYNC_H_
#define _SYS_SYNC_H_

#ifndef ASSEMBLER

#include "sys/smp.h"		/* struct spinlock, spin_lock/unlock */
#include "thread/kthread.h"

/*
 * Atomic operations and barriers are architecture-specific and therefore
 * live under kernel/arch/<arch>/ (see asmble.md).  Pull in the declaration
 * for the configured architecture; generic code below only calls the plain
 * C functions declared there.  No inline assembly appears in this header.
 */
#if defined(ARCH_i386)
#include "arch/i386/atomic.h"
#else
#error "sync.h: no atomic.h for this ARCH; add one under kernel/arch/<arch>/"
#endif

/* ------------------------------------------------------------------ */
/* spinlock - defined in arch/i386/smp.c (struct spinlock in sys/smp.h) */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* wait channel - the common blocking substrate                       */
/* ------------------------------------------------------------------ */
struct waitq {
	struct kthread	*wq_head;	/* blocked TCB list */
	struct spinlock	 wq_lock;
};

void	waitq_init(struct waitq *wq, const char *name);
void	waitq_sleep(struct waitq *wq);
int	waitq_timed_sleep(struct waitq *wq, int ticks);
void	waitq_wakeone(struct waitq *wq);
void	waitq_wakeall(struct waitq *wq);
int	waitq_waiters(struct waitq *wq);

/* ------------------------------------------------------------------ */
/* mutex - sleeping lock with priority inheritance                    */
/* ------------------------------------------------------------------ */
struct kmutex {
	struct spinlock	 m_lock;	/* protects m_owner + waitq */
	struct kthread	*m_owner;	/* current holder, 0 = free */
	struct waitq	 m_waitq;
	int		 m_inited;
};

void	mutex_init(struct kmutex *m, const char *name);
void	mutex_enter(struct kmutex *m);
int	mutex_try(struct kmutex *m);
void	mutex_exit(struct kmutex *m);
int	mutex_owned(struct kmutex *m);

/* ------------------------------------------------------------------ */
/* semaphore - counting synchronizer                                  */
/* ------------------------------------------------------------------ */
struct ksem {
	struct spinlock	 s_lock;
	int		 s_count;
	struct waitq	 s_waitq;
};

void	sem_init(struct ksem *s, int count, const char *name);
void	sem_wait(struct ksem *s);
int	sem_trywait(struct ksem *s);
void	sem_post(struct ksem *s);
int	sem_value(struct ksem *s);

/* ------------------------------------------------------------------ */
/* condition variable - always used together with a kmutex            */
/* ------------------------------------------------------------------ */
struct kcondvar {
	struct waitq	 cv_waitq;
};

void	cv_init(struct kcondvar *cv, const char *name);
void	cv_wait(struct kcondvar *cv, struct kmutex *m);
void	cv_signal(struct kcondvar *cv);
void	cv_broadcast(struct kcondvar *cv);

/* ------------------------------------------------------------------ */
/* self test                                                          */
/* ------------------------------------------------------------------ */
int	sync_selftest(void);

#endif /* !ASSEMBLER */
#endif /* _SYS_SYNC_H_ */
