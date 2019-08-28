/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "evmm_desc.h"
#include "heap.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "stack.h"
#include "hmm.h"
#include "vmx_cap.h"
#include "guest.h"
#include "host_cpu.h"
#include "scheduler.h"
#include "vmexit.h"
#include "dbg.h"
#include "event.h"
#include "ept.h"
#include "mttr.h"
#include "vmcs.h"
#include "gpm.h"
#include "vmexit_cr_access.h"
#include "gcpu.h"
#include "gcpu_state.h"
#include "gcpu_switch.h"
#include "vmm_arch.h"
#include "page_walker.h"

#include "lib/util.h"

#ifdef LIB_EFI_SERVICES
#include "lib/efi/efi_services.h"
#endif

#ifdef MODULE_IPC
#include "modules/ipc.h"
#endif

#ifdef MODULE_VMX_TIMER
#include "modules/vmx_timer.h"
#endif

#ifdef MODULE_LAPIC_ID
#include "modules/lapic_id.h"
#endif

#ifdef MODULE_TEMPLATE_TEE
#include "modules/template_tee.h"
#endif

#ifdef MODULE_TRUSTY_TEE
#include "modules/trusty_tee.h"
#endif

#ifdef MODULE_TRUSTY_GUEST
#include "modules/trusty_guest.h"
#endif

#ifdef MODULE_OPTEE_GUEST
#include "modules/optee_guest.h"
#endif

#ifdef MODULE_SUSPEND
#include "modules/suspend.h"
#endif

#ifdef MODULE_VTD
#include "modules/vtd.h"
#endif

#ifdef MODULE_MSR_ISOLATION
#include "modules/msr_isolation.h"
#endif

#ifdef MODULE_MSR_MONITOR
#include "modules/msr_monitor.h"
#endif

#ifdef MODULE_DEV_BLK
#include "modules/dev_blk.h"
#endif

#ifdef MODULE_IO_MONITOR
#include "modules/io_monitor.h"
#endif

#ifdef MODULE_XSAVE
#include "modules/xsave.h"
#else
#ifdef MODULE_FXSAVE
#include "modules/fxsave.h"
#endif
#endif

#ifdef MODULE_DR
#include "modules/dr.h"
#endif

#ifdef MODULE_CR
#include "modules/cr.h"
#endif

#ifdef MODULE_VMCALL
#include "modules/vmcall.h"
#endif

#ifdef MODULE_TSC
#include "modules/tsc.h"
#endif

#ifdef MODULE_EXT_INTR
#include "modules/ext_intr.h"
#endif

#ifdef MODULE_VMEXIT_INIT
#include "modules/vmexit_init.h"
#endif

#ifdef MODULE_VMENTER_CHECK
#include "modules/vmenter_check.h"
#endif

#ifdef MODULE_VMEXIT_TASK_SWITCH
#include "modules/vmexit_task_switch.h"
#endif

#ifdef MODULE_PROFILE
#include "modules/profile.h"
#endif

#ifdef MODULE_INTERRUPT_IPI
#include "modules/interrupt_ipi.h"
#endif

#ifdef MODULE_PERF_CTRL_ISOLATION
#include "modules/perf_ctrl_isolation.h"
#endif

#ifdef MODULE_SPECTRE
#include "modules/spectre.h"
#endif

#ifdef MODULE_L1TF
#include "modules/l1tf.h"
#endif

#ifdef MODULE_APS_STATE
#include "modules/aps_state.h"
#endif

#ifdef MODULE_VIRTUAL_APIC
#include "modules/virtual_apic.h"
#endif

#ifdef MODULE_NESTED_VT
#include "modules/nested_vt.h"
#endif

#ifdef MODULE_BLOCK_NPK
#include "modules/block_npk.h"
#endif

typedef struct {
	uint64_t	cpuid;
	uint64_t	evmm_desc;
} vmm_input_params_t;

typedef enum {
	STAGE_INIT_BSP = 0,
	STAGE_SETUP_HOST,
	STAGE_SETUP_VMX,
	STAGE_INIT_MODULE,
	STAGE_INIT_GUEST,
} vmm_boot_stage_t;
/*------------------------- globals ------------------------- */

static
volatile vmm_boot_stage_t vmm_boot_stage = STAGE_INIT_BSP;

/*-------------------------- macros ------------------------- */
#define AP_WAIT_FOR_STAGE(_STAGE)			\
	{ while (vmm_boot_stage < _STAGE) { asm_pause(); } }

#define BSP_SET_STAGE(_STAGE)				\
	{ vmm_boot_stage = _STAGE; }

