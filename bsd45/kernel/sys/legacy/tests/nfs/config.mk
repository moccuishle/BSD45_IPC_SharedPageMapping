# config.mk - kernel/sys/legacy/tests/nfs
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_sys_legacy_tests_nfs_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
sys_legacy_tests_nfs_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_sys_legacy_tests_nfs_mk))
$(shell mkdir -p ${sys_legacy_tests_nfs_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_sys_legacy_tests_nfs_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: billboard/ pcnfs-tests/ unix-tests/
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/nfs/billboard/config.mk
# sys_legacy_tests_nfs_OBJS += ${sys_legacy_tests_nfs_billboard_OBJS}
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/nfs/pcnfs-tests/config.mk
# sys_legacy_tests_nfs_OBJS += ${sys_legacy_tests_nfs_pcnfs_tests_OBJS}
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/nfs/unix-tests/config.mk
# sys_legacy_tests_nfs_OBJS += ${sys_legacy_tests_nfs_unix_tests_OBJS}

sys_legacy_tests_nfs_OBJS :=

clean::
