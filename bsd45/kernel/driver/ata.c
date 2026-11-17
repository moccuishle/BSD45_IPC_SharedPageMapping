/*
 * ata.c - minimal polling PIO ATA disk driver (read + write), LBA28.
 *
 * Unlike atapi.c (packet commands, CD-ROM only), this talks to a plain ATA
 * hard disk -- exactly what VMware presents as ide0:0.  It is used by the
 * one-shot `install` command to write the FFS image + boot blocks to disk,
 * and by the FFS root filesystem to read the system back after reboot.
 *
 * Everything polls with interrupts off; no interrupts, no DMA, one sector
 * at a time.  Only the primary master is supported (the usual VMware disk).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>

#include "arch/i386/io.h"

#define	ATA_IO		0x1f0
#define	ATA_CTL		0x3f6

#define	ATA_DATA	0
#define	ATA_ERROR	1
#define	ATA_FEATURES	1
#define	ATA_SECCOUNT	2
#define	ATA_LBA0	3
#define	ATA_LBA1	4
#define	ATA_LBA2	5
#define	ATA_DRIVE	6
#define	ATA_STATUS	7
#define	ATA_COMMAND	7

#define	ST_BSY		0x80
#define	ST_DRDY		0x40
#define	ST_DRQ		0x08
#define	ST_ERR		0x01

#define	CMD_IDENTIFY	0xec
#define	CMD_READ_LBA	0x20		/* READ SECTORS (LBA28); 0x28 is undefined */
#define	CMD_WRITE_LBA	0x30		/* WRITE SECTORS (LBA28) */

static int ata_found = 0;
static unsigned ata_nsectors = 0;	/* capacity from IDENTIFY (LBA28) */

/* 中文：连续四次虚拟读取状态寄存器，使 ATA 设备在两次写操作之间稳定下来。 */
/* EN: Four dummy status reads to let the ATA device settle between writes. */
static void
ata_settle(void)
{
	int i;

	for (i = 0; i < 4; i++)
		(void)hal_inb(ATA_IO + ATA_STATUS);
}

/* 中文：轮询 ATA 状态寄存器，直到 (status & mask)==value，超时则返回 -1。 */
/* EN: Poll ATA status until (status & mask)==value, or -1 on timeout. */
static int
ata_wait(int mask, int value, int polls)
{
	int i, st;

	for (i = 0; i < polls; i++) {
		st = hal_inb(ATA_IO + ATA_STATUS);
		if ((st & mask) == value)
			return (0);
	}
	return (-1);
}

/* 中文：选择主驱动器并等待其就绪（BSY 清零、DRDY 置位）。 */
/* EN: Select master drive and wait for it to become ready (BSY clear, DRDY set). */
static int
ata_select(void)
{
	if (ata_wait(ST_BSY, 0, 200000) < 0)
		return (-1);
	hal_outb(ATA_IO + ATA_DRIVE, 0xe0);	/* master, LBA mode */
	ata_settle();
	if (ata_wait(ST_BSY, 0, 200000) < 0)
		return (-1);
	if (ata_wait(ST_DRDY, ST_DRDY, 200000) < 0)
		return (-1);
	return (0);
}

/* 中文：通过 IDENTIFY DEVICE 命令检测主通道主盘上是否存在普通 ATA 硬盘；存在则缓存容量并返回 0，否则返回 -1。 */
/* EN: Detect a plain ATA hard disk on primary master via IDENTIFY DEVICE. Returns 0 and caches capacity if present, else -1. */
int
ata_probe(void)
{
	unsigned short id[256];
	int i, st;

	ata_found = 0;
	ata_nsectors = 0;

	if (ata_wait(ST_BSY, 0, 200000) < 0)
		return (-1);
	hal_outb(ATA_IO + ATA_DRIVE, 0xe0);	/* select master */
	ata_settle();

	hal_outb(ATA_IO + ATA_SECCOUNT, 0);
	hal_outb(ATA_IO + ATA_LBA0, 0);
	hal_outb(ATA_IO + ATA_LBA1, 0);
	hal_outb(ATA_IO + ATA_LBA2, 0);
	hal_outb(ATA_IO + ATA_COMMAND, CMD_IDENTIFY);
	ata_settle();

	st = hal_inb(ATA_IO + ATA_STATUS);
	if (st == 0 || st == 0xff)
		return (-1);			/* no device */
	if (st & ST_ERR)
		return (-1);			/* ATAPI or no disk */
	if (ata_wait(ST_BSY, 0, 200000) < 0)
		return (-1);
	if (ata_wait(ST_DRQ, ST_DRQ, 200000) < 0)
		return (-1);
	hal_insw(ATA_IO + ATA_DATA, id, 256);
	(void)ata_wait(ST_BSY, 0, 200000);

	/* word 0: bit15 must be 0 for ATA (1 means ATAPI) */
	if (id[0] & 0x8000)
		return (-1);
	/* word 49: LBA supported; words 60-61: total LBA sectors */
	if ((id[49] & 0x0200) == 0)
		return (-1);
	ata_nsectors = ((unsigned)id[61] << 16) | id[60];
	if (ata_nsectors == 0)
		return (-1);
	ata_found = 1;
	for (i = 0; i < 0; i++)		/* keep id referenced for older compilers */
		(void)id[i];
	return (0);
}

