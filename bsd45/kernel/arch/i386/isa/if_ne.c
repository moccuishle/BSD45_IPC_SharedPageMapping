/*-
 * Copyright (c) 1990, 1991 William F. Jolitz.
 * Copyright (c) 1990, 1993
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
 *	@(#)if_ne.c	8.1 (Berkeley) 6/11/93
 */

/*
 * NE2000 Ethernet driver
 *
 * Parts inspired from Tim Tucker's if_wd driver for the wd8003,
 * insight on the ne2000 gained from Robert Clements PC/FTP driver.
 */

#include "ne.h"
#if NNE > 0

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/buf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/netisr.h>
#include <net/route.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#endif

#ifdef NS
#include <netns/ns.h>
#include <netns/ns_if.h>
#endif

#include <i386/isa/isa_device.h>
#include <i386/isa/if_nereg.h>
#include <i386/isa/icu.h>

int	neprobe(), neattach(), neintr();
int	nestart(),neinit(), ether_output(), neioctl();

struct	isa_driver nedriver = {
	neprobe, neattach, "ne",
};

struct	mbuf *neget();

#define ETHER_MIN_LEN 64
#define ETHER_MAX_LEN 1536

/*
 * Ethernet software status per interface.
 *
 * Each interface is referenced by a network interface structure,
 * ns_if, which the routing code uses to locate the interface.
 * This structure contains the output queue for the interface, its address, ...
 */
struct	ne_softc {
	struct	arpcom ns_ac;		/* Ethernet common part */
#define	ns_if	ns_ac.ac_if		/* network-visible interface */
#define	ns_addr	ns_ac.ac_enaddr		/* hardware Ethernet address */
	int	ns_flags;
#define	DSF_LOCK	1		/* block re-entering enstart */
	int	ns_oactive ;
	int	ns_mask ;
	int	ns_ba;			/* byte addr in buffer ram of inc pkt */
	int	ns_cur;			/* current page being filled */
	struct	prhdr	ns_ph;		/* hardware header of incoming packet*/
	struct	ether_header ns_eh;	/* header of incoming packet */
	u_char	ns_pb[2048 /*ETHERMTU+sizeof(long)*/];
} ne_softc[NNE] ;
#define	ENBUFSIZE	(sizeof(struct ether_header) + ETHERMTU + 2 + ETHER_MIN_LEN)

int nec;

u_short boarddata[16];
 
/* EN: At splimp resets the NE2000 at id_iobase, polls ISR for DSIS_RESET, sets word-transfer DCR, checks command readback, programs ring bounds, copies ROM MAC to ns_addr; 1 or 0. */
/* 中文：在 splimp 下复位 id_iobase 处的 NE2000，轮询 ISR 等待 DSIS_RESET，设字传输 DCR、回读命令寄存器、设环形边界，并把 ROM 中的 MAC 拷到 ns_addr；成功返回 1，否则 0。 */
neprobe(dvp)
	struct isa_device *dvp;
{
	int val,i,s;
	register struct ne_softc *ns = &ne_softc[0];

#ifdef lint
	neintr(0);
#endif

	nec = dvp->id_iobase;
	s = splimp();

	/* Reset the bastard */
	val = inb(nec+ne_reset);
	DELAY(2000000);
	outb(nec+ne_reset,val);

	outb(nec+ds_cmd, DSCM_STOP|DSCM_NODMA);
	
	i = 1000000;
	while ((inb(nec+ds0_isr)&DSIS_RESET) == 0 && i-- > 0);
	if (i < 0) return (0);

	outb(nec+ds0_isr, 0xff);

	/* Word Transfers, Burst Mode Select, Fifo at 8 bytes */
	outb(nec+ds0_dcr, DSDC_WTS|DSDC_BMS|DSDC_FT1);

	outb(nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_STOP);
	DELAY(10000);

