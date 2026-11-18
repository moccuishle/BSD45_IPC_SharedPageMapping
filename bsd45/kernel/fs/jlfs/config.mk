# config.mk - kernel/fs/jlfs
#
# kbuild-style per-directory config.  jlfs is the writable FFS (UFS1)
# filesystem: read/write/create/remove/mkdir/rmdir/truncate/rename.
MAKEFILE_DIR_jlfs_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
jlfs_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_jlfs_mk))
$(shell mkdir -p ${jlfs_OBJ_DIR})

CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_jlfs_mk)

jlfs_CSRCS := jlfs.c
jlfs_COBJS := $(patsubst %.c,$(jlfs_OBJ_DIR)/%.o,$(jlfs_CSRCS))
jlfs_ASMSRC :=
jlfs_ASMOBJS :=
jlfs_OBJS := $(jlfs_COBJS) $(jlfs_ASMOBJS)

$(jlfs_COBJS):$(OBJ_DIR)/%.o:%.c
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(CFLAGS) -c $(CPPFLAGS) $< -o $@

clean::
