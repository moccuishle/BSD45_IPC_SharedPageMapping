/*
 * pagecache.c - S17: the file page cache (this port's vnode_pager).
 *
 * 4.5BSD splits this over vm_object.c / vm_page.c / vnode_pager.c.  Those
 * files need the whole vm_map/pager machinery, which this port does not
 * have, so the same job is done here in the port's terms:
 *
 *	- NPCACHE 4K pages, one physical page each, mapped into a kernel
 *	  window with pmap_map();
 *	- a hash table (cache key, page index) and an LRU;
 *	- a miss fills the page with vp->ops->readraw(), i.e. the backend's
 *	  uncached read (ISO9660: buffer cache; tmpfs: node data page);
 *	- read() is layered on pagecache_read(), so a second cat/exec of the
 *	  same file is served from memory;
 *	- mmap() gets a referenced page from pagecache_get() and maps it into
 *	  the user page directory (MAP_SHARED); MAP_PRIVATE gets a copy.
 *
 * Cache key: an ISO9660 file is immutable and identified by its LBA, so
 * pages survive close/reopen (pagecache_key() = v_key).  tmpfs is mutable,
 * so its pages are keyed on the vnode id and dropped when the vnode dies.
 *
 * Pages are preallocated at boot so the cache cannot "leak" pages into the
 * S10.1 accounting: page inuse returns to the same baseline.
 *
 * Layer rule: this file talks to vnodeops and the page allocator only.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>

#include "vfs.h"

#ifndef PAGE_SIZE
#define	PAGE_SIZE	4096
#endif
#ifndef VM_PROT_READ
#define	VM_PROT_READ	0x1
#define	VM_PROT_WRITE	0x2
#endif

extern void pmap_map(vm_offset_t, vm_offset_t, vm_offset_t, int);
extern void bcopy(const void *, void *, unsigned);
extern void bzero(void *, unsigned);

#define	NPCACHE		128
#define	PCHASH		32
#define	PCACHE_VA	0xFE900000

struct pcent {
	int		used;
	int		refs;		/* mappings / in-flight reads */
	int		onlru;		/* linked on the eviction list */
	unsigned	key;		/* pagecache_key() of the vnode */
	unsigned	pindex;
	unsigned	pa;
	unsigned char	*va;
	int		hnext;
	int		lprev, lnext;	/* LRU of evictable entries */
};

static struct pcent pcs[NPCACHE];
static int pc_hash[PCHASH];
static int pc_lru_head, pc_lru_tail;
static int pc_inited;

static unsigned pc_hits, pc_misses, pc_fills, pc_evicts;

/* A read-only backend with a stable identity may use its v_key. */
unsigned
pagecache_key(struct vnode *vp)
{

	if (vp == 0)
		return (0);
	if (vp->v_key != 0)
		return (vp->v_key);
	return (vp->v_id);		/* tmpfs: tied to this vnode */
}

static unsigned
pc_hashfn(unsigned key, unsigned pindex)
{

	return ((key * 2654435761u + pindex) % PCHASH);
}

/* ---------------------------------------------------------------- LRU */

static void
pc_lru_del(int i)
{
	struct pcent *e = &pcs[i];

	if (!e->onlru)
		return;
	if (e->lprev >= 0)
		pcs[e->lprev].lnext = e->lnext;
	else if (pc_lru_head == i)
		pc_lru_head = e->lnext;
	if (e->lnext >= 0)
		pcs[e->lnext].lprev = e->lprev;
	else if (pc_lru_tail == i)
		pc_lru_tail = e->lprev;
	e->lprev = e->lnext = -1;
	e->onlru = 0;
}

static void
pc_lru_add(int i)
{
	struct pcent *e = &pcs[i];

	if (e->onlru)
		return;
	e->lprev = pc_lru_tail;
	e->lnext = -1;
	if (pc_lru_tail >= 0)
		pcs[pc_lru_tail].lnext = i;
	else
		pc_lru_head = i;
	pc_lru_tail = i;
	e->onlru = 1;
}

