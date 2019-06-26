/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _DEADLOOP_H
#define _DEADLOOP_H

#ifndef MODULE_DEADLOOP
#error "MODULE_DEADLOOP is not defined"
#endif

#include "gcpu.h"

boolean_t deadloop_setup(guest_cpu_handle_t gcpu, uint64_t dump_gva);

#endif
