/*
 * atapi.c - minimal ATAPI (PIO) CD-ROM driver for the ISO9660 root.
 *
 * The kernel has no device framework yet, so this is deliberately small:
 * probe both IDE channels for an ATAPI packet device, then run SCSI
 * commands with PIO.  Everything polls with interrupts off.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>

#include "atapi.h"
#include "arch/i386/io.h"

#define	PRIMARY_IO	0x1f0
#define	PRIMARY_CTL	0x3f6
#define	SECONDARY_IO	0x170
#define	SECONDARY_CTL	0x376

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

#define	CMD_PACKET	0xa0
#define	CMD_IDENTIFY_PACKET 0xa1

static int	atapi_io;	/* I/O base of the channel in use */
static int	atapi_ctl;
static int	atapi_slave;	/* 0 = master, 1 = slave */
static int	atapi_unit = -1;

/* EN: Short PIO settling delay: issues four dummy hal_inb() reads of the ATA status register at atapi_io + ATA_STATUS, giving the drive time to update status. */
/* 中文：PIO 短延时：连续 4 次对 atapi_io + ATA_STATUS 状态寄存器做空读，给驱动器时间更新状态。 */
static void
udelay(void)
{
	int i;

	for (i = 0; i < 4; i++)
		(void)hal_inb(atapi_io + ATA_STATUS);
}

/* EN: Polls the ATA status register up to polls times until (status & mask) == value, returning 0 on success and -1 if the condition never appears; it never sleeps. */
/* 中文：最多轮询 polls 次 ATA 状态寄存器，直到 (status & mask) == value；满足返回 0，始终不满足返回 -1，不睡眠。 */
static int
wait_status(int mask, int value, int polls)
{
	int i, st;

	for (i = 0; i < polls; i++) {
		st = hal_inb(atapi_io + ATA_STATUS);
		if ((st & mask) == value)
			return (0);
	}
	return (-1);
}

/* EN: Sends IDENTIFY PACKET DEVICE to one channel/drive, waits on BSY/DRQ, rejects ERR and a word0 that is not ATAPI type 5, and caches io/ctl/slave; 0 if a CD-ROM answers, else -1. */
/* 中文：向指定 IDE 通道与主/从盘发 IDENTIFY PACKET DEVICE，等待 BSY/DRQ、排除 ERR 和非 ATAPI 类型 5 的 word0，并记下 io/ctl/slave；有光驱返回 0，否则 -1。 */
static int
probe_one(int io, int ctl, int slave)
{
	unsigned short id[256];
	int st, word0;

	atapi_io = io;
	atapi_ctl = ctl;
	atapi_slave = slave;

	hal_outb(io + ATA_DRIVE, 0xa0 | (slave << 4));
	udelay();
	hal_outb(io + ATA_SECCOUNT, 0);
	hal_outb(io + ATA_LBA0, 0);
	hal_outb(io + ATA_LBA1, 0);
	hal_outb(io + ATA_LBA2, 0);
	hal_outb(io + ATA_COMMAND, CMD_IDENTIFY_PACKET);
	udelay();

	st = hal_inb(io + ATA_STATUS);
	if (st == 0 || st == 0xff)		/* empty slot */
		return (-1);
	if (wait_status(ST_BSY, 0, 200000) < 0)
		return (-1);
	if (hal_inb(io + ATA_STATUS) & ST_ERR)
		return (-1);			/* ATA device, not ATAPI */
	if (wait_status(ST_DRQ, ST_DRQ, 200000) < 0)
		return (-1);
	hal_insw(io + ATA_DATA, id, 256);
	(void)wait_status(ST_BSY, 0, 200000);
	word0 = id[0];
	if ((word0 & 0x8000) == 0 || ((word0 >> 8) & 0x1f) != 5)
		return (-1);			/* not a CD-ROM */
	return (0);
}

/*
 * Run one ATAPI packet command.  `bytes' is the size of the data-in phase
 * (0 for commands without data).
 */
/* EN: Runs one 12-byte ATAPI packet command in PIO: selects the drive, programs the byte count, sends CMD_PACKET, writes the CDB as 6 words, then PIO-reads bytes into buf; 0 or -1. */
/* 中文：以 PIO 方式执行一条 12 字节 ATAPI 包命令：选定驱动器、设字节数限制、发 CMD_PACKET、把 CDB 当 6 个字写入，再把 bytes 读入 buf；成功 0，超时或 ERR 返 -1。 */
static int
atapi_packet(const unsigned char *cdb, void *buf, unsigned bytes)
{
	unsigned i, words;
	int st;

	hal_outb(atapi_io + ATA_DRIVE, 0xa0 | (atapi_slave << 4));
	udelay();
	if (wait_status(ST_BSY, 0, 200000) < 0)
		return (-1);
	hal_outb(atapi_io + ATA_FEATURES, 0);
	hal_outb(atapi_io + ATA_SECCOUNT, 0);
	hal_outb(atapi_io + ATA_LBA0, 0);
	/* byte count limit (little endian) for the data-in phase */
	hal_outb(atapi_io + ATA_LBA1, bytes & 0xff);
	hal_outb(atapi_io + ATA_LBA2, (bytes >> 8) & 0xff);
	hal_outb(atapi_io + ATA_COMMAND, CMD_PACKET);
	udelay();
	if (wait_status(ST_BSY | ST_DRQ, ST_DRQ, 200000) < 0)
		return (-1);

	/* 12-byte CDB => 6 words, low byte first */
	{
		unsigned short w[6];

		for (i = 0; i < 6; i++)
			w[i] = cdb[i * 2] | (cdb[i * 2 + 1] << 8);
		hal_outsw(atapi_io + ATA_DATA, w, 6);
	}
	udelay();

	if (wait_status(ST_BSY, 0, 2000000) < 0)
		return (-1);
	st = hal_inb(atapi_io + ATA_STATUS);
	if (st & ST_ERR)
		return (-1);
	if (bytes == 0)
		return (0);
	if ((st & ST_DRQ) == 0)
		return (-1);

	words = bytes / 2;
	hal_insw(atapi_io + ATA_DATA, buf, words);
	(void)wait_status(ST_BSY, 0, 2000000);
	return (0);
}

