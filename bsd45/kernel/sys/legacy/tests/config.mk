# config.mk - kernel/sys/legacy/tests
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_sys_legacy_tests_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
sys_legacy_tests_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_sys_legacy_tests_mk))
$(shell mkdir -p ${sys_legacy_tests_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_sys_legacy_tests_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: benchmarks/ fstests/ netccitt/ netiso/ nfs/
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/benchmarks/config.mk
# sys_legacy_tests_OBJS += ${sys_legacy_tests_benchmarks_OBJS}
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/fstests/config.mk
# sys_legacy_tests_OBJS += ${sys_legacy_tests_fstests_OBJS}
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/netccitt/config.mk
# sys_legacy_tests_OBJS += ${sys_legacy_tests_netccitt_OBJS}
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/netiso/config.mk
# sys_legacy_tests_OBJS += ${sys_legacy_tests_netiso_OBJS}
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/nfs/config.mk
# sys_legacy_tests_OBJS += ${sys_legacy_tests_nfs_OBJS}

sys_legacy_tests_OBJS :=

clean::
