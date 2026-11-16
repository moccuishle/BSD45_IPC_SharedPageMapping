/*
 * Copyright (c) 1982, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)if_apx.c	8.1 (Berkeley) 6/11/93
 */

/*
 * Driver for SGS-THOMSON MK5025 based Link level controller.
 * The chip will do LAPB in hardware, although this driver only
 * attempts to use it for HDLC framing.
 *
 * Driver written by Keith Sklower, based on lance AMD7990 
 * driver by Van Jacobsen, and information graciously supplied
 * by the ADAX corporation of Berkeley, CA.
 */

#include "apx.h"
#if NAPX > 0

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/netisr.h>
#include <net/if_types.h>
#ifdef CCITT
#include <netccitt/x25.h>
int x25_rtrequest(), x25_ifoutput();
#endif

#include <i386/isa/if_apxreg.h>

int	apxprobe(), apxattach(), apxstart(), apx_uprim(), apx_meminit();
int	apxinit(), apxoutput(), apxioctl(), apxreset(), apxdebug = 0;
void	apx_ifattach(), apxtest(), apxinput(), apxintr(), apxtint(), apxrint();

struct apx_softc {
	struct	ifnet apx_if;
	caddr_t	apx_device;		/* e.g. isa_device, vme_device, etc. */
	struct	apc_reg *apx_reg;	/* control regs for both subunits */
	struct	apc_mem *apx_hmem;	/* Host addr for shared memory */
	struct	apc_mem *apx_dmem;	/* Device (chip) addr for shared mem */
	struct	sgcp *apx_sgcp;		/* IO control port for this subunit */
	int	apx_flags;		/* Flags specific to this driver */
#define APXF_CHIPHERE	0x01		/* mk5025 present */
	int	apx_rxnum;		/* Last receiver dx we looked at */
	int	apx_txnum;		/* Last tranmistter dx we stomped on */
	int	apx_txcnt;		/* Number of packets queued for tx*/
	u_int	apx_msize;
	struct	sgae apx_csr23;		/* 24 bit init addr, as seen by chip */
	u_short	apx_csr4;		/* byte gender, set in mach dep code */
	struct	apc_modes apx_modes;	/* Parameters, as amended by ioctls */
} apx_softc[2 * NAPX];

struct apxstat {
	int	rxnull;			/* no rx bufs ready this interrupt */
	int	rxnrdy;			/* expected rx buf not ready */
	int	rx2big;			/* expected rx buf not ready */
	int	txnull;
	int	pint;			/* new primitive available interrupt */
	int	rint;			/* receive interrupts */
	int	tint;			/* transmit interrupts */
	int	anyint;			/* note all interrupts */
	int	queued;			/* got through apxinput */
	int	nxpctd;			/* received while if was down */
	int	rstfld;			/* reset didn't work */
} apxstat;

/* default operating paramters for devices */
struct	apc_modes apx_default_modes = {
 { 1,		/* apm_sgob.lsaddr; */
   3,		/* apm_sgob.rsaddr; */
   -SGMTU,	/* apm_sgob.n1; */
   ((-10)<<8),	/* apm_sgob.n2_scale; */
   -1250,	/* apm_sgob.t1; */
   -10000,	/* apm_sgob.t3; */
   -80,		/* apm_sgob.tp; */
 },
 2,		/* apm_txwin; */
 1,		/* apm_apxmode: RS_232 connector and modem clock; */
 0,		/* apm_apxaltmode: enable dtr, disable X.21 connector; */
 IFT_X25,	/* apm_iftype; */
};

/* Begin bus & endian dependence */

#include <i386/isa/isa_device.h>

struct	isa_driver apxdriver = {
	apxprobe, apxattach, "apx",
};

#define SG_RCSR(apx, csrnum) \
	 (outw(&(apx->apx_sgcp->sgcp_rap), csrnum << 1), \
	  inw(&(apx->apx_sgcp->sgcp_rdp)))

#define SG_WCSR(apx, csrnum, data) \
	 (outw(&(apx->apx_sgcp->sgcp_rap), csrnum << 1), \
	  outw(&(apx->apx_sgcp->sgcp_rdp), data))