	/* Check cmd reg and fail if not right */
	if ((i=inb(nec+ds_cmd)) != (DSCM_NODMA|DSCM_PG0|DSCM_STOP))
		return(0);

	outb(nec+ds0_tcr, 0);
	outb(nec+ds0_rcr, DSRC_MON);
	outb(nec+ds0_pstart, RBUF/DS_PGSIZE);
	outb(nec+ds0_pstop, RBUFEND/DS_PGSIZE);
	outb(nec+ds0_bnry, RBUFEND/DS_PGSIZE);
	outb(nec+ds0_imr, 0);
	outb(nec+ds0_isr, 0);
	outb(nec+ds_cmd, DSCM_NODMA|DSCM_PG1|DSCM_STOP);
	outb(nec+ds1_curr, RBUF/DS_PGSIZE);
	outb(nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_STOP);

#ifdef NEDEBUG
#define	PAT(n)	(0xa55a + 37*(n))
#define	RCON	37
	{	int i, rom, pat;

		rom=1;
		printf("ne ram ");
		
		for (i = 0; i < 0xfff0; i+=4) {
			pat = PAT(i);
			neput(&pat,i,4);
			nefetch(&pat,i,4);
			if (pat == PAT(i)) {
				if (rom) {
					rom=0;
					printf(" %x", i);
				}
			} else {
				if (!rom) {
					rom=1;
					printf("..%x ", i);
				}
			}
			pat=0;
			neput(&pat,i,4);
		}
		printf("\n");
	}
#endif

	/* Extract board address */
	nefetch ((caddr_t)boarddata, 0, sizeof(boarddata));
	for(i=0; i < 6; i++)  ns->ns_addr[i] = boarddata[i];
	splx(s);
	return (1);
}

/*
 * Fetch from onboard ROM/RAM
 */
nefetch (up, ad, len) caddr_t up; {
	u_char cmd;

	cmd = inb(nec+ds_cmd);
	outb (nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_START);

	/* Setup remote dma */
	outb (nec+ds0_isr, DSIS_RDC);
	outb (nec+ds0_rbcr0, len);
	outb (nec+ds0_rbcr1, len>>8);
	outb (nec+ds0_rsar0, ad);
	outb (nec+ds0_rsar1, ad>>8);

	/* Execute & extract from card */
	outb (nec+ds_cmd, DSCM_RREAD|DSCM_PG0|DSCM_START);
	insw (nec+ne_data, up, len/2);

	/* Wait till done, then shutdown feature */
	while ((inb (nec+ds0_isr) & DSIS_RDC) == 0) ;
	outb (nec+ds0_isr, DSIS_RDC);
	outb (nec+ds_cmd, cmd);
}

/*
 * Put to onboard RAM
 */
neput (up, ad, len) caddr_t up; {
	u_char cmd;

	cmd = inb(nec+ds_cmd);
	outb (nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_START);

	/* Setup for remote dma */
	outb (nec+ds0_isr, DSIS_RDC);
	if(len&1) len++;		/* roundup to words */
	outb (nec+ds0_rbcr0, len);
	outb (nec+ds0_rbcr1, len>>8);
	outb (nec+ds0_rsar0, ad);
	outb (nec+ds0_rsar1, ad>>8);

	/* Execute & stuff to card */
	outb (nec+ds_cmd, DSCM_RWRITE|DSCM_PG0|DSCM_START);
	outsw (nec+ne_data, up, len/2);
	
	/* Wait till done, then shutdown feature */
	while ((inb (nec+ds0_isr) & DSIS_RDC) == 0) ;
	outb (nec+ds0_isr, DSIS_RDC);
	outb (nec+ds_cmd, cmd);
}

/*
 * Reset of interface.
 */
