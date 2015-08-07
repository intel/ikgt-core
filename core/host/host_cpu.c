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

#include "host_cpu.h"
#include "guest_cpu.h"
#include "heap.h"
#include "vmx_trace.h"
#include "vmcs_api.h"
#include "vmcs_init.h"
#include "libc.h"
#include "gpm_api.h"
#include "host_memory_manager_api.h"
#include "hw_utils.h"
#include "vmx_asm.h"
#include "mon_stack_api.h"
#include "scheduler.h"
#include "hw_utils.h"
#include "em64t_defs.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(HOST_CPU_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(HOST_CPU_C, __condition)

/**************************************************************************
*
* Host CPU model for VMCS
*
**************************************************************************/

/*---------------------------- types --------------------------------------- */


/*
 * Minimum size of allocated MSR list
 */
#define MIN_SIZE_OF_MSR_LIST  4

/* main save area */
typedef struct {
	hva_t			vmxon_region_hva;
	hpa_t			vmxon_region_hpa;

	uint16_t		state_flags;
	uint8_t			padding0[6];

	ia32_vmx_msr_entry_t	*vmexit_msr_load_list;
	uint32_t		vmexit_msr_load_count;
	uint32_t		max_vmexit_msr_load_count;
	guest_cpu_handle_t	last_vmexit_gcpu;

	uint64_t		host_dr7;
} PACKED host_cpu_save_area_t;


typedef enum {
	HCPU_VMX_IS_ON_FLAG = 0, /* VMXON was executed */
} host_cpu_flags_t;

#define SET_VMX_IS_ON_FLAG(hcpu)  \
	BIT_SET((hcpu)->state_flags, HCPU_VMX_IS_ON_FLAG)
#define CLR_VMX_IS_ON_FLAG(hcpu)  \
	BIT_CLR((hcpu)->state_flags, HCPU_VMX_IS_ON_FLAG)
#define GET_VMX_IS_ON_FLAG(hcpu)  \
	BIT_GET((hcpu)->state_flags, HCPU_VMX_IS_ON_FLAG)

/*---------------------------------- globals ------------------------------- */
static host_cpu_save_area_t *g_host_cpus;
static uint16_t g_max_host_cpus;

/*--------------------------------- external funcs ------------------------- */
extern boolean_t is_cr4_osxsave_supported(void);

/*----------------------------------- interface ---------------------------- */
void host_cpu_manager_init(uint16_t max_host_cpus)
{
	MON_ASSERT(max_host_cpus);

	g_max_host_cpus = max_host_cpus;
	g_host_cpus = mon_memory_alloc(
		sizeof(host_cpu_save_area_t) * max_host_cpus);

	MON_ASSERT(g_host_cpus);
}

