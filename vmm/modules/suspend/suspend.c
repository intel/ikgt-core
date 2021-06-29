/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "gcpu.h"
#include "gcpu_state.h"
#include "gcpu_switch.h"
#include "hmm.h"
#include "heap.h"
#include "vmcs.h"
#include "vmx_cap.h"
#include "stack.h"
#include "scheduler.h"
#include "vmexit_cr_access.h"
#include "guest.h"
#include "gpm.h"
#include "acpi_pm.h"
#include "event.h"

#include "lib/util.h"
#include "lib/mp_init.h"

#include "modules/io_monitor.h"
#include "modules/ipc.h"
#include "modules/acpi.h"

/*------------------------------Types and Macros---------------------------*/
typedef struct {
	uint32_t *p_waking_vector;
	uint32_t orig_waking_vector;
	uint32_t sipi_page; // must under 1M
	uint8_t *save_area;
} suspend_data_t;

typedef struct {
	uint16_t tr;
	volatile uint8_t slept;
	uint8_t padding[5];
} suspend_percpu_data_t;

/*------------------------------Local Variables-------------------------------*/
static suspend_percpu_data_t suspend_percpu_data[MAX_CPU_NUM];
static suspend_data_t suspend_data;
/*------------------Forward Declarations for Local Functions---------------*/
static void main_from_s3(uint32_t apic_id);

/*-----------------------------C-Code Starts Here--------------------------*/

static void setup_percpu_data(void)
{
	uint16_t cpu_id;
	uint64_t esp;

	cpu_id = host_cpu_id();
	suspend_percpu_data[cpu_id].tr = asm_str();

	esp = stack_get_cpu_sp(cpu_id) - (REG_GP_COUNT * sizeof(uint64_t));
	D(VMM_ASSERT(esp < (1ULL << 32)));
	setup_cpu_startup_stack(cpu_id, (uint32_t)esp);
}

static void prepare_s3_percpu(guest_cpu_handle_t gcpu, void *unused UNUSED)
{
	guest_cpu_handle_t gcpu_next = gcpu;

	print_trace(
		"[SUSPEND] CPU(%d) going to go to S3\n",
		host_cpu_id());

	event_raise(gcpu, EVENT_SUSPEND_TO_S3, NULL);

	do {
		vmcs_clr_ptr(gcpu_next->vmcs);

		gcpu_next = gcpu_next->next_same_host_cpu;
	} while (gcpu_next != gcpu);

	vmx_off();
	if (host_cpu_id() != 0){
		suspend_percpu_data[host_cpu_id()].slept = 1;
		asm_wbinvd();

		/*
		 * Must use hlt-loop/busy-loop to stop CPU. This is because the CPU might
		 * be waked up if there is a lapic timer set before suspend especially running
		 * on top of KVM.
		 */
		__STOP_HERE__
	}
}

static void save_sipi_mem(void)
{
	uint32_t size = get_startup_code_size();

	if (suspend_data.save_area == NULL) {
		suspend_data.save_area = mem_alloc(size);
	}

	memcpy(suspend_data.save_area, (void *)(uint64_t)suspend_data.sipi_page, size);
}

static void restore_sipi_mem(void)
{
	memcpy((void *)(uint64_t)suspend_data.sipi_page, suspend_data.save_area, get_startup_code_size());
}

static boolean_t prepare_s3(guest_cpu_handle_t gcpu)
{
	uint8_t cpu_id;
#ifdef DEBUG
	gdtr64_t gdtr;
#endif

	save_sipi_mem();

	setup_sipi_page(suspend_data.sipi_page, TRUE, (uint64_t)main_from_s3);

#ifdef DEBUG
	asm_sgdt(&gdtr);
	VMM_ASSERT((gdtr.base + gdtr.limit) < (1ULL << 32));
	VMM_ASSERT(asm_get_cr3() < (1ULL << 32));
	VMM_ASSERT(asm_rdmsr(MSR_FS_BASE) < (1ULL << 32));
#endif

	suspend_data.p_waking_vector = get_waking_vector();

	suspend_data.orig_waking_vector = *suspend_data.p_waking_vector;
	*suspend_data.p_waking_vector = suspend_data.sipi_page;

	print_trace("[SUSPEND] waking_vector_reg_addr=0x%llX fw_waking_vector=0x%llX sipi_page_addr=0x%llX\n",
		suspend_data.p_waking_vector, suspend_data.orig_waking_vector, *(suspend_data.p_waking_vector));

	VMM_ASSERT_EX(suspend_data.orig_waking_vector,
		"[SUSPEND] Waking vector is NULL. S3 is not supported by the platform\n");

	ipc_exec_on_all_other_cpus(prepare_s3_percpu, NULL);
	prepare_s3_percpu(gcpu, NULL);

	for (cpu_id=1; cpu_id < host_cpu_num; cpu_id++) {
		while(suspend_percpu_data[cpu_id].slept != 1) {
			asm_pause();
		}
	}

	print_info("VMM: Enter S3\n");
	asm_wbinvd();
	return TRUE;
}

