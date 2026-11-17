# config.mk - kernel/arch/news3400
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_arch_news3400_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_news3400_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_news3400_mk))
$(shell mkdir -p ${arch_news3400_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_news3400_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: bm/ conf/ fb/ hbdev/ if/ include/ iodev/ iop/ news3400/ newsos/ sio/
# include $(MAKEFILE_DIR_project_mk)/arch/news3400/bm/config.mk
# arch_news3400_OBJS += ${arch_news3400_bm_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/news3400/conf/config.mk
# arch_news3400_OBJS += ${arch_news3400_conf_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/news3400/fb/config.mk
# arch_news3400_OBJS += ${arch_news3400_fb_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/news3400/hbdev/config.mk
# arch_news3400_OBJS += ${arch_news3400_hbdev_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/news3400/if/config.mk
# arch_news3400_OBJS += ${arch_news3400_if_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/news3400/include/config.mk
# arch_news3400_OBJS += ${arch_news3400_include_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/news3400/iodev/config.mk
# arch_news3400_OBJS += ${arch_news3400_iodev_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/news3400/iop/config.mk
# arch_news3400_OBJS += ${arch_news3400_iop_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/news3400/news3400/config.mk
# arch_news3400_OBJS += ${arch_news3400_news3400_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/news3400/newsos/config.mk
# arch_news3400_OBJS += ${arch_news3400_newsos_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/news3400/sio/config.mk
# arch_news3400_OBJS += ${arch_news3400_sio_OBJS}

arch_news3400_OBJS :=

clean::
