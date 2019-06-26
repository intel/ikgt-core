/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _PRINT_H_
#define _PRINT_H_

#include "vmm_base.h"

#include "lib/serial.h"

#ifdef LIB_PRINT
/*caller must make sure this function is NOT
called simultaneously in different cpus*/
void printf(const char *format, ...);
void print_init(boolean_t setup);
#else
#define printf(...) { }
#define print_init(setup) { }
#endif

#endif
