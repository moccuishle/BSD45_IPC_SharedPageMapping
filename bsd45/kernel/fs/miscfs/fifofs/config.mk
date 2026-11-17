# config.mk - kernel/fs/miscfs/fifofs
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_fs_miscfs_fifofs_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
fs_miscfs_fifofs_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_fs_miscfs_fifofs_mk))
$(shell mkdir -p ${fs_miscfs_fifofs_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_fs_miscfs_fifofs_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# (empty placeholder directory)

fs_miscfs_fifofs_OBJS :=

clean::
