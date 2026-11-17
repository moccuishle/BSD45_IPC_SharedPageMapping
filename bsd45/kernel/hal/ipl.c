/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * ipl.c - 16-level unified interrupt priority (os.md mechanism 4).
 *
 * One bitmap holds both hardware and software events; one dispatch loop
 * serves the highest level strictly above current_ipl.  The i386 backend
 * converts the current level into an 8259 mask, so "IPL 15" really does
 * block the timer while "IPL 0" lets it through.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "hal/ipl.h"
#include "thread/kthread.h"

int		current_ipl;
unsigned	ipending;
unsigned	ipl_hard_events;
unsigned	ipl_soft_events;
unsigned	ipl_switches;
unsigned	ipl_preempts;

#define	MAX_ACTIONS	64
static struct irq_action	action_pool[MAX_ACTIONS];
static struct irq_action	*chains[NIPL];
static unsigned			action_overflow;
static unsigned			enabled_mask;	/* IRQ lines registered */
static int			in_interrupt;
static int			in_dispatch;

/*
 * Bounded transition trace.  It is the evidence a host-side checker reads:
 * every raise must go up, every restore must come back, every dispatch must
 * pick a level strictly above the current one and return to it afterwards.
 */
#define	NITRACE		256
struct ipl_ev {
	unsigned	e_seq;
	unsigned	e_pending;
	unsigned char	e_from;
	unsigned char	e_to;
	char		e_kind;		/* R X E I D d */
};
static struct ipl_ev	itrace[NITRACE];
static unsigned		itrace_seq;

/* EN: Append one IPL transition record to the bounded trace ring used by the log checker. */
/* 中文：把一次 IPL 迁移记录写入有界跟踪环形缓冲区，供日志校验器回放。 */
static void
trace_ev(char kind, int from, int to)
{
	struct ipl_ev *e = &itrace[itrace_seq % NITRACE];

	e->e_seq = itrace_seq++;
	e->e_kind = kind;
	e->e_from = (unsigned char)from;
	e->e_to = (unsigned char)to;
	e->e_pending = ipending;
}

/* EN: Print the last N IPL transition records in a machine-parsable 'itrace:' format. */
/* 中文：按可解析的 'itrace:' 格式打印最近 N 条 IPL 迁移记录。 */
void
ipl_trace_print(int last)
{
	unsigned start, i, n;

	if (last <= 0 || last > NITRACE)
		last = NITRACE;
	n = itrace_seq < (unsigned)last ? itrace_seq : (unsigned)last;
	start = itrace_seq - n;
	for (i = start; i < itrace_seq; i++) {
		struct ipl_ev *e = &itrace[i % NITRACE];

		printf("itrace: %d %c %d %d %x\n", e->e_seq, e->e_kind,
		    e->e_from, e->e_to, e->e_pending);
	}
}

/* A line whose IPL is <= current_ipl is masked by the arch backend. */
/* EN: Build the mask of IRQ lines whose IPL is at or below the current level. */
/* 中文：生成屏蔽字：所有 IPL 小于等于当前级别的 IRQ 线都置为屏蔽。 */
static unsigned
below_mask(void)
{
	unsigned m = 0;
	int irq;

	for (irq = 0; irq < 16; irq++)
		if (hal_irq_to_ipl(irq) <= current_ipl)
			m |= 1u << irq;
	return (m);
}

/* EN: Mark one IRQ line as registered and ask the arch backend to apply the mask. */
/* 中文：把一条 IRQ 线标记为已注册，并让架构后端刷新屏蔽字。 */
void
ipl_enable_irq(int irq)
{

	if (irq < 0 || irq > 15)
		return;
	enabled_mask |= 1u << irq;
	hal_ipl_apply(current_ipl, enabled_mask);
}

/*
 * Handler chains: several devices may share an IPL level.  A level's chain
 * is served FIFO in registration order; the pool is fixed so registration
 * never allocates in interrupt context.
 * 处理函数链：多个设备可共享同一个 IPL 级别；同级按注册顺序服务；
 * 使用固定池，注册过程不在中断上下文里分配内存。
 */
/* 中文：清空指定 IPL 级别的处理函数链，并把固定池中属于该级别的处理函数槽全部标记为空闲 */
/* EN: Clear the handler chain of an IPL level and mark every handler slot of that level in the fixed pool as free */
static void
action_clear(int ipl)
{
	int i;

	chains[ipl] = 0;
	for (i = 0; i < MAX_ACTIONS; i++)
		if (action_pool[i].a_ipl == ipl)
			action_pool[i].a_used = 0;
}

