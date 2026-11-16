/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 */

/*
 * hal.h - i386 hardware abstraction layer (S24).
 *
 * This directory is the i386 platform backend: all port I/O and CPU control
 * register access is implemented in hal_arch.s.  A different platform gets
 * its own kernel/arch/<arch>/hal directory and the build selects it with
 * ARCH_<arch>; C sources never contain inline assembly.
 */
#ifndef _HAL_H_
#define _HAL_H_

unsigned char	hal_inb(int port);
void		hal_outb(int port, int value);
void		hal_outw(int port, unsigned value);
void		hal_insw(int port, void *addr, unsigned count);
void		hal_outsw(int port, const void *addr, unsigned count);

/* CPU control; the i386 implementations live in hal_arch.s. */
void		cpu_load_cr3(unsigned pd);
unsigned	cpu_read_cr3(void);
void		cpu_halt(void);
void		cpu_cli(void);
void		cpu_sti(void);
unsigned	cpu_read_esp(void);
void		cpu_write_esp(unsigned esp);
unsigned	hal_rdtsc(void);
void		cpu_breakpoint(void);
void		kgdb_return_to_user(void);

/* i386 NPX helpers used by npx.c (assembly in x86.asm). */
void		npx_init(void);
void		npx_init_finite(void);
void		npx_clear(void);
void		npx_save(void *area);
void		npx_restore(void *area);
unsigned short	npx_get_control(void);
unsigned short	npx_get_status(void);
void		npx_set_control(unsigned short control);

#endif
