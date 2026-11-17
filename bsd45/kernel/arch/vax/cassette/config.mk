# config.mk - kernel/arch/vax/cassette
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_arch_vax_cassette_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_vax_cassette_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_vax_cassette_mk))
$(shell mkdir -p ${arch_vax_cassette_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_vax_cassette_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: fromdec/
# include $(MAKEFILE_DIR_project_mk)/arch/vax/cassette/fromdec/config.mk
# arch_vax_cassette_OBJS += ${arch_vax_cassette_fromdec_OBJS}

arch_vax_cassette_OBJS :=

clean::
