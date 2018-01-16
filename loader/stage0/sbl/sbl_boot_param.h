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
#include "trusty_info.h"

#define IMAGE_ID_MAX_LEN 8

typedef struct {
	uint32_t tabsize;
	uint32_t strsize;
	uint32_t addr;
	uint32_t reserved;
} aout_t; /* a.out kernel image */

typedef struct {
	uint32_t num;
	uint32_t size;
	uint32_t addr;
	uint32_t shndx;
} elf_t; /* elf kernel */

/*only used partial of the standard multiboot_info_t, since we only need flags and cmdline */
typedef struct {
	uint32_t flags;

	/* valid if flags[0] (MBI_MEMLIMITS) set */
	uint32_t mem_lower;
	uint32_t mem_upper;

	/* valid if flags[1] set */
	uint32_t boot_device;

	/* valid if flags[2] (MBI_CMDLINE) set */
	uint32_t cmdline;

	/* valid if flags[3] (MBI_MODS) set */
	uint32_t mods_count;
	uint32_t mods_addr;

	/* valid if flags[4] or flags[5] set */
	union {
		aout_t aout_image;
		elf_t elf_image;
	} syms;

	/* valid if flags[6] (MBI_MEMMAP) set */
	uint32_t mmap_length;
	uint32_t mmap_addr;
} multiboot_info_t;

typedef struct {
	uint32_t size;
	uint64_t addr;
	uint64_t len;
	uint32_t type;
} PACKED multiboot_memory_map_t;

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
	uint32_t size_of_this_struct;
	uint32_t version;
	uint32_t vmm_heap_addr;       /* 64KB, SBL should reserve it in e820 */
	uint32_t sipi_page;           /* 4KB under 1M, SBL should reserve it in e820 */
	uint32_t vmm_runtime_addr;    /* 4MB under 4G, SBL should reserve it in e820 */
	uint32_t trusty_runtime_addr; /* 16MB under 4G, SBL should should reserve it in e820. Ignore it and set to 0 for Android. */
	gcpu_state_t payload_cpu_state;
} vmm_boot_params_t;

typedef struct {
	uint32_t size_of_this_struct;
	uint32_t version;
	uint64_t p_device_sec_info; // device_sec_info_t *
	uint64_t p_platform_info;   // platform_info_t *
	uint64_t p_vmm_boot_param;  // vmm_boot_param_t *
} image_boot_params_t;

evmm_desc_t *boot_params_parse(const init_register_t *init_reg);
#endif
