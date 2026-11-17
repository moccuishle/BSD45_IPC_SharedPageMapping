/*
 * ffs.c - minimal read-only 4.4BSD-Lite2 FFS (UFS1) root filesystem.
 *
 * This is the read side of the hdd install: after `install` writes an FFS
 * image (built by mkffs.py) to the ATA disk and the machine reboots from
 * its MBR, this parser mounts that FFS as root so execve() can load
 * /sbin/init and /bin/sh from disk.
 *
 * Only what the image uses is supported:
 *   - 512-byte device sectors, 4096-byte fs blocks, 8 frags/block;
 *   - one cylinder group; all file data in direct blocks (no indirect);
 *   - directories in struct direct format.
 * The superblock is parsed (not hardcoded) so the layout follows the disk.
 *
 * Layout constants MUST match hdd/mkffs.py.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>

#include "ata.h"
#include "ffs.h"

extern void bcopy(const void *, void *, unsigned);
extern void bzero(void *, unsigned);
extern int strcmp(const char *, const char *);
extern struct vnode *vn_alloc(void);

#define	SECSZ		512
#define	SBYTE		8192		/* superblock byte offset (SBOFF) */
#define	FS_MAGIC	0x011954
#define	INOSIZE		128
#define	NDADDR		12
#define	IFMT		0170000
#define	IFDIR		0040000
#define	IFREG		0100000
#define	MAXNAMLEN	255

/* decoded superblock */
static unsigned f_bsize, f_fsize, f_frag;
static unsigned f_iblkno, f_dblkno;	/* in fragments */
static unsigned f_ipg, f_inopb, f_nindir;
static unsigned f_fsbtodb;
static int f_mounted;

/* EN: The statically allocated FFS root vnode (inode 2).  vfs.c mounts it on "/" when booted from disk. */
static struct vnode ffs_rootv;

/* 中文：返回静态分配的 FFS 根 vnode（inode 2），从磁盘启动时由 vfs.c 挂载到根目录 */
/* EN: Return the statically allocated FFS root vnode (inode 2), mounted on "/" by vfs.c when booting from disk */
struct vnode *
ffs_root(void)
{

	return (&ffs_rootv);
}

/* 中文：判断给定 vnode 是否就是 FFS 的根 vnode */
/* EN: Test whether the given vnode is the FFS root vnode */
int
ffs_is_root(struct vnode *vp)
{

	return (vp == &ffs_rootv);
}

