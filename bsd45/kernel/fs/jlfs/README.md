# kernel/fs/jlfs - writable FFS (UFS1) filesystem

jlfs is a read/write derivative of the read-only `fs/ffs`. It mounts the same
4.4BSD-Lite2 UFS1 image produced by `hdd/mkffs.py` and implements all seven
write vnodeops: write / create / remove / mkdir / rmdir / truncate / rename.

Writes are persistent (ata_write straight to the ATA disk). Allocation uses
a cylinder-group bitmap; on first mount of a legacy image without a bitmap,
one is rebuilt by scanning the inode table and block maps. The bitmap lives
in the cylinder-group block (frag `fs_csaddr`) at offsets CG_INOMAP / CG_FRAGMAP.

## Build

config.mk contributes `jlfs.o`. It is included by kernel/Makefile and listed
in build_kernel.sh / build_kernel.ps1. There is no separate tool dependency.

## Verification

`make` in kernel/ builds jlfs into bsd45.elf. A host-side unit test
(`build/jlfs_hosttest.c`, stub ATA backed by BSD45.FFS) exercises all seven
operations; QEMU boot-from-disk confirms create/write/cat/mkdir/rm/rmdir and
persistence across reboot.
