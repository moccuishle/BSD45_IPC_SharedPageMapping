<!-- Copyright (c) 2026 jialiang.  All rights reserved. -->

# `libc/arch/x86`

## English

This directory contains the i386 platform backend for the userland C library: crt0 startup, setjmp/longjmp, the int 0x80 syscall stubs and the pthread trampoline. `config.mk` lists the assembly objects that the
build links for this architecture; C sources contain no inline assembly.

## 中文

本目录包含用户态 C 库的 i386 平台后端：crt0 启动、setjmp/longjmp、int 0x80 系统调用桩和 pthread trampoline。`config.mk` 列出该架构需要编译链接的汇编对象；C 源文件中
不允许出现内联汇编。