/* EN: Ignored for units >= NNE; prints "ne%d: reset", clears DSF_LOCK in the softc so nestart may run again, and re-enters neinit(); installed as if_reset. */
/* 中文：unit >= NNE 时直接返回；否则打印 "ne%d: reset"，清除软状态中的 DSF_LOCK 以便 nestart 能再次发送，并重新调用 neinit()；该函数挂在 if_reset。 */
nereset(unit, uban)
	int unit, uban;
{
	if (unit >= NNE)
		return;
	printf("ne%d: reset\n", unit);
	ne_softc[unit].ns_flags &= ~DSF_LOCK;
	neinit(unit);
}
 
/*
 * Interface exists: make available by filling in network interface
 * record.  System will initialize the interface when it is ready
 * to accept packets.  We get the ethernet address here.
 */
/* EN: Fills ns_if for unit id_unit: name "ne", ETHERMTU, flags BROADCAST|SIMPLEX|NOTRAILERS, and the neinit/ether_output/nestart/neioctl/nereset hooks, then calls if_attach(). */
/* 中文：为该单元的 ns_if 填写名字 ne、MTU 为 ETHERMTU、标志 BROADCAST|SIMPLEX|NOTRAILERS，以及 neinit/ether_output/nestart/neioctl/nereset 钩子，最后调用 if_attach()。 */
neattach(dvp)
	struct isa_device *dvp;
{
	int unit = dvp->id_unit;
	register struct ne_softc *ns = &ne_softc[unit];
	register struct ifnet *ifp = &ns->ns_if;

	ifp->if_unit = unit;
	ifp->if_name = nedriver.name ;
	ifp->if_mtu = ETHERMTU;
	printf (" ethernet address %s", ether_sprintf(ns->ns_addr)) ;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_NOTRAILERS;
	ifp->if_init = neinit;
	ifp->if_output = ether_output;
	ifp->if_start = nestart;
	ifp->if_ioctl = neioctl;
	ifp->if_reset = nereset;
	ifp->if_watchdog = 0;
	if_attach(ifp);
}

/*
 * Initialization of interface; set up initialization block
 * and transmit/receive descriptor rings.
 */
/* EN: Returns early if no ifaddr or already IFF_RUNNING; at splimp writes PAR/MAR, ring bounds, DCR/TCR/RCR, sets IMR 0xff, marks IFF_RUNNING, resets ns_oactive/ns_mask, then nestart(). */
/* 中文：无协议地址或已处于 IFF_RUNNING 时直接返回；在 splimp 下写 PAR/MAR、接收环边界、DCR/TCR/RCR，置 IMR 为 0xff，设置 IFF_RUNNING 并清 ns_oactive/ns_mask，最后调用 nestart()。 */
neinit(unit)
	int unit;
{
	register struct ne_softc *ns = &ne_softc[unit];
	struct ifnet *ifp = &ns->ns_if;
	int s;
	register i; char *cp;

 	if (ifp->if_addrlist == (struct ifaddr *)0) return;
	if (ifp->if_flags & IFF_RUNNING) return;

	s = splimp();

	/* set physical address on ethernet */
	outb (nec+ds_cmd, DSCM_NODMA|DSCM_PG1|DSCM_STOP);
	for (i=0 ; i < 6 ; i++) outb(nec+ds1_par0+i,ns->ns_addr[i]);

	/* clr logical address hash filter for now */
	for (i=0 ; i < 8 ; i++) outb(nec+ds1_mar0+i,0xff);

	/* init regs */
	outb (nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_STOP);
	outb (nec+ds0_rbcr0, 0);
	outb (nec+ds0_rbcr1, 0);
	outb (nec+ds0_imr, 0);
	outb (nec+ds0_isr, 0xff);

	/* Word Transfers, Burst Mode Select, Fifo at 8 bytes */
	outb(nec+ds0_dcr, DSDC_WTS|DSDC_BMS|DSDC_FT1);
	outb(nec+ds0_tcr, 0);
	outb (nec+ds0_rcr, DSRC_MON);
	outb (nec+ds0_tpsr, 0);
	outb(nec+ds0_pstart, RBUF/DS_PGSIZE);
	outb(nec+ds0_pstop, RBUFEND/DS_PGSIZE);
	outb(nec+ds0_bnry, RBUF/DS_PGSIZE);
	outb (nec+ds_cmd, DSCM_NODMA|DSCM_PG1|DSCM_STOP);
	outb(nec+ds1_curr, RBUF/DS_PGSIZE);
	ns->ns_cur = RBUF/DS_PGSIZE;
	outb (nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_START);
	outb (nec+ds0_rcr, DSRC_AB);
	outb(nec+ds0_dcr, DSDC_WTS|DSDC_BMS|DSDC_FT1);
	outb (nec+ds0_imr, 0xff);

	ns->ns_if.if_flags |= IFF_RUNNING;
	ns->ns_oactive = 0; ns->ns_mask = ~0;
	nestart(ifp);
	splx(s);
}

