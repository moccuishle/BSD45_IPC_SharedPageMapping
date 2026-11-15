/*
 * vfs.c - the vnode layer: vnode pool, namei(), mount table.
 *
 * Backends live behind vnodeops:
 *	ISO9660  (this file, read-only root, (lba, size) in the vnode)
 *	tmpfs    (tmpfs.c, writable /tmp, one page per file)
 * Keep this file free of backend internals: it may only use the primitives
 * listed in docs/kernel-boundary.md (S100.1).
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>

#include "atapi.h"
#include "isofs.h"
#include "vfs.h"

extern void bcopy(const void *, void *, unsigned);
extern void bzero(void *, unsigned);
extern int strcmp(const char *, const char *);
extern int strncmp(const char *, const char *, unsigned);
extern char *strchr(const char *, int);
extern unsigned strlen(const char *);

#define	ISO_SECTOR	2048

struct vnodeops iso_vnodeops;

static struct vnode iso_rootv;
struct vnode *rootvnode = &iso_rootv;

/* ---------------------------------------------------------------- helpers */

#define	VNODES		96
static struct vnode vnode_pool[VNODES];
static int vnode_used[VNODES];
static int vnode_free[VNODES];		/* LIFO of reusable slots */
static int vnode_nfree;
static unsigned vn_alloc_total, vn_reuse_total;
static unsigned vn_next_id = 1;

/*
 * S17: a tiny vnode cache for directories.  Files come and go with every
 * open(), but keeping the handful of directory vnodes alive is what makes
 * the name cache useful: root/bin/sbin lookups repeat on every exec and a
 * retained directory keeps its name-cache entries valid.
 */
#define	NDCACHE		8
static struct vnode *dir_cache[NDCACHE];
static int vnode_dcached[VNODES];
static int dir_cache_next;

static void vn_free(struct vnode *vp);

/*
 * S17: caches key on v_id, never on the vnode pointer, because slots are
 * recycled.  vn_newid() is also used by the two statically allocated mount
 * roots (ISO and tmpfs), which do not come from vn_alloc().
 */
unsigned
vn_newid(void)
{

	return (vn_next_id++);
}

static void
vn_init(void)
{
	int i;

	vnode_nfree = 0;
	for (i = VNODES - 1; i >= 0; i--) {
		vnode_used[i] = 0;
		vnode_dcached[i] = 0;
		vnode_free[vnode_nfree++] = i;
	}
	for (i = 0; i < NDCACHE; i++)
		dir_cache[i] = 0;
	dir_cache_next = 0;
}

/*
 * Release a vnode slot: drop everything that keys on it, then make the slot
 * reusable.  vn_put() calls this only when the vnode is not an actively
 * used directory.
 */
static void
vn_free(struct vnode *vp)
{
	int i;

	if (vp == 0 || vp == &iso_rootv || tmpfs_is_root(vp))
		return;
	i = vp - vnode_pool;
	if (i < 0 || i >= VNODES)
		return;
	namecache_purge_vp(vp);
	pagecache_purge_vp(vp);
	vp->ref = 0;
	vp->v_id = 0;
	vp->v_key = 0;
	vnode_used[i] = 0;
	vnode_dcached[i] = 0;
}

/*
 * Retain a directory vnode with ref == 0.  The slot stays busy, so the name
 * cache entries under it remain valid; the small ring evicts the oldest
 * directory once all slots are taken.
 */
