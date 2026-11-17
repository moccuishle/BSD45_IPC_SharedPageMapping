# config.mk - kernel/net/netiso
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_net_netiso_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
net_netiso_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_net_netiso_mk))
$(shell mkdir -p ${net_netiso_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_net_netiso_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# net_netiso_CSRCS := clnp_debug.c clnp_er.c clnp_frag.c clnp_input.c clnp_options.c clnp_output.c clnp_raw.c clnp_subr.c clnp_timer.c cltp_usrreq.c esis.c idrp_usrreq.c if_cons.c if_eon.c iso.c iso_chksum.c iso_pcb.c iso_proto.c iso_snpac.c tp_astring.c tp_cons.c tp_driver.c tp_emit.c tp_inet.c tp_input.c tp_iso.c tp_meas.c tp_output.c tp_pcb.c tp_subr.c tp_subr2.c tp_timer.c tp_trace.c tp_usrreq.c tuba_subr.c tuba_table.c tuba_usrreq.c
# net_netiso_COBJS := $(patsubst %.c,$(net_netiso_OBJ_DIR)/%.o,$(net_netiso_CSRCS))
# headers: argo_debug.h clnl.h clnp.h clnp_stat.h cltp_var.h cons.h cons_pcb.h eonvar.h esis.h iso.h iso_errno.h iso_pcb.h iso_snpac.h iso_var.h tp_clnp.h tp_events.h tp_ip.h tp_meas.h tp_param.h tp_pcb.h tp_seq.h tp_stat.h tp_states.h tp_timer.h tp_tpdu.h tp_trace.h tp_user.h tuba_table.h
# subdirectories: xebec/
# include $(MAKEFILE_DIR_project_mk)/net/netiso/xebec/config.mk
# net_netiso_OBJS += ${net_netiso_xebec_OBJS}

net_netiso_OBJS :=

clean::
