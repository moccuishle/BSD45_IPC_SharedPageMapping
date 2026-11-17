# config.mk - kernel/include/lib
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_include_lib_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
include_lib_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_include_lib_mk))
$(shell mkdir -p ${include_lib_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_include_lib_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# headers: libkern.h

include_lib_OBJS :=

clean::
