# config.mk - kernel/fs
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# THIS DIRECTORY PARTICIPATES IN THE KERNEL BUILD.
#
# current file directory
MAKEFILE_DIR_fs_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
fs_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_fs_mk))
$(shell mkdir -p ${fs_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_fs_mk)

# C sources (explicit; 4.5BSD tree has legacy files that must not build)
fs_CSRCS := isofs.c tmpfs.c aio.c vfs_bio.c vfs_cache.c vfs.c
fs_COBJS := $(patsubst %.c,$(fs_OBJ_DIR)/%.o,$(fs_CSRCS))
fs_ASMSRC := 
fs_ASMOBJS := $(patsubst %.s,$(fs_OBJ_DIR)/%.o,$(patsubst %.asm,$(fs_OBJ_DIR)/%.o,$(fs_ASMSRC)))

fs_OBJS := $(fs_COBJS) $(fs_ASMOBJS)

include $(MAKEFILE_DIR_project_mk)/fs/ffs/config.mk
fs_OBJS += ${fs_ffs_OBJS}

$(fs_COBJS):$(OBJ_DIR)/%.o:%.c
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(CFLAGS) -c $(CPPFLAGS) $< -o $@

clean::
