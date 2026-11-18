# current file directory
MAKEFILE_DIR_project_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
.DEFAULT_GOAL = all

# ---------------------------------------------------------------------------
# BSD45 kernel kbuild-style build (tc216 Makefile/config.mk style).
#
# Toolchain flow (matches the Windows build_kernel.ps1 "PE" mode, now on
# Ubuntu with the MinGW i686 cross toolchain):
#   gcc   -> PE/COFF object files (.o)
#   ld    -> link objects into kernel.pe  (i386pe, two passes for _end etc.)
#   objcopy -> convert kernel.pe to bsd45.elf (elf32-i386)
#
# Each source subdirectory contributes its objects through its own config.mk;
# objects are placed in build/obj/<subdir>/ by directory name.
# `make` alone performs a self-contained build (headers + assym + embed.c
# are produced by the prepare target).
# ---------------------------------------------------------------------------

# build options
ARCH ?= i386
DEBUG ?= n

# MinGW i686 cross toolchain: gcc emits PE/COFF, ld speaks i386pe,
# objcopy converts PE -> ELF32.
CROSS ?= i686-w64-mingw32-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
AR      := $(CROSS)ar

# build paths (shared with build_kernel.ps1/.sh)
BUILD_DIR ?= $(abspath $(MAKEFILE_DIR_project_mk)/../build)
OBJ_DIR   ?= $(BUILD_DIR)/obj
INC_DIR   := $(BUILD_DIR)/inc
GEN_DIR   := $(BUILD_DIR)/gen
KERNEL_PE := $(BUILD_DIR)/kernel.pe
KERNEL_ELF:= $(BUILD_DIR)/bsd45.elf
$(shell mkdir -p $(OBJ_DIR))

# flags (freestanding 32-bit i386; -fleading-underscore for PE/COFF nameing)
CPPFLAGS += -nostdinc \
    -I$(BUILD_DIR) -I$(INC_DIR) -I$(GEN_DIR) \
    -I$(MAKEFILE_DIR_project_mk) \
    -I$(MAKEFILE_DIR_project_mk)/include \
    -I$(MAKEFILE_DIR_project_mk)/include/sys \
    -I$(MAKEFILE_DIR_project_mk)/include/machine \
    -I$(MAKEFILE_DIR_project_mk)/include/i386 \
    -I$(MAKEFILE_DIR_project_mk)/arch \
    -I$(MAKEFILE_DIR_project_mk)/arch/$(ARCH) \
    -I$(MAKEFILE_DIR_project_mk)/fs \
    -I$(MAKEFILE_DIR_project_mk)/driver \
    -I$(MAKEFILE_DIR_project_mk)/net \
    -I$(MAKEFILE_DIR_project_mk)/include/sys \
    -isystem /usr/lib/gcc/i686-w64-mingw32/13-win32/include

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

ASMFLAGS := -m32 -c -x assembler-with-cpp -nostdinc -DKERNEL -DLOCORE

# ---------------------------------------------------------------------------
define PREPARE_SCRIPT
#!/usr/bin/env bash
# prepare.sh - stage headers and generate build inputs for the kbuild-style
# kernel/Makefile. Mirrors build_kernel.sh header/assym/embed steps so that
# "make" alone is a self-contained build.
set -u
ws="$(abspath $(MAKEFILE_DIR_project_mk)/..)"
src="$$ws/kernel"
bld="$$ws/build"
obj="$$bld/obj"
inc="$$bld/inc"
gen="$$bld/gen"
mkdir -p "$$bld" "$$obj" "$$gen" "$$inc/sys" "$$inc/vm" "$$inc/machine" \
         "$$inc/i386" "$$inc/lib" "$$inc/libkern"

