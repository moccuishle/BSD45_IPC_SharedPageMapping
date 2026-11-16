/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * ipl.h - unified 16-level interrupt priority (IPL 0..15).
 *
 * os.md mechanism 4: hardware interrupts, software interrupts and thread
 * preemption all live in one numeric space:
 *
 *	IPL 0	lowest, fully preemptable
 *	IPL 15	highest, all maskable interrupts blocked
 *
 * An event may preempt the current context iff its IPL is *strictly*
 * greater than current_ipl.  Pending events live in one 16-bit bitmap
 * (ipending) and are served high-to-low by ipl_dispatch(), no matter
 * whether they came from the 8259 (hard) or from softirq_raise() (soft).
 *
 * The i386 backend maps the 8259 IRQ lines into this space
 * (hal_irq_to_ipl) and turns current_ipl into a PIC mask; the policy
 * code above it (threads, softirq, sleep) never touches the PIC.
 */
#ifndef _IPL_H_
#define _IPL_H_

#define	NIPL		16

/*
 * Level assignments.  Same-level sources are served FIFO (one handler per
 * level in this port); keep clock and tty apart so a tty burst cannot
 * starve the clock.
 */
#define	IPL_NONE	0	/* user threads: maximum preemptability */
#define	IPL_SOFT0	1	/* SOFT_RECLAIM */
#define	IPL_SOFT1	2	/* SOFT_INPUT */
#define	IPL_SOFT2	3	/* SOFT_NET */
#define	IPL_SOFT3	4	/* SOFT_TEST */
#define	IPL_BIO		4	/* disk (shares the last soft level) */
#define	IPL_CLOCK	5	/* PIT / RTC */
#define	IPL_TTY		6	/* console / keyboard */
#define	IPL_NET		7	/* network devices */
#define	IPL_AIO		9	/* VFS async completion (os.md M5) */
#define	IPL_HIGH	15	/* critical: no preemption, no switch */

#define	IRQ_VEC_BASE	0x20	/* IDT vector of IRQ0 */

struct irq_action {
	int		a_ipl;
	void		(*a_handler)(void *);
	void		*a_arg;
	const char	*a_name;
	unsigned	a_runs;
	int		a_used;
	struct irq_action *a_next;	/* next handler at the same level */
};

/* current CPU state (one CPU in this port) */
extern int		current_ipl;
extern unsigned		ipending;	/* bit n = event at IPL n pending */
extern unsigned		ipl_hard_events;
extern unsigned		ipl_soft_events;
extern unsigned		ipl_switches;
extern unsigned		ipl_preempts;

void	ipl_init(void);
int	ipl_raise(int new_ipl);		/* raise, returns old level */
void	ipl_x(int old_ipl);		/* restore old level */
int	ipl_enter(int ipl);		/* interrupt entry: no sti */
void	ipl_exit(int old_ipl);		/* interrupt exit: no sti */
int	ipl_current(void);
unsigned ipl_pending(void);

int	hal_irq_to_ipl(int vector);
void	hal_ipl_init(void);
void	hal_ipl_apply(int current_ipl, unsigned enabled_mask);
void	ipl_enable_irq(int irq);
void	ipl_register(int ipl, void (*fn)(void *), void *arg, const char *name);
void	softirq_raise(int ipl, void (*fn)(void *), void *arg);
int	ipl_dispatch(void);		/* serve pending > current_ipl */
void	preempt_point(void);		/* safe point for deferred work */

void	ipl_print(void);
void	ipl_trace_print(int last);
int	ipl_selftest(void);
unsigned ipl_runs(int ipl);

#endif /* !_IPL_H_ */
