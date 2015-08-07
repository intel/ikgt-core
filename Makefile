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

ifndef PROJS
export PROJS = $(CURDIR)

debug ?= 0
ifeq ($(debug), 1)
export XMON_CMPL_OPT_FLAGS = -DDEBUG
export OUTPUTTYPE = debug
else
export XMON_CMPL_OPT_FLAGS =
export OUTPUTTYPE = release
endif

export BINDIR = $(PROJS)/bin/linux/$(OUTPUTTYPE)/
export OUTDIR = $(PROJS)/build/linux/$(OUTPUTTYPE)/

$(shell mkdir -p $(OUTDIR))
$(shell mkdir -p $(BINDIR))
endif  # PROJS

ifeq ($(debug), 1)
LDFLAGS = -T core/linker.lds -pie -z max-page-size=4096 -z common-page-size=4096
else
LDFLAGS = -T core/linker.lds -pie -s -z max-page-size=4096 -z common-page-size=4096
endif

export CC = gcc
export AS = gcc
export LD = ld
export AR = ar

XMON_ELF := xmon.elf

include core/rule.linux

.PHONY: core api plugins loader $(XMON_ELF) dist clean distclean install uninstall

TARGET := core

api_dir := $(findstring api, $(shell dir $(PROJS)))
ifdef api_dir
  XMON_CMPL_OPT_FLAGS += -DEXTERN_EVENT_HANDLER
  TARGET += api
endif  # api_dir

plugin_dir := $(findstring plugins, $(shell dir $(PROJS)))
ifdef plugin_dir
  plugin_subdirs := $(sort $(subst /,,$(shell dir $(PROJS)/plugins)))
  XMON_CMPL_OPT_FLAGS += -DPLUGIN_EXISTS
  # Loader build (which produces the final ikgt_pkg.bin) is not included
  # as it requires an xmon.elf which is built with a handler from plugin
  # usage code and the plugin usage code is not a must component if one
  # chooses to build just libraries: libmon.a, libapi.a, and libtmsl.a
  TARGET += plugins
endif  # plugin_dir

TARGET += $(XMON_ELF) loader

INSTALL      = install
INSTALL_DIR  = $(INSTALL) -d -m 0755 -p
INSTALL_DATA = $(INSTALL) -m 0644 -p
INSTALL_PROG = $(INSTALL) -m 0755 -p
IKGT_TARGET  = ikgt_pkg.bin
IKGT_CONFIG  = ikgt.cfg

# for dist targets
ifdef DESTDIR
DISTDIR = $(DESTDIR)
else
DISTDIR ?= /
endif

DESTDIR ?= $(PROJS)/dist

export XMON_CMPL_OPT_FLAGS

all: $(TARGET)

core:
	$(MAKE) -C $(PROJS)/core

api:
	$(MAKE) -C $(PROJS)/api

plugins:
	$(foreach D, $(plugin_subdirs), $(MAKE) -C $(PROJS)/plugins/$(D);)

ifeq ($(call find_xmon_opt,EXTERN_EVENT_HANDLER),)
xmon.elf:
	$(LD) $(LDFLAGS) -o $(BINDIR)$@ $(wildcard $(OUTDIR)core/*.o)
else
ifeq ($(call find_xmon_opt,PLUGIN_EXISTS),)
xmon.elf:
	$(LD) $(LDFLAGS) -o $(BINDIR)$@ $(wildcard $(OUTDIR)core/*.o) $(wildcard $(OUTDIR)api/*.o)
else
ifeq ($(call find_xmon_opt,HANDLER_EXISTS),)
xmon.elf:
	$(LD) $(LDFLAGS) -o $(BINDIR)$@ $(wildcard $(OUTDIR)core/*.o) $(wildcard $(OUTDIR)api/*.o) $(wildcard $(OUTDIR)plugin/*.o)
else
xmon.elf:
	$(LD) $(LDFLAGS) -o $(BINDIR)$@ $(wildcard $(OUTDIR)handler/*.o) $(wildcard $(OUTDIR)plugin/*.o) $(wildcard $(OUTDIR)core/*.o) $(wildcard $(OUTDIR)api/*.o)
endif  # HANDLER_EXISTS
endif  # PLUGIN_EXISTS
endif  # EXTERN_EVENT_HANDLER

loader:
	$(MAKE) -C $(PROJS)/loader

clean:
	-rm -rf $(OUTDIR)
	-rm -rf $(BINDIR)
	$(foreach D, $(plugin_subdirs), $(MAKE) -C $(PROJS)/plugins/$(D) clean;)
	$(MAKE) -C $(PROJS)/loader clean

dist: DISTDIR=$(DESTDIR)
dist: install

distclean:
	rm -rf dist/

install:
	[ -d $(DISTDIR) ] || $(INSTALL_DIR) $(DISTDIR)
	[ -d $(DISTDIR)/boot ] || $(INSTALL_DIR) $(DISTDIR)/boot
	$(INSTALL_DATA) $(BINDIR)$(IKGT_TARGET) $(DISTDIR)/boot/$(IKGT_TARGET)
	[ -d $(DISTDIR)/etc/grub.d ] || $(INSTALL_DIR) $(DISTDIR)/etc/grub.d
	$(INSTALL) -m 755 -t $(DISTDIR)/etc/grub.d $(PROJS)/package/20*
	[ -d $(DISTDIR)/etc/default/grub.d ] || $(INSTALL_DIR) $(DISTDIR)/etc/default/grub.d
	$(INSTALL_DATA) $(PROJS)/package/$(IKGT_CONFIG) $(DISTDIR)/etc/default/grub.d/$(IKGT_CONFIG)
	@if [ "x$(DISTDIR)" = "x/" ]; then \
	if [ "`which grub2-mkconfig`" != "" ]; then \
	grub2-mkconfig -o /boot/grub2/grub.cfg; \
	elif [ "`which grub-mkconfig`" != "" ]; then \
	grub-mkconfig -o /boot/grub/grub.cfg; \
	else \
	echo "ERROR: failed to find grub-mkconfig or grub2-mkconfig"; \
	fi \
	fi

uninstall:
	rm -rf /boot/$(IKGT_TARGET)
	rm -rf /etc/grub.d/20_linux_ikgt*
	rm -rf /etc/default/grub.d/ikgt*
	@if [ "`which grub2-mkconfig`" != "" ]; then \
	grub2-mkconfig -o /boot/grub2/grub.cfg; \
	elif [ "`which grub-mkconfig`" != "" ]; then \
	grub-mkconfig -o /boot/grub/grub.cfg; \
	else \
	echo "ERROR: failed to find grub-mkconfig or grub2-mkconfig"; \
	fi

# End of file
