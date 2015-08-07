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

#ifndef _MON_BOOTSTRAP_UTILS_H_
#define _MON_BOOTSTRAP_UTILS_H_

#include "mon_startup.h"

/**************************************************************************
*
* Copy and destroy input structures
*
**************************************************************************/
const mon_startup_struct_t *mon_create_startup_struct_copy(const
							   mon_startup_struct_t *
							   startup_struct_stack);

void mon_destroy_startup_struct(const mon_startup_struct_t *startup_struct);

const mon_application_params_struct_t
*mon_create_application_params_struct_copy(const
					   mon_application_params_struct_t *
					   application_params_stack);

void mon_destroy_application_params_struct(const mon_application_params_struct_t *
					   application_params_struct);

/**************************************************************************
*
* Read input data structure and create all guests
*
**************************************************************************/

/*-------------------------------------------------------------------------
 *
 * Preform initialization of guests and guest CPUs, excluding host cpu parts
 *
 * Should be called on BSP only while all APs are stopped
 *
 * Return TRUE for success
 *
 *------------------------------------------------------------------------- */
boolean_t initialize_all_guests(uint32_t number_of_host_processors,
				const mon_memory_layout_t *mon_memory_layout,
				const mon_guest_startup_t *
				primary_guest_startup_state,
				uint32_t number_of_secondary_guests,
				const mon_guest_startup_t *
				secondary_guests_startup_state_array,
				const mon_application_params_struct_t *
				application_params);

/*-------------------------------------------------------------------------
 *
 * Run init routins of all addons
 *
 * Should be called on BSP only while all APs are stopped
 *
 *-------------------------------------------------------------------------- */
void start_addons(uint32_t num_of_cpus,
		  const mon_startup_struct_t *startup_struct,
		  const mon_application_params_struct_t *
		  application_params_struct);

/*-------------------------------------------------------------------------
 *
 * Preform initialization of host cpu parts of all guest CPUs that run on
 * specified host CPU.
 *
 * Should be called on the target host CPU
 *------------------------------------------------------------------------- */
void initialize_host_vmcs_regions(cpu_id_t current_cpu_id);

/*
 * Debug print input structures
 */
void print_startup_struct(const mon_startup_struct_t *startup_struct);

#endif                          /* _MON_BOOTSTRAP_UTILS_H_ */
