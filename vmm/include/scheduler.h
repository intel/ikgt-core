/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

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
 * get gcpu from guest. Return the target gcpu on success, NULL on fail
 */
guest_cpu_handle_t get_gcpu_from_guest(guest_handle_t guest, uint32_t host_cpu);

/*
 * Set initial guest. Return the target gcpu on success, NULL on fail
 */
guest_cpu_handle_t set_initial_guest(guest_handle_t guest);

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
 * Schedule to guest. Return the target gcpu on success, NULL on fail
 */
guest_cpu_handle_t schedule_to_guest(guest_handle_t guest);

#endif                          /* _SCHEDULER_H_ */
