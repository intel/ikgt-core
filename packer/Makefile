################################################
# Copyright (c) 2015-2019 Intel Corporation.
# All rights reserved.
#
# SPDX-License-Identidfier: Apache-2.0
#
################################################

#using local gcc for evmmpacker
export CC = gcc

OUTDIR = $(BUILD_DIR)packer/
$(shell mkdir -p $(OUTDIR))

TARGET = evmmpacker

ifneq (, $(findstring -DPACK_LK, $(EVMM_CMPL_FLAGS)))
PACKAGE = evmm_lk_pkg.bin
else
ifneq (, $(findstring -DPACK_OPTEE, $(EVMM_CMPL_FLAGS)))
PACKAGE = evmm_optee_pkg.bin
else
PACKAGE = evmm_pkg.bin
endif
endif

CSOURCES = $(wildcard *.c)

INCLUDES = -I$(PROJS)/include

ifneq (, $(findstring -DDEBUG, $(EVMM_CMPL_FLAGS)))
CFLAGS = $(EVMM_CMPL_FLAGS) -static -Werror -DDEBUG
else
CFLAGS = $(EVMM_CMPL_FLAGS) -s -static -Werror
endif

CFLAGS += $(INCLUDES)

COBJS = $(addprefix $(OUTDIR), $(notdir $(patsubst %.c, %.o, $(CSOURCES))))

.PHONY: all $(COBJS) $(TARGET) clean

all: $(COBJS) $(TARGET)

#
# 1. build packer
# 2. pack stage0.bin, stage1.bin, evmm.bin into evmm_pkg.bin
#
$(COBJS): $(CSOURCES)
	$(CC) -c $(CFLAGS) -o $@ $(filter $(*F).c, $(CSOURCES))

$(TARGET): $(COBJS)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)$@ $(wildcard $(OUTDIR)*.o)

pack:$(TARGET)
ifneq (, $(findstring -DPACK_LK, $(EVMM_CMPL_FLAGS)))
	if [ $(LKBIN_DIR) ]; then cp $(LKBIN_DIR)lk.bin $(BUILD_DIR); fi
else
ifneq (, $(findstring -DPACK_OPTEE, $(EVMM_CMPL_FLAGS)))
	if [ $(OPTEEBIN_DIR) ]; then cp $(OPTEEBIN_DIR)tee-pager.bin $(BUILD_DIR); fi
endif
endif
	chmod +x $(BUILD_DIR)$(TARGET) && \
	cd $(BUILD_DIR) && \
	./$(TARGET)

clean:
	rm -f $(OBJS)
	rm -f $(OUTDIR)$(TARGET)
