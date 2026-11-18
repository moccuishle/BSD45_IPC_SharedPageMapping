/*
 * jlfs.c - writable 4.4BSD-Lite2 FFS (UFS1) filesystem for BSD45.
 *
 * Read side is derived from fs/ffs/ffs.c (same on-disk format as
 * hdd/mkffs.py).  On top of that this file adds persistent writes for all
 * seven vnodeops: write / create / remove / mkdir / rmdir / truncate /
 * rename.  Allocation uses a cylinder-group bitmap; a legacy image without
 * a bitmap has one rebuilt by scanning the inode table on first mount.
 *
 * Layout constants MUST match hdd/mkffs.py and fs/ffs/ffs.c.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>

#include "ata.h"
#include "vfs.h"
#include "jlfs.h"

extern void bcopy(const void *, void *, unsigned);
extern void bzero(void *, unsigned);
extern int strcmp(const char *, const char *);
extern struct vnode *vn_alloc(void);

#define SECSZ       512
#define SBYTE       8192
#define FS_MAGIC    0x011954
#define INOSIZE     128
#define NDADDR      12
#define NIADDR      3
#define IFMT        0170000
#define IFDIR       0040000
#define IFREG       0100000
#define MAXNAMLEN   255
#define DIRSIZ(n)   (((n) + 8 + 3) & ~3)

/* offsets inside struct fs */
#define FS_IBLKNO   16
#define FS_DBLKNO   20
#define FS_SIZE     36
#define FS_BSIZE    48
#define FS_FSIZE    52
#define FS_FRAG     56
#define FS_FSBTODB  100
#define FS_NINDIR   116
#define FS_INOPB    120
#define FS_IPG      184
#define FS_CSADDR   152
#define FS_MAGICOFF 1372

/* offsets inside the cylinder-group block */
#define CG_MAGIC    0x4a4c4653
#define CG_MAGICOFF 0
#define CG_INOMAP   64
#define CG_FRAGMAP  128

/* decoded superblock */
static unsigned f_bsize, f_fsize, f_frag, f_iblkno, f_dblkno;
static unsigned f_ipg, f_inopb, f_nindir, f_fsbtodb, f_size, f_csaddr;
static int f_mounted;
static unsigned char f_cg[4096];

static struct vnode jlfs_rootv;

static int jlfs_inode(unsigned ino, unsigned char din[INOSIZE]);
static int cg_flush(void);
static void cg_rebuild(void);
static int cg_flush(void);
static void cg_rebuild(void);

