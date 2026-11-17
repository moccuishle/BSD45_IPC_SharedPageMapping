# config.mk - kernel/driver/dev/scsi
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_driver_dev_scsi_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
driver_dev_scsi_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_driver_dev_scsi_mk))
$(shell mkdir -p ${driver_dev_scsi_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_driver_dev_scsi_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# (empty placeholder directory)

driver_dev_scsi_OBJS :=

clean::
