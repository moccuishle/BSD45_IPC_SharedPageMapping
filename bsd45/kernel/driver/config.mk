# config.mk - kernel/driver
#
# kbuild-style per-directory config (tc216 Makefile/config.mk style).
# THIS DIRECTORY PARTICIPATES IN THE KERNEL BUILD.
#
# current file directory
MAKEFILE_DIR_driver_mk := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
driver_OBJ_DIR = $(patsubst ${MAKEFILE_DIR_project_mk}%,${OBJ_DIR}/%,$(MAKEFILE_DIR_driver_mk))
$(shell mkdir -p ${driver_OBJ_DIR})

# CPPFLAGS
CPPFLAGS := $(CPPFLAGS) -I$(MAKEFILE_DIR_driver_mk)

# C sources (explicit; 4.5BSD tree has legacy files that must not build)
driver_CSRCS := pccon.c atapi.c ata.c
driver_COBJS := $(patsubst %.c,$(driver_OBJ_DIR)/%.o,$(driver_CSRCS))
driver_ASMSRC := 
driver_ASMOBJS := $(patsubst %.s,$(driver_OBJ_DIR)/%.o,$(patsubst %.asm,$(driver_OBJ_DIR)/%.o,$(driver_ASMSRC)))

driver_OBJS := $(driver_COBJS) $(driver_ASMOBJS)

$(driver_COBJS):$(OBJ_DIR)/%.o:%.c
	@echo ---------------------[build $<]----------------------------------
	$(CC) $(CFLAGS) -c $(CPPFLAGS) $< -o $@

clean::
