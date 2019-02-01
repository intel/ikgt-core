/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _MSR_ISOLATION_H_
#define _MSR_ISOLATION_H_

#ifndef MODULE_MSR_ISOLATION
#error "MODULE_MSR_ISOLATION is not defined"
#endif

typedef enum {
	GUESTS_ISOLATION = 0,
	GUEST_HOST_ISOLATION
} msr_policy_t;

void add_to_msr_isolation_list(uint32_t msr_index, uint64_t msr_value, msr_policy_t msr_policy);
void msr_isolation_init();

#endif /* _MSR_ISOLATION_H_ */
