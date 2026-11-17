# current file directory
MAKEFILE_DIR_project_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
.DEFAULT_GOAL = all

# ---------------------------------------------------------------------------
# BSD45 kernel kbuild-style build (tc216 Makefile/config.mk style).
# The authoritative driver remains build_kernel.ps1 / build_kernel.sh; this
# Makefile mirrors the same source set using recursive config.mk files, so
# `make` and the scripts build identical object lists.
# ---------------------------------------------------------------------------

# build options
ARCH ?= i386
DEBUG ?= n

# toolchain (cross i386; the port uses gcc -m32 on x86_64 hosts)
CROSS ?=
CC   := $(CROSS)gcc
LD   := $(CROSS)ld
AR   := $(CROSS)ar
OBJCOPY := $(CROSS)objcopy

# build path: the port stages headers and objects in bsd45/build (one level
# above kernel/), shared with build_kernel.ps1/.sh.
BUILD_DIR ?= $(abspath $(MAKEFILE_DIR_project_mk)/../build)
OBJ_DIR   ?= $(BUILD_DIR)/obj
$(shell test -e $(OBJ_DIR) || mkdir -p $(OBJ_DIR))

# flags (match build_kernel.sh: freestanding 32-bit i386)
# The port's headers are staged into build/inc and build/gen by build_sh.sh;
# make those the primary include roots (same as build_kernel.sh -I order).
CPPFLAGS += -nostdinc \
    -I$(BUILD_DIR)/inc \
    -I$(BUILD_DIR)/gen \
    -I$(MAKEFILE_DIR_project_mk) \
    -I$(MAKEFILE_DIR_project_mk)/include \
    -I$(MAKEFILE_DIR_project_mk)/include/sys \
    -I$(MAKEFILE_DIR_project_mk)/include/machine \
    -I$(MAKEFILE_DIR_project_mk)/include/i386 \
    -I$(MAKEFILE_DIR_project_mk)/arch \
    -I$(MAKEFILE_DIR_project_mk)/arch/$(ARCH)

CFLAGS += -m32 -std=gnu89 -O2 -ffreestanding -fleading-underscore \
    -fno-pie -fno-pic -fcommon -fno-builtin -fno-stack-protector \
    -fno-asynchronous-unwind-tables -fno-unwind-tables \
    -fno-tree-loop-distribute-patterns -march=i386 \
    -mno-sse -mno-sse2 -mno-mmx -mno-avx -mno-ms-bitfields \
    -mpreferred-stack-boundary=2 -ffunction-sections -fdata-sections \
    -DKERNEL -DARCH_$(ARCH)

ifeq (${DEBUG}, y)
CFLAGS += -O0 -g
endif

# ---------------------------------------------------------------------------
# include kernel module config.mk files (tc216 style: include + OBJS +=)
# Only directories that actually build into the kernel are included.
# ---------------------------------------------------------------------------
include $(MAKEFILE_DIR_project_mk)/lib/config.mk
OBJS += $(lib_OBJS)

include $(MAKEFILE_DIR_project_mk)/sys/config.mk
OBJS += $(sys_OBJS)

include $(MAKEFILE_DIR_project_mk)/thread/config.mk
OBJS += $(thread_OBJS)

include $(MAKEFILE_DIR_project_mk)/fs/config.mk
OBJS += $(fs_OBJS)

include $(MAKEFILE_DIR_project_mk)/driver/config.mk
OBJS += $(driver_OBJS)

include $(MAKEFILE_DIR_project_mk)/vm/config.mk
OBJS += $(vm_OBJS)

include $(MAKEFILE_DIR_project_mk)/arch/config.mk
OBJS += $(arch_OBJS)

# strip duplicate items (tc216 style)
OBJS := $(shell echo "${OBJS}" | tr ' ' '\n' | awk '!a[$$0]++' | tr '\n' ' ')

# ---------------------------------------------------------------------------
# targets
# ---------------------------------------------------------------------------
KERNEL_ELF := $(BUILD_DIR)/bsd45.elf

all: $(KERNEL_ELF)

$(KERNEL_ELF): $(OBJS)
	@echo "---------------------[link $@]----------------------------------"
	$(LD) -m elf_i386 -T $(MAKEFILE_DIR_project_mk)/../boot/i386/kernel.ld \
	    -e start --gc-sections -o $@ $(OBJS) $(shell gcc -m32 -print-libgcc-file-name)

# clean removes ONLY make-produced objects, not build/inc + build/gen which
# are staged by build_sh.sh / build_kernel.sh and required for compilation.
.PHONY : clean
clean::
	rm -f $(OBJS) $(KERNEL_ELF)
