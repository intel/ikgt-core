/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS, * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

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
