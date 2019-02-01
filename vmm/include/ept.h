/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _EPT_H
#define _EPT_H

#include "vmm_base.h"
#include "vmm_objects.h"

typedef union {
	struct {
		uint32_t ug_real_mode:1;
			/* enable UG/EPT for real mode only
			** 0 means always enable UG/EPT */
		uint32_t reserved:30;
		uint32_t enabled:1; // 0 means not enable EPT
	} bits;
	uint32_t uint32;
} ept_policy_t;

typedef union {
	struct {
		uint32_t r:1; // bit[0] readable
		uint32_t w:1; // bit[1] writable
		uint32_t x:1; // bit[2] execute
		uint32_t emt:3; // bit[5:3] ept memory type
		uint32_t ve:1; // bit[6] #VE
		uint32_t rsvd:25;
	} bits;
	uint32_t uint32;
} ept_attr_t;

typedef union {
	struct {
		uint64_t emt:3; // bit[2:0], ept memory type
		uint64_t gaw:3; // bit[5:3], must be 3 since ept page-walk length is 4
		uint64_t ad:1; // bit[6]: enable access/dirty bits
		uint64_t rsvd:5; // bit[11:7]
		uint64_t pml4_hpa:52; // bit [63:12]
	} bits;
	uint64_t uint64;
} eptp_t;

void ept_guest_init(guest_handle_t guest);
void ept_gcpu_init(guest_cpu_handle_t gcpu);
void vmexit_ept_misconfiguration(guest_cpu_handle_t gcpu);
void vmexit_ept_violation(guest_cpu_handle_t gcpu);

#endif
