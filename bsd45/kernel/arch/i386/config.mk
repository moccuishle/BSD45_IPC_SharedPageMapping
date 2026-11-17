# config.mk - kernel/arch/i386
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# THIS DIRECTORY PARTICIPATES IN THE KERNEL BUILD.
#
# current file directory
MAKEFILE_DIR_arch_i386_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
arch_i386_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_arch_i386_mk))
$(shell mkdir -p ${arch_i386_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_arch_i386_mk)

# C sources (explicit; 4.5BSD tree has legacy files that must not build)
arch_i386_CSRCS := smp.c ctx.c timer.c
arch_i386_COBJS := $(patsubst %.c,$(arch_i386_OBJ_DIR)/%.o,$(arch_i386_CSRCS))
arch_i386_ASMSRC := x86.asm
arch_i386_ASMOBJS := $(patsubst %.s,$(arch_i386_OBJ_DIR)/%.o,$(patsubst %.asm,$(arch_i386_OBJ_DIR)/%.o,$(arch_i386_ASMSRC)))

arch_i386_OBJS := $(arch_i386_COBJS) $(arch_i386_ASMOBJS)

include $(MAKEFILE_DIR_project_mk)/arch/i386/i386/config.mk
arch_i386_OBJS += ${arch_i386_i386_OBJS}

include $(MAKEFILE_DIR_project_mk)/arch/i386/isa/config.mk
arch_i386_OBJS += ${arch_i386_isa_OBJS}

$(arch_i386_COBJS):$(OBJ_DIR)/%.o:%.c
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(CFLAGS) -c $(CPPFLAGS) $< -o $@

$(arch_i386_ASMOBJS):$(OBJ_DIR)/%.o:%.s
	@echo ---------------------[build $<]----------------------------------
	$(CC) -m32 -c -x assembler-with-cpp -nostdinc $(CPPFLAGS) $< -o $@

clean::