static void host_cpu_add_msr_to_vmexit_load_list(cpu_id_t cpu,
						 uint32_t msr_index,
						 uint64_t msr_value)
{
	host_cpu_save_area_t *hcpu = &g_host_cpus[cpu];
	boolean_t update_gcpus = FALSE;
	ia32_vmx_msr_entry_t *new_msr_ptr = NULL;
	uint32_t i = 0;

	/* Check if MSR is already in the list. */
	if (hcpu->vmexit_msr_load_list != NULL) {
		for (i = 0, new_msr_ptr = hcpu->vmexit_msr_load_list;
		     i < hcpu->vmexit_msr_load_count; i++, new_msr_ptr++)
			if (new_msr_ptr->msr_index == msr_index) {
				break;
			}
	} else {
		i = hcpu->vmexit_msr_load_count;
	}

	if (i >= hcpu->vmexit_msr_load_count) {
		if (hcpu->vmexit_msr_load_list == NULL
		    || hcpu->vmexit_msr_load_count >=
		    hcpu->max_vmexit_msr_load_count) {
			/* The list is full or not allocated, expand it */
			uint32_t new_max_count =
				MAX(hcpu->max_vmexit_msr_load_count * 2,
					MIN_SIZE_OF_MSR_LIST);
			uint32_t new_size = sizeof(ia32_vmx_msr_entry_t) *
					    new_max_count;
			ia32_vmx_msr_entry_t *new_list =
				mon_malloc_aligned(new_size,
					sizeof(ia32_vmx_msr_entry_t));
			uint32_t i;

			if (new_list == NULL) {
				MON_LOG(mask_anonymous,
					level_trace,
					"%s: Memory allocation failed\n",
					__FUNCTION__);
				MON_DEADLOOP();
			}

			/* Copy the old list. */
			for (i = 0; i < hcpu->vmexit_msr_load_count; i++)
				new_list[i] = hcpu->vmexit_msr_load_list[i];

			/* Free the old list. */
			if (hcpu->vmexit_msr_load_list != NULL) {
				mon_mfree(hcpu->vmexit_msr_load_list);
			}

			/* Assign the new one. */
			hcpu->vmexit_msr_load_list = new_list;
			hcpu->max_vmexit_msr_load_count = new_max_count;

			update_gcpus = TRUE;
		}

		new_msr_ptr =
			&hcpu->vmexit_msr_load_list[hcpu->vmexit_msr_load_count
						    ++];
	}

	MON_ASSERT(new_msr_ptr);
	new_msr_ptr->msr_index = msr_index;
	new_msr_ptr->reserved = 0;
	new_msr_ptr->msr_data = msr_value;

	if (update_gcpus) {
		scheduler_gcpu_iterator_t iter;
		guest_cpu_handle_t gcpu;

		gcpu = scheduler_same_host_cpu_gcpu_first(&iter, cpu);
		while (gcpu != NULL) {
			gcpu_change_level0_vmexit_msr_load_list(gcpu,
				hcpu->vmexit_msr_load_list,
				hcpu->vmexit_msr_load_count);

			gcpu = scheduler_same_host_cpu_gcpu_next(&iter);
		}
	}
}

void
host_cpu_init_vmexit_store_and_vmenter_load_msr_lists_according_to_vmexit_load_list(
	guest_cpu_handle_t gcpu)
{
	cpu_id_t cpu = hw_cpu_id();
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	uint32_t i;

	MON_ASSERT(vmcs);
	vmcs_clear_vmexit_store_list(vmcs);
	vmcs_clear_vmenter_load_list(vmcs);

	MON_ASSERT(g_host_cpus);
	for (i = 0; i < g_host_cpus[cpu].vmexit_msr_load_count; i++) {
		vmcs_add_msr_to_vmexit_store_and_vmenter_load_lists(vmcs,
			g_host_cpus[cpu].
			vmexit_msr_load_list
			[i].msr_index,
			g_host_cpus[cpu].
			vmexit_msr_load_list
			[i].msr_data);
	}
}

/*
 * Initialize current host cpu
 */
void host_cpu_init(void)
{
	hw_write_msr(IA32_MSR_SYSENTER_CS, 0);
	hw_write_msr(IA32_MSR_SYSENTER_EIP, 0);
	hw_write_msr(IA32_MSR_SYSENTER_ESP, 0);

	{
		cpu_id_t cpu = hw_cpu_id();
		host_cpu_save_area_t *host_cpu = &(g_host_cpus[cpu]);

		host_cpu->vmexit_msr_load_list = NULL;
		host_cpu->vmexit_msr_load_count = 0;
		host_cpu->max_vmexit_msr_load_count = 0;

		if (mon_vmcs_hw_get_vmx_constraints()->may1_vm_exit_ctrl.
		    bits.save_debug_controls != 1) {
			host_cpu_add_msr_to_vmexit_load_list(cpu,
				IA32_MSR_DEBUGCTL,
				hw_read_msr
					(IA32_MSR_DEBUGCTL));
		}
		if (mon_vmcs_hw_get_vmx_constraints()->may1_vm_exit_ctrl.
		    bits.save_sys_enter_msrs != 1) {
			host_cpu_add_msr_to_vmexit_load_list(cpu,
				IA32_MSR_SYSENTER_ESP,
				hw_read_msr
					(IA32_MSR_SYSENTER_ESP));
			host_cpu_add_msr_to_vmexit_load_list(cpu,
				IA32_MSR_SYSENTER_EIP,
				hw_read_msr
					(IA32_MSR_SYSENTER_EIP));
			host_cpu_add_msr_to_vmexit_load_list(cpu,
				IA32_MSR_SYSENTER_CS,
				hw_read_msr
					(IA32_MSR_SYSENTER_CS));
		}
		if (mon_vmcs_hw_get_vmx_constraints()->may1_vm_exit_ctrl.bits.
		    save_efer != 1) {
			host_cpu_add_msr_to_vmexit_load_list(cpu, IA32_MSR_EFER,
				hw_read_msr(IA32_MSR_EFER));
		}
		if (mon_vmcs_hw_get_vmx_constraints()->may1_vm_exit_ctrl.bits.
		    save_pat != 1) {
			host_cpu_add_msr_to_vmexit_load_list(cpu, IA32_MSR_PAT,
				hw_read_msr(IA32_MSR_PAT));
		}
		if (mon_vmcs_hw_get_vmx_constraints()->may1_vm_exit_ctrl.
		    bits.load_ia32_perf_global_ctrl != 1) {
			host_cpu_add_msr_to_vmexit_load_list(cpu,
				IA32_MSR_PERF_GLOBAL_CTRL,
				hw_read_msr
					(IA32_MSR_PERF_GLOBAL_CTRL));
		}
	}
}

