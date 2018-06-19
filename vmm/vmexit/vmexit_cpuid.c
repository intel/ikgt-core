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

#include "vmm_base.h"
#include "gcpu.h"
#include "guest.h"
#include "vmm_util.h"
#include "vmexit_cpuid.h"

/* used for CPUID leaf 0x3.
 * if the signature is matched, then evmm is running. */
#define EVMM_SIGNATURE_CORP 0x43544E49  /* "INTC", edx */
#define EVMM_SIGNATURE_VMM  0x4D4D5645  /* "EVMM", ecx */


typedef void (*cpuid_filter_handler_t) (guest_cpu_handle_t, cpuid_params_t *);

typedef struct {
	/* cpuid leaf index */
	uint32_t               eax;
	uint32_t               pad;
	cpuid_filter_handler_t handler;
} cpuid_filter_t;

static
void cpuid_leaf_1h_filter(UNUSED guest_cpu_handle_t gcpu, cpuid_params_t *p_cpuid)
{
	/* hide SMX support */
	p_cpuid->ecx &= ~CPUID_ECX_SMX;

	/* hide VMX support */
	p_cpuid->ecx &= ~CPUID_ECX_VMX;
}

static
void cpuid_leaf_3h_filter(UNUSED guest_cpu_handle_t gcpu, cpuid_params_t *p_cpuid)
{
	/* use PSN index 3 to indicate whether evmm is running or not. */
	/* "EVMM" */
	p_cpuid->ecx = EVMM_SIGNATURE_VMM;
	/* "INTC" */
	p_cpuid->edx = EVMM_SIGNATURE_CORP;
}

#define DESCRIPTOR_L_BIT 0x2000

static
void cpuid_leaf_ext_1h_filter(guest_cpu_handle_t gcpu, cpuid_params_t *p_cpuid)
{
	uint64_t guest_cs_ar = vmcs_read(gcpu->vmcs, VMCS_GUEST_CS_AR);

	if ((guest_cs_ar & DESCRIPTOR_L_BIT) == 0) {
		/* Guest is not in 64 bit mode, the bit 11 of EDX should be
		 * cleared since this bit indicates syscall/sysret available
		 * in 64 bit mode. See the Intel Software Programmer Manual vol 2A
		 * CPUID instruction */

		p_cpuid->edx &= ~CPUID_EDX_SYSCALL_SYSRET;
	}
}

static cpuid_filter_t g_cpuid_filter[] = {
	{0x1,0,cpuid_leaf_1h_filter},
	{0x3,0,cpuid_leaf_3h_filter},
	{0x80000001,0,cpuid_leaf_ext_1h_filter},
	{0xFFFFFFFF,0,NULL}
};

void vmexit_cpuid_instruction(guest_cpu_handle_t gcpu)
{
	cpuid_params_t cpuid_params;
	uint32_t cpuid_eax;
	uint32_t filter_id;

	D(VMM_ASSERT(gcpu));

	cpuid_params.eax = gcpu_get_gp_reg(gcpu, REG_RAX);
	cpuid_params.ebx = gcpu_get_gp_reg(gcpu, REG_RBX);
	cpuid_params.ecx = gcpu_get_gp_reg(gcpu, REG_RCX);
	cpuid_params.edx = gcpu_get_gp_reg(gcpu, REG_RDX);
	cpuid_eax = cpuid_params.eax;

	asm_cpuid(&cpuid_params);

	for (filter_id = 0; ; ++filter_id)
	{
		if(g_cpuid_filter[filter_id].eax == cpuid_eax)
		{
			g_cpuid_filter[filter_id].handler(gcpu, &cpuid_params);
			break;
		}

		if(g_cpuid_filter[filter_id].eax > cpuid_eax)
			break;

		if(g_cpuid_filter[filter_id].handler == NULL)
			break;
	}

	/* write back to guest OS */
	gcpu_set_gp_reg(gcpu, REG_RAX, cpuid_params.eax);
	gcpu_set_gp_reg(gcpu, REG_RBX, cpuid_params.ebx);
	gcpu_set_gp_reg(gcpu, REG_RCX, cpuid_params.ecx);
	gcpu_set_gp_reg(gcpu, REG_RDX, cpuid_params.edx);

	/* increment IP to skip executed CPUID instruction */
	gcpu_skip_instruction(gcpu);

}

