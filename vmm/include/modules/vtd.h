/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _VTD_H_
#define _VTD_H_

#ifndef MODULE_VTD
#error "MODULE_VTD is not defined"
#endif

#include "vmm_base.h"

void vtd_init(void);
void vtd_activate(void);

/* According to VT-Directed-IO specification:
 *     When Caching Mode(CM) field in Capability Register is reported as
 *     Set, the domain-id value of zero is architecturally reserved. Software
 *     must not use domain-id value of zero when CM is set.
 * Currently, Guest-id range is [0....N].
 * So this function mapping domain_id=guest_id+1 to avoid "domain_id=0".
 */
inline uint16_t gid2did(uint16_t guest_id)
{
	return guest_id + 1;
}

#ifdef MULTI_GUEST_DMA
void vtd_assign_dev(uint16_t domain_id, uint16_t dev_id);
#endif

#endif /* _VTD_H_ */
