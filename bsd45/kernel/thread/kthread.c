/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * kthread.c - kernel thread pool plus user-thread slots (S21/S25).
 *
 * Kernel threads have their own 8K stacks and run through kswitch(); user
 * threads share the process address space and are resumed with resume_user()
 * by the scheduler in usermode.c.  Both live in one table so the scheduler
 * only ever walks threads, never processes.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "thread/kthread.h"
#include "hal/context.h"

#ifndef PAGE_SIZE
#define	PAGE_SIZE	4096
#endif
#ifndef VM_PROT_READ
#define	VM_PROT_READ	0x1
#define	VM_PROT_WRITE	0x2
#endif

extern void pmap_map(vm_offset_t, vm_offset_t, vm_offset_t, int);
extern unsigned vfs_page_alloc(void);
extern void bzero(void *, unsigned);

struct kthread	kthreads[NKTHREAD];
static unsigned	sched_esp;
struct kthread	*curk;
unsigned	kthread_switches;

static void
kthread_trampoline(void)
{
	struct kthread *t = curk;

	if (t != 0 && t->t_entry != 0)
		t->t_entry(t->t_arg);
	kthread_exit();
}

static int
kstack_alloc(int i)
{
	unsigned char *va = (unsigned char *)(KSTACK_VA + i * KSTACK_SIZE);
	unsigned pa;
	int j;

	for (j = 0; j < KSTACK_SIZE; j += PAGE_SIZE) {
		pa = vfs_page_alloc();
		pmap_map((vm_offset_t)(va + j), (vm_offset_t)pa,
		    (vm_offset_t)pa + PAGE_SIZE,
		    VM_PROT_READ | VM_PROT_WRITE);
	}
	return (0);
}

void
kthread_init(void)
{
	int i;

	bzero(kthreads, sizeof(kthreads));
	curk = 0;
	sched_esp = 0;
	kthread_switches = 0;
	for (i = 0; i < NKTHREAD; i++) {
		kthreads[i].t_state = KT_FREE;
		kthreads[i].t_proc = 0;
		kthreads[i].t_used = 0;
	}
	printf("kthread: %d thread slots at %x\n", NKTHREAD, KSTACK_VA);
}

struct kthread *
kthread_spawn(const char *name, void (*entry)(void *), void *arg)
{
	struct kthread *t = 0;
	unsigned *sp;
	int i;

	for (i = 0; i < NKTHREAD; i++)
		if (!kthreads[i].t_used) {
			t = &kthreads[i];
			break;
		}
	if (t == 0)
		return (0);
	bzero(t, sizeof(*t));
	t->t_state = KT_RUN;
	t->t_tid = i + 1;
	t->t_name = name;
	t->t_entry = entry;
	t->t_arg = arg;
	t->t_used = 1;
	t->t_proc = 0;
	t->t_base_ipl = 0;
	t->t_eff_ipl = 0;
	t->t_preemptable = 1;
	kstack_alloc(i);
	t->t_stack_va = (void *)(KSTACK_VA + i * KSTACK_SIZE);
	t->t_stack_top = (unsigned)t->t_stack_va + KSTACK_SIZE;
	t->t_stack_top &= ~15u;

	sp = (unsigned *)t->t_stack_top;
	*--sp = (unsigned)kthread_trampoline;	/* kswitch ret target */
	*--sp = 0;				/* ebp */
	*--sp = 0;				/* ebx */
	*--sp = 0;				/* esi */
	*--sp = 0;				/* edi */
	t->t_ksp = (unsigned)sp;
	printf("kthread: spawn %s tid %d\n", name, t->t_tid);
	return (t);
}

struct kthread *
kthread_user_spawn(struct uproc *p, int tid)
{
	struct kthread *t = 0;
	int i;

	for (i = 0; i < NKTHREAD; i++)
		if (!kthreads[i].t_used) {
			t = &kthreads[i];
			break;
		}
	if (t == 0)
		return (0);
	bzero(t, sizeof(*t));
	t->t_state = KT_RUN;
	t->t_tid = tid;
	t->t_name = "user";
	t->t_used = 1;
	t->t_proc = p;
	t->t_base_ipl = 0;
	t->t_eff_ipl = 0;
	t->t_preemptable = 1;
	/*
	 * Every user thread gets its own 8 KB kernel stack so a ring-3
	 * interrupt or syscall for one thread can never overwrite another
	 * thread's kernel frames.
	 * 每个用户线程拥有独立的 8 KB 内核栈，某个线程的 ring3 中断或系统调用
	 * 不会覆盖其它线程的内核栈帧。
	 */
	kstack_alloc(i);
	t->t_stack_va = (void *)(KSTACK_VA + i * KSTACK_SIZE);
	t->t_stack_top = (unsigned)t->t_stack_va + KSTACK_SIZE;
	t->t_stack_top &= ~15u;
	return (t);
}

struct kthread *
kthread_user_find(int tid)
{
	int i;

	for (i = 0; i < NKTHREAD; i++)
		if (kthreads[i].t_used && kthreads[i].t_proc != 0 &&
		    kthreads[i].t_tid == tid)
			return (&kthreads[i]);
	return (0);
}

void
kthread_user_free(struct kthread *t)
{

	if (t == 0)
		return;
	t->t_state = KT_FREE;
	t->t_proc = 0;
	t->t_used = 0;
}

