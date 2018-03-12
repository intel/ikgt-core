/****************************************************************************
* Copyright (c) 2015-2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0

* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
****************************************************************************/

#ifndef _SBL_BOOT_PARAM_H_
#define _SBL_BOOT_PARAM_H_

#include "stage0_asm.h"
#include "evmm_desc.h"
#include "stage0_lib.h"

#define IMAGE_ID_MAX_LEN 8

/* register value ordered by: pushal, pushfl */
typedef struct init_register {
	uint32_t eflags;
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
} init_register_t;

typedef struct {
	uint32_t size_of_this_struct;
	uint32_t version;
	uint32_t cpu_num;
	uint32_t cpu_frequency_MHz;
} platform_info_t;

typedef struct {
	uint32_t eip;
	uint32_t eax;
	uint32_t ebx;
	uint32_t esi;
	uint32_t edi;
	uint32_t ecx;
} payload_gcpu_state_t;

typedef struct {
	uint32_t size_of_this_struct;
	uint32_t version;
	uint32_t vmm_heap_addr;       /* 64KB, SBL should reserve it in e820 */
	uint32_t sipi_page;           /* 4KB under 1M, SBL should reserve it in e820 */
	uint32_t vmm_runtime_addr;    /* 4MB under 4G, SBL should reserve it in e820 */
	uint32_t trusty_runtime_addr; /* 16MB under 4G, SBL should should reserve it in e820. Ignore it and set to 0 for Android. */
	payload_gcpu_state_t payload_cpu_state;
} vmm_boot_params_t;

typedef struct {
	uint32_t size_of_this_struct;
	uint32_t version;
	uint64_t p_device_sec_info; // device_sec_info_t *
	uint64_t p_platform_info;   // platform_info_t *
	uint64_t p_vmm_boot_param;  // vmm_boot_param_t *
} image_boot_params_t;

image_boot_params_t *cmdline_parse(multiboot_info_t *mbi);
#endif
