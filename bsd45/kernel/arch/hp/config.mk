# config.mk - kernel/arch/hp
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_arch_hp_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_hp_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_hp_mk))
$(shell mkdir -p ${arch_hp_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_hp_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: dev/ hpux/
# include $(MAKEFILE_DIR_project_mk)/arch/hp/dev/config.mk
# arch_hp_OBJS += ${arch_hp_dev_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/hp/hpux/config.mk
# arch_hp_OBJS += ${arch_hp_hpux_OBJS}

arch_hp_OBJS :=

clean::
