/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _LAPIC_ID_H
#define _LAPIC_ID_H

#ifndef MODULE_LAPIC_ID
#error "MODULE_LAPIC_ID is not defined"
#endif

#include "vmm_base.h"

/*IA32 spec, volume3, chapter 10 APIC->Local APIC->Local APIC ID
 *Local APIC ID usually not be changed*/
uint32_t get_lapic_id(uint16_t hcpu);

// need to be called for all cpus
void lapic_id_init(void);

#endif