/* little-endian readers */
/* 中文：从字节缓冲区 p 读取一个 4 字节小端无符号整数 */
/* EN: Read a 4-byte little-endian unsigned integer from the byte buffer p */
static unsigned
le32(const unsigned char *p)
{
	return ((unsigned)p[0]) | ((unsigned)p[1] << 8) |
	    ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/* 中文：从字节缓冲区 p 读取一个 2 字节小端无符号整数 */
/* EN: Read a 2-byte little-endian unsigned integer from the byte buffer p */
static unsigned
le16(const unsigned char *p)
{
	return ((unsigned)p[0]) | ((unsigned)p[1] << 8);
}

/* 中文：通过 ATA 驱动从磁盘读取一个 512 字节的扇区到 buf */
/* EN: Read one 512-byte sector from the disk into buf via the ATA driver. */
static int
ffs_readsector(unsigned sec, void *buf)
{
	return (ata_read(sec, 1, buf) == 0 ? 0 : -1);
}

/* 中文：把 fragment 编号换算成设备扇区编号 */
/* EN: Convert a fragment number to a device sector number. */
static unsigned
frag_to_sec(unsigned frag)
{

	return (frag << f_fsbtodb);
}

/* 中文：从 ATA 磁盘挂载 FFS：读取字节 8192 处的超级块、校验魔数并解析本镜像使用的几何参数，成功返回 0 */
/* EN: Mount the FFS from the ATA disk: read the superblock at byte 8192, validate the magic and decode the geometry this image uses. Returns 0 on success. */
int
ffs_mount(void)
{
	/* fs_magic sits at superblock offset 1372, so read >= 4 sectors (2048B).
	 * A single 512B sector used to overflow this buffer. */
	unsigned char sb[2048];
	int r;

	if (ata_present() == 0) {
		printf("ffs: no ATA disk\n");
		return (-1);
	}
	r = ata_read(SBYTE / SECSZ, 4, sb);
	if (r < 0) {
		printf("ffs: superblock read failed (%d)\n", r);
		return (-1);
	}
	if (le32(sb + 1372) != FS_MAGIC) {	/* fs_magic offset in struct fs */
		printf("ffs: bad magic %x\n", le32(sb + 1372));
		return (-1);
	}
	f_bsize  = le32(sb + 48);		/* fs_bsize */
	f_fsize  = le32(sb + 52);		/* fs_fsize */
	f_frag   = le32(sb + 56);		/* fs_frag */
	f_iblkno = le32(sb + 16);		/* fs_iblkno (frag) */
	f_dblkno = le32(sb + 20);		/* fs_dblkno (frag) */
	f_inopb  = le32(sb + 120);		/* fs_inopb */
	f_nindir = le32(sb + 116);		/* fs_nindir */
	f_fsbtodb = le32(sb + 100);		/* fs_fsbtodb */
	f_ipg    = le32(sb + 184);		/* fs_ipg */
	if (f_bsize == 0 || f_fsize == 0 || f_inopb == 0 || f_ipg == 0) {
		printf("ffs: bad geometry bsize=%d fsize=%d inopb=%d ipg=%d\n",
		    f_bsize, f_fsize, f_inopb, f_ipg);
		return (-1);
	}
	printf("ffs: mounted bsize %d fsize %d frag %d iblkno %d dblkno %d inopb %d\n",
	    f_bsize, f_fsize, f_frag, f_iblkno, f_dblkno, f_inopb);
	f_mounted = 1;
	return (0);
}

/* 中文：当 ffs_mount() 成功后返回非零值，表示文件系统已挂载 */
/* EN: Return nonzero once ffs_mount() succeeded. */
int
ffs_mounted(void)
{

	return (f_mounted);
}

/* 中文：把磁盘上编号为 ino 的 inode 读入 128 字节的 dinode 缓冲区；inode 1 是 f_iblkno 处 inode 表的首项，每个 inode 占 INOSIZE 字节 */
/* EN: Read the on-disk inode `ino` into the 128-byte dinode buffer. inode 1 is the first entry in the inode table at fragment f_iblkno; each inode is INOSIZE bytes. */
static int
ffs_inode(unsigned ino, unsigned char din[INOSIZE])
{
	unsigned frag, sec, off;

	if (ino == 0)
		return (-1);
	frag = f_iblkno + ((ino - 1) / f_inopb);	/* inode block (frags) */
	off  = ((ino - 1) % f_inopb) * INOSIZE;
	sec  = frag_to_sec(frag) + off / SECSZ;
	off %= SECSZ;
	if (ffs_readsector(sec, din) < 0)	/* inode fits in one sector (128B) */
		return (-1);
	if (off)
		bcopy(din + off, din, INOSIZE);	/* dinode never crosses a sector */
	return (0);
}

/* 中文：用 di_db[0..11] 直接块、再用单级间接块 di_ib[0]，把逻辑 4096 字节块号映射到其磁盘 fragment；失败返回 0 */
/* EN: Map a logical 4096-byte block number to its disk fragment using di_db[0..11] then the single-indirect di_ib[0]. Returns 0 on failure. */
static unsigned
ffs_lbn_to_frag(const unsigned char din[INOSIZE], unsigned lbn)
{
	unsigned db;

	if (lbn < NDADDR)
		return (le32(din + 40 + lbn * 4));	/* di_db[lbn] */
	{
		unsigned indir = le32(din + 88);	/* di_ib[0] */
		unsigned idx = lbn - NDADDR;
		static unsigned char blk[SECSZ];
		unsigned sec = frag_to_sec(indir) + (idx * 4) / SECSZ;
		unsigned pos = (idx * 4) % SECSZ;

		if (indir == 0 || idx >= f_nindir)
			return (0);
		if (ffs_readsector(sec, blk) < 0)
			return (0);
		return (le32(blk + pos));
	}
}

/* 中文：用直接块加单级间接块读取普通 inode 的字节区间 [off,off+len) 到 buf；返回实际拷贝字节数，失败返回 -1 */
/* EN: Read byte range [off,off+len) of a regular inode using direct + single-indirect blocks. Returns bytes copied or -1. */
static int
ffs_readfile(const unsigned char din[INOSIZE], unsigned off, void *buf,
    unsigned len)
{
	unsigned size = le32(din + 8);		/* di_size low 32 bits */
	unsigned char *out = buf;
	unsigned done = 0;

	if (off >= size)
		return (0);
	if (len > size - off)
		len = size - off;
	while (done < len) {
		unsigned lbn = (off + done) / f_bsize;
		unsigned boff = (off + done) % f_bsize;
		unsigned db, sec;
		unsigned char secbuf[SECSZ];
		unsigned chunk = f_bsize - boff;

		db = ffs_lbn_to_frag(din, lbn);
		if (db == 0)
			return (-1);
		sec = frag_to_sec(db) + boff / SECSZ;
		while (chunk > 0) {
			unsigned n = SECSZ - (boff % SECSZ);

			if (n > chunk)
				n = chunk;
			if (n > len - done)
				n = len - done;
			if (ffs_readsector(sec, secbuf) < 0)
				return (-1);
			bcopy(secbuf + (boff % SECSZ), out + done, n);
			done += n;
			boff += n;
			chunk -= n;
			sec++;
		}
	}
	return ((int)done);
}

/* 中文：在目录 inode 中查找名字 name，把匹配项的子 inode 号写入 *inop；返回 0、-ENOENT 或 -EIO */
/* EN: Scan a directory inode for `name`, filling *inop with the child inode number. Returns 0, -ENOENT or -EIO. */
static int
ffs_dirlookup(const unsigned char din[INOSIZE], const char *name,
    unsigned *inop)
{
	unsigned size = le32(din + 8);
	static unsigned char dirbuf[4096];
	unsigned got, pos;

	/* directory is one block in our images */
	if (ffs_readfile(din, 0, dirbuf, size) < 0)
		return (-EIO);
	for (pos = 0; pos + 8 <= size; ) {
		unsigned ino = le32(dirbuf + pos);
		unsigned reclen = dirbuf[pos + 4] | (dirbuf[pos + 5] << 8);
		unsigned namlen = dirbuf[pos + 7];

		if (reclen < 8 || pos + reclen > size)
			return (-EIO);
		if (ino && namlen && namlen <= MAXNAMLEN &&
		    strcmp((char *)dirbuf + pos + 8, name) == 0) {
			*inop = ino;
			return (0);
		}
		pos += reclen;
	}
	got = 0;
	(void)got;
	return (-ENOENT);
}

/* 中文：把 inode 号解析为文件类型（VDIR/VREG）和大小 */
/* EN: Resolve an inode number into a type and size. */
static void
ffs_statino(unsigned ino, int *type, unsigned *size)
{
	unsigned char din[INOSIZE];

	if (ffs_inode(ino, din) < 0) {
		*type = VREG;
		*size = 0;
		return;
	}
	*type = (le16(din) & IFMT) == IFDIR ? VDIR : VREG;
	*size = le32(din + 8);
}

/* 中文：vnodeops.lookup：父 vnode 的 lba 保存其 inode 号，在该目录中查找 name 并分配一个以子 inode 为键的子 vnode */
/* EN: vnodeops.lookup: the parent vnode's lba holds its inode number; find `name` in that directory and allocate a child vnode keyed by the child inode. */
static int
ffs_lookup_op(struct vnode *dv, const char *name, struct vnode **vpp)
{
	unsigned char din[INOSIZE];
	unsigned ino, size;
	int type;

	if (dv->type != VDIR)
		return (-ENOTDIR);
	if (ffs_inode(dv->lba, din) < 0)
		return (-EIO);
	if (ffs_dirlookup(din, name, &ino) < 0)
		return (-ENOENT);
	ffs_statino(ino, &type, &size);
	{
		struct vnode *vp = vn_alloc();

		if (vp == 0)
			return (-ENOMEM);
		vp->type = type;
		vp->ops = dv->ops;
		vp->lba = ino;			/* inode number */
		vp->size = size;
		vp->v_key = 0x40000000u | ino;	/* stable key for FFS inodes */
		*vpp = vp;
	}
	return (0);
}

/* 中文：vnodeops.getattr：从 inode 报告文件的类型和大小 */
/* EN: vnodeops.getattr: report file type and size from the inode. */
static int
ffs_getattr_op(struct vnode *vp, unsigned *mode, unsigned *size)
{
	unsigned char din[INOSIZE];

	if (ffs_inode(vp->lba, din) < 0)
		return (-EIO);
	*mode = le16(din);
	*size = le32(din + 8);
	return (0);
}

/* 中文：vnodeops.readraw：直接从 ATA 磁盘读取文件字节（页缓存填充路径） */
/* EN: vnodeops.readraw: read file bytes straight from the ATA disk (page-cache fill path). */
static int
ffs_readraw_op(struct vnode *vp, unsigned off, void *buf, unsigned len)
{
	unsigned char din[INOSIZE];

	if (vp->type == VDIR)
		return (-EISDIR);
	if (ffs_inode(vp->lba, din) < 0)
		return (-EIO);
	return (ffs_readfile(din, off, buf, len) < 0 ? -EIO : 0);
}

/* 中文：vnodeops.read：像 iso_read_op 一样在页缓存之上读取文件数据 */
/* EN: vnodeops.read: layered on the page cache like iso_read_op. */
static int
ffs_read_op(struct vnode *vp, unsigned off, void *buf, unsigned len)
{

	if (vp->type == VDIR)
		return (-EISDIR);
	return (pagecache_read(vp, off, buf, len));
}

/* 中文：vnodeops.readdir：遍历目录 inode 的 struct direct 记录，逐项返回名字与是否为目录 */
/* EN: vnodeops.readdir: iterate struct direct records of a directory inode. */
static int
ffs_readdir_op(struct vnode *vp, unsigned *cookie, char *name, int *isdir)
{
	unsigned char din[INOSIZE];
	static unsigned char dirbuf[4096];
	unsigned size, pos = *cookie;

	if (vp->type != VDIR)
		return (-ENOTDIR);
	if (ffs_inode(vp->lba, din) < 0)
		return (-EIO);
	size = le32(din + 8);
	if (ffs_readfile(din, 0, dirbuf, size) < 0)
		return (-EIO);
	while (pos + 8 <= size) {
		unsigned ino = le32(dirbuf + pos);
		unsigned reclen = dirbuf[pos + 4] | (dirbuf[pos + 5] << 8);
		unsigned namlen = dirbuf[pos + 7];
		unsigned char dino_din[INOSIZE];

		if (reclen < 8 || pos + reclen > size)
			return (-EIO);
		pos += reclen;
		*cookie = pos;
		if (ino == 0 || namlen == 0 || namlen > MAXNAMLEN)
			continue;
		bcopy(dirbuf + pos - reclen + 8, name, namlen);
		name[namlen] = 0;
		{
			int t;
			unsigned s;

			ffs_statino(ino, &t, &s);
			*isdir = (t == VDIR);
		}
		return (0);
	}
	return (1);
}

struct vnodeops ffs_vnodeops = {
	ffs_lookup_op, ffs_getattr_op, ffs_read_op, 0, ffs_readdir_op,
	0, 0, 0, 0, 0, 0, ffs_readraw_op
};