/*
 * Setup output on interface.
 * Get another datagram to send off of the interface queue,
 * and map it to the interface before starting the output.
 * called only at splimp or interrupt level.
 */
/* EN: Returns if DSF_LOCK, NIC transmitting, or not IFF_RUNNING; else dequeues an mbuf, sets DSF_LOCK, neput()s it into TBUF, pads to ETHER_MIN_LEN and starts TX. */
/* 中文：若已置 DSF_LOCK、网卡正在发送或未 IFF_RUNNING 则返回；否则从发送队列取一个 mbuf，置 DSF_LOCK，用 neput 拷入 TBUF（奇数长度先 m_pullup），补齐到 ETHER_MIN_LEN 后启动发送。 */
nestart(ifp)
	struct ifnet *ifp;
{
	register struct ne_softc *ns = &ne_softc[ifp->if_unit];
	struct mbuf *m0, *m;
	int buffer;
	int len = 0, i, total,t;

	/*
	 * The DS8390 has only one transmit buffer, if it is busy we
	 * must wait until the transmit interrupt completes.
	 */
	outb(nec+ds_cmd,DSCM_NODMA|DSCM_START);

	if (ns->ns_flags & DSF_LOCK)
		return;

	if (inb(nec+ds_cmd) & DSCM_TRANS)
		return;

	if ((ns->ns_if.if_flags & IFF_RUNNING) == 0)
		return;

	IF_DEQUEUE(&ns->ns_if.if_snd, m);
 
	if (m == 0)
		return;

	/*
	 * Copy the mbuf chain into the transmit buffer
	 */

	ns->ns_flags |= DSF_LOCK;	/* prevent entering nestart */
	buffer = TBUF; len = i = 0;
	t = 0;
	for (m0 = m; m != 0; m = m->m_next)
		t += m->m_len;
		
	m = m0;
	total = t;
	for (m0 = m; m != 0; ) {
		
		if (m->m_len&1 && t > m->m_len) {
			neput(mtod(m, caddr_t), buffer, m->m_len - 1);
			t -= m->m_len - 1;
			buffer += m->m_len - 1;
			m->m_data += m->m_len - 1;
			m->m_len = 1;
			m = m_pullup(m, 2);
		} else {
			neput(mtod(m, caddr_t), buffer, m->m_len);
			buffer += m->m_len;
			t -= m->m_len;
			MFREE(m, m0);
			m = m0;
		}
	}

	/*
	 * Init transmit length registers, and set transmit start flag.
	 */

	len = total;
	if (len < ETHER_MIN_LEN) len = ETHER_MIN_LEN;
	outb(nec+ds0_tbcr0,len&0xff);
	outb(nec+ds0_tbcr1,(len>>8)&0xff);
	outb(nec+ds0_tpsr, TBUF/DS_PGSIZE);
	outb(nec+ds_cmd, DSCM_TRANS|DSCM_NODMA|DSCM_START);
}

