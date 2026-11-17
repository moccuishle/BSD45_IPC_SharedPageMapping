/*
 * install.c - one-shot "install BSD45 from the ISO onto the VMware disk".
 *
 * Guest-side half of the Ubuntu-style install flow.  Booted from the live
 * ISO, the on-board monitor's `install` command:
 *   1. detects the VMware virtual ATA disk (primary master, LBA28);
 *   2. streams /BSD45.FFS and /BSD45.ELF off the ISO9660 volume;
 *   3. writes the FFS image, the boot stages and the kernel to the disk;
 *   4. prints "reboot, disconnect the ISO" for the next boot from disk.
 *
 * Disk layout (512-byte sectors, no partitions -- a "superfloppy" MBR):
 *
 *   LBA 0        MBR = stage1_hdd (patched with kernel LBA/size)
 *   LBA 1..8     stage2, the 32-bit ELF loader (8 sectors reserved)
 *   LBA 16..     FFS superblock (image byte 8192) and the whole FFS image
 *   LBA 8192..   raw ELF kernel loaded by stage1 to 0x10000
 *
 * mkffs.py leaves the first 8192 image bytes unused, so writing the whole
 * image from disk LBA 0 makes image byte N land at disk LBA N/512; the FFS
 * superblock at byte 8192 then sits at LBA 16 exactly where ffs.c reads it.
 * The MBR/stage2 overwrite image bytes 0..4607 (the unused FFS boot area).
 *
 * To stay independent of the pmap/VM layer, files are streamed one 2048-byte
 * ISO sector at a time through a small static buffer (no scratch VA window).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>

#include "atapi.h"
#include "ata.h"
#include "vfs.h"

extern void bcopy(const void *, void *, unsigned);
extern void bzero(void *, unsigned);
extern int  strcmp(const char *, const char *);

/* boot stages embedded by build_kernel.ps1 (see embed.c) */
extern const unsigned char stage1_hdd_bin[];
extern const unsigned int  stage1_hdd_bin_len;
extern const unsigned char stage2_bin[];
extern const unsigned int  stage2_bin_len;

#define SECSZ		512
#define ISO_SECSZ	2048
#define ISO_SECS_PER_DISK	(ISO_SECSZ / SECSZ)	/* 4 */

/* disk placement */
#define D_MBR_LBA	0
#define D_STAGE2_LBA	1
#define D_STAGE2_SECS	8
#define D_KERNEL_LBA	8192	/* raw ELF kernel (4 MiB) */

static unsigned char iso_sec[ISO_SECSZ];
static unsigned char mbr[SECSZ];
static unsigned char s2[D_STAGE2_SECS * SECSZ];
static unsigned streamed_len;	/* bytes seen by the current stream */

/* 中文：把 ISO9660 文件按 2048 字节的区一次流式读入静态缓冲 iso_sec，每读一区调用 emit(chunk, sec)，成功返回 0、失败返回 -1 */
/* EN: Stream an ISO9660 file one 2048-byte extent sector at a time into the static iso_sec buffer, invoking emit(lba_chunk, sec) for each. Returns 0 or -1. */
static int
stream_iso_file(const char *path,
    int (*emit)(unsigned chunk, const unsigned char *sec))
{
	struct vnode *vp = 0;
	unsigned len, nsec, i, lba;

	if (vfs_namei(path, &vp) < 0 || vp == 0 || vp->type != VREG) {
		printf("install: %s: not found in ISO\n", path);
		return (-1);
	}
	len = vp->size;
	lba = vp->lba;			/* ISO extent, 2048-byte sectors */
	vn_put(vp);
	if (len == 0) {
		printf("install: %s: zero length\n", path);
		return (-1);
	}
	printf("install: %s: lba %d, %d bytes\n", path, lba, len);
	nsec = (len + ISO_SECSZ - 1) / ISO_SECSZ;
	streamed_len = 0;
	for (i = 0; i < nsec; i++) {
		if (atapi_read(lba + i, 1, iso_sec) < 0) {
			printf("install: CD read error at lba %d\n", lba + i);
			return (-1);
		}
		if (emit(i, iso_sec) < 0) {
			printf("install: disk write error at chunk %d\n", i);
			return (-1);
		}
		streamed_len += ISO_SECSZ;
	}
	streamed_len = len;		/* exact byte count, not rounded up */
	return (0);
}

/* 中文：把一个 FFS 镜像区块写到磁盘：CD 扇区 chunk 对应磁盘 LBA chunk*4（镜像字节 N 落在磁盘 LBA N/512） */
/* EN: Emit an FFS image chunk to the disk: CD sector `chunk` -> disk LBA chunk*4 (image byte N == disk LBA N/512). */
static int
emit_ffs(unsigned chunk, const unsigned char *sec)
{

	return (ata_write(chunk * ISO_SECS_PER_DISK, ISO_SECS_PER_DISK, sec) < 0 ?
	    -1 : 0);
}

/* 中文：把一个内核区块写到磁盘的 D_KERNEL_LBA + chunk*4 处 */
/* EN: Emit a kernel chunk to the disk at D_KERNEL_LBA + chunk*4. */
static int
emit_kernel(unsigned chunk, const unsigned char *sec)
{

	return (ata_write(D_KERNEL_LBA + chunk * ISO_SECS_PER_DISK,
	    ISO_SECS_PER_DISK, sec) < 0 ? -1 : 0);
}

