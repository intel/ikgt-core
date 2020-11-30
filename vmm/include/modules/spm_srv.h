/*
 * Copyright (c) 2020 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _TRUSTY_TEE_H
#define _TRUSTY_TEE_H

#ifndef MODULE_SPM_SRV
#error "MODULE_TRUSTY_TEE is not defined"
#endif

#include "vmm_base.h"
#include "vmm_objects.h"

boolean_t is_spm_srv_call(guest_cpu_handle_t gcpu);
int spm_mm_smc_handler(guest_cpu_handle_t gcpu);

#endif
