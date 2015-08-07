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

#ifndef _VE_H
#define _VE_H

typedef struct {
	uint32_t	exit_reason;
	uint32_t	flag;
	uint64_t	exit_qualification;
	uint64_t	gla;            /* guest linear address; */
	uint64_t	gpa;            /* guest physical address; */
	uint16_t	eptp_index;
	uint8_t		padding[6];
} ve_ept_info_t;

boolean_t mon_ve_is_hw_supported(void);
boolean_t mon_ve_is_ve_enabled(guest_cpu_handle_t gcpu);
boolean_t mon_ve_update_hpa(guest_id_t guest_id,
			    cpu_id_t guest_cpu_id,
			    hpa_t hpa,
			    uint32_t enable);
void mon_ve_enable_ve(guest_cpu_handle_t gcpu);
void mon_ve_disable_ve(guest_cpu_handle_t gcpu);
boolean_t mon_ve_handle_sw_ve(guest_cpu_handle_t gcpu,
			      uint64_t qualification,
			      uint64_t gla,
			      uint64_t gpa,
			      uint64_t view);

#endif