#define APX_RCSR(apx, csrname) inb(&(apx->apx_reg->csrname))
#define APX_WCSR(apx, csrname, data) outb(&(apx->apx_reg->csrname), data)

#define TIMO 10000 /* used in apx_uprim */

/* EN: Probes an Adax ISA board: for one or two MK5025 chips (id_msize bit 0 = single chip) fills apx_softc reg/hmem/dmem/modes/csr4 0x0210 and unit numbers, runs apxtest, returns 1. */
/* 中文：探测 Adax ISA 板：按 id_msize 最低位判断单/双 MK5025 芯片，为每个芯片填写 apx_softc 的 msize/hmem/dmem/device/reg/sgcp、csr4 = 0x0210 及单元号，调用 apxtest，返回 1。 */
apxprobe(id)
	register struct	isa_device *id;
{
	int	moffset = 0, nchips = 2, unit = id->id_unit << 1, subunit;
	struct	apc_reg *reg = (struct apc_reg *)id->id_iobase;
	register struct	apx_softc *apx = apx_softc + unit;

	/*
	 * Probing for the second MK5025 on all ISA/EISA adax boards
	 * manufactured prior to July 1992 (and some time following)
	 * will hang the bus and the system.  Thus, it is essential
	 * not to probe for the second mk5025 if it is known not to be there.
	 * As the current config scheme for 386BSD does not have a flags
	 * field, we adopt the convention of using the low order bit of
	 * the memsize to warn us that we have a single chip board.
	 */
	if (id->id_msize & 1)
		nchips = 1;
	for (subunit = 0; subunit < nchips; subunit++) {
		apx->apx_msize	= id->id_msize >> 1;
		apx->apx_hmem	= (struct apc_mem *) (id->id_maddr + moffset);
		apx->apx_dmem	= (struct apc_mem *) moffset;
		apx->apx_device	= (caddr_t) id;
		apx->apx_reg	= reg;
		apx->apx_sgcp	= reg->axr_sgcp + subunit;
		apx->apx_csr4	= 0x0210;	/* no byte swapping for PC-AT */
		apx->apx_modes	= apx_default_modes;
		apx->apx_if.if_unit = unit++;
		moffset = apx->apx_msize;
		apxtest(apx++);
	}
	return 1;
}

/* EN: Attach hook that calls apx_ifattach on both ifnets of the board's unit pair (id_unit << 1 and the next one), then returns 0. */
/* 中文：挂接钩子，对该板单元对（id_unit << 1 及其下一个）对应的两个 ifnet 分别调用 apx_ifattach，然后返回 0。 */
apxattach(id)
	struct	isa_device *id;
{
	register struct	apx_softc *apx = apx_softc + (id->id_unit << 1);

	apx_ifattach(&((apx++)->apx_if));
	apx_ifattach(&(apx->apx_if));
	return 0;
}
/* End bus & endian dependence */

/*
 * Interface exists: make available by filling in network interface
 * record.  System will initialize the interface when it is ready
 * to accept packets.
 */
/* EN: Fills the ifnet record: name "apc", mtu SGMTU, init/output/start/ioctl/reset pointing at apx routines, if_type from apx_default_modes, hdrlen 5, addrlen 8, then calls if_attach. */
/* 中文：填充 ifnet 记录：名字 "apc"、mtu 为 SGMTU、初始化/输出/启动/ioctl/复位指向 apx 各函数、if_type 取自 apx_default_modes、hdrlen 5、addrlen 8，最后调用 if_attach。 */
void
apx_ifattach(ifp)
	register struct ifnet *ifp;
{
	/*
	 * Initialize ifnet structure
	 */
	ifp->if_name	= "apc";
	ifp->if_mtu	= SGMTU;
	ifp->if_init	= apxinit;
	ifp->if_output	= apxoutput;
	ifp->if_start	= apxstart;
	ifp->if_ioctl	= apxioctl;
	ifp->if_reset	= apxreset;
	ifp->if_type	= apx_default_modes.apm_iftype;
	ifp->if_hdrlen	= 5;
	ifp->if_addrlen	= 8;
	if_attach(ifp);
}
/*
 * Initialization of interface
 */
