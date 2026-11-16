/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * zcomp.c - in-RAM compressed anonymous-page pool.
 *
 * This is the "zswap without a swap device" layer from os.md: a fixed pool
 * of 2 KB slots, a fast run-length codec, and a strict rule that only
 * process-owned anonymous pages are offered for compression.  The VM/PTE
 * side lives in kernel/other/usermode.c; this file only moves bytes and
 * accounts for saved memory.
 * 这是 os.md 的“没有 swap 设备的 zswap”：固定 2 KB 槽位池 + 快速 RLE 编码，
 * 并且只接受进程私有的匿名页。PTE/VM 侧在 kernel/other/usermode.c；本文件
 * 只负责搬运字节和统计节省的内存。
 */
#include <sys/param.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include "vm/zcomp.h"

#ifndef PAGE_SIZE
#define	PAGE_SIZE	4096
#endif

#define	ZCOMP_VA	0xFE300000
#define	ZCOMP_POOL_PAGES	(ZCOMP_SLOT_COUNT / 2)

struct zslot {
	unsigned char	*z_data;	/* 2 KB inside a pool page, or 0 */
	unsigned	 z_len;
	int		 z_used;
};

static struct zslot	zslots[ZCOMP_SLOT_COUNT];
static unsigned char	*zpool_va;	/* ZCOMP_VA */
static unsigned	char	zwork[ZCOMP_SLOT_SIZE];
static int		zcomp_ready;
static unsigned		z_compressed, z_decompressed, z_saved;
static unsigned		z_rejected, z_full;

extern vm_offset_t pmap_map(vm_offset_t, vm_offset_t, vm_offset_t, int);
extern unsigned	vfs_page_alloc(void);
extern void	bzero(void *, unsigned);
extern void	bcopy(const void *, void *, unsigned);

/*
 * Run-length encoder: pairs (count 1..255, byte).  Zero pages are encoded
 * as slot 0 with no payload.  If the result does not fit in 2 KB the page
 * is declared incompressible and the caller keeps it uncompressed.
 * 游程编码：每对 (1..255 的计数, 字节)。全零页用槽位 0 表示、不占负载；
 * 结果超过 2 KB 视为不可压缩，调用方保持原页。
 */
static int
zcomp_rle(const unsigned char *src, unsigned char *dst, unsigned *lenp)
{
	unsigned i = 0, o = 0;

	while (i < PAGE_SIZE) {
		unsigned char b = src[i];
		unsigned run = 1;

		while (i + run < PAGE_SIZE && src[i + run] == b &&
		    run < 255)
			run++;
		if (o + 2 > ZCOMP_SLOT_SIZE)
			return (-1);
		dst[o++] = (unsigned char)run;
		dst[o++] = b;
		i += run;
	}
	*lenp = o;
	return (0);
}

static int
zcomp_unrle(const unsigned char *src, unsigned len, unsigned char *dst)
{
	unsigned i = 0, o = 0;

	while (i + 1 < len) {
		unsigned run = src[i++];
		unsigned char b = src[i++];
		unsigned k;

		if (run == 0 || o + run > PAGE_SIZE)
			return (-1);
		for (k = 0; k < run; k++)
			dst[o++] = b;
	}
	if (o != PAGE_SIZE)
		return (-1);
	return (0);
}

int
zcomp_init(void)
{
	unsigned i, pa;
	unsigned char *p;

	bzero(zslots, sizeof(zslots));
	z_compressed = z_decompressed = z_saved = 0;
	z_rejected = z_full = 0;
	zpool_va = (unsigned char *)ZCOMP_VA;
	for (i = 0; i < ZCOMP_POOL_PAGES; i++) {
		pa = vfs_page_alloc();
		pmap_map((vm_offset_t)(zpool_va + i * PAGE_SIZE),
		    (vm_offset_t)pa, (vm_offset_t)pa + PAGE_SIZE,
		    VM_PROT_READ | VM_PROT_WRITE);
		p = zpool_va + i * PAGE_SIZE;
		zslots[i * 2].z_data = p;
		zslots[i * 2 + 1].z_data = p + ZCOMP_SLOT_SIZE;
	}
	zcomp_ready = 1;
	printf("zcomp: %d slots x %d bytes (%d KB pool)\n",
	    ZCOMP_SLOT_COUNT, ZCOMP_SLOT_SIZE,
	    ZCOMP_POOL_PAGES * PAGE_SIZE / 1024);
	return (0);
}

