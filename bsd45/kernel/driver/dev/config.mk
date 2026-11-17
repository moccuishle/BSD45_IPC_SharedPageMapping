# config.mk - kernel/driver/dev
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_driver_dev_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
driver_dev_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_driver_dev_mk))
$(shell mkdir -p ${driver_dev_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_driver_dev_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# driver_dev_CSRCS := cd.c vn.c
# driver_dev_COBJS := $(patsubst %.c,$(driver_dev_OBJ_DIR)/%.o,$(driver_dev_CSRCS))
# headers: cdvar.h vnioctl.h
# subdirectories: scsi/
# include $(MAKEFILE_DIR_project_mk)/driver/dev/scsi/config.mk
# driver_dev_OBJS += ${driver_dev_scsi_OBJS}

driver_dev_OBJS :=

clean::