/*
 * Init VMCS host cpu are for the target cpu. May be executed on any other CPU
 */
void host_cpu_vmcs_init(guest_cpu_handle_t gcpu)
{
	hpa_t host_msr_load_addr = 0;
	vmcs_object_t *vmcs;

	em64t_gdtr_t gdtr;
	em64t_idt_descriptor_t idtr;
	hva_t gcpu_stack;
	cpu_id_t cpu;
	vmexit_controls_t exit_controls;
	address_t base;
	uint32_t limit;
	uint32_t attributes;
	vmexit_control_t vmexit_control;
	boolean_t success;
	mon_status_t status;

	MON_ASSERT(gcpu);

	exit_controls.uint32 = 0;
	mon_memset(&vmexit_control, 0, sizeof(vmexit_control));

	cpu = hw_cpu_id();
	MON_ASSERT(cpu < g_max_host_cpus);

	vmcs = vmcs_hierarchy_get_vmcs(gcpu_get_vmcs_hierarchy(
			gcpu), VMCS_LEVEL_0);

	MON_ASSERT(vmcs);

	/*
	 *  Control Registers
	 */
	mon_vmcs_write(vmcs, VMCS_HOST_CR0,
		vmcs_hw_make_compliant_cr0(hw_read_cr0()));
	mon_vmcs_write(vmcs, VMCS_HOST_CR3, hw_read_cr3());

	if (is_cr4_osxsave_supported()) {
		em64t_cr4_t cr4_mask;
		cr4_mask.uint64 = 0;
		cr4_mask.bits.osxsave = 1;
		mon_vmcs_write(vmcs, VMCS_HOST_CR4,
			vmcs_hw_make_compliant_cr4(hw_read_cr4() |
				(mon_vmcs_read(vmcs, VMCS_GUEST_CR4) &
				 cr4_mask.uint64)));
	} else {
		mon_vmcs_write(vmcs, VMCS_HOST_CR4,
			vmcs_hw_make_compliant_cr4(hw_read_cr4()));
	}

	/*
	 *  EIP, ESP
	 */
	mon_vmcs_write(vmcs, VMCS_HOST_RIP, (uint64_t)vmexit_func);
	success = mon_stack_get_stack_pointer_for_cpu(cpu, &gcpu_stack);
	MON_ASSERT(success == TRUE);
	mon_vmcs_write(vmcs, VMCS_HOST_RSP, gcpu_stack);

	/*
	 *  Base-address fields for FS, GS, TR, GDTR, and IDTR (64 bits each).
	 */
	hw_sgdt(&gdtr);
	mon_vmcs_write(vmcs, VMCS_HOST_GDTR_BASE, gdtr.base);

	hw_sidt(&idtr);
	mon_vmcs_write(vmcs, VMCS_HOST_IDTR_BASE, idtr.base);

	/*
	 *  FS (Selector + Base)
	 */
	status =
		hw_gdt_parse_entry((uint8_t *)gdtr.base,
			hw_read_fs(), &base, &limit,
			&attributes);
	MON_ASSERT(status == MON_OK);
	mon_vmcs_write(vmcs, VMCS_HOST_FS_SELECTOR, hw_read_fs());
	mon_vmcs_write(vmcs, VMCS_HOST_FS_BASE, base);

	/*
	 *  GS (Selector + Base)
	 */
	status =
		hw_gdt_parse_entry((uint8_t *)gdtr.base,
			hw_read_gs(), &base, &limit,
			&attributes);
	MON_ASSERT(status == MON_OK);
	mon_vmcs_write(vmcs, VMCS_HOST_GS_SELECTOR, hw_read_gs());
	mon_vmcs_write(vmcs, VMCS_HOST_GS_BASE, base);

	/*
	 *  TR (Selector + Base)
	 */
	status =
		hw_gdt_parse_entry((uint8_t *)gdtr.base,
			hw_read_tr(), &base, &limit,
			&attributes);
	MON_ASSERT(status == MON_OK);
	mon_vmcs_write(vmcs, VMCS_HOST_TR_SELECTOR, hw_read_tr());
	mon_vmcs_write(vmcs, VMCS_HOST_TR_BASE, base);

	/*
	 *  Selector fields (16 bits each) for the segment registers CS, SS, DS,
	 *  ES, FS, GS, and TR
	 */
	mon_vmcs_write(vmcs, VMCS_HOST_CS_SELECTOR, hw_read_cs());
	mon_vmcs_write(vmcs, VMCS_HOST_SS_SELECTOR, hw_read_ss());
	mon_vmcs_write(vmcs, VMCS_HOST_DS_SELECTOR, hw_read_ds());
	mon_vmcs_write(vmcs, VMCS_HOST_ES_SELECTOR, hw_read_es());
	/*
	 *  MSRS
	 */

	if (mon_vmcs_hw_get_vmx_constraints()->may1_vm_exit_ctrl.
	    bits.load_sys_enter_msrs == 1) {
		mon_vmcs_write(vmcs, VMCS_HOST_SYSENTER_CS,
			hw_read_msr(IA32_MSR_SYSENTER_CS));
		mon_vmcs_write(vmcs, VMCS_HOST_SYSENTER_ESP,
			hw_read_msr(IA32_MSR_SYSENTER_ESP));
		mon_vmcs_write(vmcs, VMCS_HOST_SYSENTER_EIP,
			hw_read_msr(IA32_MSR_SYSENTER_EIP));
	}

	if (mon_vmcs_hw_get_vmx_constraints()->may1_vm_exit_ctrl.bits.load_efer
	    == 1) {
		mon_vmcs_write(vmcs, VMCS_HOST_EFER,
			hw_read_msr(IA32_MSR_EFER));
	}

	if (mon_vmcs_hw_get_vmx_constraints()->may1_vm_exit_ctrl.bits.load_pat
	    == 1) {
		mon_vmcs_write(vmcs, VMCS_HOST_PAT, hw_read_msr(IA32_MSR_PAT));
	}

	exit_controls.bits.ia32e_mode_host = 1;
	vmexit_control.vm_exit_ctrls.bit_request = exit_controls.uint32;
	vmexit_control.vm_exit_ctrls.bit_mask = exit_controls.uint32;
	gcpu_control_setup(gcpu, &vmexit_control);

	MON_ASSERT(g_host_cpus);
	if (gcpu_get_guest_level(gcpu) == GUEST_LEVEL_1_SIMPLE) {
		MON_ASSERT(vmcs_hierarchy_get_vmcs
				(gcpu_get_vmcs_hierarchy(
					gcpu), VMCS_MERGED) == vmcs)
		if ((g_host_cpus[cpu].vmexit_msr_load_count != 0)
		    &&
		    (!mon_hmm_hva_to_hpa
			     ((hva_t)g_host_cpus[cpu].vmexit_msr_load_list,
			     &host_msr_load_addr))) {
			MON_LOG(mask_anonymous,
				level_trace,
				"%s:(%d):ASSERT: hva_t to hpa_t conversion failed\n",
				__FUNCTION__,
				__LINE__);
			MON_DEADLOOP();
		}
	} else {
		/* Address remains HVA */
		host_msr_load_addr =
			(uint64_t)g_host_cpus[cpu].vmexit_msr_load_list;
	}

	/* Assigning VMExit msr-load list */
	vmcs_assign_vmexit_msr_load_list(vmcs, host_msr_load_addr,
		g_host_cpus[cpu].vmexit_msr_load_count);
}

