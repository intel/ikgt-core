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

/* Parameters from OSloader
 * change history:
 * version 0 : init version
 * version 1 : add runtime_size and barrier_size for rowhammer mitigation
 */
typedef struct {
	uint32_t size_of_this_struct;
	uint32_t version;

	/* Memory layout:
	 * barrier memory (1M bytes)
	 * TEE memory  (16M bytes)
	 * barrier memory (1M bytes)
	*/

	/* Start address of TEE memory, under 4G */
	uint32_t runtime_addr;

	/* Entry address of trusty */
	uint32_t entry_point;

	/* 16M bytes TEE actually used, added in version 1 */
	uint32_t runtime_size;

	/* 1M bytes, added in version 1 */
	uint32_t barrier_size;
} trusty_boot_params_t;

#endif
