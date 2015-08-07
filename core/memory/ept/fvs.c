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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(FVS_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(FVS_C, __condition)
#include "vmcs_init.h"
#include "ept.h"
#include "ept_hw_layer.h"
#include "host_memory_manager_api.h"
#include "scheduler.h"
#include "vmx_asm.h"
#include "ipc.h"
#include "vmx_ctrl_msrs.h"
#include "guest_internal.h"
#include "guest_cpu_internal.h"
#include "isr.h"
#include "guest_cpu_vmenter_event.h"
#include "fvs.h"
#include "mon_callback.h"
#include "common_types.h"

static
void fvs_init_eptp_switching(guest_descriptor_t *guest);
static
hpa_t fvs_get_eptp_list_paddress(guest_cpu_handle_t gcpu);

extern uint32_t vmexit_reason(void);
extern boolean_t vmcs_sw_shadow_disable[];

void fvs_initialize(guest_handle_t guest, uint32_t number_of_host_processors)
{
	guest->fvs_desc = (fvs_descriptor_t *)mon_malloc(
		sizeof(fvs_descriptor_t));

	MON_ASSERT(guest->fvs_desc);
	guest->fvs_desc->num_of_cpus = number_of_host_processors;
	guest->fvs_desc->eptp_list_paddress =
		mon_malloc(sizeof(hpa_t) * number_of_host_processors);
	guest->fvs_desc->eptp_list_vaddress =
		mon_malloc(sizeof(hva_t) * number_of_host_processors);

	MON_LOG(mask_anonymous, level_trace,
		"fvs desc allocated...=0x%016lX\n", guest->fvs_desc);
	fvs_init_eptp_switching(guest);
}

static
void fvs_init_eptp_switching(guest_descriptor_t *guest)
{
	uint32_t i;

	for (i = 0; i < guest->fvs_desc->num_of_cpus; i++) {
		guest->fvs_desc->eptp_list_vaddress[i] =
			(hva_t)mon_page_alloc(1);
		mon_memset((uint64_t *)guest->fvs_desc->eptp_list_vaddress[i],
			0,
			PAGE_4KB_SIZE);
		MON_ASSERT(guest->fvs_desc->eptp_list_vaddress[i]);
		if (!mon_hmm_hva_to_hpa(guest->fvs_desc->eptp_list_vaddress[i],
			    &guest->fvs_desc->eptp_list_paddress[i])) {
			MON_LOG(mask_anonymous,
				level_error,
				"%s:(%d):ASSERT: hva_t to hpa_t conversion failed\n",
				__FUNCTION__,
				__LINE__);
			MON_DEADLOOP();
		}
		MON_LOG(mask_anonymous, level_trace,
			"eptp list allocated...vaddr=0x%016lX paddr=0x%016lX\n",
			guest->fvs_desc->eptp_list_vaddress[i],
			guest->fvs_desc->eptp_list_paddress[i]);
	}
}

boolean_t fvs_is_eptp_switching_supported(void)
{
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();

	return hw_constraints->eptp_switching_supported;
}

void fvs_guest_vmfunc_enable(guest_cpu_handle_t gcpu)
{
	processor_based_vm_execution_controls2_t ctrls2;
	vmexit_control_t request;

	ctrls2.uint32 = 0;
	mon_zeromem(&request, sizeof(request));

	ctrls2.bits.vmfunc = 1;
	request.proc_ctrls2.bit_mask = ctrls2.uint32;
	request.proc_ctrls2.bit_request = UINT64_ALL_ONES;
	gcpu_control2_setup(gcpu, &request);
}

static
hpa_t fvs_get_eptp_list_paddress(guest_cpu_handle_t gcpu)
{
	guest_handle_t guest = mon_gcpu_guest_handle(gcpu);
	const virtual_cpu_id_t *vcpu_id = mon_guest_vcpu(gcpu);

	MON_ASSERT(guest);
	MON_ASSERT(guest->fvs_desc);
	MON_ASSERT(vcpu_id);

	return guest->fvs_desc->eptp_list_paddress[vcpu_id->guest_cpu_id];
}

/* This function is used to add a new entry to eptp_t lists of all CPUs, or
 * update an entry blindly if caller ensures the entry is already in eptp_t lists
 * of all CPUs.
 */
boolean_t mon_fvs_add_entry_to_eptp_list(guest_handle_t guest,
					 hpa_t ept_root_hpa,
					 uint32_t gaw,
					 uint64_t index)
{
	uint64_t *hva = NULL;
	eptp_t eptp;
	uint32_t ept_gaw = 0, i;

	MON_ASSERT(guest->fvs_desc);

	if (index < MAX_EPTP_ENTRIES) {
		ept_gaw = mon_ept_hw_get_guest_address_width(gaw);
		if (ept_gaw == (uint32_t)-1) {
			return FALSE;
		}
		eptp.uint64 = ept_root_hpa;
		eptp.bits.etmt = mon_ept_hw_get_ept_memory_type();
		eptp.bits.gaw = mon_ept_hw_get_guest_address_width_encoding(
			ept_gaw);
		eptp.bits.reserved = 0;
		MON_LOG(mask_anonymous,
			level_trace,
			"adding eptp entry eptp=0x%016lX index=%d\n",
			eptp.uint64,
			index);
	} else {
		return FALSE;
	}

	for (i = 0; i < guest->fvs_desc->num_of_cpus; i++) {
		hva = (uint64_t *)guest->fvs_desc->eptp_list_vaddress[i];
		*(hva + index) = eptp.uint64;
	}

	return TRUE;
}

/* This function is used to update only the existing entry in eptp_t lists of all
 * CPUs.
 * If the entry is not in eptp_t list of some CPU, this function would not
 * add it to the eptp_t list of that CPU.
 */
boolean_t mon_fvs_update_entry_in_eptp_list(guest_handle_t guest,
					    hpa_t ept_root_hpa,
					    uint32_t gaw,
					    uint64_t index)
{
	uint64_t *hva = NULL;
	eptp_t eptp;
	uint32_t ept_gaw = 0, i;

	MON_ASSERT(guest->fvs_desc);

	if (index < MAX_EPTP_ENTRIES) {
		ept_gaw = mon_ept_hw_get_guest_address_width(gaw);
		if (ept_gaw == (uint32_t)-1) {
			return FALSE;
		}
		eptp.uint64 = ept_root_hpa;
		eptp.bits.etmt = mon_ept_hw_get_ept_memory_type();
		eptp.bits.gaw = mon_ept_hw_get_guest_address_width_encoding(
			ept_gaw);
		eptp.bits.reserved = 0;
		MON_LOG(mask_anonymous,
			level_trace,
			"adding eptp entry eptp=0x%016lX index=%d\n",
			eptp.uint64,
			index);
	} else {
		return FALSE;
	}

	for (i = 0; i < guest->fvs_desc->num_of_cpus; i++) {
		hva = (uint64_t *)guest->fvs_desc->eptp_list_vaddress[i];
		/* Only update existing entry */
		if (*(hva + index) != 0) {
			*(hva + index) = eptp.uint64;
		}
	}

	return TRUE;
}

boolean_t mon_fvs_add_entry_to_eptp_list_single_core(guest_handle_t guest,
						     cpu_id_t cpu_id,
						     hpa_t ept_root_hpa,
						     uint32_t gaw,
						     uint64_t index)
{
	uint64_t *hva = NULL;
	eptp_t eptp;
	uint32_t ept_gaw = 0;

	MON_ASSERT(guest->fvs_desc);
	MON_ASSERT(cpu_id < guest->fvs_desc->num_of_cpus);

	if (index < MAX_EPTP_ENTRIES) {
		ept_gaw = mon_ept_hw_get_guest_address_width(gaw);
		if (ept_gaw == (uint32_t)-1) {
			return FALSE;
		}
		eptp.uint64 = ept_root_hpa;
		eptp.bits.etmt = mon_ept_hw_get_ept_memory_type();
		eptp.bits.gaw = mon_ept_hw_get_guest_address_width_encoding(
			ept_gaw);
		eptp.bits.reserved = 0;
		MON_LOG(mask_anonymous, level_trace,
			"adding eptp entry at index=%d for CPU %d\n",
			index, cpu_id);
	} else {
		return FALSE;
	}

	hva = (uint64_t *)guest->fvs_desc->eptp_list_vaddress[cpu_id];
	*(hva + index) = eptp.uint64;

	return TRUE;
}

boolean_t mon_fvs_delete_entry_from_eptp_list(guest_handle_t guest,
					      uint64_t index)
{
	uint64_t *hva = NULL;
	uint32_t i;

	MON_ASSERT(guest->fvs_desc);

	if (index < MAX_EPTP_ENTRIES) {
		MON_LOG(mask_anonymous, level_trace,
			"deleting eptp entry at index=%d\n", index);
	} else {
		return FALSE;
	}

	for (i = 0; i < guest->fvs_desc->num_of_cpus; i++) {
		hva = (uint64_t *)guest->fvs_desc->eptp_list_vaddress[i];
		*(hva + index) = 0;
	}

	return TRUE;
}

boolean_t mon_fvs_delete_entry_from_eptp_list_single_core(guest_handle_t guest,
							  cpu_id_t cpu_id,
							  uint64_t index)
{
	uint64_t *hva = NULL;

	MON_ASSERT(guest->fvs_desc);
	MON_ASSERT(cpu_id < guest->fvs_desc->num_of_cpus);

	if (index < MAX_EPTP_ENTRIES) {
		MON_LOG(mask_anonymous,
			level_trace,
			"deleting eptp entry at index=%d for CPU %d\n",
			index,
			cpu_id);
	} else {
		return FALSE;
	}

	hva = (uint64_t *)guest->fvs_desc->eptp_list_vaddress[cpu_id];
	*(hva + index) = 0;

	return TRUE;
}

void fvs_vmfunc_vmcs_init(guest_cpu_handle_t gcpu)
{
	uint64_t value;
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	value = mon_vmcs_read(vmcs, VMCS_VMFUNC_CONTROL);
	MON_LOG(mask_anonymous, level_trace,
		"HW vmfunc ctrl read value = 0x%016lX\n", value);
	BIT_CLR(value, EPTP_SWITCHING_BIT);
	MON_LOG(mask_anonymous, level_trace,
		"HW vmfunc ctrl bitclr value = 0x%016lX\n", value);
	mon_vmcs_write(vmcs, VMCS_VMFUNC_CONTROL, value);
	MON_LOG(mask_anonymous, level_trace,
		"EPTP switching disabled...0x%016lX\n", value);
}


void mon_fvs_enable_eptp_switching(cpu_id_t from UNUSED, void *arg)
{
	uint64_t value = 0;
	guest_handle_t guest = (guest_handle_t)arg;
	guest_cpu_handle_t gcpu =
		mon_scheduler_get_current_gcpu_for_guest(guest_get_id(guest));
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	if (fvs_is_eptp_switching_supported()) {
		value = mon_vmcs_read(vmcs, VMCS_VMFUNC_CONTROL);
		BIT_SET(value, EPTP_SWITCHING_BIT);
		mon_vmcs_write(vmcs, VMCS_VMFUNC_CONTROL, value);
		mon_vmcs_write(vmcs, VMCS_VMFUNC_EPTP_LIST_ADDRESS,
			fvs_get_eptp_list_paddress(gcpu));
	}

	gcpu->fvs_cpu_desc.enabled = TRUE;

	MON_LOG(mask_anonymous, level_trace,
		"EPTP switching enabled by IB-agent...0x%016lX\n", value);
}

void mon_fvs_disable_eptp_switching(cpu_id_t from UNUSED, void *arg)
{
	uint64_t value = 0;
	guest_handle_t guest = (guest_handle_t)arg;
	guest_cpu_handle_t gcpu =
		mon_scheduler_get_current_gcpu_for_guest(guest_get_id(guest));
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	if (fvs_is_eptp_switching_supported()) {
		value = mon_vmcs_read(vmcs, VMCS_VMFUNC_CONTROL);
		BIT_CLR(value, EPTP_SWITCHING_BIT);
		mon_vmcs_write(vmcs, VMCS_VMFUNC_CONTROL, value);
		mon_vmcs_write(vmcs, VMCS_VMFUNC_EPTP_LIST_ADDRESS, 0);
	}
	gcpu->fvs_cpu_desc.enabled = FALSE;
	MON_LOG(mask_anonymous, level_trace,
		"EPTP switching disabled by IB-agent...0x%016lX\n", value);
}


void mon_fvs_enable_fvs(guest_cpu_handle_t gcpu)
{
	guest_handle_t guest = mon_gcpu_guest_handle(gcpu);
	const virtual_cpu_id_t *vcpu_id = mon_guest_vcpu(gcpu);
	uint16_t gcpu_id = 0;
	ipc_destination_t ipc_dest;

	MON_ASSERT(vcpu_id);
	MON_ASSERT(guest->fvs_desc);
	gcpu_id = vcpu_id->guest_cpu_id;

	mon_fvs_enable_eptp_switching(gcpu_id, guest);
	mon_zeromem(&ipc_dest, sizeof(ipc_dest));
	ipc_dest.addr_shorthand = IPI_DST_ALL_EXCLUDING_SELF;
	ipc_execute_handler_sync(ipc_dest, mon_fvs_enable_eptp_switching,
		guest);

	MON_LOG(mask_anonymous, level_trace, "Fast view switch enabled...\n");
}

void mon_fvs_disable_fvs(guest_cpu_handle_t gcpu)
{
	guest_handle_t guest = mon_gcpu_guest_handle(gcpu);
	const virtual_cpu_id_t *vcpu_id = mon_guest_vcpu(gcpu);
	uint16_t gcpu_id = 0;
	ipc_destination_t ipc_dest;

	/* paranoid check. If assertion fails, possible memory corruption. */
	MON_ASSERT(guest);
	MON_ASSERT(vcpu_id);
	MON_ASSERT(guest->fvs_desc);
	gcpu_id = vcpu_id->guest_cpu_id;

	mon_fvs_disable_eptp_switching(gcpu_id, guest);
	mon_zeromem(&ipc_dest, sizeof(ipc_dest));
	ipc_dest.addr_shorthand = IPI_DST_ALL_EXCLUDING_SELF;
	ipc_execute_handler_sync(ipc_dest, mon_fvs_disable_eptp_switching,
		guest);

	MON_LOG(mask_anonymous, level_trace, "Fast view switch disabled...\n");
}

boolean_t mon_fvs_is_fvs_enabled(guest_cpu_handle_t gcpu)
{
	return gcpu->fvs_cpu_desc.enabled;
}

uint64_t mon_fvs_get_eptp_entry(guest_cpu_handle_t gcpu, uint64_t index)
{
	guest_handle_t guest = mon_gcpu_guest_handle(gcpu);
	const virtual_cpu_id_t *vcpu_id = mon_guest_vcpu(gcpu);
	uint64_t *hva = NULL;

	MON_ASSERT(guest);
	MON_ASSERT(guest->fvs_desc);
	MON_ASSERT(vcpu_id);
	hva =
		(uint64_t *)guest->fvs_desc->eptp_list_vaddress[vcpu_id->
								guest_cpu_id];

	if (index < MAX_EPTP_ENTRIES) {
		return *(hva + index);
	} else {
		return 0;
	}
}

hpa_t *mon_fvs_get_all_eptp_list_paddress(guest_cpu_handle_t gcpu)
{
	guest_handle_t guest = mon_gcpu_guest_handle(gcpu);

	MON_ASSERT(guest);
	MON_ASSERT(guest->fvs_desc);

	return guest->fvs_desc->eptp_list_paddress;
}

void fvs_save_resumed_eptp(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	gcpu->fvs_cpu_desc.vmentry_eptp =
		mon_vmcs_read(vmcs, VMCS_EPTP_ADDRESS);
}

void fvs_vmexit_handler(guest_cpu_handle_t gcpu)
{
	uint64_t r_eax, r_ecx, leptp;
	report_set_active_eptp_data_t set_active_eptp_data;
	report_fast_view_switch_data_t fast_view_switch_data;
	vmcs_object_t *vmcs;

	if (vmexit_reason() != IA32_VMX_EXIT_BASIC_REASON_VMCALL_INSTRUCTION) {
		return;
	}

	MON_ASSERT(gcpu);

	r_eax = gcpu_get_native_gp_reg(gcpu, IA32_REG_RAX);

	/* Check whether we drop because of fast view switch */
	if (r_eax != FAST_VIEW_SWITCH_LEAF) {
		return;
	}

	r_ecx = gcpu_get_native_gp_reg(gcpu, IA32_REG_RCX);

	/* Check whether view is valid */
	leptp = mon_fvs_get_eptp_entry(gcpu, r_ecx);
	set_active_eptp_data.eptp_list_index = r_ecx;
	set_active_eptp_data.update_hw = FALSE;
	if (leptp &&
	    report_mon_event(MON_EVENT_SET_ACTIVE_EPTP,
		    (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		    &set_active_eptp_data)) {
		MON_LOG(mask_anonymous,
			level_trace,
			"Switch ept called %d\n",
			r_ecx);
		vmcs = mon_gcpu_get_vmcs(gcpu);
		mon_vmcs_write(vmcs, VMCS_EPTP_ADDRESS, leptp);
		gcpu_skip_guest_instruction(gcpu);
		nmi_window_update_before_vmresume(vmcs);
	} else {
		/* View is invalid report to handler */

		MON_LOG(mask_anonymous, level_trace,
			"%s: view id=%d.Invalid view id requested.\n",
			__FUNCTION__, r_ecx);

		fast_view_switch_data.reg = r_ecx;
		report_mon_event(MON_EVENT_INVALID_FAST_VIEW_SWITCH,
			(mon_identification_data_t)gcpu,
			(const guest_vcpu_t *)mon_guest_vcpu(gcpu),
			(void *)&fast_view_switch_data);
		nmi_window_update_before_vmresume(mon_gcpu_get_vmcs(gcpu));
	}
	vmentry_func(FALSE);
}
