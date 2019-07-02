/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _SECURITY_INFO_H
#define _SECURITY_INFO_H

#ifndef MODULE_SECURITY_INFO
#error "MODULE_SECURITY_INFO is not defined"
#endif

#include "vmm_base.h"

#define INTERNAL NULL

/* When dest is INTERNAL, an internal memory will be allocated to hold the info;
 * when src is INTERNAL, the info stored in the internal memory will be moved to dest,
 * and the internal memory will be freed */
uint32_t mov_sec_info(void *dest, void *src);

#endif