#ifdef SYNC_CPU_IN_BOOT
static volatile uint32_t launched_ap_count = 0;

#define BSP_WAIT_FOR_AP(count)				\
	{ while (launched_ap_count != (uint32_t)(count)) { asm_pause(); } }

#define AP_SET_LAUNCHED()				\
	{ asm_lock_inc32((&launched_ap_count)); }
#endif

/*------------------------- forwards ------------------------ */
/*---------------------- implementation --------------------- */
/*------------------------------------------------------------------------
 *
 * THE MAIN !!!!
 *
 * Started in parallel for all available processors
 *
 * Should never return!
 *
 *------------------------------------------------------------------------ */
void vmm_main_continue(vmm_input_params_t *vmm_input_params)
{
	evmm_desc_t *evmm_desc =
		(evmm_desc_t *) vmm_input_params->evmm_desc;
	uint16_t cpuid = (uint16_t)(vmm_input_params->cpuid);

	uint64_t lowest_stacks_addr = 0;
	uint32_t stacks_size = 0;
	uint64_t heap_address;
	uint32_t heap_size;
	guest_cpu_handle_t initial_gcpu = NULL;
	guest_cpu_handle_t gcpu = NULL;

	/* stage 1: setup host */
	if (cpuid == 0) {
		print_trace(
			"\nVMM image base address = 0x%llX\n",
			evmm_desc->evmm_file.runtime_addr);

		print_trace(
		"\nInitializing all data structures...\n");

		stack_get_details(&lowest_stacks_addr, &stacks_size);
		print_trace(
			"\nStacks are successfully initialized:\n");
		print_trace(
			"\tlowest address of all stacks area = 0x%llX\n",
			lowest_stacks_addr);
		print_trace("\tsize of whole stacks area = 0x%llX\n",
			stacks_size);
		D(stack_print());

		/* Initialize Heap */
		heap_address = lowest_stacks_addr + stacks_size;
		heap_size =
			(uint32_t)((evmm_desc->evmm_file.runtime_addr +
			evmm_desc->evmm_file.runtime_total_size) -
			heap_address);
		vmm_heap_initialize(heap_address, heap_size);
		vmm_pool_initialize();

		/* Initialize GDT for all cpus */
		gdt_setup();
		print_trace("\nGDT setup is finished.\n");

		/* Initialize IDT for all cpus */
		idt_setup();
		print_trace("\nIDT setup is finished.\n");
		isr_setup();
		print_trace("\nISR setup is finished. \n");

		/* Initialize Host Memory Manager */
		hmm_setup(evmm_desc);
		mtrr_init();
		page_walk_init();

		/* Initialize EFI SERVICES */
#ifdef LIB_EFI_SERVICES
		VMM_ASSERT_EX(init_efi_services(evmm_desc->system_table_base), "Failed init efi services\n");
#endif
	}

	gdt_load(cpuid);
	print_trace("GDT is loaded.\n");
	idt_load();
	print_trace("IDT is loaded.\n");

#ifdef DEBUG
	/*
	 * The mtrr_check() is designed to check the consistency of MTRR values between BSP and AP.
	 * The logic is to record MTRR values of BSP first and then compare values of AP. So this
	 * point is the right place to execute mtrr_check() which will run BSP before APs.
	 */
	mtrr_check();
#endif

	if (cpuid == 0) {
		BSP_SET_STAGE(STAGE_SETUP_HOST);
	}

	hmm_enable();

	/* stage 2: setup vmx */
	if (cpuid != 0) {
		AP_WAIT_FOR_STAGE(STAGE_SETUP_VMX);
	}

	VMM_ASSERT_EX(check_vmx(), "VT is not supported or enabled\n");
	if (cpuid == 0) {
		vmx_cap_init();
	}
#ifdef DEBUG
	vmx_cap_check();
#endif
	/* init CR0/CR4 to the VMX compatible values */
	asm_set_cr0(get_init_cr0());
	asm_set_cr4(get_init_cr4());

	if (cpuid == 0) {
		BSP_SET_STAGE(STAGE_SETUP_VMX);
	}

	host_cpu_vmx_on();

	/* stage 3: init module */
	if (cpuid != 0) {
		AP_WAIT_FOR_STAGE(STAGE_INIT_MODULE);
	} else {
#ifdef MODULE_XSAVE
		/* xsave(MMX|SSE) equals to fxsave*/
		xsave_isolation_init(XSAVE_MMX | XSAVE_SSE);
#else
#ifdef MODULE_FXSAVE
		fxsave_isolation_init();
#endif
#endif
	}

#ifdef MODULE_L1TF
	l1tf_init();
#endif

#ifdef MODULE_FXSAVE
	fxsave_enable();
#endif

#ifdef MODULE_LAPIC_ID
	lapic_id_init();
#endif

	if (cpuid == 0) {
#ifdef MODULE_VMX_TIMER
		vmx_timer_init();
#endif

#ifdef MODULE_IPC
		/* Dependency:
		 *  LIB_LAPIC_IPI,
		 *  MODULE_LAPIC_ID */
		ipc_init();
#endif

#ifdef MODULE_MSR_ISOLATION
		msr_isolation_init();
#endif

#ifdef MODULE_SPECTRE
		spectre_init();
#endif

		cr_write_init();

#ifdef MODULE_MSR_MONITOR
		msr_monitor_init();
#endif

#ifdef MODULE_PERF_CTRL_ISOLATION
		msr_perf_ctrl_isolation_init();
#endif

#ifdef MODULE_TSC
		/* Dependency:
		 *  MODULE_MSR_MONITOR */
		tsc_init();
#endif

#ifdef MODULE_VMENTER_CHECK
		vmenter_check_init();
#endif

#ifdef MODULE_VMEXIT_TASK_SWITCH
		vmexit_task_switch_init();
#endif

#ifdef MODULE_IO_MONITOR
		io_monitor_init();
#endif

#ifdef MODULE_DEV_BLK
		/* Dependency:
		 *  MODULE_ACPI,
		 *  MODULE_IO_MONITOR */
		device_block_init();
#endif

#ifdef MODULE_DR
		dr_isolation_init();
#endif

#ifdef MODULE_CR
		cr_isolation_init();
#endif

#ifdef MODULE_VMCALL
		vmcall_init();
#endif

#ifdef MODULE_VTD
		/* Dependency:
		 *  MODULE_ACPI */
		vtd_init();
#endif

#ifdef MODULE_EXT_INTR
		ext_intr_init();
#endif

#ifdef MODULE_VIRTUAL_APIC
		virtual_apic_init();
#endif

#ifdef MODULE_NESTED_VT
		nested_vt_init();
#endif

#ifdef MODULE_VMEXIT_INIT
		vmexit_register_init_event();
#endif

#ifdef MODULE_SUSPEND
		/* Dependency:
		 *  LIB_MP_INIT,
		 *  MODULE_IPC,
		 *  MODULE_IO_MONITOR,
		 *  MODULE_ACPI */
		suspend_bsp_init(evmm_desc->sipi_ap_wkup_addr);
#endif

#ifdef MODULE_PROFILE
		profile_init();
#endif

#ifdef MODULE_INTERRUPT_IPI
		interrupt_ipi_init();
#endif

#ifdef MODULE_TEMPLATE_TEE
		template_tee_init(evmm_desc->x64_cr3);
#endif

		/* Prepare guest0 gcpu initial state */
		prepare_g0gcpu_init_state(&evmm_desc->guest0_gcpu0_state);
#ifdef MODULE_APS_STATE
		prepare_g0ap_init_state(&evmm_desc->guest0_aps_state[0]);
#endif
	} else {

#ifdef MODULE_SUSPEND
		/* Dependency:
		 *  LIB_MP_INIT,
		 *  MODULE_IPC,
		 *  MODULE_IO_MONITOR,
		 *  MODULE_ACPI */
		suspend_ap_init();
#endif
	}

	if (cpuid == 0) {
		BSP_SET_STAGE(STAGE_INIT_MODULE);
	}

	/* stage 4: create guest */
	if (cpuid != 0) {
		AP_WAIT_FOR_STAGE(STAGE_INIT_GUEST);
	} else {
		print_trace("Create guests\n");

		/* Initialize GPM */
		gpm_init();

		/* Check rowhammer mitigation for evmm */
		if (evmm_desc->evmm_file.barrier_size == 0)
			print_warn("No rawhammer mitigation for EVMM\n");

		guest_save_evmm_range(evmm_desc->evmm_file.runtime_addr - evmm_desc->evmm_file.barrier_size,
			 evmm_desc->evmm_file.runtime_total_size + 2 * evmm_desc->evmm_file.barrier_size);

		/* Create guest with RWX(0x7) attribute */
		create_guest(host_cpu_num, 0x7);

#ifdef MODULE_TRUSTY_TEE
		init_trusty_tee(evmm_desc);
#endif

#ifdef MODULE_TRUSTY_GUEST
		/* Dependency:
		 *  LIB_IPC,
		 *  MODULE_VMCALL,
		 *  MODULE_MSR_ISOLATION,
		 *  MODULE_DEADLOOP */
		init_trusty_guest(evmm_desc);
#endif

#ifdef MODULE_OPTEE_GUEST
		init_optee_guest(evmm_desc);
#endif

		BSP_SET_STAGE(STAGE_INIT_GUEST);

#ifdef MODULE_VTD
		/* vtd_done() will not affect AP boot.
		 * since BSP needs to wait for AP before gcpu_resume(), put vtd_done()
		 * here will take BSP more time and reduce the wait time for AP. */
		vtd_activate();
#endif

#ifdef MODULE_BLOCK_NPK
		block_npk();
#endif
	}

	/* stage 5: init gcpu */
	gcpu = get_current_gcpu();
	VMM_ASSERT_EX(gcpu, "initial gcpu is NULL\n");
	do {
		gcpu_set_host_state(gcpu);

		gcpu_set_ctrl_state(gcpu);

		event_raise(gcpu, EVENT_GCPU_INIT, NULL);

		cr_write_gcpu_init(gcpu);
		ept_gcpu_init(gcpu);

		event_raise(gcpu, EVENT_GCPU_MODULE_INIT, NULL);

		gcpu = gcpu->next_same_host_cpu;
	} while (gcpu != get_current_gcpu());

	event_raise(NULL, EVENT_SCHEDULE_INITIAL_GCPU, NULL);
	initial_gcpu = schedule_initial_gcpu();
	print_trace(
		"initial guest selected: uint16_t: %d GUEST_CPU_ID: %d\n",
		initial_gcpu->guest->id,
		initial_gcpu->id);
#ifdef SYNC_CPU_IN_BOOT
	if (cpuid == 0) {
		print_trace("Wait for APs to launch the first Guest CPU\n");
		BSP_WAIT_FOR_AP(host_cpu_num - 1);
	} else {
		AP_SET_LAUNCHED();
	}
#endif

	/* stage 6: launch guest */

	/* Add ifndef here to avoid duplication of print because template tee also outputs the similar log */
#ifndef MODULE_TEMPLATE_TEE
	print_info("CPU%d Launch first Guest\n", cpuid);
#endif
	gcpu_resume(initial_gcpu);

	//print_panic("CPU%d Resume initial guest cpu failed\n", cpuid);

	//VMM_DEADLOOP();
}

