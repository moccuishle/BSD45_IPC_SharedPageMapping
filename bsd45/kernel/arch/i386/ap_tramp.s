/*
 * ap_tramp.s - AP startup trampoline for BSD45 i386 SMP.
 *
 * Linked at physical 0x7000 (CPU_TRAMPOLINE_PA).  The BSP copies this 4K
 * page to 0x7000 before sending INIT-SIPI-SIPI.  An AP wakes in real mode
 * with CS:IP = 0x7000:0, switches to 32-bit protected mode using the same
 * GDT as the BSP, enables paging with the shared kernel page directory, and
 * jumps to ap_start() in the C kernel.
 *
 * The BSP patches ap_pml4/pd (kernel page dir phys) and ap_stack before SIPI.
 */
#include <machine/psl.h>
#include <machine/specialreg.h>

	.code16
	.globl	_ap_tramp_start
_ap_tramp_start:
	cli
	cld
	/* set data segments to flat real-mode */
	xorl	%eax, %eax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss

	/* load BSP's GDT (patched by BSP: gdtr pseudo-descriptor) */
	lgdt	_ap_gdtr

	/* enable protected mode (CR0.PE), keep paging off briefly */
	movl	%cr0, %eax
	orl	$0x1, %eax
	movl	%eax, %cr0

	/* far jump to 32-bit code: selector 0x08 (kernel code) */
	.byte	0x66, 0xea
	.long	_ap_prot32
	.word	0x0008

	.align	4, 0x90
_ap_prot32:
	.code32
	movw	$0x10, %ax		/* kernel data selector */
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movw	%ax, %fs
	movw	%ax, %gs

	/* load kernel page directory (physical, patched by BSP) */
	movl	_ap_pd, %eax
	movl	%eax, %cr3

	/* enable paging | protected mode */
	movl	%cr0, %eax
	orl	$0x80000001, %eax
	movl	%eax, %cr0

	/* set up AP stack (patched by BSP) */
	movl	_ap_stack, %esp

	/* jump to C AP entry: ap_start(cpu_id) */
	pushl	_ap_cpuid
	call	_ap_start
1:	hlt
	jmp	1b

	.align	4
	.globl	_ap_gdtr
_ap_gdtr:	.word	0			/* patched: GDT limit */
		.long	0			/* patched: GDT phys addr */
	.globl	_ap_pd
_ap_pd:		.long	0			/* patched: kernel PD phys */
	.globl	_ap_stack
_ap_stack:	.long	0			/* patched: AP stack top (VA) */
	.globl	_ap_cpuid
_ap_cpuid:	.long	0			/* patched: this AP's cpu id */

	.globl	_ap_tramp_end
_ap_tramp_end:
