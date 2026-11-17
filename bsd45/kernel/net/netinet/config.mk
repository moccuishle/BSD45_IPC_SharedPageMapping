# config.mk - kernel/net/netinet
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_net_netinet_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
net_netinet_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_net_netinet_mk))
$(shell mkdir -p ${net_netinet_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_net_netinet_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# net_netinet_CSRCS := if_ether.c igmp.c in.c in_cksum.c in_pcb.c in_proto.c ip_icmp.c ip_input.c ip_mroute.c ip_output.c raw_ip.c tcp_debug.c tcp_input.c tcp_output.c tcp_subr.c tcp_timer.c tcp_usrreq.c udp_usrreq.c
# net_netinet_COBJS := $(patsubst %.c,$(net_netinet_OBJ_DIR)/%.o,$(net_netinet_CSRCS))
# headers: icmp_var.h if_ether.h igmp.h igmp_var.h in.h in_pcb.h in_systm.h in_var.h ip.h ip_icmp.h ip_mroute.h ip_var.h tcp.h tcp_debug.h tcp_fsm.h tcp_seq.h tcp_timer.h tcp_var.h tcpip.h udp.h udp_var.h

net_netinet_OBJS :=

clean::