/* 中文：从固定池中分配一个中断处理函数槽，填入 IPL、回调、参数和名称并挂到该级别链头；池满则递增溢出计数并返回空 */
/* EN: Allocate a handler slot from the fixed pool, fill in IPL/callback/arg/name and push it on the level's chain; on pool exhaustion bump the overflow counter and return null */
static struct irq_action *
action_alloc(int ipl, void (*fn)(void *), void *arg, const char *name)
{
	int i;
	struct irq_action *a;

	for (i = 0; i < MAX_ACTIONS; i++) {
		a = &action_pool[i];
		if (a->a_used)
			continue;
		a->a_ipl = ipl;
		a->a_handler = fn;
		a->a_arg = arg;
		a->a_name = name;
		a->a_runs = 0;
		a->a_used = 1;
		a->a_next = chains[ipl];
		chains[ipl] = a;
		return (a);
	}
	action_overflow++;
	return (0);
}

/* EN: Reset the IPL state, pending bitmap and handler chains, then initialize the 8259s. */
/* 中文：复位 IPL 状态、pending 位图和处理函数链，然后初始化 8259。 */
void
ipl_init(void)
{
	int i;

	bzero(action_pool, sizeof(action_pool));
	for (i = 0; i < NIPL; i++)
		chains[i] = 0;
	action_overflow = 0;
	enabled_mask = 0;
	current_ipl = IPL_NONE;
	ipending = 0;
	in_interrupt = 0;
	in_dispatch = 0;
	ipl_hard_events = ipl_soft_events = 0;
	ipl_switches = ipl_preempts = 0;
	hal_ipl_init();
	hal_ipl_apply(current_ipl, enabled_mask);
	printf("ipl: 16 levels, current 0, all IRQs masked\n");
}

/* EN: Raise the current IPL to new_ipl if it is strictly higher, returning the old level. */
/* 中文：若 new_ipl 严格高于当前级别则提升 IPL，并返回旧级别。 */
int
ipl_raise(int new_ipl)
{
	int old = current_ipl;

	if (new_ipl < IPL_NONE)
		new_ipl = IPL_NONE;
	if (new_ipl > IPL_HIGH)
		new_ipl = IPL_HIGH;
	if (new_ipl <= old)
		return (old);
	current_ipl = new_ipl;
	ipl_switches++;
	trace_ev('R', old, new_ipl);
	hal_ipl_apply(current_ipl, enabled_mask);
	return (old);
}

/* EN: Restore the IPL to a saved level and dispatch any now-eligible pending events. */
/* 中文：把 IPL 恢复到保存的级别，并派发现在已允许的 pending 事件。 */
void
ipl_x(int old_ipl)
{
	int from = current_ipl;

	if (old_ipl < IPL_NONE)
		old_ipl = IPL_NONE;
	if (old_ipl > IPL_HIGH)
		old_ipl = IPL_HIGH;
	current_ipl = old_ipl;
	trace_ev('X', from, old_ipl);
	hal_ipl_apply(current_ipl, enabled_mask);
	if (!in_interrupt && !in_dispatch && old_ipl == IPL_NONE)
		(void)ipl_dispatch();
}

/* EN: Interrupt-entry helper: enter a level without re-enabling interrupts. */
/* 中文：中断入口辅助函数：进入指定级别，但不重新开中断。 */
int
ipl_enter(int ipl)
{
	int old = current_ipl;

	in_interrupt++;
	current_ipl = ipl;
	trace_ev('E', old, ipl);
	hal_ipl_apply(current_ipl, enabled_mask);
	return (old);
}

/* EN: Interrupt-exit helper: leave the interrupt nesting and restore the previous level. */
/* 中文：中断出口辅助函数：退出中断嵌套并恢复之前的级别。 */
void
ipl_exit(int old_ipl)
{

	if (in_interrupt > 0)
		in_interrupt--;
	trace_ev('I', current_ipl, old_ipl);
	current_ipl = old_ipl;
	hal_ipl_apply(current_ipl, enabled_mask);
}

/* EN: Return the CPU's current interrupt priority level. */
/* 中文：返回当前 CPU 的中断优先级级别。 */
int
ipl_current(void)
{

	return (current_ipl);
}

