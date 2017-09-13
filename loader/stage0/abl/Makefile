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
TARGET = stage0.elf

CSOURCES = stage0.c guest_setup.c abl_boot_param.c
ASOURCES = stage0_entry.S

include $(PROJS)/rule.linux

OBJS = $(OUTDIR)stage0_entry.o \
	$(OUTDIR)stage0.o \
	$(OUTDIR)guest_setup.o \
	$(OUTDIR)trusty_setup.o \
	$(OUTDIR)abl_boot_param.o \
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
	$(LD) $(LDFLAGS) -o $(OUTDIR)$@ $(OBJS) $(wildcard $(BUILD_DIR)common/*.o)

stage0.bin: $(TARGET)
	objcopy -j .text -O binary -S $(OUTDIR)$(TARGET) $(BUILD_DIR)stage0.bin
