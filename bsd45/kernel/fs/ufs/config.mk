# config.mk - kernel/fs/ufs
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_fs_ufs_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
fs_ufs_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_fs_ufs_mk))
$(shell mkdir -p ${fs_ufs_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_fs_ufs_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: ffs/ lfs/ mfs/ ufs/
# include $(MAKEFILE_DIR_project_mk)/fs/ufs/ffs/config.mk
# fs_ufs_OBJS += ${fs_ufs_ffs_OBJS}
# include $(MAKEFILE_DIR_project_mk)/fs/ufs/lfs/config.mk
# fs_ufs_OBJS += ${fs_ufs_lfs_OBJS}
# include $(MAKEFILE_DIR_project_mk)/fs/ufs/mfs/config.mk
# fs_ufs_OBJS += ${fs_ufs_mfs_OBJS}
# include $(MAKEFILE_DIR_project_mk)/fs/ufs/ufs/config.mk
# fs_ufs_OBJS += ${fs_ufs_ufs_OBJS}

fs_ufs_OBJS :=

clean::
