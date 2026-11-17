# config.mk - kernel/lib/sparc
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_lib_sparc_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
lib_sparc_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_lib_sparc_mk))
$(shell mkdir -p ${lib_sparc_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_lib_sparc_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# lib_sparc_ASMSRC := mul.s rem.s sdiv.s udiv.s umul.s urem.s
# headers: DEFS.h

lib_sparc_OBJS :=

clean::
