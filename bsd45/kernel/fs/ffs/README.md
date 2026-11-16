# `kernel/fs/ffs` — 4.4BSD-Lite2 FFS (UFS1) root filesystem

<!-- generated for the ISO -> hard-disk install path -->

## English

This directory contains the read-only FFS (UFS1) parser used by the
"install BSD45 from the ISO onto a VMware virtual disk" flow.

`ffs.c` is the read side.  When the on-board monitor runs `install`, a
small FFS image (built on the host by `bsd45/hdd/mkffs.py`) is written to
the ATA disk together with the MBR/stage1/stage2 boot blocks and the raw
ELF kernel.  On the next boot `glue.c` detects the ATA disk, calls
`ffs_mount()`, and `vfs.c` mounts the FFS root vnode on `/`, so
`execve("/sbin/init")` loads the userland from disk instead of the CD.

Supported subset (matches `hdd/mkffs.py`):

* 512-byte device sectors, `fs_bsize=4096`, `fs_fsize=512` (8 frags/block),
  one cylinder group, `fs_fsbtodb=0`.
* `struct fs` superblock at byte 8192 (`fs_magic` 0x011954 @1372).
* 128-byte `struct dinode` table at `fs_iblkno`.
* POSIX `struct direct` directories; files use only `di_db[]` direct blocks.
* Pages come from the page cache; uncached fills go straight to `ata_read()`.

`config.mk` lists `ffs.o`; `build_kernel.ps1` is the current driver and
selects the files actually compiled.

## 中文

本目录包含“从 ISO 把 BSD45 安装到 VMware 虚拟硬盘”流程所用的只读 FFS
（UFS1）解析器。

`ffs.c` 是读取端。当板载 monitor 执行 `install` 时，宿主侧由
`bsd45/hdd/mkffs.py` 生成的小型 FFS 镜像会与 MBR/stage1/stage2 引导块以及
裸 ELF 内核一起写入 ATA 磁盘。下次启动时 `glue.c` 检测到 ATA 硬盘，调用
`ffs_mount()`，`vfs.c` 把 FFS 根 vnode 挂到 `/`，于是
`execve("/sbin/init")` 从硬盘而不是光盘加载用户态。

支持的子集（与 `hdd/mkffs.py` 一致）：

* 512 字节设备扇区，`fs_bsize=4096`、`fs_fsize=512`（每块 8 frag）、单个
  柱面组、`fs_fsbtodb=0`。
* 字节 8192 处的 `struct fs` 超级块（`fs_magic` 0x011954 @1372）。
* `fs_iblkno` 处的 128 字节 `struct dinode` inode 表。
* POSIX `struct direct` 目录；文件只使用 `di_db[]` 直接块。
* 页面来自页缓存；未命中时直接通过 `ata_read()` 填充。
