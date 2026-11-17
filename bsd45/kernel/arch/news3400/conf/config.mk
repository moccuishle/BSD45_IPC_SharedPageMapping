# config.mk - kernel/arch/news3400/conf
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_arch_news3400_conf_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_news3400_conf_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_news3400_conf_mk))
$(shell mkdir -p ${arch_news3400_conf_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_news3400_conf_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# (empty placeholder directory)

arch_news3400_conf_OBJS :=

clean::
