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

#ifndef _FVS_H
#define _FVS_H

#define MAX_EPTP_ENTRIES 512
#define FVS_ENABLE_FLAG  1
#define FVS_DISABLE_FLAG 0

#define FAST_VIEW_SWITCH_LEAF      0x0  /* EPTP-switching (VM function 0) */

typedef struct  {
	hpa_t		*eptp_list_paddress;
	hva_t		*eptp_list_vaddress;
	uint32_t	padding;
	/* Each CPU has its own eptp list. Use cpu id as array index of
	 * eptp_list_paddress[] and eptp_list_vaddress[] */
	uint32_t	num_of_cpus;
} fvs_descriptor_t;

typedef fvs_descriptor_t *fvs_object_t;

boolean_t fvs_is_eptp_switching_supported(void);
void fvs_guest_vmfunc_enable(guest_cpu_handle_t gcpu);
hpa_t *mon_fvs_get_all_eptp_list_paddress(guest_cpu_handle_t gcpu);
boolean_t mon_fvs_add_entry_to_eptp_list(guest_handle_t guest,
					 hpa_t ept_root_hpa,
					 uint32_t gaw,
					 uint64_t index);
boolean_t mon_fvs_add_entry_to_eptp_list_single_core(guest_handle_t guest,
						     cpu_id_t cpu_id,
						     hpa_t ept_root_hpa,
						     uint32_t gaw,
						     uint64_t index);
boolean_t mon_fvs_delete_entry_from_eptp_list(guest_handle_t guest,
					      uint64_t index);
boolean_t mon_fvs_delete_entry_from_eptp_list_single_core(guest_handle_t guest,
							  cpu_id_t cpu_id,
							  uint64_t index);
boolean_t mon_fvs_update_entry_in_eptp_list(guest_handle_t guest,
					    hpa_t ept_root_hpa,
					    uint32_t gaw,
					    uint64_t index);
uint64_t mon_fvs_get_eptp_entry(guest_cpu_handle_t gcpu, uint64_t index);
void fvs_vmfunc_vmcs_init(guest_cpu_handle_t gcpu);
void mon_fvs_enable_fvs(guest_cpu_handle_t gcpu);
void mon_fvs_disable_fvs(guest_cpu_handle_t gcpu);
boolean_t mon_fvs_is_fvs_enabled(guest_cpu_handle_t gcpu);
void fvs_vmexit_handler(guest_cpu_handle_t gcpu);
void fvs_save_resumed_eptp(guest_cpu_handle_t gcpu);
void mon_fvs_enable_eptp_switching(cpu_id_t gcpu, void *arg);
void mon_fvs_disable_eptp_switching(cpu_id_t gcpu, void *arg);
#endif
