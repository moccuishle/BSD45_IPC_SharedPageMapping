# config.mk - kernel/sys/legacy/tests/nfs/billboard
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_sys_legacy_tests_nfs_billboard_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
sys_legacy_tests_nfs_billboard_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_sys_legacy_tests_nfs_billboard_mk))
$(shell mkdir -p ${sys_legacy_tests_nfs_billboard_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_sys_legacy_tests_nfs_billboard_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: src/
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/nfs/billboard/src/config.mk
# sys_legacy_tests_nfs_billboard_OBJS += ${sys_legacy_tests_nfs_billboard_src_OBJS}

sys_legacy_tests_nfs_billboard_OBJS :=

clean::
