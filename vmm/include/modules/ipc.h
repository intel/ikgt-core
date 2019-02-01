/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _IPC_H
#define _IPC_H

#ifndef MODULE_IPC
#error "MODULE_IPC is not defined"
#endif

#include "vmm_objects.h"

typedef void (*ipc_func_t) (guest_cpu_handle_t gcpu, void *arg);

void ipc_init(void);

void ipc_exec_on_all_other_cpus(ipc_func_t func, void* arg);

#ifdef MODULE_LAPIC_ID
void ipc_exec_on_host_cpu(uint16_t hcpu_id, ipc_func_t func, void* arg);
#endif
#endif
