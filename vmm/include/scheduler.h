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