/* EN: Return the 16-bit bitmap of pending hardware and software events. */
/* 中文：返回硬件与软件事件的 16 位 pending 位图。 */
unsigned
ipl_pending(void)
{

	return (ipending);
}

/* EN: Add a handler to the chain for one IPL level. */
/* 中文：把一个处理函数挂到某个 IPL 级别的处理函数链上。 */
void
ipl_register(int ipl, void (*fn)(void *), void *arg, const char *name)
{

	if (ipl <= IPL_NONE || ipl >= NIPL)
		return;
	(void)action_alloc(ipl, fn, arg, name);
}

/*
 * Soft interrupt: same pending bitmap as hardware.  Sources only mark the
 * bit; the dispatcher (softirq thread, syscall exit, clock) serves it.
 */
/* EN: Mark a software interrupt pending in the same bitmap used by hardware IRQs. */
/* 中文：在硬件中断共用的 pending 位图中标记一个软中断。 */
void
softirq_raise(int ipl, void (*fn)(void *), void *arg)
{

	if (ipl <= IPL_NONE || ipl >= NIPL)
		return;
	(void)action_alloc(ipl, fn, arg, "soft");
	ipending |= 1u << ipl;
	ipl_soft_events++;
}

/* EN: Serve pending events strictly above current_ipl, highest level first, until none remain. */
/* 中文：只服务严格高于 current_ipl 的 pending 事件，从最高级开始直到清空。 */
int
ipl_dispatch(void)
{
	int served = 0;

	if (in_dispatch)
		return (0);
	in_dispatch = 1;
	for (;;) {
		int lvl = -1, i;
		int save;
		struct irq_action *a;

		for (i = NIPL - 1; i > current_ipl; i--)
			if (ipending & (1u << i)) {
				lvl = i;
				break;
			}
		if (lvl < 0)
			break;
		if (chains[lvl] == 0) {
			printf("ipl: pending %d with no handler\n", lvl);
			panic("ipl_dispatch: lost interrupt");
		}
		save = current_ipl;
		current_ipl = lvl;
		trace_ev('D', save, lvl);
		ipending &= ~(1u << lvl);
		hal_ipl_apply(current_ipl, enabled_mask);
		for (a = chains[lvl]; a != 0; a = a->a_next) {
			if (a->a_handler == 0)
				continue;
			a->a_runs++;
			served++;
			a->a_handler(a->a_arg);
		}
		current_ipl = save;
		trace_ev('d', lvl, save);
		hal_ipl_apply(current_ipl, enabled_mask);
	}
	in_dispatch = 0;
	return (served);
}

/*
 * Safe point for deferred work and resource hand-over.  User-mode
 * preemption itself happens on the clock interrupt (sched_tick); this is
 * the kernel-side hook os.md calls preempt_point().
 */
/* EN: Kernel safe point that drains pending soft interrupts and runnable kernel threads. */
/* 中文：内核安全点：排空 pending 软中断并运行可运行的内核线程。 */
void
preempt_point(void)
{
	static int busy;

	ipl_preempts++;
	if (busy)
		return;
	busy = 1;
	(void)ipl_dispatch();
	if (kthread_runnable())
		kthread_run();
	busy = 0;
}

/* EN: Dump current level, pending bitmap, counters and registered handlers. */
/* 中文：打印当前级别、pending 位图、计数器和已注册的处理函数。 */
void
ipl_print(void)
{
	int i;

	printf("ipl: current %d pending %x switches %d preempts %d\n",
	    current_ipl, ipending, ipl_switches, ipl_preempts);
	printf("ipl: hard %d soft %d irq-enabled %x\n",
	    ipl_hard_events, ipl_soft_events, enabled_mask);
	for (i = 1; i < NIPL; i++) {
		struct irq_action *a;

		for (a = chains[i]; a != 0; a = a->a_next) {
			if (!a->a_used)
				continue;
			printf("ipl: %d %s handler %x runs %d\n", i,
			    a->a_name ? a->a_name : "?",
			    (unsigned)a->a_handler, a->a_runs);
		}
	}
}

/* EN: Return how many times the handler at one IPL level has run. */
/* 中文：返回某个 IPL 级别的处理函数已运行次数。 */
unsigned
ipl_runs(int ipl)
{
	struct irq_action *a;
	unsigned n = 0;

	if (ipl <= IPL_NONE || ipl >= NIPL)
		return (0);
	for (a = chains[ipl]; a != 0; a = a->a_next)
		n += a->a_runs;
	return (n);
}