/* ---------------------------------------------------------------- hash */

static int
pc_find(unsigned key, unsigned pindex)
{
	int i;

	for (i = pc_hash[pc_hashfn(key, pindex)]; i >= 0; i = pcs[i].hnext)
		if (pcs[i].used && pcs[i].key == key &&
		    pcs[i].pindex == pindex)
			return (i);
	return (-1);
}

static void
pc_hash_remove(int i)
{
	int b = pc_hashfn(pcs[i].key, pcs[i].pindex);
	int *pp = &pc_hash[b];

	while (*pp >= 0) {
		if (*pp == i) {
			*pp = pcs[i].hnext;
			pcs[i].hnext = -1;
			return;
		}
		pp = &pcs[*pp].hnext;
	}
}

static void
pc_hash_insert(int i)
{
	int b = pc_hashfn(pcs[i].key, pcs[i].pindex);

	pcs[i].hnext = pc_hash[b];
	pc_hash[b] = i;
}

static void
pc_release(int i)
{
	struct pcent *e = &pcs[i];

	if (!e->used)
		return;
	pc_hash_remove(i);
	e->used = 0;
	e->refs = 0;
	e->key = e->pindex = 0;
	pc_lru_add(i);
}

/*
 * Take the least recently used evictable entry.  Entries with refs > 0 are
 * mapped into a process (or being copied) and are not on the LRU at all, so
 * they can never be chosen here: returning 0 means "cache full".
 */
static int
pc_take(void)
{
	int i;

	i = pc_lru_head;
	if (i < 0)
		return (-1);
	pc_lru_del(i);
	if (pcs[i].used) {
		pc_hash_remove(i);
		pcs[i].used = 0;
		pc_evicts++;
	}
	pcs[i].refs = 0;
	pcs[i].key = pcs[i].pindex = 0;
	return (i);
}

/* ---------------------------------------------------------------- init */

void
pagecache_init(void)
{
	int i;

	if (pc_inited)
		return;
	pc_inited = 1;
	for (i = 0; i < PCHASH; i++)
		pc_hash[i] = -1;
	pc_lru_head = pc_lru_tail = -1;
	for (i = 0; i < NPCACHE; i++) {
		struct pcent *e = &pcs[i];

		bzero(e, sizeof(*e));
		e->pa = vfs_page_alloc();
		e->va = (unsigned char *)(PCACHE_VA + i * PAGE_SIZE);
		pmap_map((vm_offset_t)e->va, (vm_offset_t)e->pa,
		    (vm_offset_t)e->pa + PAGE_SIZE,
		    VM_PROT_READ | VM_PROT_WRITE);
		e->hnext = -1;
		e->lprev = e->lnext = -1;
		e->onlru = 0;
		pc_lru_add(i);
	}
	pc_hits = pc_misses = pc_fills = pc_evicts = 0;
}

/* ---------------------------------------------------------------- API */

/*
 * Get file page `pindex' of vp, filling it from the backend on a miss.
 * Returns its physical address and stores the kernel VA in *vap; the page
 * comes back referenced, so the caller must pagecache_unref() it.
 */
