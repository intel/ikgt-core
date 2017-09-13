/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "gcpu.h"
#include "gcpu_state.h"
#include "guest.h"
#include "heap.h"
#include "gpm.h"
#include "scheduler.h"
#include "hmm.h"
#include "page_walker.h"
#include "evmm_desc.h"
#include "host_cpu.h"
#include "vmexit_cr_access.h"
#include "vmm_arch.h"
#include "stack.h"
#include "vmx_cap.h"
#include "event.h"

#include "lib/util.h"

typedef struct {
	vmcs_field_t sel, base, limit, ar;
} segment_2_vmcs_t;

#define SEG_FIELD(SEG) \
{ \
	SEG##_SEL, \
	SEG##_BASE, \
	SEG##_LIMIT, \
	SEG##_AR \
}

/* encoding table for segments */
const segment_2_vmcs_t g_segment_2_vmcs[SEG_COUNT] = {
	SEG_FIELD(VMCS_GUEST_CS),
	SEG_FIELD(VMCS_GUEST_DS),
	SEG_FIELD(VMCS_GUEST_SS),
	SEG_FIELD(VMCS_GUEST_ES),
	SEG_FIELD(VMCS_GUEST_FS),
	SEG_FIELD(VMCS_GUEST_GS),
	SEG_FIELD(VMCS_GUEST_LDTR),
	SEG_FIELD(VMCS_GUEST_TR)
};

/* ---------------------------- APIs ---------------------------------------- */
guest_cpu_handle_t gcpu_allocate(void)
{
	guest_cpu_handle_t gcpu = NULL;

	/* allocate next gcpu */
	gcpu = (guest_cpu_handle_t)mem_alloc(sizeof(struct guest_cpu_t));

	memset(gcpu, 0, sizeof(struct guest_cpu_t));

	/* gcpu->vmcs = vmcs_allocate(); */

	gcpu->gp_ptr = (uint64_t *)(&(gcpu->gp_reg[0]));

	gcpu->vmcs = vmcs_create();
	D(VMM_ASSERT(gcpu->vmcs));

	return gcpu;
}

void gcpu_set_cr2(const guest_cpu_handle_t gcpu,
					uint64_t cr2)
{
	event_set_cr2_t event_set_cr2;

	event_set_cr2.cr2 = cr2;
	event_set_cr2.handled = FALSE;
	event_raise(gcpu, EVENT_SET_CR2, (void *)&event_set_cr2);
	if (!event_set_cr2.handled) { //CR2 is not isolated
		asm_set_cr2(cr2);
	}
}

uint64_t gcpu_get_gp_reg(const guest_cpu_handle_t gcpu,
					gp_reg_t reg)
{
	D(VMM_ASSERT(gcpu));
	D(VMM_ASSERT(reg < REG_GP_COUNT));

	switch (reg) {
	case REG_RSP:
		return vmcs_read(gcpu->vmcs, VMCS_GUEST_RSP);
	default:
		return gcpu->gp_ptr[reg];
	}
}

void gcpu_set_gp_reg(guest_cpu_handle_t gcpu,
				    gp_reg_t reg, uint64_t value)
{
	D(VMM_ASSERT(gcpu));
	D(VMM_ASSERT(reg < REG_GP_COUNT));

	switch (reg) {
	case REG_RSP:
		vmcs_write(gcpu->vmcs,
			VMCS_GUEST_RSP, value);
		return;
	default:
		gcpu->gp_ptr[reg] = value;
		return;
	}
}

void gcpu_get_seg(const guest_cpu_handle_t gcpu,
				  seg_id_t reg,
				  uint16_t *selector, uint64_t *base,
				  uint32_t *limit, uint32_t *attributes)
{
	const segment_2_vmcs_t *seg2vmcs;
	vmcs_obj_t vmcs;

	D(VMM_ASSERT(gcpu));
	D(VMM_ASSERT(reg < SEG_COUNT));

	vmcs = gcpu->vmcs;

	seg2vmcs = &g_segment_2_vmcs[reg];

	if (selector) {
		*selector = (uint16_t)vmcs_read(vmcs, seg2vmcs->sel);
	}

	if (base) {
		*base = vmcs_read(vmcs, seg2vmcs->base);
	}

	if (limit) {
		*limit = (uint32_t)vmcs_read(vmcs, seg2vmcs->limit);
	}

	if (attributes) {
		*attributes = (uint32_t)vmcs_read(vmcs, seg2vmcs->ar);
	}
}

