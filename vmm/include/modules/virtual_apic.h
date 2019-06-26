/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _VIRTUAL_APIC_H_
#define _VIRTUAL_APIC_H_

#ifndef MODULE_VIRTUAL_APIC
#error "MODULE_VIRTUAL_APIC is not defined"
#endif

typedef struct {
	uint32_t intr[8]; // 256 bits: store interrupt 0x00~0xFF in IRR
} irr_t;

/*
 * Initialize virtual apic parameter
 */
void virtual_apic_init(void);

/*
 * Set a vector into VIRR
 * [IN] gcpu: pointer to guest cpu
 * [IN] vector: the vector to be set
 */
void vapic_set_pending_intr(guest_cpu_handle_t gcpu, uint8_t vector);

/*
 * Get the highest vector in VIRR
 * [IN] gcpu: pointer to guest cpu
 * [OUT] return the highest vector in VIRR
 */
uint8_t vapic_get_pending_intr(guest_cpu_handle_t gcpu);

/*
 * Clear a vector into VIRR
 * [IN] gcpu: pointer to guest cpu
 * [IN] vector: the vector to be cleared
 */
void vapic_clear_pending_intr(guest_cpu_handle_t gcpu, uint8_t vector);

/*
 * Get all VIRR list
 * [IN] gcpu: pointer to guest cpu
 * [OUT]*p_virr: pointer to the structure save VIRR list
 */
void vapic_get_virr(guest_cpu_handle_t gcpu, irr_t * p_virr);

/*
 * Merge the given VIRR list into existig VIRR
 * [IN] gcpu: pointer to guest cpu
 * [IN]*p_virr: pointer to the structure of the VIRR list to be merged
 */
void vapic_merge_virr(guest_cpu_handle_t gcpu, irr_t * p_virr);

/*
 * Clear all VIRR list
 * [IN] gcpu: pointer to guest cpu
 */
void vapic_clear_virr(guest_cpu_handle_t gcpu);

#endif /* _VIRTUAL_APIC_H_ */


