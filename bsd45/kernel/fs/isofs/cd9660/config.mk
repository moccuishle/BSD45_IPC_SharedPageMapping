# config.mk - kernel/fs/isofs/cd9660
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_fs_isofs_cd9660_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
fs_isofs_cd9660_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_fs_isofs_cd9660_mk))
$(shell mkdir -p ${fs_isofs_cd9660_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_fs_isofs_cd9660_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# (empty placeholder directory)

fs_isofs_cd9660_OBJS :=

clean::
