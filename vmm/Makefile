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

AFLAGS += $(AINCLUDES)

include $(PROJS)/rule.linux

ifneq (, $(findstring -DDEBUG, $(EVMM_CMPL_FLAGS)))
LDFLAGS = -e vmm_main -pie -z max-page-size=4096 -z common-page-size=4096
else
LDFLAGS = -e vmm_main -pie -s -z max-page-size=4096 -z common-page-size=4096
endif

all: $(TARGET)

$(TARGET): $(SUBDIRS)
	$(LD) $(LDFLAGS) -o $(BUILD_DIR)$@ $(wildcard $(OUTDIR)/*.o) $(wildcard $(LIB_DIR)/*.o)