/*--------------------------------------------------------------------------
 *
 * Set/Get VMXON Region pointer for the current CPU
 *
 *-------------------------------------------------------------------------- */
void host_cpu_set_vmxon_region(hva_t hva, hpa_t hpa, cpu_id_t my_cpu_id)
{
	host_cpu_save_area_t *hcpu = NULL;

	MON_ASSERT(g_host_cpus);
	MON_ASSERT(my_cpu_id < g_max_host_cpus);

	hcpu = &(g_host_cpus[my_cpu_id]);

	hcpu->vmxon_region_hva = hva;
	hcpu->vmxon_region_hpa = hpa;
}

hva_t host_cpu_get_vmxon_region(hpa_t *hpa)
{
	cpu_id_t my_cpu_id = hw_cpu_id();
	host_cpu_save_area_t *hcpu = NULL;

	MON_ASSERT(g_host_cpus);
	MON_ASSERT(my_cpu_id < g_max_host_cpus);
	MON_ASSERT(hpa);

	hcpu = &(g_host_cpus[my_cpu_id]);

	*hpa = hcpu->vmxon_region_hpa;
	return hcpu->vmxon_region_hva;
}

void host_cpu_set_vmx_state(boolean_t value)
{
	cpu_id_t my_cpu_id = hw_cpu_id();
	host_cpu_save_area_t *hcpu = NULL;

	MON_ASSERT(g_host_cpus);
	MON_ASSERT(my_cpu_id < g_max_host_cpus);

	hcpu = &(g_host_cpus[my_cpu_id]);

	if (value) {
		SET_VMX_IS_ON_FLAG(hcpu);
	} else {
		CLR_VMX_IS_ON_FLAG(hcpu);
	}
}

