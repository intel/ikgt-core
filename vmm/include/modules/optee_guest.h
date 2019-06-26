/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _OPTEE_GUEST_H
#define _OPTEE_GUEST_H

#ifndef MODULE_OPTEE_GUEST
#error "MODULE_OPTEE_GUEST is not defined"
#endif

#include "evmm_desc.h"

void init_optee_guest(evmm_desc_t *evmm_desc);

#endif
