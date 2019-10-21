################################################
# Copyright (c) 2015-2019 Intel Corporation.
# All rights reserved.
#
# SPDX-License-Identidfier: Apache-2.0
#
################################################

CSOURCES = entry.c
ASOURCES = entry_64.S

include $(PROJS)/rule.linux

OBJS = $(OUTDIR)/entry_64.o \
	$(OUTDIR)/entry.o \
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

TARGET = entry.elf

LDFLAGS = -T linker.lds -z max-page-size=4096 -z noexecstack

ifeq (, $(findstring -DDEBUG, $(EVMM_CMPL_FLAGS)))
LDFLAGS += -s
endif

# Addtional flags for GNU ld
ifeq (GNU ld, $(findstring GNU ld, $(LD_VERSION)))
LDFLAGS += -z common-page-size=4096
endif

all: $(TARGET) entry.bin

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $(OUTDIR)/$@ $(OBJS) $(wildcard $(BUILD_DIR)common/*.o)

entry.bin: $(TARGET)
	objcopy -j .text -O binary -S $(OUTDIR)/$(TARGET) $(BUILD_DIR)/entry.bin
