# config.mk - kernel/lib
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# THIS DIRECTORY PARTICIPATES IN THE KERNEL BUILD.
#
# current file directory
MAKEFILE_DIR_lib_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
lib_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_lib_mk))
$(shell mkdir -p ${lib_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_lib_mk)

# C sources (explicit; 4.5BSD tree has legacy files that must not build)
lib_CSRCS := bcmp.c strcmp.c strlen.c strings.c kstr.c
lib_COBJS := $(patsubst %.c,$(lib_OBJ_DIR)/%.o,$(lib_CSRCS))
lib_ASMSRC := 
lib_ASMOBJS := $(patsubst %.s,$(lib_OBJ_DIR)/%.o,$(patsubst %.asm,$(lib_OBJ_DIR)/%.o,$(lib_ASMSRC)))

lib_OBJS := $(lib_COBJS) $(lib_ASMOBJS)

$(lib_COBJS):$(OBJ_DIR)/%.o:%.c
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(CFLAGS) -c $(CPPFLAGS) $< -o $@

clean::
