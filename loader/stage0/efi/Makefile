################################################
# Copyright (c) 2015-2019 Intel Corporation.
# All rights reserved.
#
# SPDX-License-Identidfier: Apache-2.0
#
################################################

CSOURCES = stage0.c efi_boot_param.c

include $(PROJS)/rule.linux

OBJS =  $(OUTDIR)/stage0.o \
	$(OUTDIR)/stage0_lib.o \
	$(OUTDIR)/efi_boot_param.o \
	$(LIB_DIR)/elf_ld.o \
	$(LIB_DIR)/elf32_ld.o \
	$(LIB_DIR)/elf64_ld.o \
	$(LIB_DIR)/efi_services.o \
	$(LIB_DIR)/efi_conf_table.o \
	$(LIB_DIR)/efi_mp.o \
	$(LIB_DIR)/efi_ap.o \
	$(LIB_DIR)/util.o \
	$(LIB_DIR)/string.o

ifneq (, $(findstring -DLIB_PCI, $(EVMM_CMPL_FLAGS)))
OBJS += $(LIB_DIR)/pci.o
endif

ifneq (, $(findstring -DLIB_PRINT, $(EVMM_CMPL_FLAGS)))
OBJS += $(LIB_DIR)/sprintf.o
OBJS += $(LIB_DIR)/print.o
OBJS += $(LIB_DIR)/serial.o
endif

TARGET = stage0.elf

LDFLAGS = -e stage0_main -m elf_x86_64 -pie -z max-page-size=4096 -z noexecstack

ifeq (, $(findstring -DDEBUG, $(EVMM_CMPL_FLAGS)))
LDFLAGS += -s
endif

# Addtional flags for GNU ld
ifeq (GNU ld, $(findstring GNU ld, $(LD_VERSION)))
LDFLAGS += -z common-page-size=4096
endif

all: $(TARGET) stage0.bin entry

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $(OUTDIR)/$@ $(OBJS) $(wildcard $(BUILD_DIR)common/*.o)

stage0.bin: $(TARGET) entry
	cat $(BUILD_DIR)/entry.bin $(OUTDIR)/$(TARGET) > $(BUILD_DIR)/$@

entry:
	$(MAKE) -C ../$@
