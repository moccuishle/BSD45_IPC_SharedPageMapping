# config.mk - libc/arch/x86
#
# Copyright (c) 2026 jialiang.  All rights reserved.
#
# Architecture-specific assembly for the userland C library.
# build_sh.ps1 reads these lists to select the objects for ARCH.

# obj-y +=
obj-y += crt0.s
obj-y += jmp.s
obj-y += chkstk_ms.s
obj-y += syscall.asm
obj-y += pthread.asm

# asm-y += (same list, explicit)
asm-y += crt0.s
asm-y += jmp.s
asm-y += chkstk_ms.s
asm-y += syscall.asm
asm-y += pthread.asm
