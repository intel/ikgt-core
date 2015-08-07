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

#ifndef _EPT_H
#define _EPT_H

#include "mon_defs.h"
#include "mon_objects.h"
#include "memory_address_mapper_api.h"
#include "ept_hw_layer.h"
#include "list.h"
#include "lock.h"

#define ANY_CPU_ID                                   ((cpu_id_t)-1)

/* internal data structures */
typedef struct {
	cpu_id_t		host_cpu_id;
	char			padding[2];
	invept_cmd_type_t	cmd;
	uint64_t		eptp; /* context */
	uint64_t		gpa;
} ept_invept_cmd_t;

typedef struct {
	uint64_t		ept_root_table_hpa;
	uint32_t		gaw;
	guest_id_t		guest_id;
	uint16_t		padding;
	ept_invept_cmd_t	*invept_cmd;
} ept_set_eptp_cmd_t;

typedef struct {
	uint64_t	cr0;
	uint64_t	cr4;
	boolean_t	is_initialized;
	boolean_t	ept_enabled_save;
	uint64_t	active_ept_root_table_hpa;
	uint32_t	active_ept_gaw;
	uint32_t	padding;
} ept_guest_cpu_state_t;

typedef struct {
	mam_handle_t		address_space;
	uint64_t		ept_root_table_hpa;
	uint32_t		gaw;
	guest_id_t		guest_id;
	uint16_t		padding;
	ept_guest_cpu_state_t **gcpu_state;
	list_element_t		list[1];
} ept_guest_state_t;

typedef struct {
	list_element_t	guest_state[1]; /* ept_guest_state_t */
	uint32_t	num_of_cpus;
	mon_lock_t	lock;
	uint32_t	lock_count;
} ept_state_t;

void ept_release_lock(void);
void ept_acquire_lock(void);

boolean_t ept_is_ept_supported(void);
boolean_t mon_ept_is_ept_enabled(IN guest_cpu_handle_t gcpu);
boolean_t mon_ept_is_cpu_in_non_paged_mode(guest_id_t guest_id);

mam_handle_t mon_ept_create_guest_address_space(gpm_handle_t gpm,
						boolean_t original_perms);
mam_ept_super_page_support_t mon_ept_get_mam_super_page_support(void);
mam_ept_supported_gaw_t mon_ept_get_mam_supported_gaw(uint32_t gaw);
uint32_t mon_ept_get_guest_address_width(gpm_handle_t gpm);

void mon_ept_set_current_ept(guest_cpu_handle_t gcpu,
			     uint64_t ept_root_table_hpa,
			     uint32_t ept_gaw);
void ept_get_default_ept(guest_handle_t guest,
			 uint64_t *ept_root_table_hpa,
			 uint32_t *ept_gaw);

void ept_set_pdtprs(guest_cpu_handle_t gcpu, uint64_t cr4_value);
uint64_t ept_get_eptp(guest_cpu_handle_t gcpu);
boolean_t ept_set_eptp(guest_cpu_handle_t gcpu,
		       uint64_t ept_root_table_hpa,
		       uint32_t gaw);
uint64_t mon_ept_compute_eptp(guest_handle_t guest,
			      uint64_t ept_root_table_hpa,
			      uint32_t gaw);
void mon_ept_invalidate_ept(cpu_id_t from, void *arg);

ept_guest_state_t *ept_find_guest_state(guest_id_t guest_id);

boolean_t mon_ept_enable(guest_cpu_handle_t gcpu);
void mon_ept_disable(guest_cpu_handle_t gcpu);

#ifdef DEBUG
void mon_ept_print(IN guest_handle_t guest, IN mam_handle_t address_space);
#endif

#endif