/* ---------- little-endian helpers ---------- */
static unsigned
le32(const unsigned char *p)
{
    return ((unsigned)p[0]) | ((unsigned)p[1] << 8) |
        ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static unsigned
le16(const unsigned char *p)
{
    return ((unsigned)p[0]) | ((unsigned)p[1] << 8);
}
static void
le32put(unsigned char *p, unsigned v)
{
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}
static void
le16put(unsigned char *p, unsigned v)
{
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
}

/* ---------- raw sector I/O ---------- */
static int
jlfs_readsector(unsigned sec, void *buf)
{
    int r, try;
    for (try = 0; try < 4; try++) {
        r = ata_read(sec, 1, buf);
        if (r == 0) return (0);
    }
    return (-1);
}
static int
jlfs_writesector(unsigned sec, const void *buf)
{
    int r, try;
    for (try = 0; try < 4; try++) {
        r = ata_write(sec, 1, buf);
        if (r == 0) return (0);
    }
    return (-1);
}
static unsigned
frag_to_sec(unsigned frag)
{
    return (frag << f_fsbtodb);
}

/* ---------- root vnode helpers ---------- */
struct vnode *
jlfs_root(void) { return (&jlfs_rootv); }
int
jlfs_is_root(struct vnode *vp) { return (vp == &jlfs_rootv); }

/* ------------------------------------------------------------------ */
/* mount                                                               */
/* ------------------------------------------------------------------ */
int
jlfs_mount(void)
{
    unsigned char sb[2048];
    int r;

    if (ata_present() == 0) {
        printf("jlfs: no ATA disk\n");
        return (-1);
    }
    r = ata_read(SBYTE / SECSZ, 4, sb);
    if (r < 0 || le32(sb + FS_MAGICOFF) != FS_MAGIC) {
        printf("jlfs: no FFS superblock\n");
        return (-1);
    }
    f_bsize  = le32(sb + FS_BSIZE);
    f_fsize  = le32(sb + FS_FSIZE);
    f_frag   = le32(sb + FS_FRAG);
    f_iblkno = le32(sb + FS_IBLKNO);
    f_dblkno = le32(sb + FS_DBLKNO);
    f_inopb  = le32(sb + FS_INOPB);
    f_nindir = le32(sb + FS_NINDIR);
    f_fsbtodb= le32(sb + FS_FSBTODB);
    f_ipg    = le32(sb + FS_IPG);
    f_size   = le32(sb + FS_SIZE);
    f_csaddr = le32(sb + FS_CSADDR);
    if (f_bsize == 0 || f_fsize == 0 || f_inopb == 0 || f_ipg == 0 ||
        f_frag == 0 || f_csaddr == 0) {
        printf("jlfs: bad geometry\n");
        return (-1);
    }
    /* load the cylinder-group block; if no JLFS bitmap, reconstruct it */
    if (ata_read(frag_to_sec(f_csaddr), f_bsize / SECSZ, f_cg) < 0) {
        printf("jlfs: cg read failed\n");
        return (-1);
    }
    if (le32(f_cg + CG_MAGICOFF) != CG_MAGIC) {
        printf("jlfs: rebuilding allocation bitmap\n");
        cg_rebuild();
    }
    printf("jlfs: mounted bsize %d fsize %d frag %d ipg %d\n",
        f_bsize, f_fsize, f_frag, f_ipg);
    f_mounted = 1;
    return (0);
}

int
jlfs_mounted(void) { return (f_mounted); }

/* ------------------------------------------------------------------ */
/* inode / block I/O                                                   */
/* ------------------------------------------------------------------ */
static int
jlfs_inode(unsigned ino, unsigned char din[INOSIZE])
{
    unsigned frag, sec, off;
    static unsigned char secbuf[SECSZ];

    if (ino == 0 || ino > f_ipg)
        return (-1);
    frag = f_iblkno + ((ino - 1) / f_inopb);
    off  = ((ino - 1) % f_inopb) * INOSIZE;
    sec  = frag_to_sec(frag) + off / SECSZ;
    off %= SECSZ;
    if (jlfs_readsector(sec, secbuf) < 0)
        return (-1);
    bcopy(secbuf + off, din, INOSIZE);
    return (0);
}

/* write a 128-byte dinode back to its sector (read-modify-write, since the
 * dinode never crosses a 512-byte sector boundary) */
static int
jlfs_putinode(unsigned ino, const unsigned char din[INOSIZE])
{
    unsigned frag, sec, off;
    static unsigned char buf[SECSZ];

    if (ino == 0 || ino > f_ipg)
        return (-1);
    frag = f_iblkno + ((ino - 1) / f_inopb);
    off  = ((ino - 1) % f_inopb) * INOSIZE;
    sec  = frag_to_sec(frag) + off / SECSZ;
    off %= SECSZ;
    if (jlfs_readsector(sec, buf) < 0)
        return (-1);
    bcopy(din, buf + off, INOSIZE);
    return (jlfs_writesector(sec, buf));
}

static unsigned
jlfs_lbn_to_frag(const unsigned char din[INOSIZE], unsigned lbn)
{
    if (lbn < NDADDR)
        return (le32(din + 40 + lbn * 4));
    {
        unsigned indir = le32(din + 88);
        unsigned idx = lbn - NDADDR;
        static unsigned char blk[SECSZ];
        unsigned sec, pos;
        if (indir == 0 || idx >= f_nindir)
            return (0);
        sec = frag_to_sec(indir) + (idx * 4) / SECSZ;
        pos = (idx * 4) % SECSZ;
        if (jlfs_readsector(sec, blk) < 0)
            return (0);
        return (le32(blk + pos));
    }
}

static int
jlfs_readfile(const unsigned char din[INOSIZE], unsigned off, void *buf,
    unsigned len)
{
    unsigned size = le32(din + 8);
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

        db = jlfs_lbn_to_frag(din, lbn);
        if (db == 0)
            return (-1);
        sec = frag_to_sec(db) + boff / SECSZ;
        while (chunk > 0 && done < len) {
            unsigned n = SECSZ - (boff % SECSZ);
            if (n > chunk) n = chunk;
            if (n > len - done) n = len - done;
            if (jlfs_readsector(sec, secbuf) < 0)
                return (-1);
            bcopy(secbuf + (boff % SECSZ), out + done, n);
            done += n; boff += n; chunk -= n; sec++;
        }
    }
    return ((int)done);
}