static void
dir_cache_keep(struct vnode *vp)
{
	int i = vp - vnode_pool;
	int k, j;

	if (i < 0 || i >= VNODES || vnode_dcached[i])
		return;
	for (k = 0; k < NDCACHE; k++) {
		j = (dir_cache_next + k) % NDCACHE;
		if (dir_cache[j] == 0) {
			dir_cache[j] = vp;
			vnode_dcached[i] = 1;
			dir_cache_next = (j + 1) % NDCACHE;
			return;
		}
	}
	/* all slots busy: evict the oldest unreferenced directory */
	for (k = 0; k < NDCACHE; k++) {
		j = (dir_cache_next + k) % NDCACHE;
		if (dir_cache[j]->ref == 0) {
			struct vnode *old = dir_cache[j];

			vnode_dcached[old - vnode_pool] = 0;
			dir_cache[j] = vp;
			vnode_dcached[i] = 1;
			dir_cache_next = (j + 1) % NDCACHE;
			vn_free(old);
			return;
		}
	}
	/* every retained directory is in use: do not keep this one */
	vn_free(vp);
}

struct vnode *
vn_alloc(void)
{
	int i;

	for (i = 0; i < VNODES; i++) {
		if (vnode_used[i])
			continue;
		vnode_used[i] = 1;
		vn_alloc_total++;
		bzero(&vnode_pool[i], sizeof(vnode_pool[i]));
		vnode_pool[i].ref = 1;
		vnode_pool[i].node = -1;
		vnode_pool[i].v_id = vn_newid();
		return (&vnode_pool[i]);
	}
	return (0);
}

void
vn_put(struct vnode *vp)
{

	if (vp == 0 || vp == &iso_rootv || tmpfs_is_root(vp))
		return;
	if (--vp->ref <= 0) {
		vp->ref = 0;
		/*
		 * S17: keep read-only (ISO9660) directory vnodes around so the
		 * name cache keeps hitting on the root -> bin -> file walks of
		 * execve().  tmpfs directories are created and removed at run
		 * time and must be released, or every mkdir would pin a slot.
		 * Regular files really are released too (the page cache keeps
		 * their data keyed by LBA for ISO9660).
		 */
		if (vp->type == VDIR && vp->v_key != 0)
			dir_cache_keep(vp);
		else
			vn_free(vp);
	}
}

void
vfs_stats(unsigned *vtotal, unsigned *vused, unsigned *vfree,
    unsigned *pnalloc, unsigned *pnfree, unsigned *pninuse)
{

	int i, used = 0;

	for (i = 0; i < VNODES; i++)
		used += vnode_used[i];
	*vtotal = VNODES;
	*vused = used;
	*vfree = VNODES - used;
	vfs_page_stats(pnalloc, pnfree, pninuse);
}
/* ---------------------------------------------------------------- ISO9660 */

static int
iso_scan(struct vnode *dv, const char *name, struct vnode **vpp)
{
	char nm[48];
	unsigned lba, size, pos = 0;
	int isdir, rv;

	while ((rv = iso_readdir(dv->lba, dv->size, &pos, nm, &lba, &size,
	    &isdir)) == 0) {
		if (strcmp(nm, name) == 0) {
			struct vnode *vp = vn_alloc();

			if (vp == 0)
				return (-ENOMEM);
			vp->type = isdir ? VDIR : VREG;
			vp->ops = &iso_vnodeops;
			vp->lba = lba;
			vp->size = size;
			/* S17: stable page-cache key; ISO files are immutable */
			vp->v_key = 0x80000000u | (lba & 0x7fffffff);
			*vpp = vp;
			return (0);
		}
	}
	return (rv < 0 ? -EIO : -ENOENT);
}

static int
iso_lookup_op(struct vnode *dv, const char *name, struct vnode **vpp)
{
	char want[48];
	int rv;

	if (dv->type != VDIR)
		return (-ENOTDIR);
	rv = iso_scan(dv, name, vpp);
	if (rv == -ENOENT && strchr(name, '.') == 0) {
		unsigned n = strlen(name);

		if (n + 5 < sizeof(want)) {
			bcopy(name, want, n);
			bcopy(".elf", want + n, 5);
			rv = iso_scan(dv, want, vpp);
		}
	}
	return (rv);
}

static int
iso_getattr_op(struct vnode *vp, unsigned *mode, unsigned *size)
{

	*mode = vp->type == VDIR ? 0040755 : 0100755;
	*size = vp->size;
	return (0);
}

