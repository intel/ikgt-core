/****************************************************************************
* Copyright (c) 2016 Intel Corporation
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

#ifndef _TRUSTY_INFO_H_
#define _TRUSTY_INFO_H_

/*
 * these structures definition are shared with user space
 * Do remember the structure defines MUST match with
 * trusty/lib/include/trusty_device_info.h
 */

#define BOOTLOADER_SEED_MAX_ENTRIES     4
#define BUP_MKHI_BOOTLOADER_SEED_LEN    32
#define MMC_PROD_NAME_WITH_PSN_LEN      15

typedef struct _trusty_startup_info{
	/* Used to double check structures match */
	uint32_t size_of_this_struct;

	/* total memory size for LK */
	uint32_t mem_size;

	/* Used to calibrate TSC in LK */
	uint64_t calibrate_tsc_per_ms;

	/* Used by keymaster */
	uint64_t trusty_mem_base;

	uint32_t sipi_ap_wkup_addr;
	uint8_t  padding[4];
}trusty_startup_info_t;

/*
 * Structure for RoT info (fields defined by Google Keymaster2)
 */
typedef struct _rot_data_t {
	/* version 2 for current TEE keymaster2 */
	uint32_t version;

	/* 0: unlocked, 1: locked, others not used */
	uint32_t deviceLocked;

	/* GREEN:0, YELLOW:1, ORANGE:2, others not used (no RED for TEE) */
	uint32_t verifiedBootState;

	/* The current version of the OS as an integer in the format MMmmss,
	 * where MM is a two-digit major version number, mm is a two-digit,
	 * minor version number, and ss is a two-digit sub-minor version number.
	 * For example, version 6.0.1 would be represented as 060001;
	 */
	uint32_t osVersion;

	/* The month and year of the last patch as an integer in the format,
	 * YYYYMM, where YYYY is a four-digit year and MM is a two-digit month.
	 * For example, April 2016 would be represented as 201604.
	 */
	uint32_t patchMonthYear;

	/* A secure hash (SHA-256 recommended by Google) of the key used to verify
	 * the system image key size (in bytes) is zero: denotes no key provided
	 * by Bootloader. When key size is 32, it denotes key hash256 is available.
	 * Other values not defined now.
	 */
	uint32_t keySize;
	uint8_t keyHash256[32];
} rot_data_t;

/* Structure of seed info */
typedef struct _seed_info {
	uint8_t svn;
	uint8_t padding[3];
	uint8_t seed[BUP_MKHI_BOOTLOADER_SEED_LEN];
} seed_info_t;

/* TODO: remove structure of V0 version after V1 version implemented
 *       in bootloader. */
/* Structure for EVMM to launch Trusty */
typedef struct trusty_startup_params_v0 {
	/* Size of this structure */
	uint64_t size_of_this_struct;
	/* Load time base address of trusty */
	uint32_t load_base;
	/* Load time size of trusty */
	uint32_t load_size;
	/* Seed */
	uint8_t seed[BUP_MKHI_BOOTLOADER_SEED_LEN];
	/* Rot */
	rot_data_t rot;
} trusty_startup_params_v0_t;

/* Structure for EVMM to launch Trusty */
typedef struct trusty_startup_params_v1 {
	/* Size of this structure */
	uint64_t size_of_this_struct;
	/* Load time base address of trusty */
	uint32_t load_base;
	/* Load time size of trusty */
	uint32_t load_size;
	/* Seed */
	uint32_t num_seeds;
	seed_info_t seed_list[BOOTLOADER_SEED_MAX_ENTRIES];
	/* Rot */
	rot_data_t rot;
	/* Concatenation of mmc product name with a string representation of PSN */
	char serial[MMC_PROD_NAME_WITH_PSN_LEN];
}__attribute__((packed)) trusty_startup_params_v1_t;

typedef union hfs1 {
	struct {
		uint32_t working_state: 4;   /* Current working state */
		uint32_t manuf_mode: 1;      /* Manufacturing mode */
		uint32_t part_tbl_status: 1; /* Indicates status of flash partition table */
		uint32_t reserved: 25;       /* Reserved for further use */
		uint32_t d0i3_support: 1;    /* Indicates D0i3 support */
	} field;
	uint32_t data;
} hfs1_t;

typedef struct trusty_device_info {
	/* the size of the structure, used to sync up in different
	 * modules(tos loader, TA, LK kernel)
	 */
	uint32_t size;

	/* Seed */
	uint32_t num_seeds;
	seed_info_t seed_list[BOOTLOADER_SEED_MAX_ENTRIES];

	/* root of trusty field used to binding the hw-backed key */
	rot_data_t rot;

	/* used for getting device end of manufacturing or other states */
	hfs1_t state;
	/* Concatenation of mmc product name with a string representation of PSN */
	char serial[MMC_PROD_NAME_WITH_PSN_LEN];
} PACKED trusty_device_info_t;

#endif
