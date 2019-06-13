/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _TEMPLATE_TEE_H_
#define _TEMPLATE_TEE_H_

#ifndef MODULE_TEMPLATE_TEE
#error "MODULE_TEMPLATE_TEE is not defined"
#endif

#include "vmm_base.h"
#include "gcpu.h"
#include "guest.h"

#define GUEST_REE 0 /* OSloader or Android/Linux/Fuchsia */

/* TEE BSP/AP status */
enum {
	MODE_32BIT = 0,
	MODE_64BIT,
	WAIT_FOR_SIPI,
	HLT
};

typedef struct {
	const char* tee_name; // cannot be NULL.

	/* Tee runtime info. If this info is unknown during evmm booting
	   set 0 to them */
	uint64_t tee_runtime_addr;
	uint64_t tee_runtime_size;

	uint32_t pad1;
	uint32_t smc_vmcall_id;
	uint32_t smc_param_to_tee_nr;
	uint8_t smc_param_to_tee[8];
	uint32_t smc_param_to_ree_nr;
	uint8_t smc_param_to_ree[8];

	/* If first SMC call is from REE, in that SMC, first_smc_to_tee() will be called.
	   pre_world_switch()/post_world_switch() will only be called for other SMCs */
	void (*first_smc_to_tee)(guest_cpu_handle_t gcpu);
	void (*pre_world_switch)(guest_cpu_handle_t gcpu);
	void (*post_world_switch)(guest_cpu_handle_t gcpu, guest_cpu_handle_t gcpu_prev);

	boolean_t single_gcpu; // TRUE: gcpu number = 1; FALSE: gcpu number = physical cpu number

	boolean_t launch_tee_first;
	void (*before_launching_tee)(guest_cpu_handle_t gcpu); // this callback is valid when launch_tee_first is TRUE

	uint32_t tee_bsp_status; // 32 bit no paging, 64 bit
	uint32_t tee_ap_status; // WAIT-FOR-SIPI, HLT, 32 bit, 64 bit
} tee_config_t;

#define fill_smc_param_to_tee(cfg, array)				  \
	{ 								  \
		uint32_t i;						  \
		cfg.smc_param_to_tee_nr = sizeof(array)/sizeof(array[0]); \
		for (i=0; i<cfg.smc_param_to_tee_nr; i++)		  \
			cfg.smc_param_to_tee[i] = array[i];		  \
	}

#define fill_smc_param_from_tee(cfg, array)				  \
	{								  \
		uint32_t i;						  \
		cfg.smc_param_to_ree_nr = sizeof(array)/sizeof(array[0]); \
		for (i=0; i<cfg.smc_param_to_ree_nr; i++)		  \
			cfg.smc_param_to_ree[i] = array[i];		  \
	}

guest_handle_t create_tee(tee_config_t* cfg);
/* If tee_rt_addr or tee_rt_size equals to 0, values set in create_tee() will be used */
guest_cpu_handle_t launch_tee(guest_handle_t guest, uint64_t tee_rt_addr, uint64_t tee_rt_size);
void template_tee_init(uint64_t x64_cr3);

#endif /* _TEMPLATE_TEE_H_ */