/* EN: Interface init: under splimp clears IFF_RUNNING/IFF_OACTIVE, and if apxreset(unit) succeeds while IFF_UP is set marks IFF_RUNNING and calls apxstart; returns 0 always. */
/* 中文：接口初始化：在 splimp 下清除 IFF_RUNNING/IFF_OACTIVE，若 apxreset(unit) 成功且 IFF_UP 已置则置 IFF_RUNNING 并调用 apxstart；始终返回 0。 */
apxinit(unit)
	int unit;
{
	struct ifnet *ifp = &apx_softc[unit].apx_if;
	int s = splimp();

	ifp->if_flags &= ~(IFF_RUNNING|IFF_OACTIVE);
	if (apxreset(unit) && (ifp->if_flags & IFF_UP)) {
		ifp->if_flags |= IFF_RUNNING;
		(void)apxstart(ifp);
	}
	splx(s);
	return 0;
}

/* EN: Programs timer counter 2 via CCR 0xB0, writes 1 then 0, delays 50us, then latches status with 0xE8 and returns the counter value read from axr_cnt2. */
/* 中文：通过 CCR 0xB0 配置定时器计数器 2,先写 1 再写 0,延时 50 微秒后用 0xE8 锁存状态,返回从 axr_cnt2 读出的计数值。 */
apxctr(apx)
	register struct apx_softc *apx;
{
	APX_WCSR(apx, axr_ccr, 0xB0); /* select ctr 2, write lsb+msb, mode 0 */
	APX_WCSR(apx, axr_cnt2, 0x1);
	APX_WCSR(apx, axr_cnt2, 0x0);
	DELAY(50);
	APX_WCSR(apx, axr_ccr, 0xE8); /* latch status, ctr 2; */
	return (APX_RCSR(apx, axr_cnt2));
}

/* EN: Probes the board: checks the timer counter on even units, programs SGS CSR4/CSR5, verifies csr5 high bits to set APXF_CHIPHERE, then issues SG_STOP. */
/* 中文：探测板卡:对偶数单元检查定时器计数,写 SGS 的 CSR4/CSR5 并用 csr5 高位校验 5025 芯片,成功则置 APXF_CHIPHERE,最后发 SG_STOP 停止。 */
void
apxtest(apx)
	register struct apx_softc *apx;
{
	int i =  0;

	if ((apx->apx_if.if_unit & 1) == 0 && (i = apxctr(apx)) == 0)
		apxerror(apx, "no response from timer chip", 0);
	if (SG_RCSR(apx, 1) & 0x8000)
		SG_WCSR(apx, 1, 0x8040);
	SG_WCSR(apx, 4, apx->apx_csr4);
	SG_WCSR(apx, 5, 0x08);		/* Set DTR mode in SGS thompson chip */
	if (((i = SG_RCSR(apx, 5)) & 0xff08) != 0x08)
		apxerror(apx, "no mk5025, csr5 high bits are", i);
	else
		apx->apx_flags |= APXF_CHIPHERE;
	(void) apx_uprim(apx, SG_STOP, "stop after probing");
}

/* EN: Merges both units' mode bytes into the CSRs and stops the chip; if IFF_UP is set it re-inits memory, sends SG_INIT/SG_STAT/SG_TRANS, enables IRQs and returns 1, else 0. */
/* 中文：先把配对两单元的 mode/altmode 字节合并写入 CSR 并停止芯片;若 IFF_UP 已置则重新初始化内存、依次发 SG_INIT/SG_STAT/SG_TRANS 并开中断,成功返回 1,否则返回 0。 */
apxreset(unit)
	int	unit;
{
	register struct apx_softc *apx = &apx_softc[unit ^ 1];
	u_char apm_apxmode = 0, apm_apxaltmode = 0;
#define MODE(m) (m |= apx->apx_modes.m << ((apx->apx_if.if_unit & 1) ? 1 : 0))

	MODE(apm_apxmode);
	MODE(apm_apxaltmode);
	apx = apx_softc + unit;
	MODE(apm_apxmode);
	MODE(apm_apxaltmode);
	APX_WCSR(apx, axr_mode, apm_apxmode);
	APX_WCSR(apx, axr_altmode, apm_apxaltmode);
	(void) apxctr(apx);
	(void) apx_uprim(apx, SG_STOP, "stop to reset");
	if ((apx->apx_if.if_flags & IFF_UP) == 0)
		return 0;
	apx_meminit(apx->apx_hmem, apx);
	SG_WCSR(apx, 4, apx->apx_csr4);
	SG_WCSR(apx, 2, apx->apx_csr23.f_hi);
	SG_WCSR(apx, 3, apx->apx_csr23.lo);
	if (apx_uprim(apx, SG_INIT, "init request") ||
	    apx_uprim(apx, SG_STAT, "status request") ||
	    apx_uprim(apx, SG_TRANS, "transparent mode"))
		return 0;
	SG_WCSR(apx, 0, SG_INEA);
	return 1;
}

