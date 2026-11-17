# config.mk - kernel/fs/ffs
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# THIS DIRECTORY PARTICIPATES IN THE KERNEL BUILD.
#
# current file directory
MAKEFILE_DIR_fs_ffs_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
fs_ffs_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_fs_ffs_mk))
$(shell mkdir -p ${fs_ffs_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_fs_ffs_mk)

# C sources (explicit; 4.5BSD tree has legacy files that must not build)
fs_ffs_CSRCS := ffs.c install.c
fs_ffs_COBJS := $(patsubst %.c,$(fs_ffs_OBJ_DIR)/%.o,$(fs_ffs_CSRCS))
fs_ffs_ASMSRC := 
fs_ffs_ASMOBJS := $(patsubst %.s,$(fs_ffs_OBJ_DIR)/%.o,$(patsubst %.asm,$(fs_ffs_OBJ_DIR)/%.o,$(fs_ffs_ASMSRC)))

fs_ffs_OBJS := $(fs_ffs_COBJS) $(fs_ffs_ASMOBJS)

$(fs_ffs_COBJS):$(OBJ_DIR)/%.o:%.c
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(CFLAGS) -c $(CPPFLAGS) $< -o $@

clean::
