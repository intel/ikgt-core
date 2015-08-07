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

#include "mon_defs.h"
#include "vmcs_init.h"
#include "ept_hw_layer.h"
#include "hw_utils.h"
#include "guest_cpu.h"
#include "vmcs_api.h"
#include "mon_phys_mem_types.h"
#include "libc.h"
#include "scheduler.h"
#include "guest_cpu_internal.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(EPT_HW_LAYER_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(EPT_HW_LAYER_C, __condition)

typedef struct {
	uint64_t	eptp;
	uint64_t	gpa;
} invept_arg_t;

void ASM_FUNCTION mon_asm_invept(invept_arg_t *arg,
				 uint32_t modifier,
				 uint64_t *rflags);

typedef struct {
	uint64_t	vpid;
	uint64_t	gva;
} invvpid_arg_t;

void ASM_FUNCTION mon_asm_invvpid(invvpid_arg_t *arg,
				  uint32_t modifier,
				  uint64_t *rflags);

boolean_t ept_hw_is_ept_supported(void)
{
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();

	return hw_constraints->may1_processor_based_exec_ctrl.
	       bits.secondary_controls
	       && hw_constraints->may1_processor_based_exec_ctrl2.bits.
	       enable_ept;
}

void ept_hw_set_pdtprs(guest_cpu_handle_t gcpu, uint64_t pdptr[])
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	CHECK_EXECUTION_ON_LOCAL_HOST_CPU(gcpu);

	mon_vmcs_write(vmcs, VMCS_GUEST_PDPTR0, pdptr[0]);
	mon_vmcs_write(vmcs, VMCS_GUEST_PDPTR1, pdptr[1]);
	mon_vmcs_write(vmcs, VMCS_GUEST_PDPTR2, pdptr[2]);
	mon_vmcs_write(vmcs, VMCS_GUEST_PDPTR3, pdptr[3]);
}

uint32_t mon_ept_hw_get_guest_address_width(uint32_t actual_gaw)
{
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();

	if (actual_gaw <= 21
	    && hw_constraints->ept_vpid_capabilities.bits.gaw_21_bit) {
		return 21;
	}
	if (actual_gaw <= 30
	    && hw_constraints->ept_vpid_capabilities.bits.gaw_30_bit) {
		return 30;
	}
	if (actual_gaw <= 39
	    && hw_constraints->ept_vpid_capabilities.bits.gaw_39_bit) {
		return 39;
	}
	if (actual_gaw <= 48
	    && hw_constraints->ept_vpid_capabilities.bits.gaw_48_bit) {
		return 48;
	}
	if (actual_gaw <= 57
	    && hw_constraints->ept_vpid_capabilities.bits.gaw_57_bit) {
		return 57;
	}
	MON_ASSERT(0);
	return (uint32_t)-1;
}

uint32_t mon_ept_hw_get_guest_address_width_encoding(uint32_t width)
{
	uint32_t gaw_encoding = (uint32_t)-1;

	MON_ASSERT(width == 21
		|| width == 30 || width == 39 || width == 48 || width == 57);

	gaw_encoding = (width - 21) / 9;

	return gaw_encoding;
}

uint32_t ept_hw_get_guest_address_width_from_encoding(uint32_t gaw_encoding)
{
	MON_ASSERT(gaw_encoding <= 4);

	return 21 + (gaw_encoding * 9);
}

mon_phys_mem_type_t mon_ept_hw_get_ept_memory_type(void)
{
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();

	if (hw_constraints->ept_vpid_capabilities.bits.wb) {
		return MON_PHYS_MEM_WRITE_BACK;
	}
	if (hw_constraints->ept_vpid_capabilities.bits.wp) {
		return MON_PHYS_MEM_WRITE_PROTECTED;
	}
	if (hw_constraints->ept_vpid_capabilities.bits.wt) {
		return MON_PHYS_MEM_WRITE_THROUGH;
	}
	if (hw_constraints->ept_vpid_capabilities.bits.wc) {
		return MON_PHYS_MEM_WRITE_COMBINING;
	}
	if (hw_constraints->ept_vpid_capabilities.bits.uc) {
		return MON_PHYS_MEM_UNCACHABLE;
	}
	MON_ASSERT(0);
	return MON_PHYS_MEM_UNDEFINED;
}

uint64_t ept_hw_get_eptp(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	uint64_t eptp = 0;

	MON_ASSERT(gcpu);
	CHECK_EXECUTION_ON_LOCAL_HOST_CPU(gcpu);

	if (!ept_hw_is_ept_supported()) {
		return eptp;
	}

	eptp = mon_vmcs_read(vmcs, VMCS_EPTP_ADDRESS);

	return eptp;
}

