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
export OUTDIR = $(BUILD_DIR)stage0

$(shell mkdir -p $(OUTDIR))

INCLUDES += -I$(PROJS)/loader/stage0 \
	-I$(PROJS)/loader/stage0/stage0_lib

CFLAGS += $(INCLUDES)

.PHONY : all stage0_lib stage0_sub

all : stage0_lib stage0_sub

stage0_lib :
	$(MAKE) -C $@

stage0_sub : stage0_lib
	$(MAKE) -C $(LOADER_STAGE0_SUB)