/* EN: Tries probe_one() on primary master, primary slave, secondary master, secondary slave; on the first hit stores (atapi_io << 4) | atapi_slave in atapi_unit and returns 0, else -1. */
/* 中文：依次对主通道主盘/从盘、次通道主盘/从盘调 probe_one()；首次命中时把 (atapi_io << 4) | atapi_slave 存入 atapi_unit 并返回 0，全部失败返回 -1。 */
int
atapi_probe(void)
{

	if (probe_one(PRIMARY_IO, PRIMARY_CTL, 0) == 0 ||
	    probe_one(PRIMARY_IO, PRIMARY_CTL, 1) == 0 ||
	    probe_one(SECONDARY_IO, SECONDARY_CTL, 0) == 0 ||
	    probe_one(SECONDARY_IO, SECONDARY_CTL, 1) == 0) {
		atapi_unit = (atapi_io << 4) | atapi_slave;
		return (0);
	}
	return (-1);
}

/* EN: Returns true (1) when atapi_unit >= 0, i.e. atapi_probe() already found a device; it only reads the cached variable and touches no hardware. */
/* 中文：当 atapi_unit >= 0（即 atapi_probe() 已找到设备）时返回 1；仅读取缓存变量，不访问硬件。 */
int
atapi_present(void)
{

	return (atapi_unit >= 0);
}

/* EN: Reads nsec 2048-byte sectors starting at lba, issuing one READ(12) packet per sector into consecutive buf positions; returns 0 when all arrive, -1 on no device or any failure. */
/* 中文：从逻辑块 lba 起读 nsec 个 2048 字节扇区，每扇发一条 READ(12) 包命令依次写入 buf；全部成功返回 0，无设备或任一次失败返回 -1。 */
int
atapi_read(unsigned lba, unsigned nsec, void *buf)
{
	unsigned char cdb[12];
	unsigned char *bp = buf;
	unsigned n;
	unsigned got = 0;
	unsigned one = 1;

	if (atapi_unit < 0 || nsec == 0)
		return (-1);

	/*
	 * Some emulations are happier with one sector per READ(10); the CD
	 * is slow anyway and this keeps the PIO loop trivial.
	 */
	for (n = 0; n < nsec; n++) {
		bzero(cdb, sizeof(cdb));
		cdb[0] = 0xa8;			/* READ(12) */
		cdb[2] = (lba >> 24) & 0xff;
		cdb[3] = (lba >> 16) & 0xff;
		cdb[4] = (lba >> 8) & 0xff;
		cdb[5] = lba & 0xff;
		cdb[6] = 0;
		cdb[7] = 0;
		cdb[8] = 0;
		cdb[9] = one & 0xff;
		if (atapi_packet(cdb, bp, 2048) < 0)
			return (-1);
		bp += 2048;
		lba++;
		got++;
		if (got == nsec)
			break;
	}
	return (0);
}

/* EN: Issues READ CAPACITY (0x25) into an 8-byte buffer and returns the big-endian last LBA in *lastlba; returns -1 if the packet command fails. */
/* 中文：发 READ CAPACITY(0x25) 命令到 8 字节缓冲区，把大端序的最后一个 LBA 写入 *lastlba；包命令失败返回 -1。 */
int
atapi_capacity(unsigned *lastlba)
{
	unsigned char cdb[12];
	unsigned char data[8];

	bzero(cdb, sizeof(cdb));
	cdb[0] = 0x25;			/* READ CAPACITY */
	if (atapi_packet(cdb, data, sizeof(data)) < 0)
		return (-1);
	*lastlba = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
	return (0);
}

/* EN: Issues INQUIRY (0x12) for 36 bytes, copies the 8-byte vendor and 16-byte product fields out with NULs turned into spaces and terminators added; -1 on failure. */
/* 中文：发 INQUIRY(0x12) 取 36 字节，将 8 字节厂商与 16 字节产品名拷出，NUL 换成空格并补结束符；失败返回 -1。 */
int
atapi_inquiry(char *vendor, char *product)
{
	unsigned char cdb[12];
	unsigned char data[36];
	int i;

	bzero(cdb, sizeof(cdb));
	cdb[0] = 0x12;			/* INQUIRY */
	cdb[4] = sizeof(data);
	if (atapi_packet(cdb, data, sizeof(data)) < 0)
		return (-1);
	for (i = 0; i < 8; i++)
		vendor[i] = data[8 + i] ? data[8 + i] : ' ';
	vendor[8] = 0;
	for (i = 0; i < 16; i++)
		product[i] = data[16 + i] ? data[16 + i] : ' ';
	product[16] = 0;
	return (0);
}