/* ------------------------------------------------------------------ */
/* bitmap management                                                   */
/* ------------------------------------------------------------------ */
static int cg_flush(void)
{
    return (ata_write(frag_to_sec(f_csaddr), f_bsize / SECSZ, f_cg) == 0 ? 0 : -1);
}

static void
cg_rebuild(void)
{
    unsigned ino, i;
    bzero(f_cg, sizeof(f_cg));
    le32put(f_cg + CG_MAGICOFF, CG_MAGIC);
    for (i = 0; i < f_dblkno; i++)
        f_cg[CG_FRAGMAP + i / 8] |= (1u << (i % 8));
    f_cg[CG_INOMAP] |= (1u << 0);   /* inode 1 reserved */
    for (ino = 2; ino <= f_ipg; ino++) {
        unsigned char din[INOSIZE];
        unsigned mode;
        if (jlfs_inode(ino, din) < 0)
            continue;
        mode = le16(din);
        if ((mode & IFMT) == 0)
            continue;
        f_cg[CG_INOMAP + (ino - 1) / 8] |= (1u << ((ino - 1) % 8));
        for (i = 0; i < NDADDR; i++) {
            unsigned frag = le32(din + 40 + i * 4);
            if (frag >= f_dblkno && frag < f_size)
                f_cg[CG_FRAGMAP + frag / 8] |= (1u << (frag % 8));
        }
        {
            unsigned indir = le32(din + 88);
            if (indir >= f_dblkno && indir < f_size) {
                static unsigned char ib[SECSZ];
                f_cg[CG_FRAGMAP + indir / 8] |= (1u << (indir % 8));
                if (jlfs_readsector(frag_to_sec(indir), ib) == 0)
                    for (i = 0; i < f_nindir; i++) {
                        unsigned frag = le32(ib + i * 4);
                        if (frag >= f_dblkno && frag < f_size)
                            f_cg[CG_FRAGMAP + frag / 8] |= (1u << (frag % 8));
                    }
            }
        }
    }
    cg_flush();
}

static unsigned
cg_alloc_inode(void)
{
    unsigned ino;
    for (ino = 2; ino <= f_ipg; ino++) {
        unsigned char *b = &f_cg[CG_INOMAP + (ino - 1) / 8];
        unsigned mask = 1u << ((ino - 1) % 8);
        if ((*b & mask) == 0) {
            *b |= mask;
            cg_flush();
            return (ino);
        }
    }
    return (0);
}

static void
cg_free_inode(unsigned ino)
{
    f_cg[CG_INOMAP + (ino - 1) / 8] &= ~(1u << ((ino - 1) % 8));
    cg_flush();
}

static unsigned
cg_alloc_block(void)
{
    unsigned frag, i;
    for (frag = f_dblkno; frag + f_frag <= f_size; frag += f_frag) {
        int ok = 1;
        for (i = 0; i < f_frag; i++)
            if (f_cg[CG_FRAGMAP + (frag + i) / 8] & (1u << ((frag + i) % 8))) {
                ok = 0;
                break;
            }
        if (ok) {
            for (i = 0; i < f_frag; i++)
                f_cg[CG_FRAGMAP + (frag + i) / 8] |= (1u << ((frag + i) % 8));
            cg_flush();
            return (frag);
        }
    }
    return (0);
}

static void
cg_free_block(unsigned frag)
{
    unsigned i;
    if (frag == 0) return;
    for (i = 0; i < f_frag; i++)
        f_cg[CG_FRAGMAP + (frag + i) / 8] &= ~(1u << ((frag + i) % 8));
    cg_flush();
}

/* ------------------------------------------------------------------ */
/* file write: grow di_db[] then single-indirect di_ib[0]                */
/* ------------------------------------------------------------------ */

/* set the fragment for logical block lbn in din, allocating an indirect
 * block if we cross NDADDR.  Returns frag on success, 0 on failure. */