# ---- staged headers (same as build_kernel.sh) ----
cp -f "$$src/include/sys/"*.h  "$$inc/sys/"     2>/dev/null || true
cp -f "$$src/vm/"*.h           "$$inc/vm/"      2>/dev/null || true
cp -f "$$src/include/machine/"*.h "$$inc/machine/" 2>/dev/null || true
cp -f "$$src/include/machine/"*.h "$$inc/i386/"    2>/dev/null || true
cp -f "$$src/lib/libkern.h" "$$inc/lib/libkern.h"  2>/dev/null || true
cp -f "$$src/lib/libkern.h" "$$inc/libkern/libkern.h" 2>/dev/null || true

if ! grep -q "typedef int register_t;" "$$inc/machine/types.h" 2>/dev/null; then
    printf "\ntypedef int register_t;\ntypedef unsigned int u_register_t;\n" >> "$$inc/machine/types.h"
fi

cat > "$$inc/string.h" <<"EOF"
#ifndef _STRING_H_
#define _STRING_H_
#include <sys/types.h>
unsigned int strlen(const char *);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, unsigned int);
int bcmp(const void *, const void *, unsigned int);
void bcopy(const void *, void *, unsigned int);
void bzero(void *, unsigned int);
void *memcpy(void *, const void *, unsigned int);
void *memmove(void *, const void *, unsigned int);
void *memset(void *, int, unsigned int);
#endif
EOF

cat > "$$inc/machine/endian.h" <<"EOF"
#ifndef _MACHINE_ENDIAN_H_
#define _MACHINE_ENDIAN_H_
#define _LITTLE_ENDIAN 1234
#define _BIG_ENDIAN 4321
#define _PDP_ENDIAN 3412
#define _BYTE_ORDER _LITTLE_ENDIAN
#define LITTLE_ENDIAN _LITTLE_ENDIAN
#define BIG_ENDIAN _BIG_ENDIAN
#define PDP_ENDIAN _PDP_ENDIAN
#define BYTE_ORDER _BYTE_ORDER
#endif
EOF

cat > "$$inc/machine/stdarg.h" <<"EOF"
#ifndef _MACHINE_STDARG_H_
#define _MACHINE_STDARG_H_
#include <stdarg.h>
#endif
EOF

printf "#define NISA 0\n" > "$$gen/isa.h"
printf "#define NCOM 0\n" > "$$gen/com.h"
printf "#define NPX 0\n"  > "$$gen/npx.h"
printf "#define NWD 0\n"  > "$$gen/wd.h"
printf "#define NFD 0\n"  > "$$gen/fd.h"

# ---- assym.s via host genassym (native gcc -m32) ----
echo "[prepare] genassym"
gcc -m32 -O1 -std=gnu89 -fno-strict-aliasing -mno-ms-bitfields \
    -I "$$inc" -DKERNEL -o "$$bld/genassym" "$$src/arch/i386/i386/genassym.c" 2>/dev/null
"$$bld/genassym" > "$$bld/assym.s"

cat > "$$bld/vector.s" <<"EOF"
/* vector.s - no ISA drivers attached; stubs live in i386/isa/icu.s */
EOF

# ---- embed.c: userland + boot stages as C byte arrays ----
echo "[prepare] embed.c"
append_bytes() {
    local cname="$$1" p="$$2"
    echo "const unsigned char $${cname}[] = {"
    if [ -f "$$p" ]; then
        od -An -v -tx1 "$$p" | awk "{ for (i=1;i<=NF;i++) printf \"0x%s,\", \$$i; printf \"\\n\" }"
        echo "};"
        echo "const unsigned int $${cname}_len = $$(stat -c%s "$$p" 2>/dev/null || wc -c < "$$p");"
    else
        echo "};"
        echo "const unsigned int $${cname}_len = 0;"
    fi
}
build_raw_stage() {
    local s="$$1" text="$$2" name="$$3" entry="$$4"
    local o="$$bld/$$name.embed.o" e="$$bld/$$name.embed.elf" raw="$$bld/$$name.embed.bin"
    gcc -m32 -c -x assembler-with-cpp -o "$$o" "$$s" || return 1
    ld -m elf_i386 -Ttext "$$text" -e "$$entry" -o "$$e" "$$o" || return 1
    objcopy -O binary --only-section=.text "$$e" "$$raw" || return 1
    echo "$$raw"
}
s1h="$$(build_raw_stage "$$ws/boot/i386/stage1_hdd.s" 0x7C00 stage1_hdd _start)"
s2="$$(build_raw_stage "$$ws/boot/i386/stage2.s" 0x90000 stage2 _start32)"
{
    echo "/* generated by kernel/Makefile prepare target */"
    for name in hello init; do append_bytes "$${name}_elf" "$$ws/out/userland/$$name.elf"; done
    append_bytes "stage1_hdd_bin" "$$s1h"
    append_bytes "stage2_bin" "$$s2"
} > "$$bld/embed.c"
echo "[prepare] done"

