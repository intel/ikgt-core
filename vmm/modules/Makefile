################################################
# Copyright (c) 2015-2019 Intel Corporation.
# All rights reserved.
#
# SPDX-License-Identidfier: Apache-2.0
#
################################################

CSOURCES = $(wildcard *.c)

# we make module which exsit in the EVMM_CMPL_FLAGS.
# for example, EVMM_CMPL_FLAGS has the -DMODULE_IPC,
# dir "ipc" will be included in build.
MODULE := $(patsubst -DMODULE_%, %, $(filter -DMODULE_%, $(EVMM_CMPL_FLAGS)))

SUBDIRS := $(shell echo $(MODULE) | tr A-Z a-z)

$(info module subdir:)
$(info $(SUBDIRS))

include $(PROJS)/rule.linux