/* ---------------------------------------------------------------- test */

static char	ipl_order[16];
static int	ipl_ordern;

/* EN: Self-test handler that records a level-3 dispatch in the order buffer. */
/* 中文：自测处理函数：在顺序缓冲区中记录一次 3 级派发。 */
static void
ipl_test3(void *arg)
{

	(void)arg;
	if (ipl_ordern < 15)
		ipl_order[ipl_ordern++] = '3';
}

/* EN: Self-test handler that records a level-7 dispatch in the order buffer. */
/* 中文：自测处理函数：在顺序缓冲区中记录一次 7 级派发。 */
static void
ipl_test7(void *arg)
{

	(void)arg;
	if (ipl_ordern < 15)
		ipl_order[ipl_ordern++] = '7';
}

/* EN: Self-test handler that records a level-12 dispatch in the order buffer. */
/* 中文：自测处理函数：在顺序缓冲区中记录一次 12 级派发。 */
static void
ipl_test12(void *arg)
{

	(void)arg;
	if (ipl_ordern < 15)
		ipl_order[ipl_ordern++] = 'c';
}

/* EN: Verify vector mapping, raise/restore nesting, strict high-to-low dispatch order and mask arithmetic. */
/* 中文：验证向量映射、提升/恢复嵌套、严格从高到低的派发顺序以及屏蔽字计算。 */
int
ipl_selftest(void)
{
	int ok = 1, old, old2, saved;

	/* 1. vector -> level mapping */
	if (hal_irq_to_ipl(0x20) != IPL_CLOCK ||
	    hal_irq_to_ipl(0x21) != IPL_TTY ||
	    hal_irq_to_ipl(0x2e) != IPL_BIO) {
		printf("ipltest: mapping FAIL\n");
		ok = 0;
	} else
		printf("ipltest: vector map ok\n");

	/* 2. raise/restore nesting, strict > rule */
	old = ipl_raise(IPL_CLOCK);
	if (current_ipl != IPL_CLOCK || old != IPL_NONE)
		ok = 0;
	old2 = ipl_raise(IPL_CLOCK - 1);	/* lower: no change */
	if (old2 != IPL_CLOCK || current_ipl != IPL_CLOCK)
		ok = 0;
	old2 = ipl_raise(10);
	if (old2 != IPL_CLOCK || current_ipl != 10)
		ok = 0;
	ipl_x(old2);
	if (current_ipl != IPL_CLOCK)
		ok = 0;
	ipl_x(old);
	if (current_ipl != IPL_NONE)
		ok = 0;
	printf("ipltest: raise/restore %s\n", ok ? "ok" : "FAIL");

	/* 3. one pending bitmap, served high-to-low */
	ipl_ordern = 0;
	saved = current_ipl;
	current_ipl = IPL_NONE;
	softirq_raise(3, ipl_test3, 0);
	softirq_raise(7, ipl_test7, 0);
	softirq_raise(12, ipl_test12, 0);
	(void)ipl_dispatch();
	if (ipl_ordern != 3 || ipl_order[0] != 'c' || ipl_order[1] != '7' ||
	    ipl_order[2] != '3') {
		printf("ipltest: order %d [%c%c%c] FAIL\n", ipl_ordern,
		    ipl_order[0], ipl_order[1], ipl_order[2]);
		ok = 0;
	} else
		printf("ipltest: order 12>7>3 ok\n");
	if (ipending != 0) {
		printf("ipltest: pending not drained: %x FAIL\n", ipending);
		ok = 0;
	}
	current_ipl = saved;

	/* 4. mask arithmetic: every level masks itself and below */
	current_ipl = IPL_HIGH;
	if ((below_mask() & 0xffff) != 0xffff) {
		printf("ipltest: ipl15 mask FAIL\n");
		ok = 0;
	}
	current_ipl = IPL_NONE;
	if ((below_mask() & 0xffff) != 0) {
		printf("ipltest: ipl0 mask FAIL\n");
		ok = 0;
	}
	current_ipl = saved;

	/* restore the real handlers overwritten by the test */
	action_clear(3);
	action_clear(7);
	action_clear(12);
	if (!ok) {
		printf("ipltest: FAIL\n");
		return (-1);
	}
	printf("ipltest: PASS\n");
	return (0);
}