/* EN: Sends an SGS command with SG_UAV and polls CSR1 until busy clears; returns 0 on success, 1 on missing chip, timeout or error, and early 0 for SG_STOP when already stopped. */
/* 中文：发送带 SG_UAV 的 SGS 命令并轮询 CSR1 直到忙位清除;成功返回 0,无芯片、超时或出错返回 1,SG_STOP 且芯片已停止时提前返回 0。 */
apx_uprim(apx, request, ident)
	register struct apx_softc *apx;
	char *ident;
{
	register int timo = 0;
	int reply;

	if ((apx->apx_flags & APXF_CHIPHERE) == 0)
		return 1;	/* maybe even should panic . . . */
	if ((reply = SG_RCSR(apx, 1)) & 0x8040)
		SG_WCSR(apx, 1, 0x8040); /* Magic! */
	if (request == SG_STOP && (SG_RCSR(apx, 0) & SG_STOPPED))
		return 0;
	SG_WCSR(apx, 1, request | SG_UAV);
	do {
		reply = SG_RCSR(apx, 1);
		if (timo++ >= TIMO || (reply & 0x8000)) {
				apxerror(apx, ident, reply);
				return 1;
		}
	} while (reply & SG_UAV);
	return 0;
}

/* EN: Initializes shared memory: zeroes the header, sets mode 0x0108 and csr23, and fills the TX/RX descriptors and buffers as chip-owned via SET_SGAE/SET_SGDX. */
/* 中文：初始化共享内存:清零头部,设置 mode 0x0108 与 csr23,并用 SET_SGAE/SET_SGDX 把收发描述符及缓冲区填成芯片所有。 */
apx_meminit(apc, apx)
	register struct apc_mem *apc;
	struct apx_softc *apx;
{
	register struct apc_mem *apcbase = apx->apx_dmem;
	register int i;
#define LOWADDR(e) (((u_long)&(apcbase->e)) & 0xffff)
#define HIADDR(e) ((((u_long)&(apcbase->e)) >> 16) & 0xff)
#define SET_SGAE(d, f, a) {(d).lo = LOWADDR(a); (d).f_hi = (f) | HIADDR(a);}
#define SET_SGDX(d, f, a, b) \
	{SET_SGAE((d).sgdx_ae, f, a); (d).sgdx_mcnt = (d).sgdx_bcnt = (b);}

	apx->apx_txnum = apx->apx_rxnum = apx->apx_txcnt = 0;
	bzero((caddr_t)apc, ((caddr_t)(&apc->apc_rxmd[0])) - (caddr_t)apc);
	apc->apc_mode = 0x0108;	/* 2 flag spacing, leave addr & ctl, do CRC16 */
	apc->apc_sgop = apx->apx_modes.apm_sgop;
	SET_SGAE(apx->apx_csr23, SG_UIE | SG_PROM, apc_mode);
	SET_SGAE(apc->apc_rxdd, SG_RLEN, apc_rxmd[0]);
	i = SG_TLEN | ((apx->apx_modes.apm_txwin)<< 8);
	SET_SGAE(apc->apc_txdd, i, apc_txmd[0]);
	SET_SGAE(apc->apc_stdd, 0, apc_sgsb);
	SET_SGDX(apc->apc_rxtid, SG_OWN, apc_rxidbuf[0], -SGMTU);
	SET_SGDX(apc->apc_txtid, 0, apc_txidbuf[0], 0);
	for (i = 0; i < SGRBUF; i++)
		 SET_SGDX(apc->apc_rxmd[i], SG_OWN, apc_rbuf[i][0], -SGMTU)
	for (i = 0; i < SGTBUF; i++)
		 SET_SGDX(apc->apc_txmd[i], 0, apc_tbuf[i][0], 0)
}

