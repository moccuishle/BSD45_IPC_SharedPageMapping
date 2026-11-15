/*
 * vfs_bio.c - S17: a real buffer cache in the 4.5BSD style.
 *
 * The 4.5BSD vfs_bio.c in kernel/sys has its bodies deleted (bread() is just
 * "return EIO"), and this port never compiled it: it needs struct buf
 * lists, sleep/wakeup and bdevsw.  This file implements the same *algorithm*
 * in the port's own terms:
 *
 *	- 2048-byte blocks (one ISO9660 sector), 64 buffers, each backed by a
 *	  physical page mapped into a kernel window with pmap_map();
 *	- a hash table (dev,blkno) -> buffer and a single LRU list;
 *	- bread()/breada()/brelse()/bwrite()/bdwrite();
 *	- hit/miss/read/readahead accounting for /dev/vmstat.
 *
 * There is no sleep/wakeup here: the driver is synchronous PIO, so a miss
 * simply reads the sector.  Dirty buffers are written back by bwrite()/
 * bdwrite(); the only device today is the read-only CD, so writes report
 * EROFS (the path is kept so an ATA disk can be plugged in later).
 *
 * Layer rule: this file talks to the driver (atapi_read) and the page
 * allocator (vfs_page_*), never to the process table or the VFS vnodes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>

#include "atapi.h"
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
extern int strcmp(const char *, const char *);

#define	NBUF		64
#define	NBIOHASH	32
#define	BIOSIZE		2048		/* CD sector */

#define	BUF_VA		0xFE800000	/* kernel window: 64 x 4K pages */
#define	BUF_PAGE	4096

#define	B_VALID		0x01
#define	B_DIRTY		0x02

struct buf {
	unsigned	b_dev;
	unsigned	b_blkno;
	unsigned	b_flags;
	unsigned	b_refcnt;
	unsigned char	*b_data;
	unsigned	b_pa;
	int		b_hnext;	/* hash chain */
	int		b_lprev, b_lnext;	/* LRU (inactive) list */
};

static struct buf bufs[NBUF];
static int bio_hash[NBIOHASH];
static int bio_lru_head, bio_lru_tail;
static int bio_inited;

static unsigned bio_hits, bio_misses, bio_reads, bio_readahead;
static unsigned bio_dirty;

static int
bio_bucket(unsigned dev, unsigned blkno)
{

	return ((int)((blkno * 2654435761u + dev) % NBIOHASH));
}

void
bio_init(void)
{
	int i;

	if (bio_inited)
		return;
	bio_inited = 1;
	for (i = 0; i < NBIOHASH; i++)
		bio_hash[i] = -1;
	bio_lru_head = bio_lru_tail = -1;
	bio_hits = bio_misses = bio_reads = bio_readahead = 0;
	bio_dirty = 0;
	for (i = 0; i < NBUF; i++) {
		struct buf *bp = &bufs[i];

		bzero(bp, sizeof(*bp));
		bp->b_pa = vfs_page_alloc();
		bp->b_data = (unsigned char *)(BUF_VA + i * BUF_PAGE);
		pmap_map((vm_offset_t)bp->b_data, (vm_offset_t)bp->b_pa,
		    (vm_offset_t)bp->b_pa + BUF_PAGE,
		    VM_PROT_READ | VM_PROT_WRITE);
		bp->b_hnext = -1;
		bp->b_lprev = i - 1;
		bp->b_lnext = (i + 1 < NBUF) ? i + 1 : -1;
	}
	bio_lru_head = 0;
	bio_lru_tail = NBUF - 1;
}

/* ---------------------------------------------------------------- LRU */

static void
bio_lru_remove(int i)
{
	struct buf *bp = &bufs[i];

	if (bp->b_lprev >= 0)
		bufs[bp->b_lprev].b_lnext = bp->b_lnext;
	else if (bio_lru_head == i)
		bio_lru_head = bp->b_lnext;
	if (bp->b_lnext >= 0)
		bufs[bp->b_lnext].b_lprev = bp->b_lprev;
	else if (bio_lru_tail == i)
		bio_lru_tail = bp->b_lprev;
	bp->b_lprev = bp->b_lnext = -1;
}

static void
bio_lru_append(int i)
{
	struct buf *bp = &bufs[i];

	bp->b_lprev = bio_lru_tail;
	bp->b_lnext = -1;
	if (bio_lru_tail >= 0)
		bufs[bio_lru_tail].b_lnext = i;
	else
		bio_lru_head = i;
	bio_lru_tail = i;
}

/* ---------------------------------------------------------------- hash */

static int
bio_find(unsigned dev, unsigned blkno)
{
	int i;

	for (i = bio_hash[bio_bucket(dev, blkno)]; i >= 0;
	    i = bufs[i].b_hnext)
		if (bufs[i].b_dev == dev && bufs[i].b_blkno == blkno)
			return (i);
	return (-1);
}

