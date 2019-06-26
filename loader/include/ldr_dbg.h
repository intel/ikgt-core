/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _LDR_DBG_H_
#define _LDR_DBG_H_

#include "lib/print.h"

#define LEVEL_PANIC 1
#define LEVEL_INFO 2
#define LEVEL_WARNING 3
#define LEVEL_TRACE 4

#if LOG_LEVEL >= LEVEL_PANIC
#define print_panic(fmt, ...) printf("PANIC: " fmt, ##__VA_ARGS__)
#else
#define print_panic(fmt, ...) { }
#endif
#if LOG_LEVEL >= LEVEL_INFO
#define print_info(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define print_info(fmt, ...) { }
#endif
#if LOG_LEVEL >= LEVEL_WARNING
#define print_warn(fmt, ...) printf("WARNING: " fmt, ##__VA_ARGS__)
#else
#define print_warn(fmt, ...) { }
#endif
#if LOG_LEVEL >= LEVEL_TRACE
#define print_trace(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define print_trace(fmt, ...) { }
#endif

#endif
