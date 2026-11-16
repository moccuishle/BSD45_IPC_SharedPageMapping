/*
 * tmpfs.c - the writable /tmp backend, split out of vfs.c (S100.2a).
 *
 * This backend talks to the rest of the kernel only through the primitives
 * listed in docs/kernel-boundary.md:
 *	KP-2  pmap_map()            - map a data page into the kernel window
 *	KP-3  vfs_page_alloc/free/unmap - data page ownership
 * plus bcopy/bzero and the VFS vnode helpers vn_alloc()/vn_put().
 * It never touches the process table, the scheduler or the page allocator.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>

#ifndef PAGE_SIZE
#define	PAGE_SIZE	4096
#endif
#ifndef VM_PROT_READ
#define	VM_PROT_READ	0x1
#define	VM_PROT_WRITE	0x2
#endif

#include "vfs.h"

extern unsigned vfs_page_alloc(void);
extern void vfs_page_free(unsigned pa);
extern void vfs_page_unmap(unsigned va);
extern void pmap_map(vm_offset_t, vm_offset_t, vm_offset_t, int);
extern void bcopy(const void *, void *, unsigned);
extern void bzero(void *, unsigned);
extern int strcmp(const char *, const char *);
extern unsigned strlen(const char *);

#define	TMPFS_MAXNODES	32
#define	TMPFS_NAMELEN	32
#define	TMPFS_DATASZ	4096
#define	TMPFS_VA	0xFE600000

struct tmpfs_node {
	int		used;
	int		isdir;
	char		name[TMPFS_NAMELEN];
	unsigned char	*data;		/* kernel window VA, 0 = no page */
	unsigned	pa;		/* backing physical page */
	unsigned	size;
};

static struct tmpfs_node tmpfs_nodes[TMPFS_MAXNODES];
static struct vnode tmpfs_rootv;

/*
 * tmpfs data pages are allocated lazily on the first write and handed back
 * by truncate/remove/rmdir, so "echo x > /tmp/f" in a loop reuses one page
 * instead of eating the whole pool.
 */
/* EN: Returns the node data pointer, allocating a page via vfs_page_alloc() mapped at TMPFS_VA + index * PAGE_SIZE and zeroed when create is set; 0 when create is clear. */
/* 中文：返回节点的数据指针；create 置位且尚未分配时用 vfs_page_alloc() 取一页，映射到 TMPFS_VA + (n - tmpfs_nodes) * PAGE_SIZE 并清零 TMPFS_DATASZ 字节；create 为 0 时返回 0。 */
static unsigned char *
tmpfs_getdata(struct tmpfs_node *n, int create)
{
	int i = n - tmpfs_nodes;

	if (n->data != 0)
		return (n->data);
	if (!create)
		return (0);
	n->pa = vfs_page_alloc();
	n->data = (unsigned char *)(TMPFS_VA + i * PAGE_SIZE);
	pmap_map((vm_offset_t)n->data, (vm_offset_t)n->pa,
	    (vm_offset_t)n->pa + PAGE_SIZE, VM_PROT_READ | VM_PROT_WRITE);
	bzero(n->data, TMPFS_DATASZ);
	return (n->data);
}

/* EN: Frees the node's data page by unmapping the kernel window with vfs_page_unmap() before vfs_page_free(), then clears data, pa and size; no-op when nothing is allocated. */
/* 中文：先用 vfs_page_unmap() 解除内核窗口映射，再经 vfs_page_free() 归还物理页，随后清空 data、pa 与 size；未分配时直接返回。 */
static void
tmpfs_putdata(struct tmpfs_node *n)
{

	if (n->data == 0)
		return;
	/* drop the kernel window mapping first, then recycle the page */
	vfs_page_unmap((unsigned)n->data);
	vfs_page_free(n->pa);
	n->data = 0;
	n->pa = 0;
	n->size = 0;
}

struct vnodeops tmpfs_vnodeops;

/* ---------------------------------------------------------------- root */

/* EN: Zeroes the static tmpfs_rootv and sets ref=1, type=VDIR, ops=tmpfs_vnodeops and node=-1 (the negative index that marks the root); no page or vnode is allocated. */
/* 中文：清零静态的 tmpfs_rootv，并设置 ref=1、type=VDIR、ops=tmpfs_vnodeops、node=-1（负下标即根节点标记）；不分配任何页或 vnode。 */
void
tmpfs_init(void)
{

	bzero(&tmpfs_rootv, sizeof(tmpfs_rootv));
	tmpfs_rootv.ref = 1;
	tmpfs_rootv.type = VDIR;
	tmpfs_rootv.ops = &tmpfs_vnodeops;
	tmpfs_rootv.node = -1;
}

