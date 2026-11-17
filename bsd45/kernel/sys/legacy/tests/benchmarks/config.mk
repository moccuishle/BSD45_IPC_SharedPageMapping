# config.mk - kernel/sys/legacy/tests/benchmarks
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_sys_legacy_tests_benchmarks_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
sys_legacy_tests_benchmarks_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_sys_legacy_tests_benchmarks_mk))
$(shell mkdir -p ${sys_legacy_tests_benchmarks_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_sys_legacy_tests_benchmarks_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: bench-4.1/ inet/
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/benchmarks/bench-4.1/config.mk
# sys_legacy_tests_benchmarks_OBJS += ${sys_legacy_tests_benchmarks_bench_4.1_OBJS}
# include $(MAKEFILE_DIR_project_mk)/sys/legacy/tests/benchmarks/inet/config.mk
# sys_legacy_tests_benchmarks_OBJS += ${sys_legacy_tests_benchmarks_inet_OBJS}

sys_legacy_tests_benchmarks_OBJS :=

clean::