/* 中文：在 mbr 中查找并改写 "BDMF" 清单（内核 LBA、内核字节数、stage2 字节数），返回偏移，找不到返回 -1 */
/* EN: Patch the "BDMF" manifest inside mbr (kernel LBA, kernel byte count, stage2 byte count); returns offset or -1. */
static int
patch_manifest(unsigned klen)
{
	int i, off = -1;

	for (i = 0; i <= SECSZ - 16; i++) {
		if (mbr[i] == 'B' && mbr[i + 1] == 'D' &&
		    mbr[i + 2] == 'M' && mbr[i + 3] == 'F') {
			off = i;
			break;
		}
	}
	if (off < 0)
		return (-1);
	mbr[off + 4 + 0] = (unsigned char)(D_KERNEL_LBA);
	mbr[off + 4 + 1] = (unsigned char)(D_KERNEL_LBA >> 8);
	mbr[off + 4 + 2] = (unsigned char)(D_KERNEL_LBA >> 16);
	mbr[off + 4 + 3] = (unsigned char)(D_KERNEL_LBA >> 24);
	mbr[off + 8 + 0] = (unsigned char)(klen);
	mbr[off + 8 + 1] = (unsigned char)(klen >> 8);
	mbr[off + 8 + 2] = (unsigned char)(klen >> 16);
	mbr[off + 8 + 3] = (unsigned char)(klen >> 24);
	mbr[off + 12 + 0] = (unsigned char)(stage2_bin_len);
	mbr[off + 12 + 1] = (unsigned char)(stage2_bin_len >> 8);
	mbr[off + 12 + 2] = (unsigned char)(stage2_bin_len >> 16);
	mbr[off + 12 + 3] = (unsigned char)(stage2_bin_len >> 24);
	return (off);
}

/* 中文：install 命令主体：把 ISO 上的 FFS 镜像、stage2 引导程序、内核与 MBR 依次写入 VMware 虚拟磁盘，成功返回 0、失败返回 -1 */
/* EN: The install command body. Returns 0 on success, -1 on failure. */
int
install_to_disk(void)
{
	unsigned cap, klen;

	printf("\ninstall: BSD45 ISO -> VMware virtual disk\n");

	/* Clear any ABRT/DRQ left on the primary channel by atapi_probe's
	 * IDENTIFY PACKET, then re-probe.  (VMware keeps the CD on ide1, so
	 * this is only needed when CD and disk share a channel.) */
	ata_reset();
	if (ata_probe() != 0 || ata_present() == 0) {
		printf("install: no ATA disk found on primary master\n");
		return (-1);
	}
	cap = ata_capacity();
	printf("install: ATA disk %d sectors (%d MiB)\n", cap, cap / 2048);
	if (cap < (D_KERNEL_LBA + 1024)) {
		printf("install: disk too small (need >= %d sectors)\n",
		    D_KERNEL_LBA + 1024);
		return (-1);
	}

	/* ---- 1. FFS image from the ISO -> disk LBA 0 (image byte N -> N/512) */
	/* NOTE: iso_readdir() lowercases ISO names, so query paths are lowercase. */
	printf("install: writing /bsd45.ffs to disk ...\n");
	if (stream_iso_file("/bsd45.ffs", emit_ffs) < 0) {
		printf("install: cannot write /bsd45.ffs\n");
		return (-1);
	}
	printf("install: FFS written (%d bytes)\n", streamed_len);

	/* ---- 2. stage2 (ELF loader) -> disk LBA 1 ------------------------ */
	if (stage2_bin_len > sizeof(s2)) {
		printf("install: stage2 too big\n");
		return (-1);
	}
	bzero(s2, sizeof(s2));
	bcopy(stage2_bin, s2, stage2_bin_len);
	printf("install: writing stage2 (%d bytes) at LBA %d\n",
	    stage2_bin_len, D_STAGE2_LBA);
	if (ata_write(D_STAGE2_LBA, D_STAGE2_SECS, s2) < 0) {
		printf("install: stage2 write failed\n");
		return (-1);
	}

	/* ---- 3. raw ELF kernel -> disk LBA 8192 -------------------------- */
	printf("install: writing /bsd45.elf to disk ...\n");
	if (stream_iso_file("/bsd45.elf", emit_kernel) < 0) {
		printf("install: cannot write /bsd45.elf\n");
		return (-1);
	}
	klen = streamed_len;
	printf("install: kernel written (%d bytes, %d sectors at LBA %d)\n",
	    klen, (klen + SECSZ - 1) / SECSZ, D_KERNEL_LBA);

	/* ---- 4. MBR / stage1_hdd with patched manifest -> LBA 0 ---------- */
	if (stage1_hdd_bin_len != SECSZ) {
		printf("install: bad stage1_hdd size %d\n", stage1_hdd_bin_len);
		return (-1);
	}
	bcopy(stage1_hdd_bin, mbr, SECSZ);
	if (patch_manifest(klen) < 0) {
		printf("install: manifest magic not found\n");
		return (-1);
	}
	printf("install: writing MBR (kernel LBA %d, %d bytes) at LBA 0\n",
	    D_KERNEL_LBA, klen);
	if (ata_write(D_MBR_LBA, 1, mbr) < 0) {
		printf("install: MBR write failed\n");
		return (-1);
	}

	printf("\ninstall: SUCCESS - BSD45 is on the virtual disk.\n");
	printf("install: type 'reboot', disconnect the ISO, and boot from disk.\n");
	return (0);
}