boolean_t host_cpu_get_vmx_state(void)
{
	cpu_id_t my_cpu_id = hw_cpu_id();
	host_cpu_save_area_t *hcpu = NULL;

	MON_ASSERT(g_host_cpus);
	MON_ASSERT(my_cpu_id < g_max_host_cpus);

	hcpu = &(g_host_cpus[my_cpu_id]);

	return GET_VMX_IS_ON_FLAG(hcpu) ? TRUE : FALSE;
}

void host_cpu_enable_usage_of_xmm_regs(void)
{
	em64t_cr4_t cr4;

	/* allow access to XMM registers (compiler assumes this for 64bit code) */
	cr4.uint64 = hw_read_cr4();
	cr4.bits.osfxsr = 1;
	hw_write_cr4(cr4.uint64);
}

void host_cpu_store_vmexit_gcpu(cpu_id_t cpu_id, guest_cpu_handle_t gcpu)
{
	if (cpu_id < g_max_host_cpus) {
		g_host_cpus[cpu_id].last_vmexit_gcpu = gcpu;

		MON_DEBUG_CODE(mon_trace(gcpu, "\n");
			)
	}
}

guest_cpu_handle_t host_cpu_get_vmexit_gcpu(cpu_id_t cpu_id)
{
	guest_cpu_handle_t gcpu = NULL;

	if (cpu_id < g_max_host_cpus) {
		gcpu = g_host_cpus[cpu_id].last_vmexit_gcpu;
	}
	return gcpu;
}

/*
 *  Purpose: At VMEXIT dr7 is always overwrittern with 400h. This prevents to
 *           set hardware breakponits in host-running code across
 *           VMEXIT/VMENTER transitions.  Two functions below allow to keep
 *           dr7, set by external debugger in cpu context,
 *           and apply it to hardware upon VMEXIT.
 */
void host_cpu_save_dr7(cpu_id_t cpu_id)
{
	MON_ASSERT(g_host_cpus);
	if (cpu_id < g_max_host_cpus) {
		g_host_cpus[cpu_id].host_dr7 = hw_read_dr(7);
	}
}

void host_cpu_restore_dr7(cpu_id_t cpu_id)
{
	if (cpu_id < g_max_host_cpus) {
		if (0 != g_host_cpus[cpu_id].host_dr7) {
			hw_write_dr(7, g_host_cpus[cpu_id].host_dr7);
		}
	}
}
