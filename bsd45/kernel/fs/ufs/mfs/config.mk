# config.mk - kernel/fs/ufs/mfs
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_fs_ufs_mfs_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
fs_ufs_mfs_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_fs_ufs_mfs_mk))
$(shell mkdir -p ${fs_ufs_mfs_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_fs_ufs_mfs_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# (empty placeholder directory)

fs_ufs_mfs_OBJS :=

clean::