static void
bio_hash_remove(int i)
{
	int b = bio_bucket(bufs[i].b_dev, bufs[i].b_blkno);
	int *pp = &bio_hash[b];

	while (*pp >= 0) {
		if (*pp == i) {
			*pp = bufs[i].b_hnext;
			bufs[i].b_hnext = -1;
			return;
		}
		pp = &bufs[*pp].b_hnext;
	}
}

static void
bio_hash_insert(int i)
{
	int b = bio_bucket(bufs[i].b_dev, bufs[i].b_blkno);

	bufs[i].b_hnext = bio_hash[b];
	bio_hash[b] = i;
}

/* ---------------------------------------------------------------- I/O */

static int
bio_read_block(unsigned dev, unsigned blkno, void *data)
{

	if (dev != BDEV_CD)
		return (-EINVAL);
	if (atapi_read(blkno, 1, data) < 0)
		return (-EIO);
	return (0);
}

/*
 * Take the least recently used buffer.  Dirty buffers are written back
 * first; if that fails the buffer is still recycled (a read-only CD never
 * produces dirty buffers, so this only matters once a disk exists).
 */
static int
bio_evict(void)
{
	int i;

	i = bio_lru_head;
	if (i < 0)
		return (-1);
	bio_lru_remove(i);
	if (bufs[i].b_flags & B_VALID)
		bio_hash_remove(i);
	if (bufs[i].b_flags & B_DIRTY) {
		(void)bwrite(&bufs[i]);
		if (bio_dirty > 0)
			bio_dirty--;
	}
	bufs[i].b_flags = 0;
	bufs[i].b_dev = 0;
	bufs[i].b_blkno = 0;
	bufs[i].b_refcnt = 0;
	return (i);
}

struct buf *
bread(unsigned dev, unsigned blkno)
{
	int i;
	struct buf *bp;

	if (!bio_inited)
		bio_init();
	i = bio_find(dev, blkno);
	if (i >= 0) {
		bio_hits++;
		bio_lru_remove(i);
		bufs[i].b_refcnt++;
		return (&bufs[i]);
	}
	i = bio_evict();
	if (i < 0)
		return (0);
	bp = &bufs[i];
	bp->b_dev = dev;
	bp->b_blkno = blkno;
	bp->b_refcnt = 1;
	if (bio_read_block(dev, blkno, bp->b_data) < 0) {
		bp->b_flags = 0;
		bio_lru_append(i);
		return (0);
	}
	bp->b_flags = B_VALID;
	bio_hash_insert(i);
	bio_misses++;
	bio_reads++;
	return (bp);
}

/*
 * Prefetch n blocks starting at blkno.  The buffers are left on the LRU with
 * a zero reference, so a later bread() gets a hit; if the cache is full the
 * prefetch simply stops.
 */
void
breada(unsigned dev, unsigned blkno, unsigned n)
{
	unsigned k;

	if (!bio_inited)
		bio_init();
	for (k = 0; k < n; k++) {
		int i;
		struct buf *bp;

		if (bio_find(dev, blkno + k) >= 0)
			continue;
		i = bio_evict();
		if (i < 0)
			break;
		bp = &bufs[i];
		bp->b_dev = dev;
		bp->b_blkno = blkno + k;
		bp->b_refcnt = 0;
		if (bio_read_block(dev, bp->b_blkno, bp->b_data) < 0) {
			bp->b_flags = 0;
			bio_lru_append(i);
			break;
		}
		bp->b_flags = B_VALID;
		bio_hash_insert(i);
		bio_lru_append(i);
		bio_readahead++;
		bio_reads++;
	}
}

void
brelse(struct buf *bp)
{
	int i;

	if (bp == 0 || bp < bufs || bp >= bufs + NBUF)
		return;
	i = bp - bufs;
	if (bp->b_refcnt > 0)
		bp->b_refcnt--;
	if (bp->b_refcnt == 0) {
		/* only re-queue if it is not already on the inactive list */
		if (bp->b_lprev < 0 && bp->b_lnext < 0 &&
		    bio_lru_tail != i)
			bio_lru_append(i);
	}
}

int
bwrite(struct buf *bp)
{

	if (bp == 0)
		return (-EINVAL);
	if (bp->b_dev == BDEV_CD)
		return (-EROFS);		/* no writable device yet */
	return (-ENXIO);
}

void
bdwrite(struct buf *bp)
{

	if (bp == 0)
		return;
	bp->b_flags |= B_DIRTY;
	bio_dirty++;
	if (bp->b_refcnt == 0 && bp->b_lprev < 0 && bp->b_lnext < 0)
		bio_lru_append(bp - bufs);
}

unsigned char *
buf_data(struct buf *bp)
{

	return (bp ? bp->b_data : 0);
}

void
bio_stats(unsigned *total, unsigned *used, unsigned *hits, unsigned *misses,
    unsigned *reads, unsigned *readahead, unsigned *dirty)
{
	int i, nused = 0;

	for (i = 0; i < NBUF; i++)
		if (bufs[i].b_flags & B_VALID)
			nused++;
	*total = NBUF;
	*used = nused;
	*hits = bio_hits;
	*misses = bio_misses;
	*reads = bio_reads;
	*readahead = bio_readahead;
	*dirty = bio_dirty;
}
