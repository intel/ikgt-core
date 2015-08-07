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

#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "mon_defs.h"
#include "hw_includes.h"
#include "guest_cpu.h"
#include "libc.h"
#include "mon_objects.h"

/*-------------------------------------------------------------------------
 *
 * Get current
 *
 * Return NULL if no guest cpu is running on current host cpu
 *------------------------------------------------------------------------- */
guest_cpu_handle_t mon_scheduler_current_gcpu(void);

/*-------------------------------------------------------------------------
 *
 * Get Host CPU Id for which given Guest CPU is assigned
 *
 *------------------------------------------------------------------------- */
uint16_t scheduler_get_host_cpu_id(guest_cpu_handle_t gcpu);

/*-------------------------------------------------------------------------
 *
 * Enumerate Guest CPUs assigned to the same Host CPU
 *
 * Return NULL to indicate end of enumeration
 *------------------------------------------------------------------------- */

/* user allocated enumeration context */
typedef struct scheduler_vcpu_object_t *scheduler_gcpu_iterator_t;

guest_cpu_handle_t
scheduler_same_host_cpu_gcpu_first(scheduler_gcpu_iterator_t *ctx,
				   cpu_id_t host_cpu_id);

guest_cpu_handle_t
scheduler_same_host_cpu_gcpu_next(scheduler_gcpu_iterator_t *ctx);

/* -------------------------- schedule
 * ----------------------------------------- */

/*-------------------------------------------------------------------------
 *
 * Determine initial gCPU to run on the current host CPU
 * Makes selected gCPU "current" on the current host CPU and returns it.
 *
 * If no ready vcpus on the current host CPU, returns NULL
 *------------------------------------------------------------------------- */
guest_cpu_handle_t scheduler_select_initial_gcpu(void);

/*-------------------------------------------------------------------------
 *
 * Determine next gCPU to run on the current host CPU
 * Makes selected gCPU "current" on the current host CPU and returns it.
 *
 * If no ready vcpus on the current host CPU, returns NULL
 * Note:
 * 1. scheduler_select_initial_gcpu() should be called before on this host
 * CPU
 *------------------------------------------------------------------------- */
guest_cpu_handle_t scheduler_select_next_gcpu(void);

guest_cpu_handle_t scheduler_schedule_gcpu(guest_cpu_handle_t gcpu);

/* ---------------------- initialization
 * --------------------------------------- */

/* init scheduler. */
void scheduler_init(uint16_t number_of_host_cpus);

/* register guest cpu */
void scheduler_register_gcpu(guest_cpu_handle_t gcpu_handle,
			     cpu_id_t host_cpu_id,
			     boolean_t schedule_immediately);

guest_cpu_handle_t mon_scheduler_get_current_gcpu_for_guest(guest_id_t guest_id);

#endif                          /* _SCHEDULER_H_ */
