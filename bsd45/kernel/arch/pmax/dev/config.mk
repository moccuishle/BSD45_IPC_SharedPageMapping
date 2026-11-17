# config.mk - kernel/arch/pmax/dev
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_arch_pmax_dev_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_pmax_dev_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_pmax_dev_mk))
$(shell mkdir -p ${arch_pmax_dev_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_pmax_dev_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# (empty placeholder directory)

arch_pmax_dev_OBJS :=

clean::
