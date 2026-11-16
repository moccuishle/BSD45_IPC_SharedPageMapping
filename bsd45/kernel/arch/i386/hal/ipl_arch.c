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