/*
 * S17: read() is layered on the page cache.  iso_readraw_op() is the
 * uncached path the page cache uses to fill a 4K page: it still goes
 * through the buffer cache, so a 4K page costs two cached 2K sectors and
 * sequential access gets readahead.
 */
static int
iso_readraw_op(struct vnode *vp, unsigned off, void *buf, unsigned len)
{
	unsigned done = 0;
	unsigned nsec = (vp->size + ISO_SECTOR - 1) / ISO_SECTOR;
	char *p = (char *)buf;

	if (vp->type == VDIR)
		return (-EISDIR);
	if (off >= vp->size)
		return (0);
	if (len > vp->size - off)
		len = vp->size - off;
	while (done < len) {
		unsigned pos = off + done;
		unsigned secoff = pos % ISO_SECTOR;
		unsigned n = ISO_SECTOR - secoff;
		unsigned blk = pos / ISO_SECTOR;
		struct buf *bp;

		if (n > len - done)
			n = len - done;
		bp = bread(BDEV_CD, vp->lba + blk);
		if (bp == 0)
			return (-EIO);
		bcopy(buf_data(bp) + secoff, p + done, n);
		brelse(bp);
		done += n;
		/* sequential readahead, clamped to this file's sectors */
		if (blk + 1 < nsec) {
			unsigned ra = nsec - blk - 1;

			if (ra > 4)
				ra = 4;
			breada(BDEV_CD, vp->lba + blk + 1, ra);
		}
	}
	return ((int)done);
}

static int
iso_read_op(struct vnode *vp, unsigned off, void *buf, unsigned len)
{

	if (vp->type == VDIR)
		return (-EISDIR);
	return (pagecache_read(vp, off, buf, len));
}

static int
iso_readdir_op(struct vnode *vp, unsigned *cookie, char *name, int *isdir)
{
	unsigned lba, size;
	int rv;

	if (vp->type != VDIR)
		return (-ENOTDIR);
	rv = iso_readdir(vp->lba, vp->size, cookie, name, &lba, &size,
	    isdir);
	return (rv < 0 ? -EIO : rv);
}

/* ---------------------------------------------------------------- ops */

struct vnodeops iso_vnodeops = {
	iso_lookup_op, iso_getattr_op, iso_read_op, 0, iso_readdir_op,
	0, 0, 0, 0, 0, 0, iso_readraw_op
};


/*
 * S10.4: compose an absolute path from cwd + in.  This is the ONLY place
 * that knows about the cwd; vfs_namei()/vfs_lookup_parent() keep taking
 * absolute paths exactly as before, so a backend's "dv == my root" identity
 * checks are unaffected.
 */
int
vfs_absolutize(const char *cwd, const char *in, char *out, unsigned outlen)
{
	unsigned o = 0, i = 0, n, k;

	if (in == 0 || in[0] == 0)
		return (-ENOENT);
	if (in[0] != '/') {
		if (cwd == 0 || cwd[0] != '/')
			return (-ENOENT);
		while (cwd[i] != 0 && o + 1 < outlen)
			out[o++] = cwd[i++];
		i = 0;
	} else {
		if (outlen < 2)
			return (-ENAMETOOLONG);
		out[o++] = '/';
		i = 1;
	}
	while (in[i] != 0) {
		while (in[i] == '/')
			i++;
		if (in[i] == 0)
			break;
		n = 0;
		while (in[i + n] != 0 && in[i + n] != '/')
			n++;
		if (n == 1 && in[i] == '.') {
			i += n;
			continue;
		}
		if (n == 2 && in[i] == '.' && in[i + 1] == '.') {
			while (o > 1 && out[o - 1] != '/')
				o--;
			if (o > 1)
				o--;
			i += n;
			continue;
		}
		if (o + n + 2 > outlen)
			return (-ENAMETOOLONG);
		if (o == 0 || out[o - 1] != '/')
			out[o++] = '/';
		for (k = 0; k < n; k++)
			out[o++] = in[i + k];
		i += n;
	}
	if (o == 0)
		out[o++] = '/';
	/* "/bin/" -> "/bin": vfs_namei() treats an empty component as ENOENT */
	if (o > 1 && out[o - 1] == '/')
		o--;
	out[o] = 0;
	return (0);
}

