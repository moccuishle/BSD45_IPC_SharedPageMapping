/*
 * ffs.h - minimal read-only 4.4BSD-Lite2 FFS (UFS1) root filesystem.
 *
 * This is the read side of the ISO -> hard-disk install.  After the
 * `install` monitor command writes an FFS image (built by hdd/mkffs.py)
 * to the ATA disk and the machine reboots from its MBR, ffs.c mounts
 * that image as root so execve() can load /sbin/init and /bin/sh.
 *
 * The on-disk layout is documented in hdd/mkffs.py and in README.md.
 */
#ifndef _FFS_H_
#define _FFS_H_

#include "vfs.h"

/* Probe the ATA disk, read the superblock at byte 8192, validate the
 * 4.4BSD FFS magic and decode the geometry.  0 on success, -1 on error. */
int	ffs_mount(void);

/* Nonzero once ffs_mount() succeeded. */
int	ffs_mounted(void);

/* The statically allocated root vnode (inode 2, VDIR).  vfs.c mounts it
 * on "/" when the kernel was booted from the installed hard disk. */
struct vnode *ffs_root(void);

/* True for the static root vnode (must not be recycled by vn_put). */
int	ffs_is_root(struct vnode *vp);

/* vnode operations for FFS files and directories. */
extern struct vnodeops ffs_vnodeops;

#endif /* _FFS_H_ */
