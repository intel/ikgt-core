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
uint64_t get_serial_base(void);
void serial_init(uint64_t serial_base);
void serial_puts(const char *str, uint64_t serial_base);
#endif
