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
#include "device_sec_info.h"

#define IMAGE_ID_MAX_LEN 8
#define MAX_SERIAL_NUMBER_LENGTH    16
#define SEED_ENTRY_TYPE_SVNSEED     0x1
#define SEED_ENTRY_TYPE_RPMBSEED    0x2
#define SEED_ENTRY_USAGE_USEED      0x1
#define SEED_ENTRY_USAGE_DSEED      0x2

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
	uint8_t  revision;
	uint8_t  reserved[3];
	uint8_t  boot_partition;
	uint8_t  boot_mode;
	uint16_t platform_id;
	uint32_t cpu_num;
	uint16_t security_flags; // Bit 0: Vb (0- disabled, 1 - enabled)
				// Bit 1: Mb (0- disabled, 1 - enabled)
				// Bit 2: Manufacturing state (0 - EOM not set; 1 - EOM set)
				// Bit 3: Secure debug enabled/disabled
	uint8_t reserved1[2];
	char    serial_number[MAX_SERIAL_NUMBER_LENGTH];
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
	/* SVN based seed or RPMB seed or attestation key_box */
	uint8_t type;
	/* For SVN seed: useed or dseed
	 * For RPMB seed: serial number based or not
	 */
	uint8_t usage;
	/* index for the same type and usage seed */
	uint8_t index;
	uint8_t reserved;
	/* reserved for future use */
	uint16_t flags;
	/* Total size of this seed entry */
	uint16_t seed_entry_size;
	/* SVN seed: struct seed_info
	 * RPMB seed: uint8_t rpmb_key[key_len]
	 */
	uint8_t seed[0];
} seed_entry_t;

typedef struct {
	uint8_t  revision;
	uint8_t  reserved0[3];
	uint32_t buffer_size;
	uint8_t  total_seed_count;
	uint8_t  reserved1[3];
} seed_list_t;

typedef struct {
	uint32_t size_of_this_struct;
	uint32_t version;
	uint64_t p_seed_list;       // seed_list_t *
	uint64_t p_platform_info;   // platform_info_t *
	uint64_t p_vmm_boot_param;  // vmm_boot_param_t *
} image_boot_params_t;

image_boot_params_t *cmdline_parse(multiboot_info_t *mbi);
void parse_seed_list(device_sec_info_v0_t *dev_sec_info, seed_list_t *seed_list);
#endif
