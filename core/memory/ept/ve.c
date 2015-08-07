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
#include "file_codes.h"
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VE_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VE_C, __condition)
#include "vmcs_init.h"
#include "host_memory_manager_api.h"
#include "guest.h"
#include "guest_cpu_internal.h"
#include "isr.h"
#include "guest_cpu_vmenter_event.h"
#include "ve.h"
#include "memory_address_mapper_api.h"
#include "gpm_api.h"

boolean_t mon_ve_is_hw_supported(void)
{
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();

	return hw_constraints->ve_supported;
}

boolean_t mon_ve_is_ve_enabled(guest_cpu_handle_t gcpu)
{
	return gcpu->ve_desc.ve_enabled;
}

boolean_t mon_ve_update_hpa(guest_id_t guest_id,
			    cpu_id_t guest_cpu_id,
			    hpa_t hpa,
			    uint32_t enable)
{
	guest_gcpu_econtext_t gcpu_context;
	guest_handle_t guest;
	guest_cpu_handle_t gcpu;
	virtual_cpu_id_t vcpu_id;

	/* check if hpa is used by other CPUs in previous enables */
	if (enable) {
		guest = mon_guest_handle(guest_id);
		for (gcpu = mon_guest_gcpu_first(guest, &gcpu_context); gcpu;
		     gcpu = mon_guest_gcpu_next(&gcpu_context)) {
			if (!mon_ve_is_ve_enabled(gcpu)) {
				continue;
			}
			if (gcpu->vcpu.guest_cpu_id == guest_cpu_id) {
				continue;
			}
			if (gcpu->ve_desc.ve_info_hpa == hpa) {
				return FALSE;
			}
		}
	}

	vcpu_id.guest_id = guest_id;
	vcpu_id.guest_cpu_id = guest_cpu_id;
	gcpu = gcpu_state(&vcpu_id);
	/* paranoid check. If assertion fails, possible memory corruption. */
	MON_ASSERT(gcpu);

	gcpu->ve_desc.ve_info_hpa = hpa;
	return TRUE;
}

static
void ve_activate_hw_ve(guest_cpu_handle_t gcpu, boolean_t enable)
{
	processor_based_vm_execution_controls2_t proc_ctrls2;
	vmexit_control_t vmexit_request;

	proc_ctrls2.uint32 = 0;
	mon_zeromem(&vmexit_request, sizeof(vmexit_request));

	proc_ctrls2.bits.ve = 1;
	vmexit_request.proc_ctrls2.bit_mask = proc_ctrls2.uint32;
	vmexit_request.proc_ctrls2.bit_request = enable ? UINT64_ALL_ONES : 0;

	gcpu_control2_setup(gcpu, &vmexit_request);
}

void mon_ve_enable_ve(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs;
	hva_t hva;

	if (mon_ve_is_hw_supported()) {
		vmcs = mon_gcpu_get_vmcs(gcpu);
		mon_vmcs_write(vmcs, VMCS_VE_INFO_ADDRESS,
			(uint64_t)gcpu->ve_desc.ve_info_hpa);
		ve_activate_hw_ve(gcpu, TRUE);
	} else {
		if (mon_hmm_hpa_to_hva(gcpu->ve_desc.ve_info_hpa,
			    &hva) == FALSE) {
			return;
		}
		gcpu->ve_desc.ve_info_hva = hva;
	}

	gcpu->ve_desc.ve_enabled = TRUE;
}

void mon_ve_disable_ve(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs;

	if (mon_ve_is_hw_supported()) {
		ve_activate_hw_ve(gcpu, FALSE);
		vmcs = mon_gcpu_get_vmcs(gcpu);
		mon_vmcs_write(vmcs, VMCS_VE_INFO_ADDRESS, 0);
	}

	gcpu->ve_desc.ve_enabled = FALSE;
}

/* returns TRUE - SW #VE injected */
boolean_t mon_ve_handle_sw_ve(guest_cpu_handle_t gcpu, uint64_t qualification,
			      uint64_t gla, uint64_t gpa, uint64_t view)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	ve_ept_info_t *hva;
	guest_handle_t guest;
	ia32_vmcs_exception_bitmap_t exceptions;
	em64t_cr0_t cr0;
	hpa_t hpa;
	mam_attributes_t attrs;
	ia32_vmx_vmcs_vmexit_info_idt_vectoring_t idt_vectoring_info;

	if (mon_ve_is_hw_supported()) {
		return FALSE;
	}

	if (!mon_ve_is_ve_enabled(gcpu)) {
		return FALSE;
	}

	hva = (ve_ept_info_t *)gcpu->ve_desc.ve_info_hva;
	MON_ASSERT(hva);

	/* check flag */
	if (hva->flag != 0) {
		return FALSE;
	}

	/* check PE */
	cr0.uint64 = mon_vmcs_read(vmcs, VMCS_GUEST_CR0);
	if (0 == cr0.bits.pe) {
		return FALSE;
	}

	/* check the logical processor is not in the process of delivering an
	 * event through the IDT */
	idt_vectoring_info.uint32 =
		(uint32_t)mon_vmcs_read(vmcs, VMCS_EXIT_INFO_IDT_VECTORING);
	if (idt_vectoring_info.bits.valid) {
		return FALSE;
	}

	/* check exception bitmap bit 20 */
	exceptions.uint32 =
		(uint32_t)mon_vmcs_read(vmcs, VMCS_EXCEPTION_BITMAP);
	if (exceptions.bits.ve) {
		return FALSE;
	}

	/* check eptp pte bit 63 */
	guest = mon_gcpu_guest_handle(gcpu);
	if (!mon_gpm_gpa_to_hpa(gcpu_get_current_gpm(guest),
		    gpa, &hpa, &attrs)) {
		return FALSE;
	}
	if (attrs.ept_attr.suppress_ve) {
		return FALSE;
	}

	hva->exit_reason = IA32_VMX_EXIT_BASIC_REASON_EPT_VIOLATION;
	/* must clear flag in ISR */
	hva->flag = 0xFFFFFFFF;
	hva->exit_qualification = qualification;
	hva->gla = gla;
	hva->gpa = gpa;
	hva->eptp_index = (uint16_t)view;

	/* Check again for ve getting disabled to avoid injecting late #VE */
	if (!mon_ve_is_ve_enabled(gcpu)) {
		hva->flag = 0;
		return FALSE;
	}

	/* If VE gets disabled here ve flag is already set so ve_lib will wait */

	/* inject soft #VE */
	return gcpu_inject_fault(gcpu,
		IA32_EXCEPTION_VECTOR_VIRTUAL_EXCEPTION,
		0);
}
