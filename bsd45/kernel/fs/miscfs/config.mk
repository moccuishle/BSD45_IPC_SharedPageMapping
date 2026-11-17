# config.mk - kernel/fs/miscfs
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_fs_miscfs_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
fs_miscfs_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_fs_miscfs_mk))
$(shell mkdir -p ${fs_miscfs_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_fs_miscfs_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: deadfs/ fdesc/ fifofs/ kernfs/ nullfs/ portal/ procfs/ specfs/ umapfs/ union/
# include $(MAKEFILE_DIR_project_mk)/fs/miscfs/deadfs/config.mk
# fs_miscfs_OBJS += ${fs_miscfs_deadfs_OBJS}
# include $(MAKEFILE_DIR_project_mk)/fs/miscfs/fdesc/config.mk
# fs_miscfs_OBJS += ${fs_miscfs_fdesc_OBJS}
# include $(MAKEFILE_DIR_project_mk)/fs/miscfs/fifofs/config.mk
# fs_miscfs_OBJS += ${fs_miscfs_fifofs_OBJS}
# include $(MAKEFILE_DIR_project_mk)/fs/miscfs/kernfs/config.mk
# fs_miscfs_OBJS += ${fs_miscfs_kernfs_OBJS}
# include $(MAKEFILE_DIR_project_mk)/fs/miscfs/nullfs/config.mk
# fs_miscfs_OBJS += ${fs_miscfs_nullfs_OBJS}
# include $(MAKEFILE_DIR_project_mk)/fs/miscfs/portal/config.mk
# fs_miscfs_OBJS += ${fs_miscfs_portal_OBJS}
# include $(MAKEFILE_DIR_project_mk)/fs/miscfs/procfs/config.mk
# fs_miscfs_OBJS += ${fs_miscfs_procfs_OBJS}
# include $(MAKEFILE_DIR_project_mk)/fs/miscfs/specfs/config.mk
# fs_miscfs_OBJS += ${fs_miscfs_specfs_OBJS}
# include $(MAKEFILE_DIR_project_mk)/fs/miscfs/umapfs/config.mk
# fs_miscfs_OBJS += ${fs_miscfs_umapfs_OBJS}
# include $(MAKEFILE_DIR_project_mk)/fs/miscfs/union/config.mk
# fs_miscfs_OBJS += ${fs_miscfs_union_OBJS}

fs_miscfs_OBJS :=

clean::
