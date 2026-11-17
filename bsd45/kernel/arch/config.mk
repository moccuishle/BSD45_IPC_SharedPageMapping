# config.mk - kernel/arch
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# THIS DIRECTORY PARTICIPATES IN THE KERNEL BUILD.
#
# current file directory
MAKEFILE_DIR_arch_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_mk))
$(shell mkdir -p ${arch_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_mk)

# C sources (explicit; 4.5BSD tree has legacy files that must not build)
arch_CSRCS := 
arch_COBJS := $(patsubst %.c,$(arch_OBJ_DIR)/%.o,$(arch_CSRCS))
arch_ASMSRC := 
arch_ASMOBJS := $(patsubst %.s,$(arch_OBJ_DIR)/%.o,$(patsubst %.asm,$(arch_OBJ_DIR)/%.o,$(arch_ASMSRC)))

arch_OBJS := $(arch_COBJS) $(arch_ASMOBJS)

include $(MAKEFILE_DIR_project_mk)/arch/i386/config.mk
arch_OBJS += ${arch_i386_OBJS}

$(arch_COBJS):$(OBJ_DIR)/%.o:%.c
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(CFLAGS) -c $(CPPFLAGS) $< -o $@

clean::
