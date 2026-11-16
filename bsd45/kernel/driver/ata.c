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

#include "arch/i386/hal/hal.h"

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
#define	CMD_READ_LBA	0x28		/* READ SECTORS (LBA28) */
#define	CMD_WRITE_LBA	0x30		/* WRITE SECTORS (LBA28) */

static int ata_found = 0;
static unsigned ata_nsectors = 0;	/* capacity from IDENTIFY (LBA28) */

/* EN: Four dummy status reads to let the ATA device settle between writes. */
static void
ata_settle(void)
{
	int i;

	for (i = 0; i < 4; i++)
		(void)hal_inb(ATA_IO + ATA_STATUS);
}

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

/* EN: Returns nonzero once ata_probe() found a disk. */
int
ata_present(void)
{

	return (ata_found);
}

/* EN: Return the disk capacity in 512-byte sectors (LBA28). */
unsigned
ata_capacity(void)
{

	return (ata_nsectors);
}

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

/* EN: Read nsec 512-byte sectors starting at lba into buf; 0 on success, -1 on error. One sector per command keeps the PIO loop simple. */
int
ata_read(unsigned lba, unsigned nsec, void *buf)
{
	unsigned char *p = buf;
	unsigned n;

	if (!ata_found || nsec == 0)
		return (-1);
	for (n = 0; n < nsec; n++) {
		if (ata_setup_lba(lba + n, 1) < 0)
			return (-1);
		hal_outb(ATA_IO + ATA_COMMAND, CMD_READ_LBA);
		ata_settle();
		if (ata_wait(ST_BSY, 0, 200000) < 0)
			return (-1);
		if (hal_inb(ATA_IO + ATA_STATUS) & ST_ERR)
			return (-1);
		if (ata_wait(ST_DRQ, ST_DRQ, 200000) < 0)
			return (-1);
		hal_insw(ATA_IO + ATA_DATA, p, 256);	/* 512 bytes = 256 words */
		(void)ata_wait(ST_BSY, 0, 200000);
		p += 512;
	}
	return (0);
}

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
