/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _SERIAL_H_
#define _SERIAL_H_

#include "vmm_base.h"
void serial_init(boolean_t setup);
void serial_puts(const char *str);
#endif