/* ---------------------------------------------------------------- namei */

static void mount_add(const char *path, struct vnode *root);

void
vfs_init(void)
{
	static int vfs_inited;

	if (vfs_inited)
		return;
	vfs_inited = 1;
	vn_init();
	/*
	 * S17: bring up the three caches before the first namei().  The pages
	 * for the buffer cache and the page cache are allocated once, now, so
	 * they show up in the page accounting as a fixed baseline instead of
	 * growing during the S10 stress loops.
	 */
	namecache_init();
	bio_init();
	pagecache_init();
	bzero(&iso_rootv, sizeof(iso_rootv));
	iso_rootv.ref = 1;
	iso_rootv.type = VDIR;
	iso_rootv.ops = &iso_vnodeops;
	iso_rootv.lba = iso_root();
	iso_rootv.size = iso_root_size();
	iso_rootv.v_id = vn_newid();
	iso_rootv.v_key = 0x80000000u | (iso_rootv.lba & 0x7fffffff);
	tmpfs_init();
	tmpfs_root()->v_id = vn_newid();
	rootvnode = &iso_rootv;
	mount_add("/", &iso_rootv);
	mount_add("/tmp", tmpfs_root());
}

/*
 * Resolve an absolute path.  "/tmp" (and anything under it) is a mount of
 * the tmpfs root; everything else lives on the ISO9660 image.
 */
int
vfs_namei(const char *path, struct vnode **vpp)
{
	struct vnode *vp = rootvnode;
	const char *p = path;
	int rv;

	while (*p == '/')
		p++;
	if (*p == 0) {
		*vpp = vp;
		return (0);
	}
	for (;;) {
		char name[48];
		const char *slash = p;
		int n;
		struct vnode *next;

		while (*slash && *slash != '/')
			slash++;
		n = slash - p;
		if (n == 0 || n >= (int)sizeof(name))
			return (-ENOENT);
		bcopy(p, name, n);
		name[n] = 0;

		if (vp == rootvnode && strcmp(name, "tmp") == 0)
			next = tmpfs_root();
		else {
			/*
			 * S17: ask the name cache first; on a miss do the
			 * backend lookup and remember it.  A cache hit comes
			 * back already referenced, exactly like a lookup.
			 */
			next = namecache_lookup(vp, name);
			if (next == 0) {
				rv = vp->ops->lookup(vp, name, &next);
				if (rv < 0)
					return (rv);
				namecache_enter(vp, name, next);
			}
		}
		if (vp != rootvnode && !tmpfs_is_root(vp))
			vn_put(vp);
		vp = next;
		if (*slash == 0) {
			*vpp = vp;
			return (0);
		}
		p = slash;
		while (*p == '/')
			p++;
	}
}

/*
 * Split path into the parent directory vnode and the last component, used
 * by creation/removal.
 */
int
vfs_lookup_parent(const char *path, struct vnode **dvp, char *name)
{
	char dir[128];
	const char *slash = 0, *p = path;
	int n;

	while (*p) {
		if (*p == '/')
			slash = p;
		p++;
	}
	if (slash == 0)
		return (-ENOENT);
	n = slash - path;
	if (n >= (int)sizeof(dir) - 1)
		return (-ENOENT);
	if (n == 0) {
		/* "/name": the parent is the root directory */
		dir[0] = '/';
		dir[1] = 0;
	} else {
		bcopy(path, dir, n);
		dir[n] = 0;
	}
	n = strlen(slash + 1);
	if (n == 0 || n >= 48)
		return (-ENOENT);
	bcopy(slash + 1, name, n + 1);
	return (vfs_namei(dir, dvp));
}