/* 中文：对 ATA 通道执行软件复位（SRST），清除 ATAPI IDENTIFY PACKET 遗留的 ERR/ABRT 与 DRQ 状态，避免后续 PIO 写被 QEMU 以 ERR=0x04 中止。 */
/* EN: Software-reset the ATA channel (SRST).  atapi_probe() sends IDENTIFY
 * PACKET (0xa1) to the primary master ATA disk, which ABORTs and leaves
 * ERR/ABRT + DRQ residue; QEMU then aborts our WRITE with ERR=0x04.  A
 * channel reset clears that state before PIO reads/writes. */
void
ata_reset(void)
{
	int i;

	hal_outb(ATA_CTL, 0x04);		/* SRST, nIEN */
	for (i = 0; i < 1000; i++)
		(void)hal_inb(ATA_IO + ATA_STATUS);
	hal_outb(ATA_CTL, 0x00);		/* release */
	for (i = 0; i < 1000; i++)
		(void)hal_inb(ATA_IO + ATA_STATUS);
	ata_found = 0;
}

/* 中文：若 ata_probe() 已检测到磁盘则返回非零值。 */
/* EN: Returns nonzero once ata_probe() found a disk. */
int
ata_present(void)
{

	return (ata_found);
}

/* 中文：返回磁盘容量（以 512 字节扇区为单位，LBA28）。 */
/* EN: Return the disk capacity in 512-byte sectors (LBA28). */
unsigned
ata_capacity(void)
{

	return (ata_nsectors);
}

/* 中文：一次性选择主盘并设置单扇区 LBA28 任务文件：DRIVE 寄存器只写一次且最后写，避免 QEMU 因重复写 DRIVE 而将后续 WRITE 以 ERR=0x04 中止；设备空闲就绪时返回 0。 */
/* EN: Select master and program a 1-sector LBA28 task file in ONE pass.
 * Writing ATA_DRIVE twice (once here, once in a separate select) makes
 * QEMU abort the following WRITE with ERR=0x04, so DRIVE is written once,
 * last, then settled.  Returns 0 when the device is idle and ready. */
static int
ata_setup_lba(unsigned lba, int nsec)
{
	int st;

	if (ata_wait(ST_BSY, 0, 200000) < 0)
		return (-1);
	hal_outb(ATA_IO + ATA_FEATURES, 0);	/* clear ATAPI residue */
	hal_outb(ATA_IO + ATA_SECCOUNT, nsec & 0xff);
	hal_outb(ATA_IO + ATA_LBA0, lba & 0xff);
	hal_outb(ATA_IO + ATA_LBA1, (lba >> 8) & 0xff);
	hal_outb(ATA_IO + ATA_LBA2, (lba >> 16) & 0xff);
	hal_outb(ATA_IO + ATA_DRIVE, 0xe0 | ((lba >> 24) & 0x0f));
	ata_settle();
	if (ata_wait(ST_BSY, 0, 200000) < 0)
		return (-1);
	if (ata_wait(ST_DRDY, ST_DRDY, 200000) < 0)
		return (-1);
	st = hal_inb(ATA_IO + ATA_STATUS);
	if (st & ST_ERR)
		return (-1);
	if (st & ST_DRQ)			/* must be idle before WRITE */
		return (-1);
	return (0);
}

