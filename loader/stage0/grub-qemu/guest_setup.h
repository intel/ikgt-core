/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _GUEST_SETUP_H_
#define _GUEST_SETUP_H_

#include "evmm_desc.h"

boolean_t g0_gcpu_setup(evmm_desc_t *desc, uint64_t entry);
void trusty_gcpu0_setup(evmm_desc_t *desc);

#endif