boolean_t ept_hw_set_eptp(guest_cpu_handle_t gcpu,
			  hpa_t ept_root_hpa,
			  uint32_t gaw)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	eptp_t eptp;
	uint32_t ept_gaw = 0;

	MON_ASSERT(gcpu);
	MON_ASSERT(vmcs);
	CHECK_EXECUTION_ON_LOCAL_HOST_CPU(gcpu);

	if (!ept_hw_is_ept_supported()
	    || ept_root_hpa == 0) {
		return FALSE;
	}

	ept_gaw = mon_ept_hw_get_guest_address_width(gaw);
	if (ept_gaw == (uint32_t)-1) {
		return FALSE;
	}

	eptp.uint64 = ept_root_hpa;
	eptp.bits.etmt = mon_ept_hw_get_ept_memory_type();
	eptp.bits.gaw = mon_ept_hw_get_guest_address_width_encoding(ept_gaw);
	eptp.bits.reserved = 0;

	mon_vmcs_write(vmcs, VMCS_EPTP_ADDRESS, eptp.uint64);

	return TRUE;
}

boolean_t ept_hw_is_ept_enabled(guest_cpu_handle_t gcpu)
{
	processor_based_vm_execution_controls2_t proc_ctrls2;

	CHECK_EXECUTION_ON_LOCAL_HOST_CPU(gcpu);

	proc_ctrls2.uint32 =
		(uint32_t)mon_vmcs_read(mon_gcpu_get_vmcs(gcpu),
			VMCS_CONTROL2_VECTOR_PROCESSOR_EVENTS);

	return proc_ctrls2.bits.enable_ept;
}

/* invalidate EPT */
boolean_t ept_hw_is_invept_supported(void)
{
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();

	if (ept_hw_is_ept_supported()
	    && hw_constraints->ept_vpid_capabilities.bits.invept_supported) {
		return TRUE;
	}

	return FALSE;
}

/* invalidate VPID */
boolean_t ept_hw_is_invvpid_supported(void)
{
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();

	if (ept_hw_is_ept_supported()
	    && hw_constraints->ept_vpid_capabilities.bits.invvpid_supported) {
		return TRUE;
	}

	return FALSE;
}

boolean_t ept_hw_invept_all_contexts(void)
{
	invept_arg_t arg;
	uint64_t rflags;
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();
	boolean_t status = FALSE;

	if (!ept_hw_is_invept_supported()) {
		return TRUE;
	}

	mon_zeromem(&arg, sizeof(arg));
	if (hw_constraints->ept_vpid_capabilities.bits.invept_all_contexts) {
		mon_asm_invept(&arg, INVEPT_ALL_CONTEXTS, &rflags);
		status = ((rflags & 0x8d5) == 0);
		if (!status) {
			MON_LOG(mask_anonymous,
				level_trace,
				"ept_hw_invept_all_contexts ERROR: rflags = %p\r\n",
				rflags);
		}
	}

	return status;
}

boolean_t ept_hw_invept_context(uint64_t eptp)
{
	invept_arg_t arg;
	uint64_t rflags;
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();
	boolean_t status = FALSE;

	if (!ept_hw_is_invept_supported()) {
		return TRUE;
	}

	mon_zeromem(&arg, sizeof(arg));

	MON_ASSERT(eptp != 0);
	arg.eptp = eptp;

	if (hw_constraints->ept_vpid_capabilities.bits.invept_context_wide) {
		mon_asm_invept(&arg, INVEPT_CONTEXT_WIDE, &rflags);
		status = ((rflags & 0x8d5) == 0);
		if (!status) {
			MON_LOG(mask_anonymous,
				level_trace,
				"ept_hw_invept_context ERROR: eptp = %p rflags = %p\r\n",
				eptp,
				rflags);
		}
	} else {
		ept_hw_invept_all_contexts();
	}

	return status;
}

boolean_t ept_hw_invept_individual_address(uint64_t eptp, address_t gpa)
{
	invept_arg_t arg;
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();
	uint64_t rflags;
	boolean_t status = FALSE;

	if (!ept_hw_is_invept_supported()) {
		return TRUE;
	}

	mon_zeromem(&arg, sizeof(arg));

	MON_ASSERT((eptp != 0) && (gpa != 0));
	arg.eptp = eptp;
	arg.gpa = gpa;

	if (hw_constraints->ept_vpid_capabilities.bits.invept_individual_address) {
		mon_asm_invept(&arg, INVEPT_INDIVIDUAL_ADDRESS, &rflags);
		status = ((rflags & 0x8d5) == 0);
		if (!status) {
			MON_LOG(mask_anonymous,
				level_trace,
				"ept_hw_invept_individual_address ERROR: eptp = %p gpa"
				" = %p rflags = %p\r\n",
				eptp,
				gpa,
				rflags);
		}
	} else {
		ept_hw_invept_context(eptp);
	}

	return status;
}

