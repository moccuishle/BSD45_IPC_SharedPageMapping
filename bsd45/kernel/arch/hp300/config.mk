# config.mk - kernel/arch/hp300
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_arch_hp300_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_hp300_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_hp300_mk))
$(shell mkdir -p ${arch_hp300_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_hp300_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: DOC/ conf/ dev/ hp300/ include/ scripts/ stand/
# include $(MAKEFILE_DIR_project_mk)/arch/hp300/DOC/config.mk
# arch_hp300_OBJS += ${arch_hp300_DOC_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/hp300/conf/config.mk
# arch_hp300_OBJS += ${arch_hp300_conf_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/hp300/dev/config.mk
# arch_hp300_OBJS += ${arch_hp300_dev_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/hp300/hp300/config.mk
# arch_hp300_OBJS += ${arch_hp300_hp300_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/hp300/include/config.mk
# arch_hp300_OBJS += ${arch_hp300_include_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/hp300/scripts/config.mk
# arch_hp300_OBJS += ${arch_hp300_scripts_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/hp300/stand/config.mk
# arch_hp300_OBJS += ${arch_hp300_stand_OBJS}

arch_hp300_OBJS :=

clean::