/* EN: Returns the address of the single static tmpfs root vnode, tmpfs_rootv; no lock is taken and the caller gets no extra reference. */
/* 中文：返回唯一的静态 tmpfs 根 vnode（tmpfs_rootv）的地址；不加锁，也不为调用者增加引用计数。 */
struct vnode *
tmpfs_root(void)
{

	return (&tmpfs_rootv);
}

/* EN: Returns nonzero only when vp is exactly the static tmpfs root vnode; this identity test lets vn_put()/vn_free() skip releasing the root. */
/* 中文：仅当 vp 正是静态 tmpfs 根 vnode 时返回非零；该同一性判断供 vn_put()/vn_free() 跳过对根节点的释放。 */
int
tmpfs_is_root(struct vnode *vp)
{

	return (vp == &tmpfs_rootv);
}

/* ---------------------------------------------------------------- nodes */

/* EN: Linear-searches all TMPFS_MAXNODES slots for a used node whose name strcmp-equals name, returning that node pointer or 0 when absent. */
/* 中文：在全部 TMPFS_MAXNODES 个槽中线性查找 used 且 name 经 strcmp 相等的节点，返回该节点指针，不存在时返回 0。 */
static struct tmpfs_node *
tmpfs_find(const char *name)
{
	int i;

	for (i = 0; i < TMPFS_MAXNODES; i++)
		if (tmpfs_nodes[i].used &&
		    strcmp(tmpfs_nodes[i].name, name) == 0)
			return (&tmpfs_nodes[i]);
	return (0);
}

/* EN: Rejects empty/too-long names; reuses an existing name only if isdir matches, else zeroes the first free slot, marks used/isdir, copies name (page on first write); 0 if full. */
/* 中文：名字为空或 >=TMPFS_NAMELEN 时失败；已有同名节点仅在 isdir 相符时复用，否则占首个空闲槽、清零并置 used/isdir、拷入名字（数据页留到首次写才分配），无空槽返回 0。 */
static struct tmpfs_node *
tmpfs_create_node(const char *name, int isdir)
{
	struct tmpfs_node *n;
	int i, len;

	len = strlen(name);
	if (len == 0 || len >= TMPFS_NAMELEN)
		return (0);
	if ((n = tmpfs_find(name)) != 0)
		return (n->isdir == isdir ? n : 0);
	for (i = 0; i < TMPFS_MAXNODES; i++) {
		if (tmpfs_nodes[i].used)
			continue;
		n = &tmpfs_nodes[i];
		bzero(n, sizeof(*n));
		n->used = 1;
		n->isdir = isdir;
		bcopy(name, n->name, len + 1);
		/* the data page is created on the first write */
		return (n);
	}
	return (0);
}

/* ---------------------------------------------------------------- vnodeops */

/* EN: Only the tmpfs root is searchable (else -ENOENT); a hit gets a vn_alloc()ed vnode typed VDIR/VREG from n->isdir, with tmpfs_vnodeops and the node index; -ENOMEM if alloc fails. */
/* 中文：只允许查 tmpfs 根目录（否则 -ENOENT）；命中后用 vn_alloc() 分配 vnode，按 n->isdir 置 VDIR/VREG、ops 与节点下标并返回 0，分配失败返回 -ENOMEM。 */
static int
tmpfs_lookup_op(struct vnode *dv, const char *name, struct vnode **vpp)
{
	struct tmpfs_node *n;
	struct vnode *vp;

	if (dv != &tmpfs_rootv)
		return (-ENOENT);
	n = tmpfs_find(name);
	if (n == 0)
		return (-ENOENT);
	vp = vn_alloc();
	if (vp == 0)
		return (-ENOMEM);
	vp->type = n->isdir ? VDIR : VREG;
	vp->ops = &tmpfs_vnodeops;
	vp->node = n - tmpfs_nodes;
	*vpp = vp;
	return (0);
}

/* EN: Fills *mode/*size: the root (vp->node < 0) gets 0040755/size 0, else 0040755 for a dir or 0100644 for a file, with n->size (0 for dirs); returns 0. */
/* 中文：填充 *mode/*size：根节点（vp->node < 0）为 0040755 且 size 0；否则目录为 0040755、普通文件为 0100644，并给出 n->size（目录为 0）；恒返回 0。 */
static int
tmpfs_getattr_op(struct vnode *vp, unsigned *mode, unsigned *size)
{
	struct tmpfs_node *n;

	if (vp->node < 0) {		/* the tmpfs root */
		*mode = 0040755;
		*size = 0;
		return (0);
	}
	n = &tmpfs_nodes[vp->node];
	*mode = n->isdir ? 0040755 : 0100644;
	*size = n->isdir ? 0 : n->size;
	return (0);
}

/*
 * S17: read() goes through the file page cache; this raw variant is what
 * the page cache calls to fill a page.  Writes and truncates purge the
 * cached pages of the vnode so a cat after a write sees the new data.
 */
