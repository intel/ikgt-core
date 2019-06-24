################################################
# Copyright (c) 2015-2019 Intel Corporation.
# All rights reserved.
#
# SPDX-License-Identidfier: Apache-2.0
#
################################################

export OUTDIR = $(BUILD_DIR)stage1

$(shell mkdir -p $(OUTDIR))

TARGET = stage1.bin

CSOURCES = $(wildcard *.c)
ASOURCES = $(wildcard *.S)

include $(PROJS)/rule.linux

LDFLAGS = -e stage1_main -m elf_x86_64 -pie -z max-page-size=4096 -z noexecstack

ifeq (, $(findstring -DDEBUG, $(EVMM_CMPL_FLAGS)))
LDFLAGS += -s
endif

# Addtional flags for GNU ld
ifeq (GNU ld, $(findstring GNU ld, $(LD_VERSION)))
LDFLAGS += -z common-page-size=4096
endif

all: $(TARGET)

$(TARGET):
	$(LD) $(LDFLAGS) -o $(BUILD_DIR)$@ $(wildcard $(OUTDIR)/*.o) $(wildcard $(LIB_DIR)/*.o) $(wildcard $(BUILD_DIR)common/*.o)