/*
 * Start output on interface.  Get another datagram to send
 * off of the interface queue, and copy it to the interface
 * before starting the output.
 */
/* EN: Copies queued mbufs (up to SGMTU each) into the TX ring, frees them, sets OWN/TUI/SLF/ELF and issues SG_TDMD; always returns 0, marking IFF_OACTIVE when the ring fills. */
/* 中文：从 if_snd 取出 mbuf(每个最多 SGMTU 字节)拷入发送环并释放 mbuf,置 OWN/TUI/SLF/ELF 并发 SG_TDMD;始终返回 0,环满时置 IFF_OACTIVE。 */
apxstart(ifp)
	struct ifnet *ifp;
{
	register struct apx_softc *apx = &apx_softc[ifp->if_unit];
	register struct sgdx *dx;
	struct apc_mem *apc = apx->apx_hmem;
	struct mbuf *m;
	int len;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return (0);
	do {
		dx = apc->apc_txmd + apx->apx_txnum;
		if (dx->sgdx_flags & SG_OWN)
			return (0);
		IF_DEQUEUE(&ifp->if_snd, m);
		if (m == 0)
			return (0);
		len = min(m->m_pkthdr.len, SGMTU);
		m_copydata(m, 0, len, apc->apc_tbuf[apx->apx_txnum]);
		m_freem(m);
		dx->sgdx_mcnt = -len;
		dx->sgdx_flags = (SG_OWN|SG_TUI|SG_SLF|SG_ELF) |
			(0xff & dx->sgdx_flags);
		SG_WCSR(apx, 0, SG_INEA | SG_TDMD);
		DELAY(20);
		if (++apx->apx_txnum >= SGTBUF)
			apx->apx_txnum = 0;
	} while (++apx->apx_txcnt < SGTBUF);
	apx->apx_txcnt = SGTBUF; /* in case txcnt > SGTBUF by mistake */
	ifp->if_flags |= IFF_OACTIVE;
	return (0);
}

/* EN: Scans both units' CSR0 interrupt causes, acknowledges with SG_INEA|0xfe, re-inits via apxinit on mem/rx/tx errors, otherwise dispatches apxrint/apxtint and counts PINT. */
/* 中文：扫描两个单元的 CSR0 中断原因并用 SG_INEA|0xfe 应答,内存/收/发错误时记日志并经 apxinit 重新初始化,否则分派 apxrint/apxtint 并统计 PINT。 */
void
apxintr()
{
	register struct apx_softc *apx;
	int reply;

	apxstat.anyint++;
	for (apx = apx_softc + NAPX + NAPX; --apx >= apx_softc;) {
		if (apx->apx_flags & APXF_CHIPHERE)
		    /* Try to turn off interrupt cause */
		    while ((reply = SG_RCSR(apx, 0)) & 0xff) {
			SG_WCSR(apx, 0, SG_INEA | 0xfe);
			if (reply & (SG_MERR|SG_TUR|SG_ROR)) {
				apxerror(apx, "mem, rx, or tx error", reply);
				apxinit(apx->apx_if.if_unit);
				break;
			}
			if (reply & SG_RINT)
				apxrint(apx);
			if (reply & SG_TINT)
				apxtint(apx);
			if (reply & SG_PINT)
				apxstat.pint++;
		}
	}
}

/* EN: Handles transmit-done: locates the completed descriptor, returns early as txnull if still chip-owned, clears IFF_OACTIVE, decrements txcnt and restarts output via apxstart. */
/* 中文：处理发送完成:定位已完成描述符,若仍属芯片则记为 txnull 提前返回;否则清 IFF_OACTIVE、递减 txcnt 并调用 apxstart 继续发送。 */
void
apxtint(apx)
	register struct apx_softc *apx;
{
	register struct apc_mem *apc = apx->apx_hmem;
	int i, loopcount = 0;

	apxstat.tint++;
	do {
		if ((i = apx->apx_txnum - apx->apx_txcnt) < 0)
			i += SGTBUF;
		if (apc->apc_txmd[i].sgdx_flags & SG_OWN) {
			if (loopcount)
				break;
			apxstat.txnull++;
			return;
		}
		loopcount++;
		apx->apx_if.if_flags &= ~IFF_OACTIVE;
	} while (--apx->apx_txcnt > 0);
	apxstart(&apx->apx_if);
}

