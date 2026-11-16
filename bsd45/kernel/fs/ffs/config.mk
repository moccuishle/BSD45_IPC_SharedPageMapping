# config.mk - kernel/fs/ffs
#
# Per-directory build configuration for the BSD45 kernel.
# build_kernel.ps1 is the current driver; a kbuild-style build can
# pick up obj-y/obj-m/dir-y from here.

obj-y += ffs.o

# sources in this directory:
#   ffs.c   (read-only 4.4BSD-Lite2 FFS/UFS1 parser)

# The host-side image builder lives in bsd45/hdd/mkffs.py.  It writes the
# same on-disk format that ffs.c parses (struct fs at byte 8192, 128-byte
# struct dinode table, struct direct directories, direct-block files).
