# config.mk - kernel/arch/vax/dist
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_arch_vax_dist_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_vax_dist_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_vax_dist_mk))
$(shell mkdir -p ${arch_vax_dist_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_vax_dist_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: tp/
# include $(MAKEFILE_DIR_project_mk)/arch/vax/dist/tp/config.mk
# arch_vax_dist_OBJS += ${arch_vax_dist_tp_OBJS}

arch_vax_dist_OBJS :=

clean::