static unsigned
jlfs_map_frag(unsigned char din[INOSIZE], unsigned lbn)
{
    unsigned frag;

    if (lbn < NDADDR) {
        frag = le32(din + 40 + lbn * 4);
        if (frag == 0) {
            frag = cg_alloc_block();
            if (frag == 0) return (0);
            le32put(din + 40 + lbn * 4, frag);
        }
        return (frag);
    }
    {
        unsigned indir = le32(din + 88);
        unsigned idx = lbn - NDADDR;
        unsigned char ib[SECSZ];
        unsigned sec, pos, f;
        if (idx >= f_nindir) return (0);
        sec = (indir ? frag_to_sec(indir) : 0);
        pos = (idx * 4) / SECSZ;
        if (indir == 0) {
            indir = cg_alloc_block();
            if (indir == 0) return (0);
            bzero(ib, SECSZ);
            le32put(din + 88, indir);
            sec = frag_to_sec(indir);
            f = le32(ib + (idx * 4) % SECSZ);   /* 0 from bzero */
        } else {
            if (jlfs_readsector(sec + pos, ib) < 0) return (0);
            f = le32(ib + (idx * 4) % SECSZ);
        }
        if (f == 0) {
            f = cg_alloc_block();
            if (f == 0) return (0);
            le32put(ib + (idx * 4) % SECSZ, f);
            if (jlfs_writesector(sec + pos, ib) < 0) return (0);
        }
        return (f);
    }
}

/* write [off,off+len) into a regular file, growing it.  din is updated
 * (block map + di_size + di_blocks) but NOT written back by this function. */
static int
jlfs_writefile(unsigned char din[INOSIZE], unsigned off, const void *buf,
    unsigned len)
{
    const unsigned char *in = buf;
    unsigned size = le32(din + 8);
    unsigned done = 0;

    while (done < len) {
        unsigned lbn = (off + done) / f_bsize;
        unsigned boff = (off + done) % f_bsize;
        unsigned frag, sec;
        unsigned char secbuf[SECSZ];
        unsigned chunk = f_bsize - boff;

        frag = jlfs_map_frag(din, lbn);
        if (frag == 0) return (-1);
        sec = frag_to_sec(frag) + boff / SECSZ;
        while (chunk > 0 && done < len) {
            unsigned n = SECSZ - (boff % SECSZ);
            if (n > chunk) n = chunk;
            if (n > len - done) n = len - done;
            /* read-modify-write: preserve untouched bytes in the sector */
            if (jlfs_readsector(sec, secbuf) < 0) return (-1);
            bcopy(in + done, secbuf + (boff % SECSZ), n);
            if (jlfs_writesector(sec, secbuf) < 0) return (-1);
            done += n; boff += n; chunk -= n; sec++;
        }
    }
    if (off + len > size)
        le32put(din + 8, off + len);
    return ((int)done);
}

/* free all data + indirect blocks referenced by din, reset size/map */
static void
jlfs_freefile(unsigned char din[INOSIZE])
{
    unsigned i;
    for (i = 0; i < NDADDR; i++) {
        cg_free_block(le32(din + 40 + i * 4));
        le32put(din + 40 + i * 4, 0);
    }
    {
        unsigned indir = le32(din + 88);
        if (indir) {
            unsigned char ib[SECSZ];
            if (jlfs_readsector(frag_to_sec(indir), ib) == 0)
                for (i = 0; i < f_nindir; i++)
                    cg_free_block(le32(ib + i * 4));
            cg_free_block(indir);
            le32put(din + 88, 0);
        }
    }
    le32put(din + 8, 0);        /* di_size = 0 */
    le32put(din + 104, 0);      /* di_blocks = 0 */
}

/* ------------------------------------------------------------------ */
/* directory helpers                                                   */
/* ------------------------------------------------------------------ */

