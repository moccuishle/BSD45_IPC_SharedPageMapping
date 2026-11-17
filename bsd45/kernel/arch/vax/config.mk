# config.mk - kernel/arch/vax
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_arch_vax_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_vax_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_vax_mk))
$(shell mkdir -p ${arch_vax_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_vax_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: bi/ cassette/ conf/ consolerl/ dist/ floppy/ if/ include/ inline/ stand/ uba/ vax/
# include $(MAKEFILE_DIR_project_mk)/arch/vax/bi/config.mk
# arch_vax_OBJS += ${arch_vax_bi_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/vax/cassette/config.mk
# arch_vax_OBJS += ${arch_vax_cassette_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/vax/conf/config.mk
# arch_vax_OBJS += ${arch_vax_conf_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/vax/consolerl/config.mk
# arch_vax_OBJS += ${arch_vax_consolerl_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/vax/dist/config.mk
# arch_vax_OBJS += ${arch_vax_dist_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/vax/floppy/config.mk
# arch_vax_OBJS += ${arch_vax_floppy_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/vax/if/config.mk
# arch_vax_OBJS += ${arch_vax_if_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/vax/include/config.mk
# arch_vax_OBJS += ${arch_vax_include_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/vax/inline/config.mk
# arch_vax_OBJS += ${arch_vax_inline_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/vax/stand/config.mk
# arch_vax_OBJS += ${arch_vax_stand_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/vax/uba/config.mk
# arch_vax_OBJS += ${arch_vax_uba_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/vax/vax/config.mk
# arch_vax_OBJS += ${arch_vax_vax_OBJS}

arch_vax_OBJS :=

clean::
