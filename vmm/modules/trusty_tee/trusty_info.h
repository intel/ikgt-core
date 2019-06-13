/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _TRUSTY_INFO_H_
#define _TRUSTY_INFO_H_

typedef struct {
	/* Used to double check structures match */
	uint32_t size_of_this_struct;

	/* Total memory size for TEE */
	uint32_t mem_size;

	/* Used to calibrate TSC in TEE */
	uint64_t calibrate_tsc_per_ms;

	/* Used by keymaster */
	uint64_t trusty_mem_base;

	uint32_t sipi_ap_wkup_addr;
	uint8_t  padding[4];
} trusty_startup_info_t;

/* Parameters from OSloader */
typedef struct {
	uint32_t size_of_this_struct;
	uint32_t version;

	/* 16MB under 4G */
	uint32_t runtime_addr;

	/* Entry address of trusty */
	uint32_t entry_point;
} trusty_boot_params_t;

#endif
