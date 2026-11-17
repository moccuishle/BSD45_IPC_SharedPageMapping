# config.mk - kernel/arch/hp300/stand
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_arch_hp300_stand_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_hp300_stand_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_hp300_stand_mk))
$(shell mkdir -p ${arch_hp300_stand_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_hp300_stand_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: libsa/
# include $(MAKEFILE_DIR_project_mk)/arch/hp300/stand/libsa/config.mk
# arch_hp300_stand_OBJS += ${arch_hp300_stand_libsa_OBJS}

arch_hp300_stand_OBJS :=

clean::
