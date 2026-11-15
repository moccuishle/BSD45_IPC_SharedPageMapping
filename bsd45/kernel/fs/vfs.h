/*
 * vfs.h - a small vnode/VFS layer.
 *
 * Everything that used to be special-cased in the syscall switch (ramfs,
 * ISO9660, tmpfs) now goes through a vnode with an ops vector, so the
 * syscall code no longer knows which file system it is talking to.
 */
#ifndef _VFS_H_
#define _VFS_H_

#define	VREG	1
#define	VDIR	2

struct vnode;

struct vnodeops {
	int	(*lookup)(struct vnode *dv, const char *name,
		    struct vnode **vpp);
	int	(*getattr)(struct vnode *vp, unsigned *mode, unsigned *size);
	int	(*read)(struct vnode *vp, unsigned off, void *buf,
		    unsigned len);
	int	(*write)(struct vnode *vp, unsigned off, const void *buf,
		    unsigned len);
	int	(*readdir)(struct vnode *vp, unsigned *cookie, char *name,
		    int *isdir);
	int	(*create)(struct vnode *dv, const char *name,
		    struct vnode **vpp);
	int	(*remove)(struct vnode *dv, const char *name);
	int	(*mkdir)(struct vnode *dv, const char *name);
	int	(*rmdir)(struct vnode *dv, const char *name);
	int	(*truncate)(struct vnode *vp);
	int	(*rename)(struct vnode *dv, const char *old,
		    const char *new);
	/*
	 * S17: uncached read, used by the page cache to fill a page.  The
	 * backend must go straight to its storage; read() itself is layered
	 * on top of the page cache and must not call this directly.
	 */
	int	(*readraw)(struct vnode *vp, unsigned off, void *buf,
		    unsigned len);
};

struct vnode {
	int			ref;
	int			type;		/* VREG or VDIR */
	struct vnodeops		*ops;
	unsigned		lba;		/* ISO: start sector */
	unsigned		size;		/* ISO: byte size */
	int			node;		/* tmpfs: table slot */
	/*
	 * S17: unique id for the name cache and the page cache.  A vnode slot
	 * is recycled from a LIFO pool, so pointer identity is not stable;
	 * caches key on v_id and vn_put() clears it on free.
	 */
	unsigned		v_id;
	/*
	 * S17: stable page-cache key for read-only backends.  ISO9660 files
	 * are immutable and are identified by LBA, so their pages survive a
	 * close/reopen; 0 means "key on v_id" (tmpfs, which is mutable).
	 */
	unsigned		v_key;
};

extern struct vnode *rootvnode;

/*
 * VFS core services available to backends (S100.2a): a backend may allocate
 * a vnode for a lookup result, but must not reach into the pool itself.
 */
struct vnode *vn_alloc(void);

/*
 * Backends (S100.2a: tmpfs lives in tmpfs.c behind these, so vfs.c stays a
 * forwarding layer and the microkernel variant can replace the backend with
 * an IPC client without touching namei()/the mount table).
 */
void	tmpfs_init(void);
struct vnode *tmpfs_root(void);
int	tmpfs_is_root(struct vnode *vp);

void	vfs_init(void);
int	vfs_namei(const char *path, struct vnode **vpp);
int	vfs_lookup_parent(const char *path, struct vnode **dvp, char *name);
int	vfs_create(const char *path, int isdir, struct vnode **vpp);
int	vfs_remove(const char *path, int isdir);
int	vfs_mount_list(char *buf, unsigned len);
int	vfs_absolutize(const char *cwd, const char *in, char *out,
	    unsigned outlen);
int	vfs_rename(const char *oldpath, const char *newpath);
void	vn_put(struct vnode *vp);
unsigned vn_newid(void);
void	vfs_stats(unsigned *vtotal, unsigned *vused, unsigned *vfree,
	    unsigned *pnalloc, unsigned *pnfree, unsigned *pninuse);

/* ---------------------------------------------------------------- S17 caches */

/*
 * Buffer cache (vfs_bio.c): 2048-byte blocks, hash + LRU, hit/miss
 * accounting.  The only block device today is the ATAPI CD, write returns
 * EROFS; bdwrite/bwrite stay here so an ATA disk can plug in unchanged.
 */
struct buf;

#define	BDEV_CD		0

void	bio_init(void);
struct buf *bread(unsigned dev, unsigned blkno);
void	breada(unsigned dev, unsigned blkno, unsigned n);
void	brelse(struct buf *bp);
int	bwrite(struct buf *bp);
void	bdwrite(struct buf *bp);
unsigned char *buf_data(struct buf *bp);
void	bio_stats(unsigned *total, unsigned *used, unsigned *hits,
	    unsigned *misses, unsigned *reads, unsigned *readahead,
	    unsigned *dirty);

/* Name cache (vfs_cache.c). */
void	namecache_init(void);
struct vnode *namecache_lookup(struct vnode *dv, const char *name);
void	namecache_enter(struct vnode *dv, const char *name,
	    struct vnode *vp);
void	namecache_purge(struct vnode *dv, const char *name);
void	namecache_purge_vp(struct vnode *vp);
void	namecache_stats(unsigned *total, unsigned *used, unsigned *hits,
	    unsigned *misses);

/* File page cache (pagecache.c), the vnode_pager of this port. */
void	pagecache_init(void);
unsigned pagecache_key(struct vnode *vp);
int	pagecache_read(struct vnode *vp, unsigned off, void *buf,
	    unsigned len);
unsigned pagecache_get(struct vnode *vp, unsigned pindex,
	    unsigned char **vap);
unsigned pagecache_unref(struct vnode *vp, unsigned pindex);
unsigned pagecache_unref_key(unsigned key, unsigned pindex);
void	pagecache_purge_vp(struct vnode *vp);
void	pagecache_stats(unsigned *total, unsigned *used, unsigned *hits,
	    unsigned *misses, unsigned *fills, unsigned *evicts);

/* Render the whole cache state for /dev/vmstat. */
int	vfs_cache_report(char *buf, unsigned len);

/* Kernel primitives the backends use (KP-3 in docs/kernel-boundary.md). */
unsigned vfs_page_alloc(void);
void	vfs_page_free(unsigned pa);
void	vfs_page_unmap(unsigned va);
void	vfs_page_stats(unsigned *nalloc, unsigned *nfreed, unsigned *ninuse);

#endif
