/*
 * Copyright (c) 2026 jialiang.  All rights reserved.
 *
 * zcomp.h - in-RAM compressed anonymous-page pool (os.md mechanism 3,
 * "内存压缩：插在 RAM 与 swap 之间的回收层").
 *
 * A page is compressed into a 2 KB slot; the PTE becomes a not-present
 * "compressed entry" carrying the slot id.  A later fault decompresses the
 * slot into a fresh page and rebuilds the mapping.  File-backed pages are
 * never compressed: they can be dropped and re-read from the file.
 * 页被压缩进 2 KB 槽位，PTE 变成携带槽位号的 not-present “压缩 entry”；
 * 再次缺页时解压到新页并重建映射。文件页永不压缩：它们可以直接丢弃再读。
 */
#ifndef _ZCOMP_H_
#define _ZCOMP_H_

#define	ZCOMP_SLOT_ZERO		0u
#define	ZCOMP_SLOT_NONE		0xffffffffu
#define	ZCOMP_SLOT_COUNT	128
#define	ZCOMP_SLOT_SIZE		2048

/* return 0 on success, -1 when the page is not compressible/pool full */
int	zcomp_init(void);
int	zcomp_store(const unsigned char *src, unsigned *slotp, unsigned *lenp);
int	zcomp_load(unsigned slot, unsigned char *dst);
void	zcomp_free_slot(unsigned slot);
void	zcomp_stats(unsigned *compressed, unsigned *decompressed,
	    unsigned *saved, unsigned *rejected, unsigned *full);
void	zcomp_print(void);
int	zcomp_selftest(void);

#endif /* !_ZCOMP_H_ */
