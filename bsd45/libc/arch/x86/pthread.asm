/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * pthread.asm - i386 thread trampoline for libc pthread_create.
 */
	.text

	.globl	_pthread_tramp
	.align	4
_pthread_tramp:
	movl	%eax, %ecx
	pushl	4(%ecx)
	call	*(%ecx)
	pushl	%eax
	call	_pthread_exit
	addl	$4, %esp
	ret