/* find `name` in dinode din; fill *inop. 0 on found, -ENOENT / -EIO. */
static int
jlfs_dirlookup(const unsigned char din[INOSIZE], const char *name,
    unsigned *inop)
{
    unsigned size = le32(din + 8);
    static unsigned char dirbuf[4096];
    unsigned pos;

    if (size > sizeof(dirbuf) || jlfs_readfile(din, 0, dirbuf, size) < 0)
        return (-EIO);
    for (pos = 0; pos + 8 <= size; ) {
        unsigned ino = le32(dirbuf + pos);
        unsigned reclen = le16(dirbuf + pos + 4);
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
    return (-ENOENT);
}

/* read all entries of directory dinode into dirbuf; return byte size. */
static int
jlfs_dirload(const unsigned char din[INOSIZE], unsigned char dirbuf[4096],
    unsigned *sizep)
{
    unsigned size = le32(din + 8);
    if (size > 4096)
        return (-1);
    if (size == 0) {
        *sizep = 0;
        return (0);
    }
    if (jlfs_readfile(din, 0, dirbuf, size) < 0)
        return (-1);
    *sizep = size;
    return (0);
}

/* store dirbuf[0..size) as the directory data (single block in our images) */
static int
jlfs_dirstore(unsigned char din[INOSIZE], const unsigned char dirbuf[4096],
    unsigned size)
{
    unsigned frag = le32(din + 40);
    if (frag == 0) {
        frag = cg_alloc_block();
        if (frag == 0) return (-1);
        le32put(din + 40, frag);
        le32put(din + 104, f_frag);
    }
    if (jlfs_writefile(din, 0, dirbuf, size) < 0)
        return (-1);
    return (0);
}

/* append an entry (ino, dtype, name) to directory dinode din */
static int
jlfs_diradd(unsigned char din[INOSIZE], unsigned ino, unsigned char dtype,
    const char *name)
{
    static unsigned char dirbuf[4096];
    unsigned size, namlen, reclen;

    if (jlfs_dirload(din, dirbuf, &size) < 0)
        return (-EIO);
    namlen = strlen(name);
    if (namlen == 0 || namlen > MAXNAMLEN)
        return (-ENAMETOOLONG);
    reclen = DIRSIZ(namlen);
    if (size + reclen > 4096)
        return (-ENOSPC);
    le32put(dirbuf + size, ino);
    le16put(dirbuf + size + 4, reclen);
    dirbuf[size + 6] = dtype;
    dirbuf[size + 7] = namlen;
    bcopy(name, dirbuf + size + 8, namlen);
    bzero(dirbuf + size + 8 + namlen, reclen - 8 - namlen);
    if (jlfs_dirstore(din, dirbuf, size + reclen) < 0)
        return (-EIO);
    return (0);
}

/* remove entry `name` from directory dinode din (coalesce into prev rec) */
static int
jlfs_dirremove(unsigned char din[INOSIZE], const char *name)
{
    static unsigned char dirbuf[4096];
    unsigned size, pos, prev;

    if (jlfs_dirload(din, dirbuf, &size) < 0)
        return (-EIO);
    for (pos = 0, prev = 0; pos + 8 <= size; ) {
        unsigned reclen = le16(dirbuf + pos + 4);
        unsigned namlen = dirbuf[pos + 7];
        if (reclen < 8 || pos + reclen > size)
            return (-EIO);
        if (namlen && strcmp((char *)dirbuf + pos + 8, name) == 0) {
            if (prev < pos) {
                /* merge this record into the previous one */
                le16put(dirbuf + prev + 4, le16(dirbuf + prev + 4) + reclen);
                bcopy(dirbuf + pos + reclen, dirbuf + pos,
                    size - pos - reclen);
                bzero(dirbuf + size - reclen, reclen);
                size -= reclen;
            } else {
                /* first record: just mark inode 0 (skip) */
                le32put(dirbuf + pos, 0);
            }
            if (jlfs_dirstore(din, dirbuf, size) < 0)
                return (-EIO);
            return (0);
        }
        prev = pos;
        pos += reclen;
    }
    return (-ENOENT);
}

/* rename an entry in place: if the new name fits in the old record's name
 * field, overwrite it; else rebuild the whole directory. */
static int
jlfs_dirrename(unsigned char din[INOSIZE], const char *old, const char *new)
{
    static unsigned char dirbuf[4096], tmp[4096];
    unsigned size, pos;

    if (jlfs_dirload(din, dirbuf, &size) < 0)
        return (-EIO);
    for (pos = 0; pos + 8 <= size; ) {
        unsigned reclen = le16(dirbuf + pos + 4);
        unsigned namlen = dirbuf[pos + 7];
        if (reclen < 8 || pos + reclen > size)
            return (-EIO);
        if (namlen && strcmp((char *)dirbuf + pos + 8, old) == 0) {
            unsigned nn = strlen(new);
            if (nn == 0 || nn > MAXNAMLEN)
                return (-ENAMETOOLONG);
            if (nn + 8 <= reclen) {
                bzero(dirbuf + pos + 8, reclen - 8);
                bcopy(new, dirbuf + pos + 8, nn);
                dirbuf[pos + 7] = nn;
            } else {
                /* rebuild into tmp with a larger record */
                unsigned o = 0, npos = 0;
                while (o < size) {
                    unsigned rl = le16(dirbuf + o + 4);
                    unsigned nl = dirbuf[o + 7];
                    const char *nm = (const char *)dirbuf + o + 8;
                    if (nl && strcmp(nm, old) == 0) nm = new, nl = nn;
                    {
                        unsigned nr = DIRSIZ(nl);
                        le32put(tmp + npos, le32(dirbuf + o));
                        le16put(tmp + npos + 4, nr);
                        tmp[npos + 6] = dirbuf[o + 6];
                        tmp[npos + 7] = nl;
                        bcopy(nm, tmp + npos + 8, nl);
                        bzero(tmp + npos + 8 + nl, nr - 8 - nl);
                        npos += nr;
                    }
                    o += rl;
                }
                bcopy(tmp, dirbuf, npos);
                size = npos;
            }
            if (jlfs_dirstore(din, dirbuf, size) < 0)
                return (-EIO);
            return (0);
        }
        pos += reclen;
    }
    return (-ENOENT);
}

/* test whether a directory is empty (only "." and ".." entries) */
static int
jlfs_dirempty(const unsigned char din[INOSIZE])
{
    static unsigned char dirbuf[4096];
    unsigned size, pos;
    if (jlfs_dirload(din, dirbuf, &size) < 0)
        return (0);
    for (pos = 0; pos + 8 <= size; ) {
        unsigned reclen = le16(dirbuf + pos + 4);
        unsigned namlen = dirbuf[pos + 7];
        if (reclen < 8 || pos + reclen > size)
            return (0);
        if (namlen == 1 && dirbuf[pos + 8] == '.') { pos += reclen; continue; }
        if (namlen == 2 && dirbuf[pos + 8] == '.' && dirbuf[pos + 9] == '.') {
            pos += reclen; continue;
        }
        if (namlen) return (0);
        pos += reclen;
    }
    return (1);
}

/* ------------------------------------------------------------------ */
/* read-side vnodeops (from ffs.c)                                     */
/* ------------------------------------------------------------------ */
static void
jlfs_statino(unsigned ino, int *type, unsigned *size)
{
    unsigned char din[INOSIZE];
    if (jlfs_inode(ino, din) < 0) {
        *type = VREG; *size = 0; return;
    }
    *type = (le16(din) & IFMT) == IFDIR ? VDIR : VREG;
    *size = le32(din + 8);
}

static int
jlfs_lookup_op(struct vnode *dv, const char *name, struct vnode **vpp)
{
    unsigned char din[INOSIZE];
    unsigned ino, size;
    int type;

    if (dv->type != VDIR) return (-ENOTDIR);
    if (jlfs_inode(dv->lba, din) < 0) return (-EIO);
    if (jlfs_dirlookup(din, name, &ino) < 0) return (-ENOENT);
    jlfs_statino(ino, &type, &size);
    {
        struct vnode *vp = vn_alloc();
        if (vp == 0) return (-ENOMEM);
        vp->type = type;
        vp->ops = dv->ops;
        vp->lba = ino;
        vp->size = size;
        vp->v_key = 0;              /* mutable: key on v_id so writes purge */
        *vpp = vp;
    }
    return (0);
}

static int
jlfs_getattr_op(struct vnode *vp, unsigned *mode, unsigned *size)
{
    unsigned char din[INOSIZE];
    if (jlfs_inode(vp->lba, din) < 0) return (-EIO);
    *mode = le16(din);
    *size = le32(din + 8);
    return (0);
}

static int
jlfs_readraw_op(struct vnode *vp, unsigned off, void *buf, unsigned len)
{
    unsigned char din[INOSIZE];
    if (vp->type == VDIR) return (-EISDIR);
    if (jlfs_inode(vp->lba, din) < 0) return (-EIO);
    return (jlfs_readfile(din, off, buf, len) < 0 ? -EIO : 0);
}

static int
jlfs_read_op(struct vnode *vp, unsigned off, void *buf, unsigned len)
{
    if (vp->type == VDIR) return (-EISDIR);
    return (pagecache_read(vp, off, buf, len));
}

static int
jlfs_readdir_op(struct vnode *vp, unsigned *cookie, char *name, int *isdir)
{
    unsigned char din[INOSIZE];
    static unsigned char dirbuf[4096];
    unsigned size, pos = *cookie;

    if (vp->type != VDIR) return (-ENOTDIR);
    if (jlfs_inode(vp->lba, din) < 0) return (-EIO);
    size = le32(din + 8);
    if (size == 0 || size > 4096 ||
        jlfs_readfile(din, 0, dirbuf, size) < 0)
        return (-EIO);
    while (pos + 8 <= size) {
        unsigned ino = le32(dirbuf + pos);
        unsigned reclen = le16(dirbuf + pos + 4);
        unsigned namlen = dirbuf[pos + 7];
        if (reclen < 8 || pos + reclen > size) return (-EIO);
        pos += reclen;
        *cookie = pos;
        if (ino == 0 || namlen == 0 || namlen > MAXNAMLEN) continue;
        bcopy(dirbuf + pos - reclen + 8, name, namlen);
        name[namlen] = 0;
        {
            int t; unsigned s;
            jlfs_statino(ino, &t, &s);
            *isdir = (t == VDIR);
        }
        return (0);
    }
    return (1);
}

/* ------------------------------------------------------------------ */
/* write-side vnodeops                                                 */
/* ------------------------------------------------------------------ */

static int
jlfs_write_op(struct vnode *vp, unsigned off, const void *buf, unsigned len)
{
    unsigned char din[INOSIZE];
    int n;

    if (vp->type == VDIR) return (-EISDIR);
    if (jlfs_inode(vp->lba, din) < 0) return (-EIO);
    n = jlfs_writefile(din, off, buf, len);
    if (n < 0) return (-EIO);
    if (jlfs_putinode(vp->lba, din) < 0) return (-EIO);
    vp->size = le32(din + 8);
    pagecache_purge_vp(vp);
    return (n);
}

static int
jlfs_create_op(struct vnode *dv, const char *name, struct vnode **vpp)
{
    unsigned char din[INOSIZE], ddin[INOSIZE];
    unsigned ino;
    struct vnode *vp;
    int rv;

    if (dv->type != VDIR) return (-ENOTDIR);
    if (jlfs_inode(dv->lba, ddin) < 0) return (-EIO);
    if (jlfs_dirlookup(ddin, name, &ino) == 0) return (-EEXIST);
    ino = cg_alloc_inode();
    if (ino == 0) return (-ENOSPC);
    bzero(din, INOSIZE);
    le16put(din, IFREG | 0644);
    le16put(din + 2, 1);            /* di_nlink */
    le32put(din + 8, 0);            /* di_size */
    le32put(din + 108, 1);          /* di_gen */
    if (jlfs_putinode(ino, din) < 0) { cg_free_inode(ino); return (-EIO); }
    rv = jlfs_diradd(ddin, ino, 8 /* DT_REG */, name);
    if (rv < 0) {
        cg_free_inode(ino);
        bzero(din, INOSIZE);
        jlfs_putinode(ino, din);
        return (rv);
    }
    if (jlfs_putinode(dv->lba, ddin) < 0) return (-EIO);
    vp = vn_alloc();
    if (vp == 0) return (-ENOMEM);
    vp->type = VREG; vp->ops = &jlfs_vnodeops; vp->lba = ino;
    vp->size = 0; vp->v_key = 0;
    *vpp = vp;
    return (0);
}

static int
jlfs_remove_op(struct vnode *dv, const char *name)
{
    unsigned char din[INOSIZE], ddin[INOSIZE];
    unsigned ino;

    if (dv->type != VDIR) return (-ENOTDIR);
    if (jlfs_inode(dv->lba, ddin) < 0) return (-EIO);
    if (jlfs_dirlookup(ddin, name, &ino) < 0) return (-ENOENT);
    if (jlfs_inode(ino, din) < 0) return (-EIO);
    if ((le16(din) & IFMT) == IFDIR) return (-EISDIR);
    jlfs_freefile(din);
    bzero(din, INOSIZE);
    jlfs_putinode(ino, din);
    cg_free_inode(ino);
    if (jlfs_dirremove(ddin, name) < 0) return (-EIO);
    if (jlfs_putinode(dv->lba, ddin) < 0) return (-EIO);
    return (0);
}

static int
jlfs_mkdir_op(struct vnode *dv, const char *name)
{
    unsigned char din[INOSIZE], ddin[INOSIZE], dirbuf[SECSZ];
    unsigned ino, frag, namlen;

    if (dv->type != VDIR) return (-ENOTDIR);
    if (jlfs_inode(dv->lba, ddin) < 0) return (-EIO);
    if (jlfs_dirlookup(ddin, name, &ino) == 0) return (-EEXIST);
    ino = cg_alloc_inode();
    if (ino == 0) return (-ENOSPC);
    frag = cg_alloc_block();
    if (frag == 0) { cg_free_inode(ino); return (-ENOSPC); }

    bzero(dirbuf, SECSZ);
    /* "." */
    le32put(dirbuf, ino); le16put(dirbuf + 4, DIRSIZ(1));
    dirbuf[6] = 4; dirbuf[7] = 1; dirbuf[8] = '.';
    /* ".." */
    le32put(dirbuf + DIRSIZ(1), dv->lba);
    le16put(dirbuf + DIRSIZ(1) + 4, DIRSIZ(2));
    dirbuf[DIRSIZ(1) + 6] = 4; dirbuf[DIRSIZ(1) + 7] = 2;
    dirbuf[DIRSIZ(1) + 8] = '.'; dirbuf[DIRSIZ(1) + 9] = '.';
    if (jlfs_writesector(frag_to_sec(frag), dirbuf) < 0) {
        cg_free_block(frag); cg_free_inode(ino); return (-EIO);
    }
    bzero(din, INOSIZE);
    le16put(din, IFDIR | 0755);
    le16put(din + 2, 2);            /* di_nlink: "." + parent ref */
    le32put(din + 8, DIRSIZ(1) + DIRSIZ(2));
    le32put(din + 40, frag);        /* di_db[0] */
    le32put(din + 104, f_frag);
    le32put(din + 108, 1);
    if (jlfs_putinode(ino, din) < 0) {
        cg_free_block(frag); cg_free_inode(ino); return (-EIO);
    }
    namlen = strlen(name);
    if (jlfs_diradd(ddin, ino, 4 /* DT_DIR */, name) < 0 || namlen == 0) {
        cg_free_block(frag); cg_free_inode(ino);
        bzero(din, INOSIZE); jlfs_putinode(ino, din);
        return (-EIO);
    }
    if (jlfs_putinode(dv->lba, ddin) < 0) return (-EIO);
    return (0);
}

static int
jlfs_rmdir_op(struct vnode *dv, const char *name)
{
    unsigned char din[INOSIZE], ddin[INOSIZE];
    unsigned ino, frag;

    if (dv->type != VDIR) return (-ENOTDIR);
    if (jlfs_inode(dv->lba, ddin) < 0) return (-EIO);
    if (jlfs_dirlookup(ddin, name, &ino) < 0) return (-ENOENT);
    if (jlfs_inode(ino, din) < 0) return (-EIO);
    if ((le16(din) & IFMT) != IFDIR) return (-ENOTDIR);
    if (!jlfs_dirempty(din)) return (-ENOTEMPTY);
    frag = le32(din + 40);
    cg_free_block(frag);
    bzero(din, INOSIZE);
    jlfs_putinode(ino, din);
    cg_free_inode(ino);
    if (jlfs_dirremove(ddin, name) < 0) return (-EIO);
    if (jlfs_putinode(dv->lba, ddin) < 0) return (-EIO);
    return (0);
}

static int
jlfs_truncate_op(struct vnode *vp)
{
    unsigned char din[INOSIZE];
    if (vp->type == VDIR) return (-EISDIR);
    if (jlfs_inode(vp->lba, din) < 0) return (-EIO);
    jlfs_freefile(din);
    if (jlfs_putinode(vp->lba, din) < 0) return (-EIO);
    vp->size = 0;
    pagecache_purge_vp(vp);
    return (0);
}

static int
jlfs_rename_op(struct vnode *dv, const char *old, const char *new)
{
    unsigned char ddin[INOSIZE];
    unsigned ino;

    if (dv->type != VDIR) return (-ENOTDIR);
    if (jlfs_inode(dv->lba, ddin) < 0) return (-EIO);
    if (jlfs_dirlookup(ddin, old, &ino) < 0) return (-ENOENT);
    if (jlfs_dirlookup(ddin, new, &ino) == 0) return (-EEXIST);
    if (jlfs_dirrename(ddin, old, new) < 0) return (-EIO);
    if (jlfs_putinode(dv->lba, ddin) < 0) return (-EIO);
    return (0);
}

struct vnodeops jlfs_vnodeops = {
    jlfs_lookup_op, jlfs_getattr_op, jlfs_read_op, jlfs_write_op,
    jlfs_readdir_op, jlfs_create_op, jlfs_remove_op,
    jlfs_mkdir_op, jlfs_rmdir_op, jlfs_truncate_op,
    jlfs_rename_op, jlfs_readraw_op
};
