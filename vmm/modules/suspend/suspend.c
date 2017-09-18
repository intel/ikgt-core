/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
* Copyright (C) 2000 - 2007, R. Byron Moore
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions, and the following disclaimer,
*    without modification.
* 2. Redistributions in binary form must reproduce at minimum a disclaimer
*    substantially similar to the "NO WARRANTY" disclaimer below
*    ("Disclaimer") and any redistribution must be conditioned upon
*    including a substantially similar Disclaimer requirement for further
*    binary redistribution.
* 3. Neither the names of the above-listed copyright holders nor the names
*    of any contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
*
* NO WARRANTY
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
* A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
* HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
* IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGES.
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
#include "gcpu_switch.h"
#include "hmm.h"
#include "vmm_util.h"
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

/*------------------------------Types and Macros---------------------------*/
typedef struct {
	uint32_t *p_waking_vector;
	uint32_t orig_waking_vector;
	uint32_t sipi_page; // must under 1M
} suspend_data_t;

typedef struct {
	uint16_t tr;
	volatile uint8_t slept;
	uint8_t padding[5];
} suspend_percpu_data_t;

/*------------------------------Local Variables-------------------------------*/
static suspend_percpu_data_t suspend_percpu_data[MAX_CPU_NUM];
static suspend_data_t suspend_data;
acpi_fadt_info_t acpi_fadt_info;
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

	do {
		vmcs_clr_ptr(gcpu_next->vmcs);

		gcpu_next = gcpu_next->next_same_host_cpu;
	} while (gcpu_next != gcpu);

	vmx_off();
	if (host_cpu_id() != 0){
		suspend_percpu_data[host_cpu_id()].slept = 1;
		asm_wbinvd();
		asm_hlt();
	}
}

static void prepare_s3(guest_cpu_handle_t gcpu)
{
	uint8_t cpu_id;
#ifdef DEBUG
	gdtr64_t gdtr;
#endif

	setup_sipi_page(suspend_data.sipi_page, TRUE, (uint64_t)main_from_s3);

#ifdef DEBUG
	asm_sgdt(&gdtr);
	VMM_ASSERT((gdtr.base + gdtr.limit) < (1ULL << 32));
	VMM_ASSERT(asm_get_cr3() < (1ULL << 32));
	VMM_ASSERT(asm_rdmsr(MSR_FS_BASE) < (1ULL << 32));
#endif

	suspend_data.orig_waking_vector = *(suspend_data.p_waking_vector);
	*(suspend_data.p_waking_vector) = suspend_data.sipi_page;

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
	}
	else {
		suspend_percpu_data[cpu_id].slept = 0;
	}

	event_raise(NULL, EVENT_RESUME_FROM_S3, NULL);

	host_cpu_clear_pending_nmi();
	gcpu_resume(gcpu);
}

void suspend_s3_io_handler(guest_cpu_handle_t gcpu,
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

static void suspend_guest_setup(UNUSED guest_cpu_handle_t gcpu, void *pv)
{
	guest_handle_t guest = (guest_handle_t)pv;

	/* protect sipi page with ept for Guest0 */
	if (guest->id == 0)
		gpm_remove_mapping(guest, (uint64_t)suspend_data.sipi_page, 0x1000);
}

void suspend_bsp_init(uint32_t sipi_page)
{
	uint8_t i;

	D(VMM_ASSERT(((sipi_page & PAGE_4K_MASK) == 0) && (sipi_page < 0x100000)));
	event_register(EVENT_GUEST_MODULE_INIT, suspend_guest_setup);

	acpi_pm_init(&acpi_fadt_info);
	suspend_data.p_waking_vector = acpi_fadt_info.p_waking_vector;
	suspend_data.sipi_page = sipi_page;

	setup_percpu_data();

	for (i = 0; i < ACPI_PM1_CNTRL_COUNT; ++i) {
		print_trace("[SUSPEND] Install handler at Pm1%cControlBlock(0x%llX)\n",
			'a' + i, acpi_fadt_info.port_id[i]);
		io_monitor_register(0,acpi_fadt_info.port_id[i],
			NULL, suspend_s3_io_handler);
	}
}

void suspend_ap_init(void)
{
	setup_percpu_data();
}