endef

# self-contained preparation: staged headers, assym.s, embed.c
# ---------------------------------------------------------------------------
.PHONY: prepare
prepare:
	@echo "---------------------[prepare]---------------------------------------"
	$(file >$(BUILD_DIR)/.prepare.gen.sh,$(PREPARE_SCRIPT))
	@bash $(BUILD_DIR)/.prepare.gen.sh

# ---------------------------------------------------------------------------
# include kernel module config.mk files (each dir contributes *_OBJS)
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

# embed.o is generated into build/ by the prepare target
EMBED_OBJ := $(OBJ_DIR)/embed.o
OBJS += $(EMBED_OBJ)

# strip duplicate items
OBJS := $(sort $(OBJS))

# all generated objects depend on prepare
$(OBJS): | prepare

$(EMBED_OBJ): $(BUILD_DIR)/embed.c
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(CFLAGS) -c $(CPPFLAGS) $< -o $@

# ---------------------------------------------------------------------------
# targets: PE link (two passes for _end/_edata/_etext) then objcopy to ELF
# ---------------------------------------------------------------------------
all: $(KERNEL_ELF)

LDFLAGS := -m i386pe --image-base 0xFE000000 -Ttext 0xFE001000 \
    --subsystem console -e start --gc-sections --disable-reloc-section \
    --file-alignment 0x1000 --section-alignment 0x1000

# pass 1: discover the section layout with harmless symbols
$(KERNEL_PE): $(OBJS)
	@echo "---------------------[link PE pass1 $@]------------------------------"
	$(LD) $(LDFLAGS) --defsym _end=0xFE080000 --defsym _edata=0xFE040000 \
	    --defsym _etext=0xFE040000 -o $@ $(OBJS) 2>&1 | head -40
	@END=$$($(OBJDUMP) -h $@ | awk "/^ *[0-9]+ *\.(text|data|bss)/ { \
	    sz=strtonum(\"0x\"\$$3); va=strtonum(\"0x\"\$$4); \
	    if (\$$2==\".text\") t=va+sz; \
	    if (\$$2==\".bss\")  d=va; \
	    e=va+sz; if (e>m) m=e } END { \
	    end=int((m+4095)/4096)*4096; \
	    if (t==0) t=4261562368; if (d==0) d=m; \
	    printf \"%x %x %x\", end, d, t }"); \
	set -- $$END; \
	echo "  _end=0x$$1 _edata=0x$$2 _etext=0x$$3"; \
	echo "---------------------[link PE pass2 $@]------------------------------"; \
	$(LD) $(LDFLAGS) --defsym _end=0x$$1 --defsym _edata=0x$$2 \
	    --defsym _etext=0x$$3 -o $@ $(OBJS) 2>&1 | head -40

$(KERNEL_ELF): $(KERNEL_PE)
	@echo "---------------------[objcopy -> ELF $@]-----------------------------"
	$(OBJCOPY) -O elf32-i386 -R .idata -R .reloc $< $@
	@file $@

clean::
	rm -f $(OBJS) $(KERNEL_PE) $(KERNEL_ELF)
