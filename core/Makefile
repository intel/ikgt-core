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
CSOURCES = main.c
ASOURCES = mon_entry.S
include rule.linux

SUBDIRS = arch common dbg guest host ipc libc \
		  memory startup utils vmexit vmx acpi

$(shell mkdir -p $(OUTDIR)core/)

.PHONY: all subdirs libmon.a

TARGET = libmon.a

all: subdirs $(TARGET)

subdirs:
	$(foreach D, $(SUBDIRS), $(MAKE) -C $(D);)

libmon.a:
	$(AR) rcs $(BINDIR)$@ $(wildcard $(OUTDIR)core/*.o)

