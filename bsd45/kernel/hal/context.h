/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * context.h - platform-independent execution-context primitives.
 *
 * The scheduler and preemption policy live in portable C; a new architecture
 * only implements these five hooks:
 *
 *	hal_kernel_stack_set()	point the CPU at a thread's ring-0 stack
 *	hal_switch_stack()	switch between kernel stacks
 *	hal_intr_from_user()	was the interrupted context ring 3?
 *	hal_intr_user_resume()	make a saved user context runnable again
 *	hal_user_stack_top()	return the initial user stack pointer
 *
 * 调度与抢占策略在可移植 C 里；新架构只需实现下面 5 个钩子：
 *	hal_kernel_stack_set()	把 CPU 指向线程的 ring0 栈
 *	hal_switch_stack()	在内核栈之间切换
 *	hal_intr_from_user()	被中断的上下文是否来自 ring3
 *	hal_intr_user_resume()	让保存的用户上下文重新可运行
 *	hal_user_stack_top()	返回初始用户栈指针
 */
#ifndef _CONTEXT_H_
#define _CONTEXT_H_

struct sysregs;

void	hal_kernel_stack_set(unsigned top);
void	hal_switch_stack(unsigned *old_sp, unsigned new_sp);
int	hal_intr_from_user(const struct sysregs *r);
void	hal_intr_user_resume(struct sysregs *r);
unsigned hal_user_stack_top(void);

/* address space / CPU hooks used by the user/kernel boundary */
void	hal_address_space_activate(unsigned pd);
unsigned hal_address_space_current(void);
void	hal_cpu_halt(void);
void	hal_syscall_gate_init(void);

#endif /* !_CONTEXT_H_ */
