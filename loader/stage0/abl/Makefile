################################################
# Copyright (c) 2015-2019 Intel Corporation.
# All rights reserved.
#
# SPDX-License-Identidfier: Apache-2.0
#
################################################

TARGET = stage0.elf

CSOURCES = stage0.c abl_boot_param.c
ASOURCES = stage0_entry.S

include $(PROJS)/rule.linux

OBJS = $(OUTDIR)/stage0_entry.o \
	$(OUTDIR)/stage0.o \
	$(OUTDIR)/stage0_lib.o \
	$(OUTDIR)/abl_boot_param.o \
	$(LIB_DIR)/elf_ld.o \
	$(LIB_DIR)/elf32_ld.o \
	$(LIB_DIR)/elf64_ld.o \
	$(LIB_DIR)/util.o \
	$(LIB_DIR)/string.o \
	$(LIB_DIR)/sprintf.o

ifneq (, $(findstring -DLIB_PCI, $(EVMM_CMPL_FLAGS)))
OBJS += $(LIB_DIR)/pci.o
endif

ifneq (, $(findstring -DLIB_PRINT, $(EVMM_CMPL_FLAGS)))
OBJS += $(LIB_DIR)/print.o
OBJS += $(LIB_DIR)/serial.o
endif

ifneq (, $(findstring -DDEBUG, $(EVMM_CMPL_FLAGS)))
LDFLAGS = -T linker.lds -z max-page-size=4096 -z common-page-size=4096
else
LDFLAGS = -T linker.lds -s -z max-page-size=4096 -z common-page-size=4096
endif

all: $(TARGET) stage0.bin

$(TARGET):
	$(LD) $(LDFLAGS) -o $(OUTDIR)/$@ $(OBJS) $(wildcard $(BUILD_DIR)common/*.o)

stage0.bin: $(TARGET)
	objcopy -j .text -O binary -S $(OUTDIR)/$(TARGET) $(BUILD_DIR)stage0.bin
