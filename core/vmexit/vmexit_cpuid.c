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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMEXIT_CPUID_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMEXIT_CPUID_C, __condition)
#include "mon_defs.h"
#include "list.h"
#include "memory_allocator.h"
#include "guest_cpu.h"
#include "guest.h"
#include "hw_utils.h"
#include "vmexit_cpuid.h"
#include "mon_callback.h"

#define CPUID_EAX 0
#define CPUID_EBX 1
#define CPUID_ECX 2
#define CPUID_EDX 3

#define DESCRIPTOR_L_BIT 0x2000

typedef struct {
	list_element_t		list;
	/* cpuid leaf index */
	address_t		cpuid;
	cpuid_filter_handler_t	handler;
} cpuid_filter_descriptor_t;

static
void vmexit_cpuid_filter_install(guest_handle_t guest,
				 address_t cpuid,
				 cpuid_filter_handler_t handler)
{
	list_element_t *filter_desc_list = guest_get_cpuid_list(guest);
	cpuid_filter_descriptor_t *p_filter_desc =
		mon_malloc(sizeof(*p_filter_desc));

	MON_ASSERT(NULL != p_filter_desc);

	if (NULL != p_filter_desc) {
		p_filter_desc->cpuid = cpuid;
		p_filter_desc->handler = handler;
		list_add(filter_desc_list, &p_filter_desc->list);
	}
}

static
vmexit_handling_status_t vmexit_cpuid_instruction(guest_cpu_handle_t gcpu)
{
	cpuid_params_t cpuid_params;
	uint32_t req_id;
	list_element_t *filter_desc_list =
		guest_get_cpuid_list(mon_gcpu_guest_handle(gcpu));
	list_element_t *list_iterator;
	cpuid_filter_descriptor_t *p_filter_desc;
	report_cpuid_data_t cpuid_data;

	cpuid_params.m_rax = gcpu_get_native_gp_reg(gcpu, IA32_REG_RAX);
	cpuid_params.m_rbx = gcpu_get_native_gp_reg(gcpu, IA32_REG_RBX);
	cpuid_params.m_rcx = gcpu_get_native_gp_reg(gcpu, IA32_REG_RCX);
	cpuid_params.m_rdx = gcpu_get_native_gp_reg(gcpu, IA32_REG_RDX);
	cpuid_data.params = (uint64_t)&cpuid_params;
	req_id = (uint32_t)(cpuid_params.m_rax);

	if (!report_mon_event(MON_EVENT_CPUID, (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu), &cpuid_data)) {
		/* get the real h/w values */
		hw_cpuid(&cpuid_params);

		/* pass to filters for virtualization */
		LIST_FOR_EACH(filter_desc_list, list_iterator) {
			p_filter_desc =
				LIST_ENTRY(list_iterator,
					cpuid_filter_descriptor_t,
					list);
			if (p_filter_desc->cpuid == req_id) {
				p_filter_desc->handler(gcpu, &cpuid_params);
			}
		}
	}

	/* write back to guest OS */
	gcpu_set_native_gp_reg(gcpu, IA32_REG_RAX, cpuid_params.m_rax);
	gcpu_set_native_gp_reg(gcpu, IA32_REG_RBX, cpuid_params.m_rbx);
	gcpu_set_native_gp_reg(gcpu, IA32_REG_RCX, cpuid_params.m_rcx);
	gcpu_set_native_gp_reg(gcpu, IA32_REG_RDX, cpuid_params.m_rdx);

	/* increment IP to skip executed CPUID instruction */
	gcpu_skip_guest_instruction(gcpu);

	return VMEXIT_HANDLED;
}


static
void cpuid_leaf_1h_filter(guest_cpu_handle_t gcpu, cpuid_params_t *p_cpuid)
{
	MON_ASSERT(p_cpuid);

	/* hide SMX support */
	BIT_CLR64(p_cpuid->m_rcx, CPUID_LEAF_1H_ECX_SMX_SUPPORT);

	/* hide VMX support */
	BIT_CLR64(p_cpuid->m_rcx, CPUID_LEAF_1H_ECX_VMX_SUPPORT);
}

static
void cpuid_leaf_3h_filter(guest_cpu_handle_t gcpu, cpuid_params_t *p_cpuid)
{
	MON_ASSERT(p_cpuid);

	/* use PSN index 3 to indicate whether xmon is running or not. */
	/* "XMON" */
	p_cpuid->m_rcx = XMON_RUNNING_SIGNATURE_MON;
	/* "INTC" */
	p_cpuid->m_rdx = XMON_RUNNING_SIGNATURE_CORP;
}

static
void cpuid_leaf_ext_1h_filter(guest_cpu_handle_t gcpu, cpuid_params_t *p_cpuid)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	uint64_t guest_cs_ar = mon_vmcs_read(vmcs, VMCS_GUEST_CS_AR);

	MON_ASSERT(p_cpuid);

	if (BITMAP_GET(guest_cs_ar, DESCRIPTOR_L_BIT) == 0) {
		/* Guest is not in 64 bit mode, the bit 11 of EDX should be
		 * cleared since this bit indicates syscall/sysret available
		 * in 64 bit mode. See the Intel Software Programmer Manual vol 2A
		 * CPUID instruction */

		BIT_CLR64(p_cpuid->m_rdx, CPUID_EXT_LEAF_1H_EDX_SYSCALL_SYSRET);
	}
}


void vmexit_cpuid_guest_intialize(guest_id_t guest_id)
{
	guest_handle_t guest = mon_guest_handle(guest_id);

	MON_ASSERT(guest);

	/* install CPUID vmexit handler */
	vmexit_install_handler(guest_id,
		vmexit_cpuid_instruction,
		IA32_VMX_EXIT_BASIC_REASON_CPUID_INSTRUCTION);

	/* register cpuid(leaf 0x1) filter handler */
	vmexit_cpuid_filter_install(guest, CPUID_LEAF_1H, cpuid_leaf_1h_filter);

	/* register cpuid(leaf 0x3) filter handler */
	vmexit_cpuid_filter_install(guest, CPUID_LEAF_3H, cpuid_leaf_3h_filter);

	/* register cpuid(ext leaf 0x80000001) filter handler */
	vmexit_cpuid_filter_install(guest, CPUID_EXT_LEAF_1H,
		cpuid_leaf_ext_1h_filter);

	MON_LOG(mask_mon, level_trace,
		"finish vmexit_cpuid_guest_intialize\r\n");

	return;
}
