# config.mk - kernel/arch/sparc
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_arch_sparc_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_sparc_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_sparc_mk))
$(shell mkdir -p ${arch_sparc_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_sparc_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: conf/ dev/ fpu/ include/ rcons/ sbus/ sparc/ sunos/
# include $(MAKEFILE_DIR_project_mk)/arch/sparc/conf/config.mk
# arch_sparc_OBJS += ${arch_sparc_conf_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/sparc/dev/config.mk
# arch_sparc_OBJS += ${arch_sparc_dev_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/sparc/fpu/config.mk
# arch_sparc_OBJS += ${arch_sparc_fpu_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/sparc/include/config.mk
# arch_sparc_OBJS += ${arch_sparc_include_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/sparc/rcons/config.mk
# arch_sparc_OBJS += ${arch_sparc_rcons_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/sparc/sbus/config.mk
# arch_sparc_OBJS += ${arch_sparc_sbus_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/sparc/sparc/config.mk
# arch_sparc_OBJS += ${arch_sparc_sparc_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/sparc/sunos/config.mk
# arch_sparc_OBJS += ${arch_sparc_sunos_OBJS}

arch_sparc_OBJS :=

clean::
