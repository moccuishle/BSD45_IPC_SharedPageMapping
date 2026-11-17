/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)isa.c	8.1 (Berkeley) 6/11/93
 */

/*
 * code to manage AT bus
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/syslog.h>

#include <machine/segments.h>
#include <i386/isa/isa_device.h>
#include <i386/isa/icu.h>
#include <vm/vm.h>

/*
 * Configure all ISA devices
 */
/* 中文：配置全部 ISA 设备：在 splhigh 下逐类探测 bio/tty/net/null 设备表，整合中断屏蔽位，处理 netmask 为 0 的特例后恢复中断级 */
/* EN: Configure all ISA devices: under splhigh probe the bio/tty/net/null device tables, merge interrupt masks, handle the netmask==0 special case and restore the interrupt level */
isa_configure() {
	struct isa_device *dvp;
	struct isa_driver *dp;

	splhigh();
	INTREN(IRQ_SLAVE);
	for (dvp = isa_devtab_bio; config_isadev(dvp,&biomask); dvp++);
	for (dvp = isa_devtab_tty; config_isadev(dvp,&ttymask); dvp++);
	for (dvp = isa_devtab_net; config_isadev(dvp,&netmask); dvp++);
	for (dvp = isa_devtab_null; config_isadev(dvp,0); dvp++);
#include "sl.h"
#if NSL > 0
	netmask |= ttymask;
	ttymask |= netmask;
#endif

	/*
	 * The problem is... if netmask == 0, then the loopback
	 * code can do some really ugly things.
	 * workaround for this: if netmask == 0, set it to 0x8000, which
	 * is the value used by splsoftclock.  this is nasty, but it
	 * should work until this interrupt system goes away. -- cgd
	 */
	if (netmask == 0)
		netmask = 0x8000;       /* same as for softclock.  XXX */

	/* biomask |= ttymask ;  can some tty devices use buffers? */
	printf("biomask %x ttymask %x netmask %x\n", biomask, ttymask, netmask);
	splnone();
}

/*
 * Configure an ISA device.
 */
/* EN: Probes an ISA device through id_driver->probe; if alive, prints name/unit, calls attach, enables and masks the IRQ, installs its IDT gate; returns 1, else 0. */
/* 中文：经 id_driver->probe 探测 ISA 设备，存活则打印名称、调用 attach、使能并屏蔽中断、安装 IDT 门，返回 1；无驱动时返回 0。 */
config_isadev(isdp, mp)
	struct isa_device *isdp;
	int *mp;
{
	struct isa_driver *dp;
 
	if (dp = isdp->id_driver) {
		if (isdp->id_maddr) {
			extern int atdevbase;

			isdp->id_maddr -= 0xa0000;
			isdp->id_maddr += atdevbase;
		}
		isdp->id_alive = (*dp->probe)(isdp);
		if (isdp->id_alive) {
			printf("%s%d", dp->name, isdp->id_unit);
			(*dp->attach)(isdp);
			printf(" at 0x%x ", isdp->id_iobase);
			if(isdp->id_irq) {
				int intrno;

				intrno = ffs(isdp->id_irq)-1;
				printf("irq %d ", intrno);
				INTREN(isdp->id_irq);
				if(mp)INTRMASK(*mp,isdp->id_irq);
				setidt(ICU_OFFSET+intrno, isdp->id_intr,
					 SDT_SYS386IGT, SEL_KPL);
			}
			if (isdp->id_drq != -1) printf("drq %d ", isdp->id_drq);
			printf("on isa\n");
		}
		return (1);
	} else	return(0);
}

#define	IDTVEC(name)	__CONCAT(X,name)
/* default interrupt vector table */
extern	IDTVEC(intr0), IDTVEC(intr1), IDTVEC(intr2), IDTVEC(intr3),
	IDTVEC(intr4), IDTVEC(intr5), IDTVEC(intr6), IDTVEC(intr7),
	IDTVEC(intr8), IDTVEC(intr9), IDTVEC(intr10), IDTVEC(intr11),
	IDTVEC(intr12), IDTVEC(intr13), IDTVEC(intr14), IDTVEC(intr15);
	
/*
 * Fill in default interrupt table (in case of spuruious interrupt
 * during configuration of kernel, setup interrupt control unit
 */