/* EN: Uncached page-cache fill read: -EISDIR for the root or a dir, 0 if no data page or off >= size, else bcopy()s min(len, size-off) bytes and returns that count. */
/* 中文：供页缓存填页的无缓存读取：根节点或目录返回 -EISDIR，无数据页或 off >= size 返回 0，否则 bcopy 复制 min(len, size-off) 字节并返回该字节数。 */
static int
tmpfs_readraw_op(struct vnode *vp, unsigned off, void *buf, unsigned len)
{
	struct tmpfs_node *n;

	if (vp->node < 0)
		return (-EISDIR);
	n = &tmpfs_nodes[vp->node];
	if (n->isdir)
		return (-EISDIR);
	if (n->data == 0 || off >= n->size)
		return (0);
	if (len > n->size - off)
		len = n->size - off;
	bcopy(n->data + off, buf, len);
	return ((int)len);
}

/* EN: Thin wrapper over pagecache_read() for a tmpfs file, after rejecting the root (vp->node < 0) and directory nodes with -EISDIR, so data is served from the vnode's page cache. */
/* 中文：对 tmpfs 文件薄封装 pagecache_read()：先对根节点（vp->node < 0）和目录节点返回 -EISDIR，数据因此由该 vnode 的页缓存提供。 */
static int
tmpfs_read_op(struct vnode *vp, unsigned off, void *buf, unsigned len)
{
	struct tmpfs_node *n;

	if (vp->node < 0)
		return (-EISDIR);
	n = &tmpfs_nodes[vp->node];
	if (n->isdir)
		return (-EISDIR);
	return (pagecache_read(vp, off, buf, len));
}

/* EN: Dir -EISDIR, off >= TMPFS_DATASZ -ENOSPC; clips len, lazily allocs the 4096-byte page (fail -ENOSPC), copies, grows n->size, purges page cache, returns count. */
/* 中文：目录返回 -EISDIR，off >= TMPFS_DATASZ 返回 -ENOSPC；把 len 截到页尾，经 tmpfs_getdata(n,1) 惰性分配 4096 字节页（失败返回 -ENOSPC），拷贝后增长 n->size，再 purge 该 vnode 的页缓存并返回字节数。 */
static int
tmpfs_write_op(struct vnode *vp, unsigned off, const void *buf, unsigned len)
{
	struct tmpfs_node *n;

	if (vp->node < 0)
		return (-EISDIR);
	n = &tmpfs_nodes[vp->node];
	if (n->isdir)
		return (-EISDIR);
	if (off >= TMPFS_DATASZ)
		return (-ENOSPC);
	if (len > TMPFS_DATASZ - off)
		len = TMPFS_DATASZ - off;
	if (tmpfs_getdata(n, 1) == 0)
		return (-ENOSPC);
	bcopy(buf, n->data + off, len);
	if (off + len > n->size)
		n->size = off + len;
	pagecache_purge_vp(vp);
	return ((int)len);
}

/* EN: vp unused: from *cookie scans the global table for the next used entry, copies name/isdir, sets *cookie = i+1, returns 0; at end sets *cookie = TMPFS_MAXNODES, returns 1. */
/* 中文：忽略 vp，从 *cookie 起扫描全局节点表找下一个 used 表项，拷出名字与 isdir 并把 *cookie 置为 i+1 后返回 0；扫描完毕把 *cookie 置为 TMPFS_MAXNODES 并返回 1。 */
static int
tmpfs_readdir_op(struct vnode *vp, unsigned *cookie, char *name, int *isdir)
{
	unsigned i;

	(void)vp;
	for (i = *cookie; i < TMPFS_MAXNODES; i++) {
		if (!tmpfs_nodes[i].used)
			continue;
		*cookie = i + 1;
		bcopy(tmpfs_nodes[i].name, name,
		    strlen(tmpfs_nodes[i].name) + 1);
		*isdir = tmpfs_nodes[i].isdir;
		return (0);
	}
	*cookie = TMPFS_MAXNODES;
	return (1);
}

/* EN: Regular file, tmpfs root only (else -ENOENT): node creation failure gives -ENOSPC, vn_alloc failure -ENOMEM, else type VREG + tmpfs_vnodeops + node index are set. */
/* 中文：只在 tmpfs 根目录创建普通文件（否则 -ENOENT）：tmpfs_create_node(name,0) 返回 0 则得 -ENOSPC，vn_alloc 失败则 -ENOMEM；否则 vnode 置为 VREG、tmpfs_vnodeops 与节点下标。 */
static int
tmpfs_create_op(struct vnode *dv, const char *name, struct vnode **vpp)
{
	struct tmpfs_node *n;
	struct vnode *vp;

	if (dv != &tmpfs_rootv)
		return (-ENOENT);
	n = tmpfs_create_node(name, 0);
	if (n == 0)
		return (-ENOSPC);
	vp = vn_alloc();
	if (vp == 0)
		return (-ENOMEM);
	vp->type = VREG;
	vp->ops = &tmpfs_vnodeops;
	vp->node = n - tmpfs_nodes;
	*vpp = vp;
	return (0);
}

