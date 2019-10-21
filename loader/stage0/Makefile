################################################
# Copyright (c) 2015-2019 Intel Corporation.
# All rights reserved.
#
# SPDX-License-Identidfier: Apache-2.0
#
################################################

export OUTDIR = $(BUILD_DIR)stage0

$(shell mkdir -p $(OUTDIR))

INCLUDES += -I$(PROJS)/loader/stage0 \
	-I$(PROJS)/loader/stage0/stage0_lib \
	-I$(PROJS)/loader/stage0/entry

CFLAGS += $(INCLUDES)

.PHONY : all stage0_lib stage0_sub

all : stage0_lib stage0_sub

stage0_lib :
	$(MAKE) -C $@

stage0_sub : stage0_lib
	$(MAKE) -C $(LOADER_STAGE0_SUB)
