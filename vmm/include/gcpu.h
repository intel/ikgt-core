/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _GCPU_H_
#define _GCPU_H_

#include "vmm_arch.h"
#include "vmm_objects.h"
#include "vmexit.h"
#include "evmm_desc.h"
#include "dbg.h"
#include "vmcs.h"
#include "host_cpu.h"

/**************************************************************************
*
* Define guest-related global structures
*
**************************************************************************/
struct guest_cpu_t {
	uint64_t			gp_reg[REG_GP_COUNT];
	uint64_t			*gp_ptr;
	vmcs_obj_t			vmcs;
	guest_handle_t		guest;

	uint16_t			id;
	uint8_t				is_vmentry_fail;
	uint8_t				pad[5];

	struct guest_cpu_t	*next_same_host_cpu;
	struct guest_cpu_t	*next_same_guest;

	uint32_t			pending_intr[8]; // pending_int[0] is used as a global indictor.
							 // pending_int[1~7] is used to store interrupt 0x20~0xFF
};

typedef struct {
	boolean_t is_pf; //page fault
	uint16_t ec; //error code
	uint16_t pad;
} pf_ec_t;

typedef struct {
	boolean_t is_pf;
	uint16_t ec;
	uint16_t pad;
	uint64_t cr2;
} pf_info_t;

void gcpu_set_cr2(const guest_cpu_handle_t gcpu, uint64_t cr2);

uint64_t gcpu_get_gp_reg(const guest_cpu_handle_t gcpu, gp_reg_t reg);
void gcpu_set_gp_reg(guest_cpu_handle_t gcpu, gp_reg_t reg, uint64_t value);

void gcpu_get_seg(const guest_cpu_handle_t gcpu,
			seg_id_t reg,
			uint16_t *selector,
			uint64_t *base,
			uint32_t *limit,
			uint32_t *attributes);
void gcpu_set_seg(guest_cpu_handle_t gcpu,
			seg_id_t reg,
			uint16_t selector,
			uint64_t base,
			uint32_t limit,
			uint32_t attributes);

uint64_t gcpu_get_visible_cr0(const guest_cpu_handle_t gcpu);
uint64_t gcpu_get_visible_cr4(const guest_cpu_handle_t gcpu);
void gcpu_update_guest_mode(const guest_cpu_handle_t gcpu);

/*-------------------------------------------------------------------------
 * Function: gcpu_gva_to_hva
 *  Description: This function is used in order to convert Guest Virtual Address
 *               to Host Virtual Address (GVA-->HVA).
 *  Input:  gcpu - guest cpu handle.
 *	    gva - guest virtual address.
 *          access - access rights, can be read, write, or read and write
 *  Output: p_hva - host virtual address, it is valid when return true.
 *          p_pfec - page fault error code, it is valid when return false.
 *  Return Value: TRUE in case the mapping successful (it exists).
 *------------------------------------------------------------------------- */
boolean_t gcpu_gva_to_hva(guest_cpu_handle_t gcpu, uint64_t gva, uint32_t access, uint64_t *p_hva, pf_ec_t *p_pfec);
/* COPY DATA FROM GVA to HVA */
boolean_t gcpu_copy_from_gva(guest_cpu_handle_t gcpu, uint64_t src_gva, uint64_t dst_hva, uint64_t size, pf_info_t *p_pfinfo);
/* COPY DATA FOR HVA to GVA */
boolean_t gcpu_copy_to_gva(guest_cpu_handle_t gcpu, uint64_t dst_gva, uint64_t src_hva, uint64_t size, pf_info_t *p_pfinfo);

/* Private API for guest.c */
guest_cpu_handle_t gcpu_allocate(void);

void gcpu_skip_instruction(guest_cpu_handle_t gcpu);

void gcpu_set_pending_intr(const guest_cpu_handle_t gcpu, uint8_t vector);
void gcpu_clear_pending_intr(const guest_cpu_handle_t gcpu, uint8_t vector);
uint8_t gcpu_get_pending_intr(const guest_cpu_handle_t gcpu);

#endif   /* _GCPU_H_ */