/* buffer successor/predecessor in ring? */
#define succ(n) (((n)+1 >= RBUFEND/DS_PGSIZE) ? RBUF/DS_PGSIZE : (n)+1)
#define pred(n) (((n)-1 < RBUF/DS_PGSIZE) ? RBUFEND/DS_PGSIZE-1 : (n)-1)

/*
 * Controller interrupt.
 */
/* EN: Acks ISR, counts RXE/TXE errors and TX packets, walks the RX ring calling nerecv for 0x21/RPC frames, advances BNRY, restarts TX, re-reads ISR and loops while nonzero. */
/* 中文：中断处理：确认 ISR，统计收发错误与已发包数，遍历接收环对状态为 0x21/RPC 的帧调用 nerecv，前移 BNRY 边界，重启发送，再重读 ISR 非零则继续循环。 */
neintr(unit)
{
	register struct ne_softc *ns = &ne_softc[unit];
	u_char cmd,isr;

	/* Save cmd, clear interrupt */
	cmd = inb (nec+ds_cmd);
loop:
	isr = inb (nec+ds0_isr);
	outb(nec+ds_cmd,DSCM_NODMA|DSCM_START);
	outb(nec+ds0_isr, isr);

	/* Receiver error */
	if (isr & DSIS_RXE) {
		/* need to read these registers to clear status */
		(void) inb(nec+ ds0_rsr);
		(void) inb(nec+ 0xD);
		(void) inb(nec + 0xE);
		(void) inb(nec + 0xF);
		ns->ns_if.if_ierrors++;
	}

	/* We received something; rummage thru tiny ring buffer */
	if (isr & (DSIS_RX|DSIS_RXE|DSIS_ROVRN)) {
		u_char pend,lastfree;

		outb(nec+ds_cmd, DSCM_START|DSCM_NODMA|DSCM_PG1);
		pend = inb(nec+ds1_curr);
		outb(nec+ds_cmd, DSCM_START|DSCM_NODMA|DSCM_PG0);
		lastfree = inb(nec+ds0_bnry);

		/* Have we wrapped? */
		if (lastfree >= RBUFEND/DS_PGSIZE)
			lastfree = RBUF/DS_PGSIZE;
		if (pend < lastfree && ns->ns_cur < pend)
			lastfree = ns->ns_cur;
		else	if (ns->ns_cur > lastfree)
			lastfree = ns->ns_cur;

		/* Something in the buffer? */
		while (pend != lastfree) {
			u_char nxt;

			/* Extract header from microcephalic board */
			nefetch(&ns->ns_ph,lastfree*DS_PGSIZE,
				sizeof(ns->ns_ph));
			ns->ns_ba = lastfree*DS_PGSIZE+sizeof(ns->ns_ph);

			/* Incipient paranoia */
			if (ns->ns_ph.pr_status == DSRS_RPC ||
				/* for dequna's */
				ns->ns_ph.pr_status == 0x21)
				nerecv (ns);
#ifdef NEDEBUG
			else  {
				printf("cur %x pnd %x lfr %x ",
					ns->ns_cur, pend, lastfree);
				printf("nxt %x len %x ", ns->ns_ph.pr_nxtpg,
					(ns->ns_ph.pr_sz1<<8)+ ns->ns_ph.pr_sz0);
				printf("Bogus Sts %x\n", ns->ns_ph.pr_status);	
			}
#endif

			nxt = ns->ns_ph.pr_nxtpg ;

			/* Sanity check */
			if ( nxt >= RBUF/DS_PGSIZE && nxt <= RBUFEND/DS_PGSIZE
				&& nxt <= pend)
				ns->ns_cur = nxt;
			else	ns->ns_cur = nxt = pend;

			/* Set the boundaries */
			lastfree = nxt;
			outb(nec+ds0_bnry, pred(nxt));
			outb(nec+ds_cmd, DSCM_START|DSCM_NODMA|DSCM_PG1);
			pend = inb(nec+ds1_curr);
			outb(nec+ds_cmd, DSCM_START|DSCM_NODMA|DSCM_PG0);
		}
		outb(nec+ds_cmd, DSCM_START|DSCM_NODMA);
	}

	/* Transmit error */
	if (isr & DSIS_TXE) {
		ns->ns_flags &= ~DSF_LOCK;
		/* Need to read these registers to clear status */
		ns->ns_if.if_collisions += inb(nec+ds0_tbcr0);
		ns->ns_if.if_oerrors++;
	}

	/* Packet Transmitted */
	if (isr & DSIS_TX) {
		ns->ns_flags &= ~DSF_LOCK;
		++ns->ns_if.if_opackets;
		ns->ns_if.if_collisions += inb(nec+ds0_tbcr0);
	}

	/* Receiver ovverun? */
	if (isr & DSIS_ROVRN) {
		log(LOG_ERR, "ne%d: error: isr %x\n", ns-ne_softc, isr
			/*, DSIS_BITS*/);
		outb(nec+ds0_rbcr0, 0);
		outb(nec+ds0_rbcr1, 0);
		outb(nec+ds0_tcr, DSTC_LB0);
		outb(nec+ds0_rcr, DSRC_MON);
		outb(nec+ds_cmd, DSCM_START|DSCM_NODMA);
		outb(nec+ds0_rcr, DSRC_AB);
		outb(nec+ds0_tcr, 0);
	}

	/* Any more to send? */
	outb (nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_START);
	nestart(&ns->ns_if);
	outb (nec+ds_cmd, cmd);
	outb (nec+ds0_imr, 0xff);

	/* Still more to do? */
	isr = inb (nec+ds0_isr);
	if(isr) goto loop;
}