int
vfs_create(const char *path, int isdir, struct vnode **vpp)
{
	struct vnode *dv;
	char name[48];
	int rv;

	rv = vfs_lookup_parent(path, &dv, name);
	if (rv < 0)
		return (rv);
	if (!dv->ops->create && !dv->ops->mkdir) {
		vn_put(dv);
		return (-EROFS);
	}
	if (isdir) {
		rv = dv->ops->mkdir ? dv->ops->mkdir(dv, name) : -EROFS;
		if (rv == 0)
			rv = dv->ops->lookup(dv, name, vpp);
	} else {
		rv = dv->ops->create ? dv->ops->create(dv, name, vpp) :
		    -EROFS;
	}
	if (rv == 0)
		namecache_purge(dv, name);
	vn_put(dv);
	return (rv);
}

/*
 * S17: unlink/rmdir with name-cache invalidation, so a removed file is not
 * found again through a stale positive entry.
 */
int
vfs_remove(const char *path, int isdir)
{
	struct vnode *dv;
	char name[48];
	int rv;

	rv = vfs_lookup_parent(path, &dv, name);
	if (rv < 0)
		return (rv);
	if (isdir)
		rv = dv->ops->rmdir ? dv->ops->rmdir(dv, name) : -EROFS;
	else
		rv = dv->ops->remove ? dv->ops->remove(dv, name) : -EROFS;
	if (rv == 0)
		namecache_purge(dv, name);
	vn_put(dv);
	return (rv);
}

/*
 * Rename within one directory.  The ISO9660 root is read-only, so only
 * the tmpfs mount can do this; mv(1) falls back to cp+unlink otherwise.
 */
int
vfs_rename(const char *oldpath, const char *newpath)
{
	struct vnode *odv, *ndv;
	char oname[48], nname[48];
	int rv;

	rv = vfs_lookup_parent(oldpath, &odv, oname);
	if (rv < 0)
		return (rv);
	rv = vfs_lookup_parent(newpath, &ndv, nname);
	if (rv < 0) {
		vn_put(odv);
		return (rv);
	}
	if (odv != ndv || odv->ops->rename == 0)
		rv = -EXDEV;
	else
		rv = odv->ops->rename(odv, oname, nname);
	if (rv == 0) {
		/* old name is gone, new name may previously have been cached */
		namecache_purge(odv, oname);
		namecache_purge(ndv, nname);
	}
	vn_put(odv);
	vn_put(ndv);
	return (rv);
}
/* the mount table these two mounts are all we have for now */
struct vfs_mount {
	const char	*m_path;
	struct vnode	*m_root;
};
static struct vfs_mount mounts[2];
static int nmounts;

static void
mount_add(const char *path, struct vnode *root)
{

	if (nmounts < 2) {
		mounts[nmounts].m_path = path;
		mounts[nmounts].m_root = root;
		nmounts++;
	}
}

/* ---- S12: render the mount table for the /dev/mounts pseudo file ---- */

static unsigned
app_str(char *buf, unsigned o, unsigned len, const char *s)
{

	while (*s && o + 1 < len)
		buf[o++] = *s++;
	return (o);
}

static unsigned
app_num(char *buf, unsigned o, unsigned len, unsigned v)
{
	char num[12];
	int n = 0;

	if (v == 0)
		num[n++] = '0';
	while (v > 0) {
		num[n++] = '0' + (v % 10);
		v /= 10;
	}
	while (n > 0 && o + 1 < len)
		buf[o++] = num[--n];
	return (o);
}