void gcpu_set_seg(guest_cpu_handle_t gcpu,
				  seg_id_t reg,
				  uint16_t selector, uint64_t base,
				  uint32_t limit, uint32_t attributes)
{
	const segment_2_vmcs_t *seg2vmcs;
	vmcs_obj_t vmcs;

	D(VMM_ASSERT(gcpu));
	D(VMM_ASSERT(reg < SEG_COUNT));

	vmcs = gcpu->vmcs;

	seg2vmcs = &g_segment_2_vmcs[reg];

	vmcs_write(vmcs, seg2vmcs->sel, selector);
	vmcs_write(vmcs, seg2vmcs->base, base);
	vmcs_write(vmcs, seg2vmcs->limit, limit);
	vmcs_write(vmcs, seg2vmcs->ar, attributes);
}

uint64_t gcpu_get_visible_cr0(const guest_cpu_handle_t gcpu)
{
	uint64_t mask;
	uint64_t shadow;
	uint64_t real_value;

	D(VMM_ASSERT(gcpu));

	real_value = vmcs_read(gcpu->vmcs, VMCS_GUEST_CR0);
	mask = vmcs_read(gcpu->vmcs, VMCS_CR0_MASK);
	shadow = vmcs_read(gcpu->vmcs, VMCS_CR0_SHADOW);

	return (real_value & ~mask) | (shadow & mask);
}

uint64_t gcpu_get_visible_cr4(const guest_cpu_handle_t gcpu)
{
	uint64_t mask;
	uint64_t shadow;
	uint64_t real_value;

	D(VMM_ASSERT(gcpu));

	real_value = vmcs_read(gcpu->vmcs, VMCS_GUEST_CR4);
	mask = vmcs_read(gcpu->vmcs, VMCS_CR4_MASK);
	shadow = vmcs_read(gcpu->vmcs, VMCS_CR4_SHADOW);

	return (real_value & ~mask) | (shadow & mask);
}

void gcpu_set_pending_intr(const guest_cpu_handle_t gcpu, uint8_t vector)
{
	uint8_t group;
	uint8_t index;

	D(VMM_ASSERT(gcpu));
	D(VMM_ASSERT(vector >= 0x20);) // first 32 entry in IDT is reserved

	group = vector>>5;
	index = vector & 0x1F;

	gcpu->pending_intr[group] |= (1U<<index);
	gcpu->pending_intr[0] |= (1U<<group);
}

void gcpu_clear_pending_intr(const guest_cpu_handle_t gcpu, uint8_t vector)
{
	uint8_t group;
	uint8_t index;

	D(VMM_ASSERT(gcpu));
	D(VMM_ASSERT(vector >= 0x20);) // first 32 entry in IDT is reserved

	group = vector>>5;
	index = vector & 0x1F;

	if (gcpu->pending_intr[group] == (1U<<index))
	{
		gcpu->pending_intr[group] = 0;
		gcpu->pending_intr[0] &= ~(1U<<group);
	}
	else
	{
		gcpu->pending_intr[group] &= ~(1U<<index);
	}
}

uint8_t gcpu_get_pending_intr(const guest_cpu_handle_t gcpu)
{
	uint8_t group;
	uint8_t index;

	D(VMM_ASSERT(gcpu));

	if (gcpu->pending_intr[0] == 0)
		return 0; // invalid vector

	group = (uint8_t)asm_bsr32(gcpu->pending_intr[0]);
	D(VMM_ASSERT(group != 0));
	D(VMM_ASSERT(group < 8));
	index = (uint8_t)asm_bsr32(gcpu->pending_intr[group]);
	return (group<<5|index);
}

void gcpu_update_guest_mode(const guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs = gcpu->vmcs;
	uint64_t efer;
	uint32_t entry_ctrl;
	uint64_t cr0;

	// update guest mode
	cr0 = vmcs_read(vmcs, VMCS_GUEST_CR0);
	efer = vmcs_read(vmcs, VMCS_GUEST_EFER);
	entry_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_ENTRY_CTRL);

	if ((cr0 & CR0_PG) && (efer & EFER_LME)) {
		efer |= EFER_LMA;
		entry_ctrl |= ENTRY_GUEST_IA32E_MODE;
	}else{
		efer &= ~EFER_LMA;
		entry_ctrl &= ~ENTRY_GUEST_IA32E_MODE;
	}

	vmcs_write(vmcs, VMCS_GUEST_EFER, efer);
	vmcs_write(vmcs, VMCS_ENTRY_CTRL, entry_ctrl);
}