/* EN: Receive handler: skips still-owned descriptors, drops chained buffers and re-syncs (calling apxreset on mismatch), passes good frames to apxinput, advancing apx_rxnum. */
/* 中文：接收处理:跳过仍属芯片的描述符,丢弃链式缓冲并在失步时调用 apxreset 复位,正常帧交给 apxinput,并推进 rxnum。 */
void
apxrint(apx)
	register struct apx_softc *apx;
{
	register struct apc_mem *apc = apx->apx_hmem;
	register struct sgdx *dx = apc->apc_rxmd + apx->apx_rxnum;
	int i = 0;
#define SGNEXTRXMD \
dx = ++apx->apx_rxnum == SGRBUF ? &apc->apc_rxmd[apx->apx_rxnum = 0] : dx + 1;

	apxstat.rint++;
	/*
	 * Out of sync with hardware, should never happen?
	 */
	while (dx->sgdx_flags & SG_OWN) {
		apxstat.rxnrdy++;
		if (++i == SGRBUF) {
			apxstat.rxnull++;
			return;
		}
		SGNEXTRXMD;
	}
	/*
	 * Process all buffers with valid data
	 */
	while ((dx->sgdx_flags & SG_OWN) == 0) {
		if ((dx->sgdx_flags & (SG_SLF|SG_ELF)) != (SG_SLF|SG_ELF)) {
			/*
			 * Find the end of the packet so we synch up.
			 * We throw the data away.
			 */
			apxerror(apx, "chained buffer", dx->sgdx_flags);
			do {
				apxstat.rx2big++;
				dx->sgdx_bcnt = 0;
				dx->sgdx_flags = SG_OWN | (0xff&dx->sgdx_flags);
				SGNEXTRXMD;
			} while (!(dx->sgdx_flags & (SG_OWN|SG_SLF|SG_ELF)));
			/*
			 * If search terminated without successful completion
			 * we reset the hardware (conservative).
			 */
			if ((dx->sgdx_flags & (SG_OWN|SG_SLF|SG_ELF)) !=
			    SG_ELF) {
				apxreset(apx->apx_if.if_unit);
				return;
			}
		} else
			apxinput(&apx->apx_if, apc->apc_rbuf[apx->apx_rxnum],
					-dx->sgdx_mcnt);
		dx->sgdx_bcnt = 0;
		dx->sgdx_flags = SG_OWN | (0xff & dx->sgdx_flags);
		SGNEXTRXMD;
	}
}

/* EN: Wraps a received frame into an mbuf and queues it on ipintrq for NETISR_IP after stripping the 4-byte PPP HDLC header, dropping and counting when not IFF_UP or the queue is full. */
/* 中文：把收到的帧封装成 mbuf,去掉 4 字节 PPP HDLC 头后挂到 ipintrq 并 schednetisr(NETISR_IP);接口未 UP 或队列满时计数并丢弃。 */
void
apxinput(ifp, buffer, len)
	register struct ifnet *ifp;
	caddr_t buffer;
{
	extern struct ifqueue hdintrq, ipintrq;
	register struct ifqueue *inq;
	register u_char *cp = (u_char *)buffer;
	struct mbuf *m, *m_devget();
	int isr;

	ifp->if_ipackets++;
	if ((ifp->if_flags & IFF_UP) == 0) {
		apxstat.nxpctd++;
		return;
	}
	if (cp[0] == 0xff && cp[1] == 0x3) {
		/* This is a UI HDLC Packet, so we'll assume PPP
		   protocol.  for now, IP only. */
		buffer += 4;
		len -= 4;
		inq = &ipintrq;
		isr = NETISR_IP;
	} else {
#ifdef CCITT
		inq = &hdintrq;
		isr = NETISR_CCITT;
	}
	if (len <= 0) {
#endif
		return;
	}
	m = m_devget(buffer, len, 0, ifp, (void (*)())0);
	if (m == 0)
		return;
	if(IF_QFULL(inq)) {
		IF_DROP(inq);
		m_freem(m);
	} else {
		apxstat.queued++;
		IF_ENQUEUE(inq, m);
		schednetisr(isr);
	}
}

