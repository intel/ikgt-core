/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _OPTEE_INFO_H_
#define _OPTEE_INFO_H_

typedef struct {
	/* Used to double check structures match */
	uint32_t size_of_this_struct;

	/* total memory size for OP-TEE */
	uint32_t mem_size;

	/* Used to calibrate TSC in OP-TEE */
	uint64_t calibrate_tsc_per_ms;

	uint64_t optee_mem_base;

	uint32_t sipi_ap_wkup_addr;
	uint8_t  padding[4];
} optee_startup_info_t;

/* Different vmcall parameters structure from OSloader */
typedef struct {
	/* Size of this structure */
	uint64_t size_of_this_struct;
	/* Load time base address of trusty */
	uint32_t load_base;
	/* Load time size of trusty */
	uint32_t load_size;

	/* other fields will not used in EVMM */
} optee_boot_params_v0_t;

typedef struct {
	uint32_t size_of_this_struct;
	uint32_t version;

	/* 16MB under 4G */
	uint32_t runtime_addr;

	/* Entry address of trusty */
	uint32_t entry_point;
} optee_boot_params_v1_t;

#endif