/*
 * Ethernet interface receiver interface.
 * If input error just drop packet.
 * Otherwise examine packet to determine type.  If can't determine length
 * from type, then have to drop packet.  Othewise decapsulate
 * packet based on type and pass to type specific higher-level
 * input routine.
 */
/* EN: Counts if_ipackets, drops lengths outside ETHER_MIN_LEN..ETHER_MAX_LEN, DMAs the frame across pages (ns_cur wraps 0x7f to 0x46), strips header+CRC, calls neread. */
/* 中文：累加 if_ipackets，长度不在 ETHER_MIN_LEN..ETHER_MAX_LEN 内则丢弃，跨页从网卡内存 DMA 出整帧（ns_cur 超过 0x7f 回绕到 0x46），去掉以太头与 CRC 后调用 neread。 */
nerecv(ns)
	register struct ne_softc *ns;
{
	int len,i;

	ns->ns_if.if_ipackets++;
	len = ns->ns_ph.pr_sz0 + (ns->ns_ph.pr_sz1<<8);
	if(len < ETHER_MIN_LEN || len > ETHER_MAX_LEN)
		return;

	/* this need not be so torturous - one/two bcopys at most into mbufs */
	nefetch(ns->ns_pb, ns->ns_ba, min(len,DS_PGSIZE-sizeof(ns->ns_ph)));
	if (len > DS_PGSIZE-sizeof(ns->ns_ph)) {
		int l = len - (DS_PGSIZE-sizeof(ns->ns_ph)), b ;
		u_char *p = ns->ns_pb + (DS_PGSIZE-sizeof(ns->ns_ph));

		if(++ns->ns_cur > 0x7f) ns->ns_cur = 0x46;
		b = ns->ns_cur*DS_PGSIZE;
		
		while (l >= DS_PGSIZE) {
			nefetch(p, b, DS_PGSIZE);
			p += DS_PGSIZE; l -= DS_PGSIZE;
			if(++ns->ns_cur > 0x7f) ns->ns_cur = 0x46;
			b = ns->ns_cur*DS_PGSIZE;
		}
		if (l > 0)
			nefetch(p, b, l);
	}
	/* don't forget checksum! */
	len -= sizeof(struct ether_header) + sizeof(long);
			
	neread(ns,(caddr_t)(ns->ns_pb), len);
}