/*
 * Process an ioctl request.
 */
/* EN: Handles SIOCSIFADDR, SIOCSIFFLAGS (re-inits on up/down change) and SIOCSIFMODE (saves apx_modes only while down, else EINVAL) at splimp; returns the error code. */
/* 中文：在 splimp 下处理 SIOCSIFADDR、SIOCSIFFLAGS(上下线切换时重新初始化)和 SIOCSIFMODE(仅下线时保存 apx_modes,否则 EINVAL),返回错误码。 */
apxioctl(ifp, cmd, data)
	register struct ifnet *ifp;
	int cmd;
	caddr_t data;
{
	register struct ifaddr *ifa = (struct ifaddr *)data;
	int s = splimp(), error = 0;
	struct apx_softc *apx = &apx_softc[ifp->if_unit];

	switch (cmd) {

	case SIOCSIFADDR:
#ifdef CCITT
		ifa->ifa_rtrequest = x25_rtrequest;
		break;

	case SIOCSIFCONF_X25:
		ifp->if_output = x25_ifoutput;
		ifp->if_flags |= IFF_UP;
		error = hd_ctlinput(PRC_IFUP, ifa->ifa_addr);
		if (error == 0)
			apxinit(ifp->if_unit);
#endif
		break;

	case SIOCSIFFLAGS:
		if (((ifp->if_flags & IFF_UP) == 0 &&
		     (ifp->if_flags & IFF_RUNNING)) ||
		    (ifp->if_flags & IFF_UP) &&
		     (ifp->if_flags & IFF_RUNNING) == 0)
			apxinit(ifp->if_unit);
		break;

	case SIOCSIFMODE:
		if ((ifp->if_flags & IFF_UP) == 0)
			apx->apx_modes = *(struct apc_modes *)data;
		else
	default:
			error = EINVAL;

	}
	splx(s);
	return (error);
}

/* EN: Logs a LOG_WARNING line naming the apc unit, the caller's message and the status word in hex; changes no state and returns nothing. */
/* 中文：用 LOG_WARNING 打印一行日志,包含 apc 单元号、调用者消息和十六进制状态字;不改变任何状态,无返回值。 */
apxerror(apx, msg, data)
	register struct	apx_softc *apx;	
	char	*msg;
{
	log(LOG_WARNING, "apc%d: %s, stat=0x%x\n",
		apx->apx_if.if_unit, msg, data);
}

/*
 * For debugging loopback activity.
 */
/* EN: Bumps if_opackets, prepends the 4-byte PPP header {-1,3,0,0x21}, queues on if_snd and kicks if_start unless IFF_OACTIVE; returns ENOBUFS on mbuf or queue shortage. */
/* 中文：递增 if_opackets,前置 4 字节 PPP 头 {-1,3,0,0x21} 后挂到 if_snd,非 IFF_OACTIVE 时调用 if_start;分配失败或队列满返回 ENOBUFS。 */
apxoutput(ifp, m, dst, rt)
register struct ifnet *ifp;
register struct mbuf *m;
struct sockaddr *dst;
struct rtentry *rt;
{
	int s = splimp(), error = 0;
	static char pppheader[4] = { -1, 3, 0, 0x21 };
	/*
	 * Queue message on interface, and start output if interface
	 * not yet active.
	 */
	ifp->if_opackets++;
	M_PREPEND(m, sizeof pppheader, M_DONTWAIT);
	if (m == 0) {
		splx(s);
		return ENOBUFS;
	}
	bcopy(pppheader, mtod(m, caddr_t), sizeof pppheader);
	if (IF_QFULL(&ifp->if_snd)) {
		IF_DROP(&ifp->if_snd);
		m_freem(m);
		error = ENOBUFS;
	} else {
		IF_ENQUEUE(&ifp->if_snd, m);
		if ((ifp->if_flags & IFF_OACTIVE) == 0)
			(*ifp->if_start)(ifp);
	}
	splx(s);
	return (error);
}
#endif /* NAPX */
