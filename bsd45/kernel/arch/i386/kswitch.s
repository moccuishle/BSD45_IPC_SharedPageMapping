/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * kswitch.s - cooperative kernel context switch (S21).
 *
 *	void kswitch(unsigned *old_esp, unsigned new_esp);
 *
 * Saves the callee-saved registers on the current kernel stack, stores the
 * resulting stack pointer in *old_esp, switches to new_esp and pops the
 * saved registers there.  A fresh thread's stack is prepared by
 * kthread_spawn() with a return address at the top.
 */
	.text

	.globl	_kswitch
	.align	4
_kswitch:
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp), %eax		/* old_esp */
	movl	24(%esp), %edx		/* new_esp */
	movl	%esp, (%eax)
	movl	%edx, %esp
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