/* 中文：填充默认中断向量表，为两级 8259 的 16 个 IRQ 安装 IDT 中断门并初始化两片 8259 控制器 */
/* EN: Fill the default interrupt vector table by installing IDT gates for both 8259's 16 IRQs and initializing the two 8259 controllers */
isa_defaultirq() {

/* first icu */
	setidt(32, &IDTVEC(intr0),  SDT_SYS386IGT, SEL_KPL);
	setidt(33, &IDTVEC(intr1),  SDT_SYS386IGT, SEL_KPL);
	setidt(34, &IDTVEC(intr2),  SDT_SYS386IGT, SEL_KPL);
	setidt(35, &IDTVEC(intr3),  SDT_SYS386IGT, SEL_KPL);
	setidt(36, &IDTVEC(intr4),  SDT_SYS386IGT, SEL_KPL);
	setidt(37, &IDTVEC(intr5),  SDT_SYS386IGT, SEL_KPL);
	setidt(38, &IDTVEC(intr6),  SDT_SYS386IGT, SEL_KPL);
	setidt(39, &IDTVEC(intr7),  SDT_SYS386IGT, SEL_KPL);

/* second icu */
	setidt(40, &IDTVEC(intr8),  SDT_SYS386IGT, SEL_KPL);
	setidt(41, &IDTVEC(intr9),  SDT_SYS386IGT, SEL_KPL);
	setidt(42, &IDTVEC(intr10),  SDT_SYS386IGT, SEL_KPL);
	setidt(43, &IDTVEC(intr11),  SDT_SYS386IGT, SEL_KPL);
	setidt(44, &IDTVEC(intr12),  SDT_SYS386IGT, SEL_KPL);
	setidt(45, &IDTVEC(intr13),  SDT_SYS386IGT, SEL_KPL);
	setidt(46, &IDTVEC(intr14),  SDT_SYS386IGT, SEL_KPL);
	setidt(47, &IDTVEC(intr15),  SDT_SYS386IGT, SEL_KPL);

	/* initialize 8259's */
	outb(0xf1,0);
	outb(0x20,0x11);
	outb(0x21,32);
	outb(0x21,4);
	outb(0x21,1);
	outb(0x21,0xff);

	outb(0xa0,0x11);
	outb(0xa1,40);
	outb(0xa1,2);
	outb(0xa1,1);
	outb(0xa1,0xff);
}

/* stuff needed for virtual to physical calculations */

struct buf *dma_bounce[8];
#define MAXDMASZ 512

/* XXX temporary crud */
/* EN: Pure predicate returning 1 when address x is at or above KERNBASE (kernel virtual space) and 0 otherwise; used to decide if DMA needs a bounce buffer. */
/* 中文：纯判断函数：地址 x 位于 KERNBASE 及以上的内核虚拟空间时返回 1，否则返回 0，用于决定 DMA 是否需要弹跳缓冲区。 */
kernel_space(x)
unsigned long x;
{
	if (x >= KERNBASE) return 1;
	else return 0;
}


/****************************************************************************/
/*                                 at_dma                                   */
/* set up DMA read/write operation and virtual address addr for nbytes      */
/****************************************************************************/
/* EN: Programs 8237 DMA channel chan: user-space addresses go through dma_bounce[chan] (geteblk buffer, copied on write); writes mode, pmap_extract'd address and count-1. */
/* 中文：为 8237 DMA 通道 chan 编程：用户空间地址改用 geteblk 分配的 dma_bounce[chan] 并在写时拷贝，再写模式、pmap_extract 物理地址与计数减一。 */
at_dma(read,addr,nbytes, chan)
int read;
unsigned long addr;
int nbytes;
{
	unsigned long phys;
	int s,raw;
	caddr_t bounce;

	if (kernel_space(addr)) raw = 0;
	else raw = 1;

	if(raw) {
		if (dma_bounce[chan] == 0)
			dma_bounce[chan] = geteblk(MAXDMASZ);
		bounce = dma_bounce[chan]->b_un.b_addr;
	}

	/* copy bounce buffer on write */
	if (raw && !read) bcopy(addr,bounce,nbytes);

	/* Set read/write bytes */
	if (read) {
		outb(0xC,0x46); outb(0xB,0x46);
	} else {
		outb(0xC,0x4A); outb(0xB,0x4A);
	}
	/* Send start address */
	if (raw) phys = (unsigned long) bounce;
	else phys = addr;
	/* translate to physical */
	phys = pmap_extract(kernel_pmap, (vm_offset_t)phys);
	outb(0x4,phys & 0xFF);
	outb(0x4,(phys>>8) & 0xFF);
	outb(0x81,(phys>>16) & 0xFF);
	/* Send count */
	nbytes--;
	outb(0x5,nbytes & 0xFF);
	outb(0x5,(nbytes>>8) & 0xFF);
	/* set channel 2 */
	outb(0x0A,chan);
}

/*
 * Handle a NMI, possibly a machine check.
 * return true to panic system, false to ignore.
 */
/* 中文：处理 NMI（可能是机器校验）：以 LOG_CRIT 记录 0x61 与 0x70 端口值，返回 0 表示忽略而不触发 panic */
/* EN: Handle an NMI (possibly a machine check): log port 0x61 and 0x70 at LOG_CRIT and return 0 to ignore it rather than panic */
isa_nmi(cd) {

	log(LOG_CRIT, "\nNMI port 61 %x, port 70 %x\n", inb(0x61), inb(0x70));
	return(0);
}

/*
 * Caught a stray interrupt, notify
 */
/* 中文：处理意外（杂散）中断：以 LOG_ERR 记录由 d 解码出的中断号 */
/* EN: Handle a stray interrupt: log at LOG_ERR the interrupt number decoded from d via ffs */
isa_strayintr(d) {

	/* for some reason, we get bursts of intr #7, even if not enabled! */
	log(LOG_ERR,"ISA strayintr %d", ffs(d)-1);
}