int
zcomp_store(const unsigned char *src, unsigned *slotp, unsigned *lenp)
{
	unsigned i, len, allzero = 1;

	if (!zcomp_ready)
		return (-1);
	for (i = 0; i < PAGE_SIZE; i++)
		if (src[i] != 0) {
			allzero = 0;
			break;
		}
	if (allzero) {
		*slotp = ZCOMP_SLOT_ZERO;
		*lenp = 0;
		z_compressed++;
		z_saved += PAGE_SIZE;
		return (0);
	}
	if (zcomp_rle(src, zwork, &len) < 0) {
		z_rejected++;
		return (-1);
	}
	for (i = 1; i < ZCOMP_SLOT_COUNT; i++)
		if (!zslots[i].z_used)
			break;
	if (i >= ZCOMP_SLOT_COUNT) {
		z_full++;
		return (-1);
	}
	bcopy(zwork, zslots[i].z_data, len);
	zslots[i].z_len = len;
	zslots[i].z_used = 1;
	*slotp = i;
	*lenp = len;
	z_compressed++;
	z_saved += PAGE_SIZE - len;
	return (0);
}

int
zcomp_load(unsigned slot, unsigned char *dst)
{
	unsigned i;

	if (!zcomp_ready || slot >= ZCOMP_SLOT_COUNT)
		return (-1);
	if (slot == ZCOMP_SLOT_ZERO) {
		for (i = 0; i < PAGE_SIZE; i++)
			dst[i] = 0;
		z_decompressed++;
		return (0);
	}
	if (!zslots[slot].z_used)
		return (-1);
	if (zcomp_unrle(zslots[slot].z_data, zslots[slot].z_len, dst) < 0) {
		zslots[slot].z_used = 0;
		return (-1);
	}
	zslots[slot].z_used = 0;
	z_decompressed++;
	return (0);
}

void
zcomp_free_slot(unsigned slot)
{

	if (slot > 0 && slot < ZCOMP_SLOT_COUNT)
		zslots[slot].z_used = 0;
}

void
zcomp_stats(unsigned *compressed, unsigned *decompressed, unsigned *saved,
    unsigned *rejected, unsigned *full)
{

	*compressed = z_compressed;
	*decompressed = z_decompressed;
	*saved = z_saved;
	*rejected = z_rejected;
	*full = z_full;
}

void
zcomp_print(void)
{
	unsigned used = 0, i;

	for (i = 1; i < ZCOMP_SLOT_COUNT; i++)
		if (zslots[i].z_used)
			used++;
	printf("zcomp: compressed %d decompressed %d saved %d KB\n",
	    z_compressed, z_decompressed, z_saved / 1024);
	printf("zcomp: slots %d/%d rejected %d full %d\n",
	    used, ZCOMP_SLOT_COUNT - 1, z_rejected, z_full);
}

int
zcomp_selftest(void)
{
	static unsigned char page[4096];
	unsigned slot, len, i;
	int ok = 1;

	if (!zcomp_ready)
		return (-1);
	/* zero page */
	for (i = 0; i < PAGE_SIZE; i++)
		page[i] = 0;
	if (zcomp_store(page, &slot, &len) < 0 || slot != ZCOMP_SLOT_ZERO)
		ok = 0;
	for (i = 0; i < PAGE_SIZE; i++)
		page[i] = 0xAA;
	if (zcomp_store(page, &slot, &len) < 0)
		ok = 0;
	for (i = 0; i < PAGE_SIZE; i++)
		page[i] = 0;
	if (zcomp_load(slot, page) < 0)
		ok = 0;
	for (i = 0; i < PAGE_SIZE; i++)
		if (page[i] != 0xAA) {
			ok = 0;
			break;
		}
	printf("zcomptest: zero+RLE roundtrip %s\n", ok ? "ok" : "FAIL");
	printf("zcomptest: %s\n", ok ? "PASS" : "FAIL");
	return (ok ? 0 : -1);
}
