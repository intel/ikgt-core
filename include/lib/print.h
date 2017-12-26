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
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

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