static void main_from_s3(uint32_t cpu_id)
{
	uint8_t i;
	guest_cpu_handle_t gcpu;
	vmcs_obj_t vmcs;
	gdtr64_t gdtr;
	guest_cpu_handle_t gcpu_next;
	uint16_t tr_val = suspend_percpu_data[cpu_id].tr;

	/* clear [busy] bit of TSS segment descriptor */
	asm_sgdt(&gdtr);
	BITARRAY_CLR((uint64_t *)(gdtr.base + tr_val), 41);

	/* restore TR */
	asm_ltr(tr_val);

	if (cpu_id == 0) {
		vmm_print_init(FALSE);
		print_info("VMM: resume from s3\n");
		wakeup_aps(*(suspend_data.p_waking_vector));
		*(suspend_data.p_waking_vector) = suspend_data.orig_waking_vector;
	}

	asm_set_cr0(get_init_cr0());
	asm_set_cr4(get_init_cr4());

	/* fxsave_enable() is not required since we will not swap gcpu before resume.
	 * in next vmexit, the cr4 will be set from VMCS_HOST_CR4,
	 * which will have CR4.FXSAVE set if fxsave module is enabled.
	 */

	gcpu = get_current_gcpu();
	VMM_ASSERT_EX(gcpu, "S3: gcpu is NULL\n");
	vmcs = gcpu->vmcs;

	host_cpu_vmx_on();
	vmcs_set_ptr(vmcs);

	gcpu_next = gcpu;
	do {
		vmcs = gcpu_next->vmcs;
		vmcs_clear_launched(vmcs);

		gcpu_next = gcpu_next->next_same_host_cpu;
	} while (gcpu_next != gcpu);

	gcpu_set_reset_state(gcpu);

	if (cpu_id == 0) {
		vmcs_write(gcpu->vmcs, VMCS_GUEST_ACTIVITY_STATE,
				ACTIVITY_STATE_ACTIVE);
		gcpu_set_seg(gcpu,
			SEG_CS,
			suspend_data.orig_waking_vector >> 4,
			suspend_data.orig_waking_vector & 0xFFFFFFF0, 0xFFFF, 0x9B);
		vmcs_write(gcpu->vmcs, VMCS_GUEST_RIP, suspend_data.orig_waking_vector & 0xF);

		for (i=1; i < host_cpu_num; i++) {
			while (suspend_percpu_data[i].slept != 0) {
				asm_pause();
			}
		}
		restore_sipi_mem();
	}
	else {
		suspend_percpu_data[cpu_id].slept = 0;
	}

	event_raise(gcpu, EVENT_RESUME_FROM_S3, NULL);

	host_cpu_clear_pending_nmi();
	gcpu_resume(gcpu);
}

static void suspend_s3_io_handler(guest_cpu_handle_t gcpu,
				uint16_t port_id,
				uint32_t port_size,
				uint32_t value)
{
	print_init(FALSE);

	if(acpi_pm_is_s3(port_id, port_size, value)) {
		prepare_s3(gcpu);
		/* After suspend_percpu_s3() executed, vmx is off,
		 * so when evmm try to do gcpu_resume() will cause exception.
		 * So here pass transparently and deadloop without dump msg.
		 */
		io_transparent_write_handler(gcpu, port_id, port_size, value);
		__STOP_HERE__;
	} else {
		io_transparent_write_handler(gcpu, port_id, port_size, value);
	}
}

static void setup_pm1x_monitor(acpi_generic_address_t *pm1x_reg)
{
	/*
	 * NOTE: Currently, only IO is supported/tested, so assert here if GAS type is MEM.
	 *       Need to revisit it if firmware use MMIO/MEM in future.
	 */
	switch (pm1x_reg->as_id) {
		case ACPI_GAS_ID_MEM:
			VMM_ASSERT("[SUSPEND] MMIO/MEM trigger is not supported!\n");
			break;
		case ACPI_GAS_ID_IO:
			io_monitor_register(0, pm1x_reg->addr, NULL, suspend_s3_io_handler);
			break;
		default:
			VMM_ASSERT("[SUSPEND] Invalid suspend address type\n");
			break;
	}
	print_trace("[SUSPEND] Install handler at Pm1XControlBlock, ASID=%d, addr=0x%llx\n",
			pm1x_reg->as_id, pm1x_reg->addr);
}

static void monitor_pm1x_reg(void)
{
	acpi_generic_address_t *pm1x_reg;

	/* Monitor PM1A Control Register -- Required */
	pm1x_reg = get_pm1x_reg(ACPI_PM1A_CNT);
	VMM_ASSERT_EX(pm1x_reg, "PM1A_CNT/X_PM1A_CNT is required!\n");
	setup_pm1x_monitor(pm1x_reg);

	/* Monitor PM1B Control Register -- Optional */
	pm1x_reg = get_pm1x_reg(ACPI_PM1B_CNT);
	if (pm1x_reg)
		setup_pm1x_monitor(pm1x_reg);
}

void suspend_bsp_init(uint32_t sipi_page)
{
	D(VMM_ASSERT(((sipi_page & PAGE_4K_MASK) == 0) && (sipi_page < 0x100000)));

	acpi_pm_init();
	suspend_data.sipi_page = sipi_page;

	setup_percpu_data();

	monitor_pm1x_reg();
}

void suspend_ap_init(void)
{
	setup_percpu_data();
}
