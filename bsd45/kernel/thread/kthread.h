/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * kthread.h - kernel-only threads (S21).
 *
 * A kernel thread is an execution carrier that never enters user mode.  It
 * has its own kernel stack and runs the scheduler's cooperative context
 * switch.  Softirq processing and page reclaim use these threads, so those
 * jobs no longer run on whatever user thread happened to call into the
 * kernel.
 * 内核线程是永不进入用户态的执行载体：它有自己的内核栈，并走调度器的
 * 协作式上下文切换；软中断与页回收在这些线程上运行，不再借用用户线程的栈。
 */
#ifndef _KTHREAD_H_
#define _KTHREAD_H_

#include "sys/platform.h"

#define	NKTHREAD	16
#define	KT_FREE		0
#define	KT_RUN		1
#define	KT_BLOCK	2
#define	KT_ZOMBIE	3

#define	KSTACK_VA	0xFEA00000
#define	KSTACK_SIZE	8192

struct uproc;

struct kthread {
	int		t_state;
	int		t_tid;
	const char	*t_name;
	void		(*t_entry)(void *);	/* kernel threads only */
	void		*t_arg;
	unsigned	t_ksp;		/* saved kernel esp while switched out */
	unsigned	t_stack_top;
	void		*t_stack_va;
	int		t_used;
	/* user threads: t_proc != NULL */
	struct uproc	*t_proc;
	struct sysregs	t_regs;
	int		t_wait_pid;
	unsigned	t_wait_status;
	int		t_wait_options;
	int		t_pipeidx;
	int		t_piperw;
	unsigned	t_pipeat;
	unsigned	t_pipelen;
	int		t_exitcode;
	int		t_join_tid;	/* thread waiting in thread_join */
	int		t_detached;
	unsigned	t_sigmask;	/* thread-directed blocked mask */
	unsigned	t_sig;		/* thread-directed pending signals */
	/* os.md mechanism 4: scheduling/IPL properties */
	int		t_base_ipl;	/* preemption sensitivity (0..15) */
	int		t_eff_ipl;	/* base + priority inheritance */
	int		t_preemptable;	/* may its resources be taken over? */
	int		t_boosted;	/* inheriting a higher level */
};

extern struct kthread *curk;
extern struct kthread kthreads[NKTHREAD];

void	kthread_init(void);
struct kthread *kthread_spawn(const char *name, void (*entry)(void *),
	    void *arg);
struct kthread *kthread_user_spawn(struct uproc *p, int tid);
struct kthread *kthread_user_find(int tid);
void	kthread_user_free(struct kthread *t);
void	kthread_exit(void);
void	kthread_block(void);
void	kthread_wake(struct kthread *t);
void	kthread_poll(void);	/* run one runnable kernel thread */
void	kthread_run(void);	/* run until none are runnable */
void	kthread_print(void);
int	kthread_count(void);
int	kthread_runnable(void);
unsigned kthread_selftest(void);
extern unsigned kthread_switches;

/* context switch: save current esp in *old, continue on new stack */
void	kswitch(unsigned *old, unsigned new);

/* os.md mechanism 4: per-thread IPL policy + priority inheritance */
void	thread_ipl_set(struct kthread *t, int base_ipl, int preemptable);
int	thread_ipl_effective(struct kthread *t);
int	thread_ipl_boost(struct kthread *t, int ipl);
void	thread_ipl_unboost(struct kthread *t);
struct kthread *kthread_find(int tid);
int	kthread_ipl_selftest(void);
extern unsigned ipl_boosts;
extern unsigned ipl_donations;

/* softirq */
#define	SOFT_RECLAIM	0
#define	SOFT_INPUT	1
#define	SOFT_NET	2
#define	SOFT_TEST	3
#define	NSOFT		4

void	softirq_init(void);
void	softint_schedule(int id);
void	softint_register(int id, void (*fn)(void));
void	softirq_print(void);
unsigned softirq_count(void);
int	softirq_selftest(void);

/* page cache reclaim hook (pagecache.c) */
int	pagecache_reclaim(unsigned n);

#endif