/* 中文：从 lba 开始的扇区读取 nsec 个 512 字节扇区到 buf；每次只发一个命令以简化 PIO 循环，成功返回 0，出错返回 -1。 */
/* EN: Read nsec 512-byte sectors starting at lba into buf; 0 on success, -1 on error. One sector per command keeps the PIO loop simple. */
int
ata_read(unsigned lba, unsigned nsec, void *buf)
{
	unsigned char *p = buf;
	unsigned n;

	if (!ata_found || nsec == 0)
		return (-1);
	for (n = 0; n < nsec; n++) {
		if (ata_setup_lba(lba + n, 1) < 0) {
			printf("ata_read: setup failed st=");
			printf("%x", hal_inb(ATA_IO + ATA_STATUS));
			printf(" err=");
			printf("%x", hal_inb(ATA_IO + ATA_ERROR));
			printf("\n");
			return (-1);
		}
		hal_outb(ATA_IO + ATA_COMMAND, CMD_READ_LBA);
		ata_settle();
		if (ata_wait(ST_BSY, 0, 200000) < 0) {
			printf("ata_read: bsy timeout\n");
			return (-1);
		}
		{
			int s1 = hal_inb(ATA_IO + ATA_STATUS);

			if (s1 & ST_ERR) {
				printf("ata_read: cmd aborted st=");
				printf("%x", s1);
				printf(" err=");
				printf("%x", hal_inb(ATA_IO + ATA_ERROR));
				printf("\n");
				return (-1);
			}
			if (ata_wait(ST_DRQ, ST_DRQ, 200000) < 0) {
				printf("ata_read: no drq st=");
				printf("%x", s1);
				printf("\n");
				return (-1);
			}
		}
		hal_insw(ATA_IO + ATA_DATA, p, 256);	/* 512 bytes = 256 words */
		(void)ata_wait(ST_BSY, 0, 200000);
		p += 512;
	}
	return (0);
}

/* 中文：轮询状态寄存器，等待 BSY 清零且 DRQ 置位（或出现 ERR）；数据就绪返回 0，出错返回 -1。 */
/* EN: Wait until BSY clears and DRQ sets (or ERR), polling STATUS. Returns 0 ready-for-data, -1 error. */
static int
ata_wait_drq(void)
{
	int i, st;

	for (i = 0; i < 200000; i++) {
		st = hal_inb(ATA_IO + ATA_STATUS);
		if (st & ST_ERR)
			return (-1);
		if ((st & ST_BSY) == 0 && (st & ST_DRQ))
			return (0);
	}
	return (-1);
}

/* 中文：等待驱动器完成数据传输（BSY 清零）；返回最终状态值，超时返回 -1。 */
/* EN: Wait for the drive to finish a data-in flight (BSY clear). Returns final STATUS or -1 on timeout. */
static int
ata_wait_complete(void)
{
	int i, st;

	for (i = 0; i < 200000; i++) {
		st = hal_inb(ATA_IO + ATA_STATUS);
		if ((st & ST_BSY) == 0)
			return (st);
	}
	return (-1);
}

/* 中文：从 buf 写入 nsec 个 512 字节扇区到从 lba 开始的位置；成功返回 0，出错返回 -1。 */
/* EN: Write nsec 512-byte sectors from buf starting at lba; 0 on success, -1 on error. */
int
ata_write(unsigned lba, unsigned nsec, const void *buf)
{
	const unsigned char *p = buf;
	unsigned n;
	int st;

	if (!ata_found || nsec == 0)
		return (-1);
	for (n = 0; n < nsec; n++) {
		if (ata_setup_lba(lba + n, 1) < 0)
			return (-1);
		hal_outb(ATA_IO + ATA_COMMAND, CMD_WRITE_LBA);
		ata_settle();
		{
			int s0 = hal_inb(ATA_IO + ATA_STATUS);
			if (s0 & ST_ERR) {
				printf("ata_write: ABRT at command st=");
				printf("%x", s0);
				printf(" err=");
				printf("%x", hal_inb(ATA_IO + ATA_ERROR));
				printf("\n");
				return (-1);
			}
		}
		if (ata_wait_drq() < 0) {
			printf("ata_write: no DRQ st=");
			printf("%x", hal_inb(ATA_IO + ATA_STATUS));
			printf(" err=");
			printf("%x", hal_inb(ATA_IO + ATA_ERROR));
			printf("\n");
			return (-1);
		}
		hal_outsw(ATA_IO + ATA_DATA, p, 256);	/* 512 bytes = 256 words */
		st = ata_wait_complete();
		if (st < 0) {
			printf("ata_write: completion timeout\n");
			return (-1);
		}
		if (st & ST_ERR) {
			printf("ata_write: ERR after data st=");
			printf("%x", st);
			printf(" err=");
			printf("%x", hal_inb(ATA_IO + ATA_ERROR));
			printf("\n");
			return (-1);
		}
		p += 512;
	}
	return (0);
}
