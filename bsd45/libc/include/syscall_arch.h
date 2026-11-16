/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * syscall_arch.h - libc/arch/<arch> syscall ABI.
 */
#ifndef _SYSCALL_ARCH_H_
#define _SYSCALL_ARCH_H_

int	__syscall0(int n);
int	__syscall1(int n, int a);
int	__syscall2(int n, int a, int b);
int	__syscall3(int n, int a, int b, int c);
int	__syscall4(int n, int a, int b, int c, int d);
int	__syscall5(int n, int a, int b, int c, int d, int e);

#endif
