# config.mk - kernel/vm
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# THIS DIRECTORY PARTICIPATES IN THE KERNEL BUILD.
#
# current file directory
MAKEFILE_DIR_vm_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
vm_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_vm_mk))
$(shell mkdir -p ${vm_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_vm_mk)

# C sources (explicit; 4.5BSD tree has legacy files that must not build)
vm_CSRCS := zcomp.c pagecache.c
vm_COBJS := $(patsubst %.c,$(vm_OBJ_DIR)/%.o,$(vm_CSRCS))
vm_ASMSRC := 
vm_ASMOBJS := $(patsubst %.s,$(vm_OBJ_DIR)/%.o,$(patsubst %.asm,$(vm_OBJ_DIR)/%.o,$(vm_ASMSRC)))

vm_OBJS := $(vm_COBJS) $(vm_ASMOBJS)

$(vm_COBJS):$(OBJ_DIR)/%.o:%.c
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(CFLAGS) -c $(CPPFLAGS) $< -o $@

clean::
