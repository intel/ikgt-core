################################################
# Copyright (c) 2015-2019 Intel Corporation.
# All rights reserved.
#
# SPDX-License-Identidfier: Apache-2.0
#
################################################

export PROJS = $(CURDIR)/..
export LIB_DIR = $(BUILD_DIR)lib

INCLUDES = -I. \
	-I$(PROJS)/include \
	-I$(PROJS)/loader/include

AINCLUDES += -I$(PROJS)/loader/include \
	-I$(PROJS)/include

AFLAGS += $(AINCLUDES)

CFLAGS += $(INCLUDES)

.PHONY : all stage0 stage1 common

all : stage0 stage1

stage0 : common
	$(MAKE) -C $@

stage1 : common
	$(MAKE) -C $@

common :
	$(MAKE) -C $@

# End of file

