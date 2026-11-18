# config.mk - kernel/thread
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# THIS DIRECTORY PARTICIPATES IN THE KERNEL BUILD.
#
# current file directory
MAKEFILE_DIR_thread_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
thread_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_thread_mk))
$(shell mkdir -p ${thread_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_thread_mk)

# C sources (explicit; 4.5BSD tree has legacy files that must not build)
thread_CSRCS := kthread.c softirq.c
thread_COBJS := $(patsubst %.c,$(thread_OBJ_DIR)/%.o,$(thread_CSRCS))
thread_ASMSRC := kswitch.s
thread_ASMOBJS := $(patsubst %.s,$(thread_OBJ_DIR)/%.o,$(patsubst %.asm,$(thread_OBJ_DIR)/%.o,$(thread_ASMSRC)))

thread_OBJS := $(thread_COBJS) $(thread_ASMOBJS)

$(thread_COBJS):$(OBJ_DIR)/%.o:%.c
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(CFLAGS) -c $(CPPFLAGS) $< -o $@

$(thread_ASMOBJS):$(OBJ_DIR)/%.o:%.s
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(ASMFLAGS) $(CPPFLAGS) $< -o $@

clean::