void
kthread_block(void)
{
	struct kthread *t = curk;

	if (t == 0)
		return;
	t->t_state = KT_BLOCK;
	hal_switch_stack(&t->t_ksp, sched_esp);
}

void
kthread_wake(struct kthread *t)
{

	if (t != 0 && t->t_state == KT_BLOCK)
		t->t_state = KT_RUN;
}

void
kthread_exit(void)
{
	struct kthread *t = curk;

	if (t == 0)
		return;
	t->t_state = KT_FREE;
	t->t_used = 0;
	hal_switch_stack(&t->t_ksp, sched_esp);
	for (;;)
		;
}

int
kthread_runnable(void)
{
	int i;

	for (i = 0; i < NKTHREAD; i++)
		if (kthreads[i].t_used && kthreads[i].t_proc == 0 &&
		    kthreads[i].t_state == KT_RUN)
			return (1);
	return (0);
}

void
kthread_poll(void)
{
	int i;
	struct kthread *t = 0;

	for (i = 0; i < NKTHREAD; i++)
		if (kthreads[i].t_used && kthreads[i].t_proc == 0 &&
		    kthreads[i].t_state == KT_RUN) {
			t = &kthreads[i];
			break;
		}
	if (t == 0)
		return;
	curk = t;
	kthread_switches++;
	hal_switch_stack(&sched_esp, t->t_ksp);
	curk = 0;
}

void
kthread_run(void)
{

	while (kthread_runnable())
		kthread_poll();
}

void
kthread_print(void)
{
	int i;

	printf("kthread: switches %d\n", kthread_switches);
	for (i = 0; i < NKTHREAD; i++) {
		struct kthread *t = &kthreads[i];

		if (!t->t_used)
			continue;
		printf("kthread: tid %d %s %s state %d ksp %x\n", t->t_tid,
		    t->t_proc ? "user" : "kern",
		    t->t_name ? t->t_name : "?", t->t_state, t->t_ksp);
	}
}

int
kthread_count(void)
{
	int i, n = 0;

	for (i = 0; i < NKTHREAD; i++)
		if (kthreads[i].t_used)
			n++;
	return (n);
}

/* ------------------------------------------------- os.md mechanism 4 */

unsigned	ipl_boosts;
unsigned	ipl_donations;

void
thread_ipl_set(struct kthread *t, int base_ipl, int preemptable)
{

	if (t == 0)
		return;
	if (base_ipl < 0)
		base_ipl = 0;
	if (base_ipl > 15)
		base_ipl = 15;
	t->t_base_ipl = base_ipl;
	if (!t->t_boosted)
		t->t_eff_ipl = base_ipl;
	t->t_preemptable = preemptable ? 1 : 0;
}

int
thread_ipl_effective(struct kthread *t)
{

	return (t != 0 ? t->t_eff_ipl : 0);
}

/*
 * Priority inheritance: a thread that holds a resource a higher-IPL thread
 * wants runs at the requester's level until it releases the resource.
 * This is the os.md rule for non-preemptable threads (preemptable ones can
 * be suspended instead; this port records that policy in t_preemptable).
 */
int
thread_ipl_boost(struct kthread *t, int ipl)
{

	if (t == 0 || ipl <= t->t_eff_ipl)
		return (t != 0 ? t->t_eff_ipl : 0);
	t->t_eff_ipl = ipl;
	t->t_boosted = 1;
	ipl_boosts++;
	return (t->t_eff_ipl);
}

void
thread_ipl_unboost(struct kthread *t)
{

	if (t == 0 || !t->t_boosted)
		return;
	t->t_boosted = 0;
	t->t_eff_ipl = t->t_base_ipl;
}

struct kthread *
kthread_find(int tid)
{
	int i;

	for (i = 0; i < NKTHREAD; i++)
		if (kthreads[i].t_used && kthreads[i].t_tid == tid)
			return (&kthreads[i]);
	return (0);
}

int
kthread_ipl_selftest(void)
{
	struct kthread t;
	int ok = 1;

	bzero(&t, sizeof(t));
	thread_ipl_set(&t, 2, 1);
	if (t.t_base_ipl != 2 || thread_ipl_effective(&t) != 2)
		ok = 0;
	if (thread_ipl_boost(&t, 14) != 14)
		ok = 0;
	if (!t.t_boosted || thread_ipl_effective(&t) != 14)
		ok = 0;
	if (thread_ipl_boost(&t, 9) != 14)	/* no downgrade */
		ok = 0;
	thread_ipl_unboost(&t);
	if (t.t_boosted || thread_ipl_effective(&t) != 2)
		ok = 0;
	thread_ipl_set(&t, 5, 0);
	if (t.t_preemptable != 0 || t.t_base_ipl != 5)
		ok = 0;
	printf("ipltest: thread ipl/inheritance %s\n", ok ? "ok" : "FAIL");
	return (ok ? 0 : -1);
}

/* ---------------------------------------------------------------- test */

static int ktest_counter;

static void
ktest_entry(void *arg)
{
	int i;

	(void)arg;
	for (i = 0; i < 3; i++) {
		ktest_counter++;
		kthread_block();
	}
}

unsigned
kthread_selftest(void)
{
	struct kthread *t;

	ktest_counter = 0;
	t = kthread_spawn("ktest", ktest_entry, 0);
	if (t == 0)
		return (0);
	while (t->t_state != KT_FREE) {
		kthread_wake(t);
		kthread_poll();
	}
	return (unsigned)ktest_counter;
}
