# config.mk - kernel/include
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_include_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
include_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_include_mk))
$(shell mkdir -p ${include_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_include_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: i386/ lib/ machine/ sys/
# include $(MAKEFILE_DIR_project_mk)/include/i386/config.mk
# include_OBJS += ${include_i386_OBJS}
# include $(MAKEFILE_DIR_project_mk)/include/lib/config.mk
# include_OBJS += ${include_lib_OBJS}
# include $(MAKEFILE_DIR_project_mk)/include/machine/config.mk
# include_OBJS += ${include_machine_OBJS}
# include $(MAKEFILE_DIR_project_mk)/include/sys/config.mk
# include_OBJS += ${include_sys_OBJS}

include_OBJS :=

clean::
