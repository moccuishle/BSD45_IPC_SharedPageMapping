/*
 * jlfs.h - writable 4.4BSD-Lite2 FFS (UFS1) filesystem for BSD45.
 *
 * jlfs is a read/write derivative of the read-only ffs.c.  It mounts the
 * same on-disk image produced by hdd/mkffs.py, but adds persistent writes:
 * create/remove/mkdir/rmdir/truncate/rename and write.  Allocation uses a
 * cylinder-group bitmap; on first mount of a legacy image (no bitmap yet)
 * the bitmap is reconstructed by scanning the inode table and block maps.
 *
 * On-disk layout matches hdd/mkffs.py (see ffs.c / fs/ffs/README.md).
 */
#ifndef _JLFS_H_
#define _JLFS_H_

#include "vfs.h"

/* Probe ATA disk, validate the 4.4BSD FFS superblock, decode geometry and
 * load/rebuild the allocation bitmap.  0 on success, -1 on error. */
int	jlfs_mount(void);
int	jlfs_mounted(void);

/* Static root vnode (inode 2, VDIR), mounted on "/" by vfs.c. */
struct vnode *jlfs_root(void);
int	jlfs_is_root(struct vnode *vp);

extern struct vnodeops jlfs_vnodeops;

#endif /* _JLFS_H_ */
