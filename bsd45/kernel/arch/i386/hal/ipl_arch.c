/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * ipl_arch.c - i386 backend for the platform-independent IPL layer.
 *
 * Only this file knows about the 8259 PIC and port I/O.  Another platform
 * implements the same three entry points (hal_irq_to_ipl, hal_ipl_init,
 * hal_ipl_apply) and the generic kernel/hal/ipl.c policy code needs no
 * change; this is the HAL boundary os.md asks for.
 * 本文件是 IPL 层的 i386 后端，只有它知道 8259 和端口 I/O。其它平台只需
 * 实现同样三个入口（hal_irq_to_ipl / hal_ipl_init / hal_ipl_apply），
 * 通用的 kernel/hal/ipl.c 策略代码无需修改，这就是 os.md 要求的 HAL 边界。
 */
#include <sys/param.h>
#include <sys/systm.h>

#include "arch/i386/hal/hal.h"
#include "hal/ipl.h"

#define	PIC1_CMD	0x20
#define	PIC1_DATA	0x21
#define	PIC2_CMD	0xA0
#define	PIC2_DATA	0xA1

/* 中文：向空端口 0x80 写一次以产生简短的 I/O 延迟，用于 8259 端口写操作间的时序等待 */
/* EN: Produce a short I/O delay by writing to dummy port 0x80, used for timing between 8259 port writes */
static void
io_wait(void)
{

	hal_outb(0x80, 0);
}

/*
 * IRQ line / IDT vector -> IPL.  This is the only place the PC IRQ layout is
 * encoded; a different interrupt controller maps its vectors here.
 * IRQ 线 / IDT 向量 -> IPL。PC 的 IRQ 布局只在这里出现；换中断控制器时
 * 只需改这一张表。
 */
/* 中文：把 IRQ 线/IDT 向量映射为对应的中断优先级 IPL，PC 的 IRQ 布局仅在此处编码 */
/* EN: Map an IRQ line/IDT vector to its interrupt priority level IPL; the PC IRQ layout is encoded only here */
int
hal_irq_to_ipl(int vector)
{
	int irq;

	irq = (vector >= IRQ_VEC_BASE) ? vector - IRQ_VEC_BASE : vector;
	switch (irq) {
	case 0:			/* PIT */
		return (IPL_CLOCK);
	case 1:			/* keyboard */
		return (IPL_TTY);
	case 2:			/* 8259 cascade */
		return (IPL_NET);
	case 3: case 4:		/* serial */
		return (IPL_TTY);
	case 6: case 8:		/* floppy / RTC */
		return (IPL_BIO);
	case 14: case 15:	/* ATA */
		return (IPL_BIO);
	default:
		return (IPL_NET);
	}
}

/* A line whose IPL is <= current_ipl is masked. */
/* 中文：计算中断屏蔽位图，把 IPL 不高于 current_ipl 的所有 IRQ 线对应的位设为 1 */
/* EN: Compute the interrupt mask bitmap, setting a bit for every IRQ line whose IPL is not above current_ipl */
static unsigned
below_mask(int current_ipl)
{
	unsigned m = 0;
	int irq;

	for (irq = 0; irq < 16; irq++)
		if (hal_irq_to_ipl(irq) <= current_ipl)
			m |= 1u << irq;
	return (m);
}

/* Program both 8259 mask registers from the enabled set and current level. */
/* 中文：根据当前 IPL 与使能掩码计算需屏蔽的中断线，并写入两片 8259 的屏蔽寄存器 */
/* EN: Compute the lines to mask from the current IPL and enabled mask, then program both 8259 mask registers */
void
hal_ipl_apply(int current_ipl, unsigned enabled_mask)
{
	unsigned dis;

	dis = (~enabled_mask & 0xffff) |
	    (below_mask(current_ipl) & enabled_mask);
	hal_outb(PIC1_DATA, dis & 0xff);
	hal_outb(PIC2_DATA, (dis >> 8) & 0xff);
}

/* Initialize master/slave 8259s to vectors 0x20-0x2f and mask every line. */
/* 中文：初始化主/从两片 8259，把中断向量设置为 0x20-0x2f 并初始屏蔽所有中断线 */
/* EN: Initialize the master and slave 8259 PICs, mapping vectors to 0x20-0x2f and initially masking all lines */
void
hal_ipl_init(void)
{
	hal_outb(PIC1_CMD, 0x11); io_wait();
	hal_outb(PIC2_CMD, 0x11); io_wait();
	hal_outb(PIC1_DATA, 0x20); io_wait();
	hal_outb(PIC2_DATA, 0x28); io_wait();
	hal_outb(PIC1_DATA, 0x04); io_wait();
	hal_outb(PIC2_DATA, 0x02); io_wait();
	hal_outb(PIC1_DATA, 0x01); io_wait();
	hal_outb(PIC2_DATA, 0x01); io_wait();
	hal_outb(PIC1_DATA, 0xff);
	hal_outb(PIC2_DATA, 0xff);
}
