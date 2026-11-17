# config.mk - kernel/sys/legacy/tests/fstests
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_sys_legacy_tests_fstests_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
sys_legacy_tests_fstests_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_sys_legacy_tests_fstests_mk))
$(shell mkdir -p ${sys_legacy_tests_fstests_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_sys_legacy_tests_fstests_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# (empty placeholder directory)

sys_legacy_tests_fstests_OBJS :=

clean::