void gcpu_skip_instruction(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs;
	uint64_t inst_length;
	uint64_t rip;

	D(VMM_ASSERT(gcpu));

	vmcs = gcpu->vmcs;
	inst_length = vmcs_read(vmcs, VMCS_EXIT_INSTR_LEN);
	rip = vmcs_read(vmcs, VMCS_GUEST_RIP);
	vmcs_write(vmcs, VMCS_GUEST_RIP, rip + inst_length);
}

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
boolean_t gcpu_gva_to_hva(guest_cpu_handle_t gcpu,
				uint64_t gva,
				uint32_t access,
				uint64_t *p_hva,
				pf_ec_t *p_pfec)
{
	uint64_t gpa;

	D(VMM_ASSERT(gcpu);)
	VMM_ASSERT_EX(p_hva, "p_hva is NULL\n");
	VMM_ASSERT_EX(p_pfec, "p_pfec is NULL\n");
	VMM_ASSERT_EX((access & (GUEST_CAN_READ | GUEST_CAN_WRITE)) != 0,
		"%s: access(%d) is invalid\n",__FUNCTION__, access);

	if (!gcpu_gva_to_gpa(gcpu, gva, access, &gpa, p_pfec)) {
		print_warn("%s: Failed to convert gva=0x%llX to gpa with access(%d)\n",
			__FUNCTION__, gva, access);
		return FALSE;
	}

	if (!gpm_gpa_to_hva(gcpu->guest, gpa, access, p_hva)) {
		print_warn("%s: Failed to convert gpa=0x%llX to hva with access(%d)\n",
			__FUNCTION__, gpa, access);
		//set error code to 0(not #PF in guest)
		p_pfec->is_pf = FALSE;
		p_pfec->ec = 0;
		return FALSE;
	}

	return TRUE;
}

boolean_t gcpu_copy_from_gva(guest_cpu_handle_t gcpu,
				uint64_t src_gva,
				uint64_t dst_hva,
				uint64_t size,
				pf_info_t *p_pfinfo)
{
	uint64_t cnt;
	uint64_t src_hva;
	pf_ec_t pfec;

	D(VMM_ASSERT(gcpu));
	VMM_ASSERT_EX(dst_hva, "dst_hva is NULL\n");
	VMM_ASSERT_EX(p_pfinfo, "p_pfinfo is NULL\n");

	while (size > 0) {
		if (!gcpu_gva_to_hva(gcpu, src_gva, GUEST_CAN_READ, &src_hva, &pfec)) {
			p_pfinfo->is_pf = pfec.is_pf;
			p_pfinfo->ec = pfec.ec;
			p_pfinfo->cr2 = src_gva;
			return FALSE;
		}

		cnt = MIN(size, PAGE_4K_SIZE - (src_hva & PAGE_4K_MASK));
		memcpy((void *)dst_hva, (void *)src_hva, cnt);
		size -= cnt;
		src_gva += cnt;
		dst_hva += cnt;
	}

	return TRUE;
}

boolean_t gcpu_copy_to_gva(guest_cpu_handle_t gcpu,
				uint64_t dst_gva,
				uint64_t src_hva,
				uint64_t size,
				pf_info_t *p_pfinfo)
{
	uint64_t cnt;
	uint64_t dst_hva;
	pf_ec_t pfec;

	D(VMM_ASSERT(gcpu));
	VMM_ASSERT_EX(src_hva, "src_hva is NULL\n");
	VMM_ASSERT_EX(p_pfinfo, "p_pfinfo is NULL\n");

	while (size > 0) {
		if (!gcpu_gva_to_hva(gcpu, dst_gva, GUEST_CAN_WRITE, &dst_hva, &pfec)) {
			p_pfinfo->is_pf = pfec.is_pf;
			p_pfinfo->ec = pfec.ec;
			p_pfinfo->cr2 = dst_gva;
			return FALSE;
		}

		cnt = MIN(size, PAGE_4K_SIZE - (dst_hva & PAGE_4K_MASK));
		memcpy((void *)dst_hva, (void *)src_hva, cnt);
		size -= cnt;
		dst_gva += cnt;
		src_hva += cnt;
	}

	return TRUE;
}