/*
 * Pass a packet to the higher levels.
 * We deal with the trailer protocol here.
 */
/* EN: ntohs()s ether_type, unwraps trailer frames (recomputes type/len, checks offset and resid), builds mbufs with neget, then passes the packet to ether_input. */
/* 中文：把 ether_type 转为主机序，处理 trailer 协议帧（重算类型与长度并检查偏移/resid 合法性），用 neget 构造 mbuf 链后交给 ether_input。 */
neread(ns, buf, len)
	register struct ne_softc *ns;
	char *buf;
	int len;
{
	register struct ether_header *eh;
    	struct mbuf *m;
	int off, resid;
	register struct ifqueue *inq;

	/*
	 * Deal with trailer protocol: if type is trailer type
	 * get true type from first 16-bit word past data.
	 * Remember that type was trailer by setting off.
	 */
	eh = (struct ether_header *)buf;
	eh->ether_type = ntohs((u_short)eh->ether_type);
#define	nedataaddr(eh, off, type)	((type)(((caddr_t)((eh)+1)+(off))))
	if (eh->ether_type >= ETHERTYPE_TRAIL &&
	    eh->ether_type < ETHERTYPE_TRAIL+ETHERTYPE_NTRAILER) {
		off = (eh->ether_type - ETHERTYPE_TRAIL) * 512;
		if (off >= ETHERMTU) return;		/* sanity */
		eh->ether_type = ntohs(*nedataaddr(eh, off, u_short *));
		resid = ntohs(*(nedataaddr(eh, off+2, u_short *)));
		if (off + resid > len) return;		/* sanity */
		len = off + resid;
	} else	off = 0;

	if (len == 0) return;

	/*
	 * Pull packet off interface.  Off is nonzero if packet
	 * has trailing header; neget will then force this header
	 * information to be at the front, but we still have to drop
	 * the type and length which are at the front of any trailer data.
	 */
	m = neget(buf, len, off, &ns->ns_if);
	if (m == 0) return;

	ether_input(&ns->ns_if, eh, m);
}

/*
 * Supporting routines
 */

/*
 * Pull read data off a interface.
 * Len is length of data, with local net header stripped.
 * Off is non-zero if a trailer protocol was used, and
 * gives the offset of the trailer information.
 * We copy the trailer information and then all the normal
 * data into mbufs.  When full cluster sized units are present
 * we copy into clusters.
 */
/* EN: Allocates an mbuf chain (MGETHDR/MGET, MCLGET past MINCLSIZE), copies totlen bytes skipping the trailer offset, sets rcvif/len; frees all and returns 0 on failure. */
/* 中文：分配 mbuf 链（MGETHDR/MGET，长度 >=MINCLSIZE 时用 MCLGET 集群），拷贝 totlen 字节并在有 trailer 时跳过其偏移，设置 m_pkthdr.rcvif/len；分配失败则释放整链返回 0。 */
struct mbuf *
neget(buf, totlen, off0, ifp)
	caddr_t buf;
	int totlen, off0;
	struct ifnet *ifp;
{
	struct mbuf *top, **mp, *m, *p;
	int off = off0, len;
	register caddr_t cp = buf;
	char *epkt;

	buf += sizeof(struct ether_header);
	cp = buf;
	epkt = cp + totlen;


