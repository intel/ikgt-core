################################################
# Copyright (c) 2015-2019 Intel Corporation.
# All rights reserved.
#
# SPDX-License-Identidfier: Apache-2.0
#
################################################

export OUTDIR = $(BUILD_DIR)vmm

$(shell mkdir -p $(OUTDIR))

LIB_DIR = $(BUILD_DIR)lib

TARGET = evmm.elf

CSOURCES = main.c

SUBDIRS = guest host \
	utils vmexit vmx modules

INCLUDES = -I. \
	-I$(PROJS)/include \
	-I$(PROJS)/vmm/include \

AINCLUDES += -I$(PROJS)/vmm/include

CFLAGS += $(INCLUDES)

ifneq (, $(findstring -DSTACK_PROFILE, $(EVMM_CMPL_FLAGS)))
CFLAGS += -finstrument-functions
endif

AFLAGS += $(AINCLUDES)

include $(PROJS)/rule.linux

LDFLAGS = -e vmm_main -pie -z max-page-size=4096

ifeq (, $(findstring -DDEBUG, $(EVMM_CMPL_FLAGS)))
LDFLAGS += -s
endif

# Addtional flags for GNU ld
ifeq (GNU ld, $(findstring GNU ld, $(LD_VERSION)))
LDFLAGS += -z common-page-size=4096
endif

all: $(TARGET)

$(TARGET): $(SUBDIRS)
	$(LD) $(LDFLAGS) -o $(BUILD_DIR)$@ $(wildcard $(OUTDIR)/*.o) $(wildcard $(LIB_DIR)/*.o)
