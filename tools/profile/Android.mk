################################################
# Copyright (c) 2015-2019 Intel Corporation.
# All rights reserved.
#
# SPDX-License-Identidfier: Apache-2.0
#
################################################

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := module_profile
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := module_profile.c
LOCAL_STATIC_LIBRARIES := libc liblog
LOCAL_MODULE_STEM_64 := $(LOCAL_MODULE)64

include $(BUILD_EXECUTABLE)
