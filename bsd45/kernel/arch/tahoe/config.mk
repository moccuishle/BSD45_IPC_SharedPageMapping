# config.mk - kernel/arch/tahoe
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_arch_tahoe_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_tahoe_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_tahoe_mk))
$(shell mkdir -p ${arch_tahoe_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_tahoe_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: align/ conf/ dist/ if/ include/ inline/ math/ stand/ tahoe/ vba/
# include $(MAKEFILE_DIR_project_mk)/arch/tahoe/align/config.mk
# arch_tahoe_OBJS += ${arch_tahoe_align_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/tahoe/conf/config.mk
# arch_tahoe_OBJS += ${arch_tahoe_conf_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/tahoe/dist/config.mk
# arch_tahoe_OBJS += ${arch_tahoe_dist_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/tahoe/if/config.mk
# arch_tahoe_OBJS += ${arch_tahoe_if_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/tahoe/include/config.mk
# arch_tahoe_OBJS += ${arch_tahoe_include_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/tahoe/inline/config.mk
# arch_tahoe_OBJS += ${arch_tahoe_inline_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/tahoe/math/config.mk
# arch_tahoe_OBJS += ${arch_tahoe_math_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/tahoe/stand/config.mk
# arch_tahoe_OBJS += ${arch_tahoe_stand_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/tahoe/tahoe/config.mk
# arch_tahoe_OBJS += ${arch_tahoe_tahoe_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/tahoe/vba/config.mk
# arch_tahoe_OBJS += ${arch_tahoe_vba_OBJS}

arch_tahoe_OBJS :=

clean::