int
vfs_mount_list(char *buf, unsigned len)
{
	unsigned o = 0;
	int i;

	for (i = 0; i < nmounts; i++) {
		int tmp = tmpfs_is_root(mounts[i].m_root);

		o = app_str(buf, o, len, mounts[i].m_path);
		o = app_str(buf, o, len, tmp ? " tmpfs lba=" : " iso9660 lba=");
		o = app_num(buf, o, len, tmp ? 0 : iso_root());
		o = app_str(buf, o, len, " size=");
		o = app_num(buf, o, len, tmp ? 0 : iso_root_size());
		o = app_str(buf, o, len, tmp ? " rw\n" : " ro\n");
	}
	if (o < len)
		buf[o] = 0;
	return ((int)o);
}

/* ---- S17: render the cache state for the /dev/vmstat pseudo file ---- */

int
vfs_cache_report(char *buf, unsigned len)
{
	unsigned o = 0;
	unsigned bt, bu, bh, bmiss, br, bra, bd;
	unsigned nt, nu, nh, nmiss;
	unsigned pt, pu, ph, pmiss, pfills, pevicts;
	unsigned vt, vu, vf, pa, pfree, pinuse;

	bio_stats(&bt, &bu, &bh, &bmiss, &br, &bra, &bd);
	namecache_stats(&nt, &nu, &nh, &nmiss);
	pagecache_stats(&pt, &pu, &ph, &pmiss, &pfills, &pevicts);
	vfs_stats(&vt, &vu, &vf, &pa, &pfree, &pinuse);

	o = app_str(buf, o, len, "vnode: total ");
	o = app_num(buf, o, len, vt);
	o = app_str(buf, o, len, " used ");
	o = app_num(buf, o, len, vu);
	o = app_str(buf, o, len, " free ");
	o = app_num(buf, o, len, vf);
	o = app_str(buf, o, len, "\n");

	o = app_str(buf, o, len, "buf: total ");
	o = app_num(buf, o, len, bt);
	o = app_str(buf, o, len, " used ");
	o = app_num(buf, o, len, bu);
	o = app_str(buf, o, len, " hits ");
	o = app_num(buf, o, len, bh);
	o = app_str(buf, o, len, " misses ");
	o = app_num(buf, o, len, bmiss);
	o = app_str(buf, o, len, " reads ");
	o = app_num(buf, o, len, br);
	o = app_str(buf, o, len, " readahead ");
	o = app_num(buf, o, len, bra);
	o = app_str(buf, o, len, " dirty ");
	o = app_num(buf, o, len, bd);
	o = app_str(buf, o, len, "\n");

	o = app_str(buf, o, len, "name: total ");
	o = app_num(buf, o, len, nt);
	o = app_str(buf, o, len, " used ");
	o = app_num(buf, o, len, nu);
	o = app_str(buf, o, len, " hits ");
	o = app_num(buf, o, len, nh);
	o = app_str(buf, o, len, " misses ");
	o = app_num(buf, o, len, nmiss);
	o = app_str(buf, o, len, "\n");

	o = app_str(buf, o, len, "page: total ");
	o = app_num(buf, o, len, pt);
	o = app_str(buf, o, len, " used ");
	o = app_num(buf, o, len, pu);
	o = app_str(buf, o, len, " hits ");
	o = app_num(buf, o, len, ph);
	o = app_str(buf, o, len, " misses ");
	o = app_num(buf, o, len, pmiss);
	o = app_str(buf, o, len, " fills ");
	o = app_num(buf, o, len, pfills);
	o = app_str(buf, o, len, " evicts ");
	o = app_num(buf, o, len, pevicts);
	o = app_str(buf, o, len, "\n");

	o = app_str(buf, o, len, "pages: alloc ");
	o = app_num(buf, o, len, pa);
	o = app_str(buf, o, len, " freed ");
	o = app_num(buf, o, len, pfree);
	o = app_str(buf, o, len, " inuse ");
	o = app_num(buf, o, len, pinuse);
	o = app_str(buf, o, len, "\n");

	if (o < len)
		buf[o] = 0;
	return ((int)o);
}
