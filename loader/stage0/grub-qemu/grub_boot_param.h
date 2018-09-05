/****************************************************************************
 * Copyright (c) 2018 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	http://www.apache.org/licenses/LICENSE2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ****************************************************************************/

#ifndef _GRUB_BOOT_PARAM_H_
#define _GRUB_BOOT_PARAM_H_

#include "stage0_asm.h"
#include "stage0_lib.h"
#include "evmm_desc.h"

#define GRUB_HEAP_ADDR      0X12551000
#define GRUB_HEAP_SIZE      0x5400000

#define MULTIBOOT_HEADER_SIZE   32

typedef enum {
	MFIRST_MODULE = 0,
	TRUSTYIMG = MFIRST_MODULE,
	TESTRUNNER,
	GRUB_MODULE_COUNT
} grub_module_index_t;

evmm_desc_t *boot_params_parse(multiboot_info_t *mbi);
void init_memory_manager(uint64_t heap_base_address, uint32_t heap_size);
void *allocate_memory(uint32_t size_request);

#endif
