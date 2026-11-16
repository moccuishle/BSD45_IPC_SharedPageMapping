/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * syscall.asm - i386 int 0x80 stubs for libc.
 *
 * C code calls __syscallN(n, a, b, ...); the kernel ABI is
 * eax=n, ebx=a, ecx=b, edx=c, esi=d, edi=e.
 */
	.text

	.globl	___syscall0
	.align	4
___syscall0:
	movl	4(%esp), %eax
	int	$0x80
	ret

	.globl	___syscall1
	.align	4
___syscall1:
	pushl	%ebx
	movl	8(%esp), %eax
	movl	12(%esp), %ebx
	int	$0x80
	popl	%ebx
	ret

	.globl	___syscall2
	.align	4
___syscall2:
	pushl	%ebx
	movl	8(%esp), %eax
	movl	12(%esp), %ebx
	movl	16(%esp), %ecx
	int	$0x80
	popl	%ebx
	ret

	.globl	___syscall3
	.align	4
___syscall3:
	pushl	%ebx
	movl	8(%esp), %eax
	movl	12(%esp), %ebx
	movl	16(%esp), %ecx
	movl	20(%esp), %edx
	int	$0x80
	popl	%ebx
	ret

	.globl	___syscall4
	.align	4
___syscall4:
	pushl	%ebx
	pushl	%esi
	movl	12(%esp), %eax
	movl	16(%esp), %ebx
	movl	20(%esp), %ecx
	movl	24(%esp), %edx
	movl	28(%esp), %esi
	int	$0x80
	popl	%esi
	popl	%ebx
	ret

	.globl	___syscall5
	.align	4
___syscall5:
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	16(%esp), %eax
	movl	20(%esp), %ebx
	movl	24(%esp), %ecx
	movl	28(%esp), %edx
	movl	32(%esp), %esi
	movl	36(%esp), %edi
	int	$0x80
	popl	%edi
	popl	%esi
	popl	%ebx
	ret
