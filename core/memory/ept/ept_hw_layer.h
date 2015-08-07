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

#ifndef _EPT_HW_LAYER_H
#define _EPT_HW_LAYER_H

#include "mon_defs.h"
#include "mon_objects.h"
#include "mon_phys_mem_types.h"
#include "scheduler.h"

#define EPT_LOG(...)        MON_LOG(mask_ept, level_trace, __VA_ARGS__)
#define EPT_PRINTERROR(...) MON_LOG(mask_ept, level_error, __VA_ARGS__)

#define EPT_NUM_PDPTRS      4

typedef union {
	struct {
		uint32_t etmt:3;
		uint32_t gaw:3;
		uint32_t reserved:6;
		uint32_t address_space_root_low:20;
		uint32_t address_space_root_high;
	} bits;
	uint64_t uint64;
} eptp_t;

typedef enum {
	INVEPT_INDIVIDUAL_ADDRESS = 0,
	INVEPT_CONTEXT_WIDE,
	INVEPT_ALL_CONTEXTS
} invept_cmd_type_t;

typedef enum {
	INVVPID_INDIVIDUAL_ADDRESS = 0,
	INVVPID_SINGLE_CONTEXT,
	INVVPID_ALL_CONTEXTS,
	INVVPID_SINGLE_CONTEXT_GLOBAL
} invvpid_cmd_type_t;

boolean_t ept_hw_is_ept_supported(void);
boolean_t ept_hw_is_ept_enabled(guest_cpu_handle_t gcpu);

boolean_t ept_hw_enable_ept(guest_cpu_handle_t gcpu);
void ept_hw_disable_ept(guest_cpu_handle_t gcpu);

uint64_t ept_hw_get_eptp(guest_cpu_handle_t gcpu);
boolean_t ept_hw_set_eptp(guest_cpu_handle_t gcpu,
			  hpa_t ept_root_hpa,
			  uint32_t gaw);

mon_phys_mem_type_t mon_ept_hw_get_ept_memory_type(void);

uint32_t mon_ept_hw_get_guest_address_width(uint32_t actual_gaw);
uint32_t mon_ept_hw_get_guest_address_width_encoding(uint32_t width);
uint32_t ept_hw_get_guest_address_width_from_encoding(uint32_t gaw_encoding);

void ept_hw_set_pdtprs(guest_cpu_handle_t gcpu, uint64_t pdptr[]);

boolean_t ept_hw_is_invept_supported(void);
boolean_t ept_hw_invept_all_contexts(void);
boolean_t ept_hw_invept_context(uint64_t eptp);
boolean_t ept_hw_invept_individual_address(uint64_t eptp, address_t gpa);
boolean_t ept_hw_invvpid_single_context(uint64_t vpid);
boolean_t ept_hw_invvpid_all_contexts(void);
boolean_t ept_hw_is_invvpid_supported(void);
boolean_t ept_hw_invvpid_individual_address(uint64_t vpid, address_t gva);

#define CHECK_EXECUTION_ON_LOCAL_HOST_CPU(gcpu)                 \
	MON_DEBUG_CODE(                                            \
	{                                                       \
		cpu_id_t host_cpu_id = scheduler_get_host_cpu_id(gcpu);  \
		MON_ASSERT(host_cpu_id == hw_cpu_id());                \
	}                                                       \
	)

#endif   /*_EPT_HW_LAYER_H */
