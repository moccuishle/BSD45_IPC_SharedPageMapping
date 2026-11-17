# config.mk - kernel/net/netiso/xebec/test
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_net_netiso_xebec_test_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
net_netiso_xebec_test_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_net_netiso_xebec_test_mk))
$(shell mkdir -p ${net_netiso_xebec_test_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_net_netiso_xebec_test_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# (empty placeholder directory)

net_netiso_xebec_test_OBJS :=

clean::