	if (off) {
		cp += off + 2 * sizeof(u_short);
		totlen -= 2 * sizeof(u_short);
	}

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == 0)
		return (0);
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = totlen;
	m->m_len = MHLEN;

	top = 0;
	mp = &top;
	while (totlen > 0) {
		if (top) {
			MGET(m, M_DONTWAIT, MT_DATA);
			if (m == 0) {
				m_freem(top);
				return (0);
			}
			m->m_len = MLEN;
		}
		len = min(totlen, epkt - cp);
		if (len >= MINCLSIZE) {
			MCLGET(m, M_DONTWAIT);
			if (m->m_flags & M_EXT)
				m->m_len = len = min(len, MCLBYTES);
			else
				len = m->m_len;
		} else {
			/*
			 * Place initial small packet/header at end of mbuf.
			 */
			if (len < m->m_len) {
				if (top == 0 && len + max_linkhdr <= m->m_len)
					m->m_data += max_linkhdr;
				m->m_len = len;
			} else
				len = m->m_len;
		}
		bcopy(cp, mtod(m, caddr_t), (unsigned)len);
		cp += len;
		*mp = m;
		mp = &m->m_next;
		totlen -= len;
		if (cp == epkt)
			cp = buf;
	}
	return (top);
}

/*
 * Process an ioctl request.
 */
/* EN: At splimp handles if_ioctl: SIOCSIFADDR sets IFF_UP, calls neinit, stores ac_ipaddr/NS address and calls arpwhohas; SIOCSIFFLAGS stops or re-inits; other commands return EINVAL. */
/* 中文：在 splimp 下处理 if_ioctl：SIOCSIFADDR 置 IFF_UP、调用 neinit、记录 ac_ipaddr/NS 地址并调用 arpwhohas；SIOCSIFFLAGS 停止或重新初始化网卡；其它命令返回 EINVAL。 */
neioctl(ifp, cmd, data)
	register struct ifnet *ifp;
	int cmd;
	caddr_t data;
{
	register struct ifaddr *ifa = (struct ifaddr *)data;
	struct ne_softc *ns = &ne_softc[ifp->if_unit];
	struct ifreq *ifr = (struct ifreq *)data;
	int s = splimp(), error = 0;


	switch (cmd) {

	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;

		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			neinit(ifp->if_unit);	/* before arpwhohas */
			((struct arpcom *)ifp)->ac_ipaddr =
				IA_SIN(ifa)->sin_addr;
			/* EN: Not defined here; only occurrence is neioctl's SIOCSIFADDR/AF_INET branch calling arpwhohas() after neinit() to broadcast an ARP request for the new address. */
			/* 中文：本文件未定义该函数；唯一出现处是 neioctl 的 SIOCSIFADDR/AF_INET 分支，在 neinit() 和设置 ac_ipaddr 之后调用 arpwhohas() 为新地址广播 ARP 请求。 */
			arpwhohas((struct arpcom *)ifp, &IA_SIN(ifa)->sin_addr);
			break;
#endif
#ifdef NS
		case AF_NS:
		    {
			register struct ns_addr *ina = &(IA_SNS(ifa)->sns_addr);

			if (ns_nullhost(*ina))
				ina->x_host = *(union ns_host *)(ns->ns_addr);
			else {
				/* 
				 * The manual says we can't change the address 
				 * while the receiver is armed,
				 * so reset everything
				 */
				ifp->if_flags &= ~IFF_RUNNING; 
				bcopy((caddr_t)ina->x_host.c_host,
				    (caddr_t)ns->ns_addr, sizeof(ns->ns_addr));
			}
			neinit(ifp->if_unit); /* does ne_setaddr() */
			break;
		    }
#endif
		default:
			neinit(ifp->if_unit);
			break;
		}
		break;

	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) == 0 &&
		    ifp->if_flags & IFF_RUNNING) {
			ifp->if_flags &= ~IFF_RUNNING;
			outb(nec+ds_cmd,DSCM_STOP|DSCM_NODMA);
		} else if (ifp->if_flags & IFF_UP &&
		    (ifp->if_flags & IFF_RUNNING) == 0)
			neinit(ifp->if_unit);
		break;

#ifdef notdef
	case SIOCGHWADDR:
		bcopy((caddr_t)ns->ns_addr, (caddr_t) &ifr->ifr_data,
			sizeof(ns->ns_addr));
		break;
#endif

	default:
		error = EINVAL;
	}
	splx(s);
	return (error);
}
#endif
