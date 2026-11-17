# config.mk - kernel/fs/isofs
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_fs_isofs_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
fs_isofs_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_fs_isofs_mk))
$(shell mkdir -p ${fs_isofs_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_fs_isofs_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: cd9660/
# include $(MAKEFILE_DIR_project_mk)/fs/isofs/cd9660/config.mk
# fs_isofs_OBJS += ${fs_isofs_cd9660_OBJS}

fs_isofs_OBJS :=

clean::
