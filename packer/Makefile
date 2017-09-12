################################################################################
# Copyright (c) 2015 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

#using local gcc for evmmpacker
export CC = gcc

OUTDIR = $(BUILD_DIR)packer/
$(shell mkdir -p $(OUTDIR))

TARGET = evmmpacker

ifneq (, $(findstring -DPACK_LK, $(EVMM_CMPL_FLAGS)))
PACKAGE = evmm_lk_pkg.bin
else
PACKAGE = evmm_pkg.bin
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
endif
	chmod +x $(BUILD_DIR)$(TARGET) && \
	cd $(BUILD_DIR) && \
	./$(TARGET)

clean:
	rm -f $(OBJS)
	rm -f $(OUTDIR)$(TARGET)