unsigned
pagecache_get(struct vnode *vp, unsigned pindex, unsigned char **vap)
{
	struct pcent *e;
	unsigned key, mode = 0, size = 0, start, n;
	int i, rv;

	if (!pc_inited)
		pagecache_init();
	if (vp == 0 || vap == 0)
		return (0);
	key = pagecache_key(vp);
	if (key == 0)
		return (0);
	i = pc_find(key, pindex);
	if (i >= 0) {
		e = &pcs[i];
		pc_hits++;
		pc_lru_del(i);
		e->refs++;
		*vap = e->va;
		return (e->pa);
	}
	i = pc_take();
	if (i < 0)
		return (0);
	e = &pcs[i];
	e->used = 1;
	e->key = key;
	e->pindex = pindex;
	e->refs = 1;
	bzero(e->va, PAGE_SIZE);
	if (vp->ops->getattr)
		vp->ops->getattr(vp, &mode, &size);
	start = pindex * PAGE_SIZE;
	n = (size > start) ? (size - start) : 0;
	if (n > PAGE_SIZE)
		n = PAGE_SIZE;
	if (n > 0 && vp->ops->readraw != 0) {
		rv = vp->ops->readraw(vp, start, e->va, n);
		if (rv < 0) {
			e->used = 0;
			e->refs = 0;
			e->key = e->pindex = 0;
			pc_lru_add(i);
			return (0);
		}
	}
	pc_hash_insert(i);
	pc_misses++;
	pc_fills++;
	*vap = e->va;
	return (e->pa);
}

/*
 * Copy len bytes at file offset off through the page cache.  Returns the
 * number of bytes copied, or -ENOMEM if the cache has no unreferenced page.
 */
int
pagecache_read(struct vnode *vp, unsigned off, void *buf, unsigned len)
{
	unsigned done = 0, key, mode = 0, size = 0;
	char *p = (char *)buf;

	/*
	 * Clamp to EOF here: the page cache zero-fills the tail of the last
	 * page (mmap relies on that), but read(2) must stop at st_size or a
	 * cat would loop forever on zero bytes.
	 */
	if (vp->ops->getattr)
		vp->ops->getattr(vp, &mode, &size);
	if (off >= size)
		return (0);
	if (len > size - off)
		len = size - off;
	key = pagecache_key(vp);
	while (done < len) {
		unsigned pindex = (off + done) / PAGE_SIZE;
		unsigned poff = (off + done) % PAGE_SIZE;
		unsigned n = PAGE_SIZE - poff;
		unsigned char *va;
		unsigned pa;

		if (n > len - done)
			n = len - done;
		pa = pagecache_get(vp, pindex, &va);
		if (pa == 0)
			return (done > 0 ? (int)done : -ENOMEM);
		bcopy(va + poff, p + done, n);
		pagecache_unref_key(key, pindex);
		done += n;
	}
	return ((int)done);
}

unsigned
pagecache_unref_key(unsigned key, unsigned pindex)
{
	int i;

	if (!pc_inited || key == 0)
		return (0);
	i = pc_find(key, pindex);
	if (i < 0)
		return (0);
	if (pcs[i].refs > 0) {
		pcs[i].refs--;
		if (pcs[i].refs == 0)
			pc_lru_add(i);
	}
	return (1);
}

unsigned
pagecache_unref(struct vnode *vp, unsigned pindex)
{

	return (vp ? pagecache_unref_key(pagecache_key(vp), pindex) : 0);
}

/*
 * Drop all unreferenced pages of a vnode.  Referenced pages (an active
 * mmap) stay: munmap()/exit drops the reference later.  Only called for
 * mutable (tmpfs) vnodes; ISO9660 pages are immutable and keyed by LBA.
 */
void
pagecache_purge_vp(struct vnode *vp)
{
	unsigned key;
	int i;

	if (!pc_inited || vp == 0)
		return;
	if (vp->v_key != 0)
		return;			/* immutable: keep the pages */
	key = vp->v_id;
	if (key == 0)
		return;
	for (i = 0; i < NPCACHE; i++)
		if (pcs[i].used && pcs[i].key == key && pcs[i].refs == 0)
			pc_release(i);
}

void
pagecache_stats(unsigned *total, unsigned *used, unsigned *hits,
    unsigned *misses, unsigned *fills, unsigned *evicts)
{
	int i, nused = 0;

	for (i = 0; i < NPCACHE; i++)
		nused += pcs[i].used;
	*total = NPCACHE;
	*used = nused;
	*hits = pc_hits;
	*misses = pc_misses;
	*fills = pc_fills;
	*evicts = pc_evicts;
}
