/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _TRUSTY_TEE_H
#define _TRUSTY_TEE_H

#ifndef MODULE_TRUSTY_TEE
#error "MODULE_TRUSTY_TEE is not defined"
#endif

#include "evmm_desc.h"

void init_trusty_tee(evmm_desc_t *evmm_desc);

#endif
