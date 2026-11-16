/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * context_arch.c - i386 implementation of the context HAL.
 *
 * This is the only place that knows about the TSS ring-0 stack pointer,
 * the kswitch assembly entry, the ring-3 CS bits and the IF flag.  The
 * scheduler calls the generic hooks from kernel/hal/context.h.
 * 本文件是 i386 的上下文 HAL：只有它知道 TSS ring0 栈指针、kswitch 汇编
 * 入口、ring3 的 CS 位和 IF 标志；调度器只调用 kernel/hal/context.h 的
 * 通用钩子。
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>

#include <machine/segments.h>

#include "arch/i386/hal/hal.h"
#include "hal/context.h"
#include "subsys/subsys.h"
#include "thread/kthread.h"

extern struct user *proc0paddr;
extern void syscall_entry(void);

void
hal_kernel_stack_set(unsigned top)
{

	if (proc0paddr != 0)
		proc0paddr->u_pcb.pcb_tss.tss_esp0 = (int)top;
}

void
hal_switch_stack(unsigned *old_sp, unsigned new_sp)
{

	kswitch(old_sp, new_sp);
}

int
hal_intr_from_user(const struct sysregs *r)
{

	return (r != 0 && (r->cs & 3) == 3);
}

void
hal_intr_user_resume(struct sysregs *r)
{

	if (r != 0)
		r->eflags |= 0x200;	/* IF=1: user mode runs with interrupts on */
}

unsigned
hal_user_stack_top(void)
{

	return (0xBFC00000u);
}

void
hal_address_space_activate(unsigned pd)
{

	cpu_load_cr3(pd);
}

unsigned
hal_address_space_current(void)
{

	return (cpu_read_cr3());
}

void
hal_cpu_halt(void)
{

	cpu_halt();
}

void
hal_syscall_gate_init(void)
{

	setidt(0x80, (char *)&syscall_entry, SDT_SYS386TGT, SEL_UPL);
}
