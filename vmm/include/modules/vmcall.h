/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _VMCALL_H_
#define _VMCALL_H_

#ifndef MODULE_VMCALL
#error "MODULE_VMCALL is not defined"
#endif

#include "vmm_objects.h"

#define get_vmcall_id(gcpu) (gcpu_get_gp_reg(gcpu, REG_RAX))

typedef void (*vmcall_handler_t) (guest_cpu_handle_t gcpu);

void vmcall_register(uint16_t guest_id, uint32_t vmcall_id,
			 vmcall_handler_t handler);
void vmcall_init();

#endif                          /* _VMCALL_H_ */
