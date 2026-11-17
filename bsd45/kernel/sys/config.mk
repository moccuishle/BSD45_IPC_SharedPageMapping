# config.mk - kernel/sys
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# THIS DIRECTORY PARTICIPATES IN THE KERNEL BUILD.
#
# current file directory
MAKEFILE_DIR_sys_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
sys_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_sys_mk))
$(shell mkdir -p ${sys_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_sys_mk)

# C sources (explicit; 4.5BSD tree has legacy files that must not build)
sys_CSRCS := usermode.c stubs.c glue.c mon.c
sys_COBJS := $(patsubst %.c,$(sys_OBJ_DIR)/%.o,$(sys_CSRCS))
sys_ASMSRC := usermode_asm.s
sys_ASMOBJS := $(patsubst %.s,$(sys_OBJ_DIR)/%.o,$(patsubst %.asm,$(sys_OBJ_DIR)/%.o,$(sys_ASMSRC)))

sys_OBJS := $(sys_COBJS) $(sys_ASMOBJS)

$(sys_COBJS):$(OBJ_DIR)/%.o:%.c
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(CFLAGS) -c $(CPPFLAGS) $< -o $@

$(sys_ASMOBJS):$(OBJ_DIR)/%.o:%.s
	@echo ---------------------[build $<]----------------------------------
	$(CC) -m32 -c -x assembler-with-cpp -nostdinc $(CPPFLAGS) $< -o $@

clean::
