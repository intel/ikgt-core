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