/* EN: Unlinks a regular file from the tmpfs root: -ENOENT for a non-root dir or a missing/dir entry, else frees the node data page via tmpfs_putdata() and clears n->used. */
/* 中文：从 tmpfs 根目录删除普通文件：非根目录或找不到/本身是目录的表项返回 -ENOENT，否则用 tmpfs_putdata() 释放节点数据页并清除 n->used。 */
static int
tmpfs_remove_op(struct vnode *dv, const char *name)
{
	struct tmpfs_node *n;

	if (dv != &tmpfs_rootv)
		return (-ENOENT);
	n = tmpfs_find(name);
	if (n == 0 || n->isdir)
		return (-ENOENT);
	tmpfs_putdata(n);
	n->used = 0;
	return (0);
}

/* EN: Creates a subdirectory only under the tmpfs root: -ENOENT for another directory, -EEXIST if the name is already used, otherwise tmpfs_create_node(name,1) yielding 0 or -ENOSPC. */
/* 中文：仅在 tmpfs 根目录下建立子目录：非根目录返回 -ENOENT，名字已被占用返回 -EEXIST，否则调用 tmpfs_create_node(name,1)，成功返回 0、失败返回 -ENOSPC。 */
static int
tmpfs_mkdir_op(struct vnode *dv, const char *name)
{

	if (dv != &tmpfs_rootv)
		return (-ENOENT);
	if (tmpfs_find(name) != 0)
		return (-EEXIST);
	return (tmpfs_create_node(name, 1) ? 0 : -ENOSPC);
}

/* EN: Truncates to zero length: frees the data page via tmpfs_putdata() (also zeroing n->pa/n->size) and purges the vnode page cache; a directory gives -EISDIR. */
/* 中文：用 tmpfs_putdata() 释放数据页（同时清零 n->pa 与 n->size）并 purge 该 vnode 页缓存，把 tmpfs 文件截断到 0 长度；目录返回 -EISDIR。 */
static int
tmpfs_truncate_op(struct vnode *vp)
{
	struct tmpfs_node *n = &tmpfs_nodes[vp->node];

	if (n->isdir)
		return (-EISDIR);
	tmpfs_putdata(n);
	pagecache_purge_vp(vp);
	return (0);
}

/* EN: Removes a subdirectory from the tmpfs root: -ENOENT for a non-root dir or a missing/non-dir entry, else drops the data page and clears n->used; emptiness is not checked. */
/* 中文：从 tmpfs 根目录删除子目录：非根目录或找不到/不是目录的表项返回 -ENOENT，否则释放数据页并清除 n->used；不检查目录是否为空。 */
static int
tmpfs_rmdir_op(struct vnode *dv, const char *name)
{
	struct tmpfs_node *n;

	if (dv != &tmpfs_rootv)
		return (-ENOENT);
	n = tmpfs_find(name);
	if (n == 0 || !n->isdir)
		return (-ENOENT);
	tmpfs_putdata(n);
	n->used = 0;
	return (0);
}

/* EN: Renames in place: non-root -EROFS (not -ENOENT), missing old -ENOENT, existing new -EEXIST, empty/too-long new -ENAMETOOLONG; else zeroes the name field and copies the new name. */
/* 中文：原地重命名节点：非根目录返回 -EROFS（而非 -ENOENT），旧名不存在返回 -ENOENT，新名已存在返回 -EEXIST，新名为空或过长返回 -ENAMETOOLONG；否则清零 name 字段并拷入新名字。 */
static int
tmpfs_rename_op(struct vnode *dv, const char *old, const char *new)
{
	struct tmpfs_node *n;
	int len;

	if (dv != &tmpfs_rootv)
		return (-EROFS);
	n = tmpfs_find(old);
	if (n == 0)
		return (-ENOENT);
	if (tmpfs_find(new) != 0)
		return (-EEXIST);
	len = strlen(new);
	if (len == 0 || len >= TMPFS_NAMELEN)
		return (-ENAMETOOLONG);
	bzero(n->name, TMPFS_NAMELEN);
	bcopy(new, n->name, len + 1);
	return (0);
}

struct vnodeops tmpfs_vnodeops = {
	tmpfs_lookup_op, tmpfs_getattr_op, tmpfs_read_op, tmpfs_write_op,
	tmpfs_readdir_op, tmpfs_create_op, tmpfs_remove_op,
	tmpfs_mkdir_op, tmpfs_rmdir_op, tmpfs_truncate_op,
	tmpfs_rename_op, tmpfs_readraw_op
};