typedef void (*func_main_continue_t) (void *params);
static inline void hw_set_stack_pointer(uint64_t new_rsp, func_main_continue_t func, void *params)
{
	__asm__ __volatile__(
		"mov %2, %%rdi;"
		"mov %0, %%rsp;"
		"call *%1;"
		"jmp ."
		::"r"(new_rsp), "r"((uint64_t)func), "r"((uint64_t)params)
	);
}

void vmm_main(uint32_t cpuid,
			uint64_t evmm_desc_u)
{
	evmm_desc_t *evmm_desc = (evmm_desc_t *) evmm_desc_u;
	uint64_t new_stack_pointer = 0;
	vmm_input_params_t input_params;

	VMM_ASSERT_EX((evmm_desc != NULL), "evmm_desc is NULL\n");

	if (cpuid == 0) {
		/* save number of CPUs */
		host_cpu_num = (uint16_t)evmm_desc->num_of_cpu;
		top_of_memory = evmm_desc->top_of_mem;
		tsc_per_ms = evmm_desc->tsc_per_ms;
		vmm_print_init(FALSE);
		VMM_ASSERT_EX((host_cpu_num >= 1) && (host_cpu_num <= MAX_CPU_NUM), "invalid host_cpu_num(%d)\n", host_cpu_num);
		D(VMM_ASSERT(top_of_memory != 0 && ((top_of_memory & PAGE_4K_MASK) == 0)));
		D(VMM_ASSERT(tsc_per_ms != 0));

		/*at least be 4G to have MMIO mapped*/
		if (top_of_memory < (1ULL << 32))
			top_of_memory = (1ULL << 32);

		stack_initialize(evmm_desc);
	} else {
		AP_WAIT_FOR_STAGE(STAGE_SETUP_HOST);
	}

	/* setup stack */
	new_stack_pointer = stack_get_cpu_sp((uint16_t)cpuid);

	input_params.cpuid = cpuid;
	input_params.evmm_desc = evmm_desc_u;

	/* reserve first 16*8 bytes of stack to save/restore GP register */
	hw_set_stack_pointer(new_stack_pointer - (REG_GP_COUNT * sizeof(uint64_t)),
		(func_main_continue_t)vmm_main_continue, &input_params);
	VMM_DEADLOOP();
}
