# config.mk - kernel/net/net
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_net_net_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
net_net_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_net_net_mk))
$(shell mkdir -p ${net_net_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_net_net_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# net_net_CSRCS := bpf.c bpf_filter.c if.c if_ethersubr.c if_loop.c if_sl.c radix.c raw_cb.c raw_usrreq.c route.c rtsock.c slcompress.c
# net_net_COBJS := $(patsubst %.c,$(net_net_OBJ_DIR)/%.o,$(net_net_CSRCS))
# headers: bpf.h bpf_compat.h bpfdesc.h if.h if_arp.h if_dl.h if_llc.h if_slvar.h if_types.h netisr.h radix.h raw_cb.h route.h slcompress.h slip.h

net_net_OBJS :=

clean::
