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

/* Move security info from src to dest. All parameters can't be NULL. Returns number of byte moved */
uint32_t mov_secinfo(void *dest, void *src);

/* Move security info from src to memory which will be allocated in the function,
   returns the allocated memory address */
void *mov_secinfo_to_internal(void *src);

/* Move security info from allocated memory which gets from mov_secinfo_to_internal() to dest.
  Then the allocated memory will be freed. All parameters can't be NULL. Returns number of byte moved */
uint32_t mov_secinfo_from_internal(void *dest, void *handle);

#endif
