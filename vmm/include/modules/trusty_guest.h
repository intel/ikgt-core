/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _TRUSTY_GUEST_H
#define _TRUSTY_GUEST_H

#ifndef MODULE_TRUSTY_GUEST
#error "MODULE_TRUSTY_GUEST is not defined"
#endif

#include "evmm_desc.h"

void init_trusty_guest(evmm_desc_t *evmm_desc);
void trusty_register_deadloop_handler(evmm_desc_t *evmm_desc);

#endif
