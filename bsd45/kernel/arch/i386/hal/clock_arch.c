/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * clock_arch.c - i386 timer backend: PIT channel 0 + IDT vector + PIC EOI.
 *
 * The generic kernel/hal/clock.c owns tick accounting, the scheduling
 * quantum, the latency histogram and the call into sched_tick().  A new
 * platform only implements hal_clock_init() and hal_clock_eoi().
 * 本文件是 i386 定时器后端：PIT 通道 0、IDT 向量和 PIC EOI。通用的
 * kernel/hal/clock.c 负责节拍统计、调度时间片、延迟直方图和调用
 * sched_tick()；换平台只需实现 hal_clock_init() 与 hal_clock_eoi()。
 */
#include <sys/param.h>
#include <sys/systm.h>

#include <machine/segments.h>

#include "arch/i386/hal/hal.h"
#include "hal/ipl.h"

#define	PIT_CH0		0x40
#define	PIT_CMD		0x43
#define	PIT_FREQ	1193182
#define	PIT_HZ		100
#define	PIC_EOI		0x20

extern void irq0_entry(void);
extern void setidt(int idx, char *func, int typ, int dpl);

/* 中文：向时钟中断控制器发送中断结束（EOI）信号。 */
/* EN: Send an end-of-interrupt (EOI) signal to the clock interrupt controller. */
void
hal_clock_eoi(void)
{

	hal_outb(PIC_EOI, 0x20);
}

/* 中文：初始化硬件抽象层的时钟设备与定时器。 */
/* EN: Initialize the HAL clock device and timer. */
void
hal_clock_init(void)
{
	unsigned div;

	setidt(IRQ_VEC_BASE + 0, (char *)&irq0_entry, SDT_SYS386TGT, SEL_KPL);
	ipl_enable_irq(0);

	div = PIT_FREQ / PIT_HZ;
	hal_outb(PIT_CMD, 0x34);		/* ch0, lo/hi, mode 2 */
	hal_outb(PIT_CH0, div & 0xff);
	hal_outb(PIT_CH0, (div >> 8) & 0xff);
}
