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

#include "vmm_base.h"
#include "gcpu.h"
#include "vmm_objects.h"

/*
 * Register guest cpu
 */
void register_gcpu(guest_cpu_handle_t gcpu_handle, uint16_t host_cpu_id);

/*
 * Get current gCPU on current host cpu
 */
guest_cpu_handle_t get_current_gcpu(void);

/*
 * Makes selected gCPU "initial" on the current host CPU and returns it.
 */
guest_cpu_handle_t schedule_initial_gcpu(void);

/*
 * Determine next gCPU to run on the current host CPU
 * Makes selected gCPU "current" on the current host CPU and returns it.
 */
guest_cpu_handle_t schedule_next_gcpu();

/*
 * Schedule to next gcpu on same host as the initial gcpu
 */
void schedule_next_gcpu_as_init(uint16_t host_cpu_id);

#endif                          /* _SCHEDULER_H_ */
