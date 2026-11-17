# config.mk - kernel/fs/nfs
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_fs_nfs_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
fs_nfs_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_fs_nfs_mk))
$(shell mkdir -p ${fs_nfs_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_fs_nfs_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# fs_nfs_CSRCS := nfs_bio.c nfs_node.c nfs_nqlease.c nfs_serv.c nfs_socket.c nfs_srvcache.c nfs_subs.c nfs_syscalls.c nfs_vfsops.c nfs_vnops.c
# fs_nfs_COBJS := $(patsubst %.c,$(fs_nfs_OBJ_DIR)/%.o,$(fs_nfs_CSRCS))
# headers: nfs.h nfsdiskless.h nfsm_subs.h nfsmount.h nfsnode.h nfsproto.h nfsrtt.h nfsrvcache.h nqnfs.h rpcv2.h xdr_subs.h

fs_nfs_OBJS :=

clean::