boolean_t ept_hw_invvpid_individual_address(uint64_t vpid, address_t gva)
{
	invvpid_arg_t arg;
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();
	uint64_t rflags;
	boolean_t status = FALSE;

	if (!ept_hw_is_invvpid_supported()) {
		MON_ASSERT(0);
		return TRUE;
	}

	arg.vpid = vpid;
	arg.gva = gva;

	if (hw_constraints->ept_vpid_capabilities.bits.
	    invvpid_individual_address) {
		mon_asm_invvpid(&arg, INVVPID_INDIVIDUAL_ADDRESS, &rflags);
		status = ((rflags & 0x8d5) == 0);
		if (!status) {
			MON_LOG(mask_anonymous,
				level_trace,
				"ept_hw_invvpid_individual_address ERROR: vpid = %d"
				" gva = %p rflags = %p\r\n",
				vpid,
				gva,
				rflags);
			MON_ASSERT(0);
		}
	}

	return status;
}

boolean_t ept_hw_invvpid_all_contexts(void)
{
	invvpid_arg_t arg;
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();
	uint64_t rflags;
	boolean_t status = FALSE;

	if (!ept_hw_is_invvpid_supported()) {
		MON_ASSERT(0);
		return TRUE;
	}

	arg.vpid = 0;
	/* arg.gva = gva; */

	if (hw_constraints->ept_vpid_capabilities.bits.invvpid_all_contexts) {
		mon_asm_invvpid(&arg, INVVPID_ALL_CONTEXTS, &rflags);
		status = ((rflags & 0x8d5) == 0);
		if (!status) {
			MON_LOG(mask_anonymous,
				level_trace,
				"ept_hw_invvpid_all_contexts ERROR: rflags = %p\r\n",
				rflags);
			MON_ASSERT(0);
		}
	}

	return status;
}

boolean_t ept_hw_invvpid_single_context(uint64_t vpid)
{
	invvpid_arg_t arg;
	const vmcs_hw_constraints_t *hw_constraints =
		mon_vmcs_hw_get_vmx_constraints();
	uint64_t rflags;
	boolean_t status = FALSE;

	if (!ept_hw_is_invvpid_supported()) {
		MON_ASSERT(0);
		return TRUE;
	}

	arg.vpid = vpid;

	if (hw_constraints->ept_vpid_capabilities.bits.invvpid_context_wide) {
		mon_asm_invvpid(&arg, INVVPID_SINGLE_CONTEXT, &rflags);
		status = ((rflags & 0x8d5) == 0);
		if (!status) {
			MON_LOG(mask_anonymous,
				level_trace,
				"ept_hw_invvpid_all_contexts ERROR: rflags = %p\r\n",
				rflags);
			MON_ASSERT(0);
		}
	}

	return status;
}

boolean_t ept_hw_enable_ept(guest_cpu_handle_t gcpu)
{
	processor_based_vm_execution_controls2_t proc_ctrls2;
	vmexit_control_t vmexit_request;

	CHECK_EXECUTION_ON_LOCAL_HOST_CPU(gcpu);

	MON_ASSERT(gcpu);

	if (!ept_hw_is_ept_supported()) {
		return FALSE;
	}

	proc_ctrls2.uint32 = 0;
	mon_zeromem(&vmexit_request, sizeof(vmexit_request));

	proc_ctrls2.bits.enable_ept = 1;
	proc_ctrls2.bits.enable_vpid = 1;
	mon_vmcs_write(mon_gcpu_get_vmcs(
			gcpu), VMCS_VPID, 1 + gcpu->vcpu.guest_id);
	vmexit_request.proc_ctrls2.bit_mask = proc_ctrls2.uint32;
	vmexit_request.proc_ctrls2.bit_request = UINT64_ALL_ONES;

	gcpu_control_setup(gcpu, &vmexit_request);

	return TRUE;
}

void ept_hw_disable_ept(guest_cpu_handle_t gcpu)
{
	processor_based_vm_execution_controls2_t proc_ctrls2;
	vmexit_control_t vmexit_request;

	CHECK_EXECUTION_ON_LOCAL_HOST_CPU(gcpu);

	ept_hw_invvpid_single_context(1 + gcpu->vcpu.guest_id);
	proc_ctrls2.uint32 = 0;
	mon_zeromem(&vmexit_request, sizeof(vmexit_request));

	proc_ctrls2.bits.enable_ept = 1;
	proc_ctrls2.bits.enable_vpid = 1;
	mon_vmcs_write(mon_gcpu_get_vmcs(gcpu), VMCS_VPID, 0);
	vmexit_request.proc_ctrls2.bit_mask = proc_ctrls2.uint32;
	vmexit_request.proc_ctrls2.bit_request = 0;

	gcpu_control_setup(gcpu, &vmexit_request);

	/* EPT_LOG("CPU#%d disable EPT\r\n", hw_cpu_id()); */
}
