# config.mk - kernel/net
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_net_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
net_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_net_mk))
$(shell mkdir -p ${net_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_net_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: net/ netccitt/ netinet/ netiso/ netns/
# include $(MAKEFILE_DIR_project_mk)/net/net/config.mk
# net_OBJS += ${net_net_OBJS}
# include $(MAKEFILE_DIR_project_mk)/net/netccitt/config.mk
# net_OBJS += ${net_netccitt_OBJS}
# include $(MAKEFILE_DIR_project_mk)/net/netinet/config.mk
# net_OBJS += ${net_netinet_OBJS}
# include $(MAKEFILE_DIR_project_mk)/net/netiso/config.mk
# net_OBJS += ${net_netiso_OBJS}
# include $(MAKEFILE_DIR_project_mk)/net/netns/config.mk
# net_OBJS += ${net_netns_OBJS}

net_OBJS :=

clean::
