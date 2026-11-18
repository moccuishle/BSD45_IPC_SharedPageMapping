# config.mk - kernel/arch/i386/isa
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# THIS DIRECTORY PARTICIPATES IN THE KERNEL BUILD.
#
# current file directory
MAKEFILE_DIR_arch_i386_isa_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_i386_isa_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_i386_isa_mk))
$(shell mkdir -p ${arch_i386_isa_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_i386_isa_mk)

# C sources (explicit; 4.5BSD tree has legacy files that must not build)
arch_i386_isa_CSRCS := pmap.c machdep.c
arch_i386_isa_COBJS := $(patsubst %.c,$(arch_i386_isa_OBJ_DIR)/%.o,$(arch_i386_isa_CSRCS))
arch_i386_isa_ASMSRC := locore.s
arch_i386_isa_ASMOBJS := $(patsubst %.s,$(arch_i386_isa_OBJ_DIR)/%.o,$(patsubst %.asm,$(arch_i386_isa_OBJ_DIR)/%.o,$(arch_i386_isa_ASMSRC)))

arch_i386_isa_OBJS := $(arch_i386_isa_COBJS) $(arch_i386_isa_ASMOBJS)

$(arch_i386_isa_COBJS):$(OBJ_DIR)/%.o:%.c
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(CFLAGS) -c $(CPPFLAGS) $< -o $@

$(arch_i386_isa_ASMOBJS):$(OBJ_DIR)/%.o:%.s
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(ASMFLAGS) $(CPPFLAGS) $< -o $@

clean::
