# config.mk - kernel/net/netns
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_net_netns_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
net_netns_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_net_netns_mk))
$(shell mkdir -p ${net_netns_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_net_netns_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# (empty placeholder directory)

net_netns_OBJS :=

clean::
