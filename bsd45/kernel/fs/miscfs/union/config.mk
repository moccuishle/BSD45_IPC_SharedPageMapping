# config.mk - kernel/fs/miscfs/union
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_fs_miscfs_union_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
fs_miscfs_union_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_fs_miscfs_union_mk))
$(shell mkdir -p ${fs_miscfs_union_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_fs_miscfs_union_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: libc/
# include $(MAKEFILE_DIR_project_mk)/fs/miscfs/union/libc/config.mk
# fs_miscfs_union_OBJS += ${fs_miscfs_union_libc_OBJS}

fs_miscfs_union_OBJS :=

clean::
