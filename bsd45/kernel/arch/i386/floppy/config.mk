# config.mk - kernel/arch/i386/floppy
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# This directory is NOT part of the current kernel build (legacy 4.5BSD
# tree / placeholder). Sources are recorded as COMMENTS for reference.
#
# current file directory
MAKEFILE_DIR_arch_i386_floppy_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_i386_floppy_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_i386_floppy_mk))
$(shell mkdir -p ${arch_i386_floppy_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_i386_floppy_mk)

# ---- NOT BUILT: source inventory (reference only) ----
# subdirectories: arp/ bad144/ bin/ cat/ cp/ dd/ disklabel/ dump/ ed/ fsck/ ftp/ ifconfig/ init/ ls/ mkdir/ mknod/ mount/ mv/ netstat/ newfs/ ping/ restore/ rm/ route/ rsh/ sh/ slattach/ stty/ sync/ tar/ telnet/ umount/
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/arp/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_arp_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/bad144/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_bad144_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/bin/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_bin_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/cat/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_cat_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/cp/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_cp_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/dd/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_dd_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/disklabel/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_disklabel_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/dump/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_dump_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/ed/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_ed_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/fsck/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_fsck_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/ftp/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_ftp_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/ifconfig/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_ifconfig_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/init/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_init_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/ls/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_ls_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/mkdir/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_mkdir_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/mknod/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_mknod_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/mount/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_mount_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/mv/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_mv_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/netstat/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_netstat_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/newfs/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_newfs_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/ping/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_ping_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/restore/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_restore_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/rm/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_rm_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/route/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_route_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/rsh/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_rsh_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/sh/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_sh_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/slattach/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_slattach_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/stty/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_stty_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/sync/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_sync_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/tar/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_tar_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/telnet/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_telnet_OBJS}
# include $(MAKEFILE_DIR_project_mk)/arch/i386/floppy/umount/config.mk
# arch_i386_floppy_OBJS += ${arch_i386_floppy_umount_OBJS}

arch_i386_floppy_OBJS :=

clean::
