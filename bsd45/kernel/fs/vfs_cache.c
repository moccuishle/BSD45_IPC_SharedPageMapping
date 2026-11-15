/*
 * vfs_cache.c - S17: the directory name cache (4.5BSD vfs_cache.c's role).
 *
 * The 4.5BSD file keys entries on (dvp->v_id, name) and holds a reference on
 * the result.  This port has a LIFO vnode pool, so the same v_id trick is
 * used here: vn_alloc() hands out a fresh v_id and vn_put() clears it when
 * the slot is released, which also purges everything that pointed at it.
 *
 * The cache deliberately does NOT hold a vnode reference: a cache entry is a
 * hint.  namecache_lookup() validates vp->v_id and only then bumps vp->ref,
 * so a stale entry can never resurrect a freed vnode slot.  Lookup results
 * are entered by vfs_namei(); create/remove/rename purge the affected name.
 *
 * Layer rule: this file only touches struct vnode (its v_id, ref) and the
 * vnode pool's id allocator, never the process table or a backend.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include "vfs.h"

extern void bcopy(const void *, void *, unsigned);
extern void bzero(void *, unsigned);
extern int strcmp(const char *, const char *);
extern unsigned strlen(const char *);

#define	NCACHE		64
#define	NCHASH		32
#define	NC_NAMELEN	48

struct ncent {
	int		used;
	unsigned	dv_id;
	unsigned	v_id;
	char		name[NC_NAMELEN];
	struct vnode	*vp;
	int		hnext;
	int		lprev, lnext;	/* LRU of live entries */
};

static struct ncent nc[NCACHE];
static int nc_hash[NCHASH];
static int nc_lru_head, nc_lru_tail;
static int nc_inited;

static unsigned nc_hits, nc_misses;

static unsigned
nc_hashfn(unsigned dv_id, const char *name)
{
	unsigned h = dv_id * 31;

	while (*name)
		h = h * 33 + (unsigned char)*name++;
	return (h % NCHASH);
}

/* ---------------------------------------------------------------- LRU */

static void
nc_lru_remove(int i)
{
	struct ncent *e = &nc[i];

	if (e->lprev >= 0)
		nc[e->lprev].lnext = e->lnext;
	else if (nc_lru_head == i)
		nc_lru_head = e->lnext;
	if (e->lnext >= 0)
		nc[e->lnext].lprev = e->lprev;
	else if (nc_lru_tail == i)
		nc_lru_tail = e->lprev;
	e->lprev = e->lnext = -1;
}

static void
nc_lru_append(int i)
{
	struct ncent *e = &nc[i];

	e->lprev = nc_lru_tail;
	e->lnext = -1;
	if (nc_lru_tail >= 0)
		nc[nc_lru_tail].lnext = i;
	else
		nc_lru_head = i;
	nc_lru_tail = i;
}

/* ---------------------------------------------------------------- hash */

static int
nc_find(unsigned dv_id, const char *name)
{
	int i;

	for (i = nc_hash[nc_hashfn(dv_id, name)]; i >= 0;
	    i = nc[i].hnext)
		if (nc[i].used && nc[i].dv_id == dv_id &&
		    strcmp(nc[i].name, name) == 0)
			return (i);
	return (-1);
}

static void
nc_hash_remove(int i)
{
	int b = nc_hashfn(nc[i].dv_id, nc[i].name);
	int *pp = &nc_hash[b];

	while (*pp >= 0) {
		if (*pp == i) {
			*pp = nc[i].hnext;
			nc[i].hnext = -1;
			return;
		}
		pp = &nc[*pp].hnext;
	}
}

static void
nc_hash_insert(int i)
{
	int b = nc_hashfn(nc[i].dv_id, nc[i].name);

	nc[i].hnext = nc_hash[b];
	nc_hash[b] = i;
}

static void
nc_drop(int i)
{
	struct ncent *e = &nc[i];

	if (!e->used)
		return;
	nc_lru_remove(i);
	nc_hash_remove(i);
	e->used = 0;
	e->vp = 0;
	e->dv_id = e->v_id = 0;
	e->name[0] = 0;
}

/* ---------------------------------------------------------------- API */

void
namecache_init(void)
{
	int i;

	if (nc_inited)
		return;
	nc_inited = 1;
	for (i = 0; i < NCHASH; i++)
		nc_hash[i] = -1;
	nc_lru_head = nc_lru_tail = -1;
	for (i = 0; i < NCACHE; i++) {
		bzero(&nc[i], sizeof(nc[i]));
		nc[i].hnext = -1;
		nc[i].lprev = nc[i].lnext = -1;
	}
	nc_hits = nc_misses = 0;
}

/*
 * Return a referenced vnode for (dv, name), or 0 on a miss.  A stale entry
 * (vnode slot freed/reused) is dropped on the spot.
 */
struct vnode *
namecache_lookup(struct vnode *dv, const char *name)
{
	int i;
	struct vnode *vp;

	if (!nc_inited || dv == 0 || name == 0 || name[0] == 0 ||
	    strlen(name) >= NC_NAMELEN)
		return (0);
	i = nc_find(dv->v_id, name);
	if (i < 0) {
		nc_misses++;
		return (0);
	}
	vp = nc[i].vp;
	if (vp == 0 || vp->v_id == 0 || vp->v_id != nc[i].v_id) {
		/* the cached vnode was recycled: this is a miss */
		nc_drop(i);
		nc_misses++;
		return (0);
	}
	nc_hits++;
	nc_lru_remove(i);
	nc_lru_append(i);
	vp->ref++;
	return (vp);
}

void
namecache_enter(struct vnode *dv, const char *name, struct vnode *vp)
{
	int i, n, len;

	if (!nc_inited || dv == 0 || vp == 0 || name == 0 || name[0] == 0)
		return;
	len = strlen(name);
	if (len >= NC_NAMELEN)
		return;
	if ((i = nc_find(dv->v_id, name)) >= 0) {
		nc[i].vp = vp;
		nc[i].v_id = vp->v_id;
		nc_lru_remove(i);
		nc_lru_append(i);
		return;
	}
	/* free slot first, then the oldest live entry */
	for (i = 0; i < NCACHE; i++)
		if (!nc[i].used)
			break;
	if (i == NCACHE) {
		i = nc_lru_head;
		if (i < 0)
			return;
		nc_drop(i);
	}
	n = i;
	nc[n].used = 1;
	nc[n].dv_id = dv->v_id;
	nc[n].v_id = vp->v_id;
	nc[n].vp = vp;
	bcopy(name, nc[n].name, len);
	nc[n].name[len] = 0;
	nc_hash_insert(n);
	nc_lru_append(n);
}

void
namecache_purge(struct vnode *dv, const char *name)
{
	int i;

	if (!nc_inited || dv == 0 || name == 0)
		return;
	i = nc_find(dv->v_id, name);
	if (i >= 0)
		nc_drop(i);
}

void
namecache_purge_vp(struct vnode *vp)
{
	int i;

	if (!nc_inited || vp == 0)
		return;
	for (i = 0; i < NCACHE; i++)
		if (nc[i].used && (nc[i].vp == vp ||
		    (vp->v_id != 0 && nc[i].v_id == vp->v_id)))
			nc_drop(i);
}

void
namecache_stats(unsigned *total, unsigned *used, unsigned *hits,
    unsigned *misses)
{
	int i, nused = 0;

	for (i = 0; i < NCACHE; i++)
		nused += nc[i].used;
	*total = NCACHE;
	*used = nused;
	*hits = nc_hits;
	*misses = nc_misses;
}
