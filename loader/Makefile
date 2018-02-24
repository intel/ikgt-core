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

