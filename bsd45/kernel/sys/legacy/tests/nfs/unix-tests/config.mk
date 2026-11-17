# config.mk - kernel/sys/legacy/tests/nfs/unix-tests
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_sys_legacy_tests_nfs_unix_tests_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
sys_legacy_tests_nfs_unix_tests_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_sys_legacy_tests_nfs_unix_tests_mk))
$(shell mkdir -p ${sys_legacy_tests_nfs_unix_tests_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_sys_legacy_tests_nfs_unix_tests_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: basic/ general/ special/ tools/
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/nfs/unix-tests/basic/config.mk
# sys_legacy_tests_nfs_unix_tests_OBJS += ${sys_legacy_tests_nfs_unix_tests_basic_OBJS}
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/nfs/unix-tests/general/config.mk
# sys_legacy_tests_nfs_unix_tests_OBJS += ${sys_legacy_tests_nfs_unix_tests_general_OBJS}
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/nfs/unix-tests/special/config.mk
# sys_legacy_tests_nfs_unix_tests_OBJS += ${sys_legacy_tests_nfs_unix_tests_special_OBJS}
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/nfs/unix-tests/tools/config.mk
# sys_legacy_tests_nfs_unix_tests_OBJS += ${sys_legacy_tests_nfs_unix_tests_tools_OBJS}

sys_legacy_tests_nfs_unix_tests_OBJS :=

clean::
