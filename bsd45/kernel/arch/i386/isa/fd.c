/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Don Ahn.
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
 *	@(#)fd.c	8.1 (Berkeley) 6/11/93
 */

#include "fd.h"
#if NFD > 0
/*
 * This driver assumed that NFD == 2. Now it works for NFD == 1 or NFD == 2
 * It will probably not work for NFD > 2.
 */
#include <sys/param.h>
#include <sys/dkbad.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/buf.h>
#include <sys/uio.h>

#include <i386/isa/isa_device.h>
#include <i386/isa/fdreg.h>
#include <i386/isa/icu.h>

#define	FDUNIT(s)	((s)&1)
#define	FDTYPE(s)	(((s)>>1)&7)
#define FDMOTOR(u) 	(fd_unit[(u)].motor ? (1 << (4 + (u))) : 0)

#define b_cylin b_resid
#define b_step b_resid
#define FDBLK 512
#define NUMTYPES 4

struct fd_type {
	int	sectrac;		/* sectors per track         */
	int	secsize;		/* size code for sectors     */
	int	datalen;		/* data len when secsize = 0 */
	int	gap;			/* gap len between sectors   */
	int	tracks;			/* total num of tracks       */
	int	size;			/* size of disk in sectors   */
	int	steptrac;		/* steps per cylinder        */
	int	trans;			/* transfer speed code       */
};

struct fd_type fd_types[NUMTYPES] = {
 	{ 18,2,0xFF,0x1B,80,2880,1,0 },	/* 1.44 meg HD 3.5in floppy    */
	{ 15,2,0xFF,0x1B,80,2400,1,0 },	/* 1.2 meg HD floppy           */
	{ 9,2,0xFF,0x23,40,720,2,1 },	/* 360k floppy in 1.2meg drive */
	{ 9,2,0xFF,0x2A,40,720,1,1 },	/* 360k floppy in DD drive     */
};

struct fd_u {
	int type;		/* Drive type (HD, DD     */
	int active;		/* Drive activity boolean */
	int motor;		/* Motor on flag          */
	struct buf head;	/* Head of buf chain      */
	struct buf rhead;	/* Raw head of buf chain  */
	int reset;
} fd_unit[NFD];


extern int hz;

/* state needed for current transfer */
static fdc;	/* floppy disk controller io base register */
int	fd_dmachan = 2;
static int fd_skip;
static int fd_state;
static int fd_retry;
static int fd_drive;
static int fd_track = -1;
static int fd_status[7];

/*
	make sure bounce buffer for DMA is aligned since the DMA chip
	doesn't roll over properly over a 64k boundary
*/
extern struct buf *dma_bounce[];

/****************************************************************************/
/*                      autoconfiguration stuff                             */
/****************************************************************************/
int fdprobe(), fdattach(), fd_turnoff();

struct	isa_driver fddriver = {
	fdprobe, fdattach, "fd",
};

/* EN: ISA probe stub that returns 1 unconditionally, so the fd driver always claims its configured device without touching any hardware. */
/* 中文：ISA 探测桩函数，无条件返回 1，因此 fd 驱动总是占用所配置的设备，且不访问任何硬件。 */
fdprobe(dev)
struct isa_device *dev;
{
	return 1;
}

fdattach(dev)
struct isa_device *dev;
{	int	s;

	fdc = dev->id_iobase;
	/* Set transfer to 500kbps */
	outb(fdc+fdctl,0);
	fd_turnoff(0);
}

/* EN: Returns the constant 2400 sectors (the 1.2MB HD type in fd_types) while ignoring dev, so the size answer is fixed rather than per-unit or per-type. */
/* 中文：固定返回常量 2400 个扇区（fd_types 中 1.2MB 高密盘型），忽略 dev 参数，因此大小结果是写死的，不按单元或盘型区分。 */
int
Fdsize(dev)
dev_t	dev;
{
	return(2400);
}

/****************************************************************************/
/*                               fdstrategy                                 */
/****************************************************************************/
/* EN: Validates unit/block range, converts to FDBLK and b_cylin, disksorts bp under splbio, starts the xfer if both drives idle; bad requests get EINVAL/ENOSPC then biodone. */
/* 中文：校验单元号与块号范围，换算成 FDBLK 块和 b_cylin，在 splbio 下用 disksort 把 bp 插入该单元队列，若两个驱动器都空闲则启动传输；非法请求置 EINVAL/ENOSPC 并 biodone。 */
Fdstrategy(bp)
	register struct buf *bp;	/* IO operation to perform */
{
	register struct buf *dp;
	long nblocks,blknum;
 	int	unit, type, s;

 	unit = FDUNIT(minor(bp->b_dev));
 	type = FDTYPE(minor(bp->b_dev));

#ifdef FDTEST
printf("fdstrat%d, blk = %d, bcount = %d, addr = %x|",
	unit, bp->b_blkno, bp->b_bcount,bp->b_un.b_addr);
#endif
	if ((unit >= NFD) || (bp->b_blkno < 0)) {
		printf("fdstrat: unit = %d, blkno = %d, bcount = %d\n",
			unit, bp->b_blkno, bp->b_bcount);
		pg("fd:error in fdstrategy");
		bp->b_error = EINVAL;
		bp->b_flags |= B_ERROR;
		goto bad;
	}
	/*
	 * Set up block calculations.
	 */
	blknum = (unsigned long) bp->b_blkno * DEV_BSIZE/FDBLK;
 	nblocks = fd_types[type].size;
	if (blknum + (bp->b_bcount / FDBLK) > nblocks) {
		if (blknum == nblocks) {
			bp->b_resid = bp->b_bcount;
		} else {
			bp->b_error = ENOSPC;
			bp->b_flags |= B_ERROR;
		}
#ifdef FDTEST
printf("fdstrat%d, too big\n");
#endif 
		goto bad;
	}
 	bp->b_cylin = blknum / (fd_types[type].sectrac * 2);
	dp = &fd_unit[unit].head;
	dp->b_step = (fd_types[fd_unit[unit].type].steptrac);
	s = splbio();
	disksort(dp, bp);
	if ((fd_unit[0].head.b_active == 0)
#if NFD > 1
	    && (fd_unit[1].head.b_active == 0)
#endif
	    ) {
#ifdef FDDEBUG
printf("T|");
#endif
		dp->b_active = 1;
		fd_drive = unit;
		fd_track = -1;  /* force seek on first xfer */
		untimeout(fd_turnoff,unit);
		fdstart(unit);		/* start drive if idle */
	}
	splx(s);
	return;

bad:
	biodone(bp);
}

/****************************************************************************/
/*                            motor control stuff                           */
/****************************************************************************/
/* EN: Writes the digital output byte at fdc+fdout: ORs in the unit number, 0xC when not reset, and the FDMOTOR bit of every unit whose motor flag is on. */
/* 中文：向 fdc+fdout 写数字输出寄存器：或上单元号、非 reset 时的 0xC，以及所有 motor 标志已置位单元的 FDMOTOR 位。 */
set_motor(unit,reset)
int unit,reset;
{
	outb(fdc+fdout,unit | (reset ? 0 : 0xC)  | FDMOTOR(0) 
#if NFD > 1
	     | FDMOTOR(1)
#endif
	     );
}

/* EN: Clears fd_unit[unit].motor, then writes the motor port addressed to the opposite drive (set_motor(0,0) if unit 0, else set_motor(1,0)), dropping this unit's motor bit. */
/* 中文：清除 fd_unit[unit].motor 标志，再以另一台驱动器的编号重写马达控制口（unit 为 0 时 set_motor(0,0)，否则 set_motor(1,0)），使本单元的马达位被清掉。 */
fd_turnoff(unit)
int unit;
{
	fd_unit[unit].motor = 0;
	if (unit) set_motor(0,0);
	else set_motor(1,0);
}

/* EN: Sets fd_unit[unit].motor to 1 and selects that unit by calling set_motor(unit, 0), which writes the motor port and turns its motor bit on. */
/* 中文：将 fd_unit[unit].motor 置 1，并调用 set_motor(unit, 0) 选中该单元，从而写入马达控制口并打开其马达位。 */
fd_turnon(unit)
int unit;
{
	fd_unit[unit].motor = 1;
	set_motor(unit,0);
}

/****************************************************************************/
/*                             fdc in/out                                   */
/****************************************************************************/
/* EN: Polls the FDC status for DIO|RQM; returns -1 if only RQM is set (busy/wrong direction), otherwise reads and returns the byte from the data register. */
/* 中文：轮询 FDC 状态等待 DIO|RQM 同时置位；若只置 RQM 则返回 -1（忙或方向不符），否则读取并返回数据寄存器字节。 */
int
in_fdc()
{
	int i;
	while ((i = inb(fdc+fdsts) & (NE7_DIO|NE7_RQM)) != (NE7_DIO|NE7_RQM))
		if (i == NE7_RQM) return -1;
	return inb(fdc+fddata);
}

/* EN: Error helper: reads up to 7 result bytes with in_fdc into fd_status, stopping at the first negative, then printf's all seven as hex. */
/* 中文：出错辅助函数：用 in_fdc 最多读取 7 个结果字节存入 fd_status，遇到负值即停止，随后用 printf 以十六进制打印这 7 个字节。 */
dump_stat()
{
	int i;
	for(i=0;i<7;i++) {
		fd_status[i] = in_fdc();
		if (fd_status[i] < 0) break;
	}
printf("FD bad status :%lx %lx %lx %lx %lx %lx %lx\n",
	fd_status[0], fd_status[1], fd_status[2], fd_status[3],
	fd_status[4], fd_status[5], fd_status[6] );
}

/* EN: Waits until status shows RQM without DIO, eating pending output bytes via dump_stat on a direction error, tracks the worst retry count in static maxcnt, then writes x to fddata. */
/* 中文：等待状态仅置 RQM 而无 DIO，方向出错时用 dump_stat 吃掉残留输出字节，用静态 maxcnt 记录最大重试次数，最后把 x 写入 fddata。 */
out_fdc(x)
int x;
{
	int r,errcnt;
	static int maxcnt = 0;
	errcnt = 0;
	do {
		r = (inb(fdc+fdsts) & (NE7_DIO|NE7_RQM));
		if (r== NE7_RQM) break;
		if (r==(NE7_DIO|NE7_RQM)) {
			dump_stat(); /* error: direction. eat up output */
#ifdef FDOTHER
printf("%lx\n",x);
#endif
		}
		/* printf("Error r = %d:",r); */
		errcnt++;
	} while (1);
	if (errcnt > maxcnt) {
		maxcnt = errcnt;
#ifdef FDOTHER
printf("New MAX = %d\n",maxcnt);
#endif
	}
	outb(fdc+fddata,x);
}

/* see if fdc responding */
/* EN: Polls fdsts up to 100 times for NE7_RQM; returns 0 if the controller responded (ready), 1 if it never did. */
/* 中文：最多 100 次轮询 fdsts 的 NE7_RQM 位；控制器有响应（就绪）返回 0，始终无响应返回 1。 */
int
check_fdc()
{
	int i;
	for(i=0;i<100;i++) {
		if (inb(fdc+fdsts)& NE7_RQM) return 0;
	}
	return 1;
}

/****************************************************************************/
/*                           fdopen/fdclose                                 */
/****************************************************************************/
/* EN: Prints the minor, unit and type, returns ENXIO when unit >= NFD or type >= NUMTYPES, else 0; the check_fdc busy test is commented out. */
/* 中文：打印 minor、单元号和盘型，unit >= NFD 或 type >= NUMTYPES 时返回 ENXIO，否则返回 0；调用 check_fdc 的忙检测已被注释掉。 */
Fdopen(dev, flags)
	dev_t	dev;
	int	flags;
{
 	int unit = FDUNIT(minor(dev));
 	int type = FDTYPE(minor(dev));
	int s;

	printf("fdopen %x %d %d\n", minor(dev), unit, type);
	/* check bounds */
	if (unit >= NFD) return(ENXIO);
	if (type >= NUMTYPES) return(ENXIO);
/*
	if (check_fdc()) return(EBUSY);
*/

	/* Set proper disk type, only allow one type */
	return 0;
}

/* EN: Close entry point that only returns 0: no motor shutdown, queue flush or state change happens here, motor-off is left to the fd_turnoff timeout. */
/* 中文：关闭入口，仅返回 0：这里不做马达关闭、队列清空或状态变更，关马达交给 fd_turnoff 超时完成。 */
Fdclose(dev, flags)
	dev_t dev;
{
	return (0);
}

/****************************************************************************/
/*                            fdread/fdwrite                                */
/****************************************************************************/
/*
 * Routines to do raw IO for a unit.
 */
/* EN: Raw read entry: returns ENXIO when unit >= NFD, otherwise runs physio over Fdstrategy on fd_unit[unit].rhead with B_READ and minphys and returns its error. */
/* 中文：原始读入口：unit >= NFD 时返回 ENXIO，否则以 B_READ 和 minphys 通过 physio 在 fd_unit[unit].rhead 队列上调用 Fdstrategy，并返回其错误码。 */
Fdread(dev, uio)			/* character read routine */
dev_t dev;
struct uio *uio;
{
 	int unit = FDUNIT(minor(dev)) ;
	if (unit >= NFD) return(ENXIO);
	return(physio(Fdstrategy,&fd_unit[unit].rhead,dev,B_READ,minphys,uio));
}

/* EN: Raw write entry: returns ENXIO when unit >= NFD, otherwise runs physio over Fdstrategy on fd_unit[unit].rhead with B_WRITE and minphys and returns its error. */
/* 中文：原始写入口：unit >= NFD 时返回 ENXIO，否则以 B_WRITE 和 minphys 通过 physio 在 fd_unit[unit].rhead 队列上调用 Fdstrategy，并返回其错误码。 */
Fdwrite(dev, uio)			/* character write routine */
dev_t dev;
struct uio *uio;
{
 	int unit = FDUNIT(minor(dev)) ;
	if (unit >= NFD) return(ENXIO);
	return(physio(Fdstrategy,&fd_unit[unit].rhead,dev,B_WRITE,minphys,uio));
}

/****************************************************************************/
/*                                 fdstart                                  */
/****************************************************************************/
/* EN: Under splbio: motor off means turn on and re-arm fdstart in hz ticks; otherwise take next bp, clear fd_retry/fd_skip, force reset (state 5) if needed, then seek or fdintr(0xff). */
/* 中文：在 splbio 下：马达未开则开启并在 hz 个 tick 后重排 fdstart；否则取队首 bp，清 fd_retry/fd_skip，必要时先做复位（状态 5），再发寻道命令或直接调用 fdintr(0xff)。 */
fdstart(unit)
int unit;
{
	register struct buf *dp,*bp;
	int s;

#ifdef FDTEST
printf("fd%d|",unit);
#endif 
	s = splbio();
	if (!fd_unit[unit].motor) {
		fd_turnon(unit);
		/* Wait for 1 sec */
		timeout(fdstart,unit,hz);
		/*DELAY(1000000);*/
	}else
		 {
		/* make sure drive is selected as well as on */
		/*set_motor(unit,0);*/

		dp = &fd_unit[unit].head;
		bp = dp->b_actf;
		fd_retry = 0;
		if (fd_unit[unit].reset) fd_state = 1;
		else {
			/* DO a RESET */
			fd_unit[unit].reset = 1;
			fd_state = 5;
		}
		fd_skip = 0;
#ifdef FDDEBUG
printf("Seek %d %d\n", bp->b_cylin, dp->b_step);
#endif
		if (bp->b_cylin != fd_track) {
		/* Seek necessary, never quite sure where head is at! */
		out_fdc(15);	/* Seek function */
		out_fdc(unit);	/* Drive number */
		out_fdc(bp->b_cylin * dp->b_step);
		} else fdintr(0xff);
	}
	splx(s);
}

/* EN: Transfer timeout handler: senses drive status (0x4) and ST0/PCN (0x8), prints them with printf, then fails the active bp through badtrans so the queue moves on. */
/* 中文：传输超时处理：用 0x4 读驱动器状态、用 0x8 读 ST0/PCN 并用 printf 打印，然后调用 badtrans 使当前 bp 失败，让队列继续推进。 */
fd_timeout(x)
int x;
{
	int i,j;
	struct buf *dp,*bp;

	dp = &fd_unit[fd_drive].head;
	bp = dp->b_actf;

	out_fdc(0x4);
	out_fdc(fd_drive);
	i = in_fdc();
	printf("Timeout drive status %lx\n",i);

	out_fdc(0x8);
	i = in_fdc();
	j = in_fdc();
	printf("ST0 = %lx, PCN = %lx\n",i,j);

	if (bp) badtrans(dp,bp);
}

/****************************************************************************/
/*                                 fdintr                                   */
/****************************************************************************/
/* EN: FDC interrupt state machine: state 1 starts DMA and sends READ/WRITE, state 2 reads 7 status bytes, retries on ST0 error and biodones at end; states 3-5 seek/specify/reset. */
/* 中文：FDC 中断状态机：状态 1 寻道后启动 DMA 并发出读/写命令，状态 2 读取 7 个状态字节、ST0 出错则重试，待 fd_skip 传满 b_bcount 后 biodone；状态 3~5 处理寻道、specify、重校准和复位。 */
fdintr(unit)
{
	register struct buf *dp,*bp;
	struct buf *dpother;
	int read,head,trac,sec,i,s,sectrac,cyl;
	unsigned long blknum;
	struct fd_type *ft;

#ifdef FDTEST
	printf("state %d, unit %d, dr %d|",fd_state,unit,fd_drive);
#endif

	dp = &fd_unit[fd_drive].head;
	bp = dp->b_actf;
	read = bp->b_flags & B_READ;
 	ft = &fd_types[FDTYPE(bp->b_dev)];

	switch (fd_state) {
	case 1 : /* SEEK DONE, START DMA */
		/* Make sure seek really happened*/
		if (unit != 0xff) {
			out_fdc(0x8);
			i = in_fdc();
			cyl = in_fdc();
			if (!(i&0x20) || (cyl != bp->b_cylin*dp->b_step)) {
printf("Stray int ST0 = %lx, PCN = %lx:",i,cyl);
				return;
			}
		}

		fd_track = bp->b_cylin;
		at_dma(read,bp->b_un.b_addr+fd_skip,FDBLK, fd_dmachan);
		blknum = (unsigned long)bp->b_blkno*DEV_BSIZE/FDBLK
			+ fd_skip/FDBLK;
		sectrac = ft->sectrac;
		sec = blknum %  (sectrac * 2);
		head = sec / sectrac;
		sec = sec % sectrac + 1;

		if (read)  out_fdc(0xE6);	/* READ */
		else out_fdc(0xC5);		/* WRITE */
		out_fdc(head << 2 | fd_drive);	/* head & unit */
		out_fdc(fd_track);		/* track */
		out_fdc(head);
		out_fdc(sec);			/* sector XXX +1? */
		out_fdc(ft->secsize);		/* sector size */
		out_fdc(sectrac);		/* sectors/track */
		out_fdc(ft->gap);		/* gap size */
		out_fdc(ft->datalen);		/* data length */
		fd_state = 2;
		/* XXX PARANOIA */
		untimeout(fd_timeout,2);
		timeout(fd_timeout,2,hz);
		break;
	case 2 : /* IO DONE, post-analyze */
		untimeout(fd_timeout,2);
		for(i=0;i<7;i++) {
			fd_status[i] = in_fdc();
		}
		if (fd_status[0]&0xF8) {
#ifdef FDOTHER
printf("status0 err %d:",fd_status[0]);
#endif
			goto retry;
		}
/*
		if (fd_status[1]){
			printf("status1 err %d:",fd_status[0]);
			goto retry;
		}
		if (fd_status[2]){
			printf("status2 err %d:",fd_status[0]);
			goto retry;
		}
*/
		/* All OK */
		if (!kernel_space(bp->b_un.b_addr+fd_skip)) {
			/* RAW transfer */
			if (read) bcopy(dma_bounce[fd_dmachan]->b_un.b_addr,
				bp->b_un.b_addr+fd_skip, FDBLK);
		}
		fd_skip += FDBLK;
		if (fd_skip >= bp->b_bcount) {
#ifdef FDTEST
printf("DONE %d|", bp->b_blkno);
#endif
			/* ALL DONE */
			fd_skip = 0;
			bp->b_resid = 0;
			dp->b_actf = bp->av_forw;
			biodone(bp);
			nextstate(dp);

		} else {
#ifdef FDDEBUG
printf("next|");
#endif
			/* set up next transfer */
			blknum = (unsigned long)bp->b_blkno*DEV_BSIZE/FDBLK
				+ fd_skip/FDBLK;
			fd_state = 1;
			bp->b_cylin = (blknum / (ft->sectrac * 2));
			if (bp->b_cylin != fd_track) {
#ifdef FDTEST
printf("Seek|");
#endif
				/* SEEK Necessary */
				out_fdc(15);	/* Seek function */
				out_fdc(fd_drive);/* Drive number */
				out_fdc(bp->b_cylin * dp->b_step);
				break;
			} else fdintr(0xff);
		}
		break;
	case 3:
#ifdef FDOTHER
printf("Seek %d %d\n", bp->b_cylin, dp->b_step);
#endif
		/* Seek necessary */
		out_fdc(15);	/* Seek function */
		out_fdc(fd_drive);/* Drive number */
		out_fdc(bp->b_cylin * dp->b_step);
		fd_state = 1;
		break;
	case 4:
		out_fdc(3); /* specify command */
		out_fdc(0xDF);
		out_fdc(2);
		out_fdc(7);	/* Recalibrate Function */
		out_fdc(fd_drive);
		fd_state = 3;
		break;
	case 5:
#ifdef FDOTHER
		printf("**RESET**\n");
#endif
		/* Try a reset, keep motor on */
		set_motor(fd_drive,1);
		set_motor(fd_drive,0);
		outb(fdc+fdctl,ft->trans);
		fd_retry++;
		fd_state = 4;
		break;
	default:
		printf("Unexpected FD int->");
		out_fdc(0x8);
		i = in_fdc();
		sec = in_fdc();
		printf("ST0 = %lx, PCN = %lx\n",i,sec);
		out_fdc(0x4A); 
		out_fdc(fd_drive);
		for(i=0;i<7;i++) {
			fd_status[i] = in_fdc();
		}
	printf("intr status :%lx %lx %lx %lx %lx %lx %lx ",
		fd_status[0], fd_status[1], fd_status[2], fd_status[3],
		fd_status[4], fd_status[5], fd_status[6] );
		break;
	}
	return;
retry:
	switch(fd_retry) {
	case 0: case 1:
	case 2: case 3:
		break;
	case 4:
		fd_retry++;
		fd_state = 5;
		fdintr(0xff);
		return;
	case 5: case 6: case 7:
		break;
	default:
		printf("FD err %lx %lx %lx %lx %lx %lx %lx\n",
		fd_status[0], fd_status[1], fd_status[2], fd_status[3],
		fd_status[4], fd_status[5], fd_status[6] );
		badtrans(dp,bp);
		return;
	}
	fd_state = 1;
	fd_retry++;
	fdintr(0xff);
}

/* EN: Marks bp with B_ERROR and EIO, sets b_resid to b_bcount - fd_skip, removes it from the unit queue, clears fd_skip, biodones it and calls nextstate. */
/* 中文：将 bp 置 B_ERROR 和 EIO，把 b_resid 设为 b_bcount - fd_skip，从单元队列中摘除，清 fd_skip，调用 biodone 后转入 nextstate。 */
badtrans(dp,bp)
struct buf *dp,*bp;
{

	bp->b_flags |= B_ERROR;
	bp->b_error = EIO;
	bp->b_resid = bp->b_bcount - fd_skip;
	dp->b_actf = bp->av_forw;
	fd_skip = 0;
	biodone(bp);
	nextstate(dp);

}

/*
	nextstate : After a transfer is done, continue processing
	requests on the current drive queue.  If empty, go to
	the other drives queue.  If that is empty too, timeout
	to turn off the current drive in 5 seconds, and go
	to state 0 (not expecting any interrupts).
*/

/* EN: After a transfer: fdstart if this queue has work; else switch to the other drive (5s turnoff timer); if both idle, fd_turnoff in 5*hz, fd_state 0, clear b_active. */
/* 中文：传输结束后：本队列还有请求则继续 fdstart；否则切换到另一驱动器队列并挂 5 秒关马达定时器；两边都空时在 5*hz 后关马达、置 fd_state 为 0 并清 active。 */
nextstate(dp)
struct buf *dp;
{
	
	if (dp->b_actf) 
		fdstart(fd_drive);
	else {
#if NFD > 1
		struct buf *dpother;

		dpother = &fd_unit[fd_drive ? 0 : 1].head;

		if (dpother->b_actf) {
#ifdef FDTEST
printf("switch|");
#endif
			untimeout(fd_turnoff,fd_drive);
			timeout(fd_turnoff,fd_drive,5*hz);
			fd_drive = 1 - fd_drive;
			dp->b_active = 0;
			dpother->b_active = 1;
			fdstart(fd_drive);
		} else 
#endif
		{
#ifdef FDTEST
printf("off|");
#endif
			untimeout(fd_turnoff,fd_drive);
			timeout(fd_turnoff,fd_drive,5*hz);
			fd_state = 0;
			dp->b_active = 0;
		}
	}
}

Fdioctl() {}
Fddump() {}
#endif
