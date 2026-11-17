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

/* 中文：设置持久化内核栈的栈顶，将其写入 proc0 的 TSS esp0 字段供陷入内核时使用 */
/* EN: Set the persistent kernel stack top by writing it into proc0's TSS esp0 field for kernel-entry use */
void
hal_kernel_stack_set(unsigned top)
{

	if (proc0paddr != 0)
		proc0paddr->u_pcb.pcb_tss.tss_esp0 = (int)top;
}

/* 中文：通过 kswitch 汇编例程把当前栈指针切换到给定的新栈，old_sp 用于保存原栈指针 */
/* EN: Switch the current stack pointer to a new stack via the kswitch assembly routine, with old_sp saving the previous pointer */
void
hal_switch_stack(unsigned *old_sp, unsigned new_sp)
{

	kswitch(old_sp, new_sp);
}

/* 中文：判断一次中断是否来自用户态：检查保存的 CS 寄存器低两位是否为 3（ring 3） */
/* EN: Determine whether an interrupt came from user mode by checking if the saved CS register's low two bits are 3 (ring 3) */
int
hal_intr_from_user(const struct sysregs *r)
{

	return (r != 0 && (r->cs & 3) == 3);
}

/* 中文：为用户态中断返回做准备，在保存的 EFLAGS 中置位 IF，使用户态恢复后中断处于开启状态 */
/* EN: Prepare for returning to user mode after an interrupt by setting IF in the saved EFLAGS so interrupts are enabled on resume */
void
hal_intr_user_resume(struct sysregs *r)
{

	if (r != 0)
		r->eflags |= 0x200;	/* IF=1: user mode runs with interrupts on */
}

/* 中文：返回 i386 平台上用户态栈的预设栈顶地址 */
/* EN: Return the preset top address of the user-mode stack on the i386 platform */
unsigned
hal_user_stack_top(void)
{

	return (0xBFC00000u);
}

/* 中文：通过加载 CR3 控制寄存器来激活给定的页目录，切换当前地址空间 */
/* EN: Activate a given page directory by loading it into control register CR3, switching the current address space */
void
hal_address_space_activate(unsigned pd)
{

	cpu_load_cr3(pd);
}

/* 中文：读取 CR3 控制寄存器，返回当前激活的页目录物理地址 */
/* EN: Read control register CR3 and return the physical address of the currently active page directory */
unsigned
hal_address_space_current(void)
{

	return (cpu_read_cr3());
}

/* 中文：暂停 CPU 执行，调用底层 cpu_halt 让处理器进入停机状态 */
/* EN: Halt CPU execution by calling the low-level cpu_halt to put the processor into a stopped state */
void
hal_cpu_halt(void)
{

	cpu_halt();
}

/* 中文：在 IDT 中安装 0x80 号系统调用门，指向 syscall_entry 并设为用户态可见的 386 陷阱门 */
/* EN: Install the 0x80 system-call gate in the IDT pointing at syscall_entry as a user-visible 386 trap gate */
void
hal_syscall_gate_init(void)
{

	setidt(0x80, (char *)&syscall_entry, SDT_SYS386TGT, SEL_UPL);
}
