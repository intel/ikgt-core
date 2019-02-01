################################################
# Copyright (c) 2015-2019 Intel Corporation.
# All rights reserved.
#
# SPDX-License-Identidfier: Apache-2.0
#
################################################

export OUTDIR = $(BUILD_DIR)lib

$(shell mkdir -p $(OUTDIR))

SUBDIRS = util image_loader string

# we make lib which exsit in the EVMM_CMPL_FLAGS.
# for example, EVMM_CMPL_FLAGS has the -DLIB_PCI,
# dir "pci" will be included in build.
LIB := $(patsubst -DLIB_%, %, $(filter -DLIB_%, $(EVMM_CMPL_FLAGS)))

SUBDIRS += $(shell echo $(LIB) | tr A-Z a-z)
$(info lib subdir:)
$(info $(SUBDIRS))

INCLUDES = -I$(PROJS)/include

CFLAGS += $(INCLUDES)

include $(PROJS)/rule.linux
