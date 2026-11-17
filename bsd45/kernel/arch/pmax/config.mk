# config.mk - kernel/arch/pmax
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_arch_pmax_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_pmax_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_pmax_mk))
$(shell mkdir -p ${arch_pmax_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_pmax_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: conf/ dev/ dist/ include/ pmax/ stand/ ultrix/
# include $(MAKEFILE_DIR_project_mk)/arch/pmax/conf/config.mk
# arch_pmax_OBJS += ${arch_pmax_conf_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/pmax/dev/config.mk
# arch_pmax_OBJS += ${arch_pmax_dev_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/pmax/dist/config.mk
# arch_pmax_OBJS += ${arch_pmax_dist_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/pmax/include/config.mk
# arch_pmax_OBJS += ${arch_pmax_include_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/pmax/pmax/config.mk
# arch_pmax_OBJS += ${arch_pmax_pmax_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/pmax/stand/config.mk
# arch_pmax_OBJS += ${arch_pmax_stand_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/pmax/ultrix/config.mk
# arch_pmax_OBJS += ${arch_pmax_ultrix_OBJS}

arch_pmax_OBJS :=

clean::
