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
#include "vmm_arch.h"
#include "dbg.h"
#include "vmm_objects.h"
#include "guest.h"
#include "gcpu.h"
#include "gcpu_state.h"
#include "gpm.h"
#include "scheduler.h"
#include "hmm.h"
#include "host_cpu.h"
#include "event.h"
#include "heap.h"
#include "trusty_info.h"
#include "gcpu_inject_event.h"

#include "lib/util.h"
#include "lib/image_loader.h"

#include "modules/vmcall.h"
#ifdef MODULE_MSR_ISOLATION
#include "modules/msr_isolation.h"
#endif
#ifdef MODULE_DEADLOOP
#include "modules/deadloop.h"
#endif
#ifdef MODULE_IPC
#include "modules/ipc.h"
#endif
#include "modules/trusty_guest.h"

#ifdef MODULE_EPT_UPDATE
#include "modules/ept_update.h"
#endif

#ifdef MODULE_VTD
#include "modules/vtd.h"
#endif

typedef enum {
	TRUSTY_VMCALL_SMC             = 0x74727500,
	TRUSTY_VMCALL_PENDING_INTR    = 0x74727505,
	TRUSTY_VMCALL_DUMP_INIT       = 0x74727507,
}vmcall_id_t;

/* 0x31 is not used in Android (in CHT, BXT, GSD simics) */
#define LK_TIMER_INTR	0x31

/* Trusted OS calls internal to secure monitor */
#define	SMC_ENTITY_SECURE_MONITOR	60

/* Used in SMC_STDCALL_NR */
#define SMC_NR(entity, fn, fastcall, smc64) ((((fastcall) & 0x1) << 31) | \
					     (((smc64) & 0x1) << 30) | \
					     (((entity) & 0x3F) << 24) | \
					     ((fn) & 0xFFFF) \
					    )

/* Used in SMC_SC_NOP */
#define SMC_STDCALL_NR(entity, fn)	SMC_NR((entity), (fn), 0, 0)

/*
 * SMC_SC_NOP - origin defination is in smcall.h in lk
 * need use this defination to identify the timer interrupt
 * from Android to LK
 */
#define SMC_SC_NOP	SMC_STDCALL_NR  (SMC_ENTITY_SECURE_MONITOR, 3)

static boolean_t guest_in_ring0(guest_cpu_handle_t gcpu)
{
	uint64_t  cs_sel;
	vmcs_obj_t vmcs = gcpu->vmcs;

	cs_sel = vmcs_read(vmcs, VMCS_GUEST_CS_SEL);
	/* cs_selector[1:0] is CPL*/
	if ((cs_sel & 0x3) == 0)
	{
		return TRUE;
	}

	gcpu_inject_ud(gcpu);
	return FALSE;
}

enum {
	SMC_STAGE_BOOT = 0,
	SMC_STAGE_LK_DONE,
	SMC_STAGE_ANDROID_DONE
};

static uint32_t smc_stage;
static trusty_desc_t *trusty_desc;
static void *dev_sec_info;

static void smc_copy_gp_regs(guest_cpu_handle_t gcpu, guest_cpu_handle_t next_gcpu)
{
	uint64_t rdi, rsi, rdx, rbx;

	rdi = gcpu_get_gp_reg(gcpu, REG_RDI);
	rsi = gcpu_get_gp_reg(gcpu, REG_RSI);
	rdx = gcpu_get_gp_reg(gcpu, REG_RDX);
	rbx = gcpu_get_gp_reg(gcpu, REG_RBX);
	gcpu_set_gp_reg(next_gcpu, REG_RDI, rdi);
	gcpu_set_gp_reg(next_gcpu, REG_RSI, rsi);
	gcpu_set_gp_reg(next_gcpu, REG_RDX, rdx);
	gcpu_set_gp_reg(next_gcpu, REG_RBX, rbx);

	/* send timer interrupt to LK */
	if ((gcpu->guest->id == 0) &&
		(SMC_SC_NOP == rdi) &&
		(LK_TIMER_INTR == rsi)) {
		gcpu_set_pending_intr(next_gcpu, LK_TIMER_INTR);
	}
}

/* Reserved size for dev_sec_info/trusty_startup and Stack(1 page) */
/*
 * Trusty load size formula:
 * MEM assigned to Trusty is 16MB size in total
 * Stack size must be reserved
 * LK run time memory will be calculated by LK itself
 * The rest region can be use to load Trusy image
 * ------------------------
 *     ^     |          |
 *   Stack   |          |
 * ----------|          |
 *     ^     |          |
 *     |     |          |
 *   HEAP    |          |
 *     |     |    M     |
 *  ---------|          |
 *     ^     |    E     |
 *     |     |          |
 *   Trusty  |    M     |
 *   load    |          |
 *   size    |          |
 *     |     |          |
 *  ---------|          |
 *     ^     |          |
 *  Boot info|          |
 * ------------------------

 */

static void relocate_trusty_image(void)
{
	/* lk region: first page is trusty info, last page is stack. */
	trusty_desc->lk_file.runtime_addr += PAGE_4K_SIZE;
	trusty_desc->lk_file.runtime_total_size -= PAGE_4K_SIZE;

	VMM_ASSERT_EX(relocate_elf_image(&(trusty_desc->lk_file), &trusty_desc->gcpu0_state.rip),
			"relocate trusty image failed!\n");

	/* restore lk runtime address and total size */
	trusty_desc->lk_file.runtime_addr -= PAGE_4K_SIZE;
	trusty_desc->lk_file.runtime_total_size += PAGE_4K_SIZE;
}

static void setup_trusty_sec_info(void *sec_info)
{
	VMM_ASSERT_EX(sec_info, "sec_info is NULL!\n");
	VMM_ASSERT_EX(*((uint32_t *)sec_info) < 0x1000, "size of sec_info exceed 1 page!\n");

	gpm_remove_mapping(guest_handle(0),
				trusty_desc->lk_file.runtime_addr,
				trusty_desc->lk_file.runtime_total_size);

	memcpy((void *)trusty_desc->lk_file.runtime_addr, sec_info, *((uint32_t *)sec_info));
	memset(sec_info, 0, *((uint32_t *)sec_info));

#ifdef DEBUG //remove VMM's access to LK's memory to make sure VMM will not read/write to LK in runtime
	hmm_unmap_hpa(trusty_desc->lk_file.runtime_addr, trusty_desc->lk_file.runtime_total_size);
#endif
	print_trace(
			"Primary guest GPM: remove sguest image base %llx size 0x%x\r\n",
			trusty_desc->lk_file.runtime_addr,
			trusty_desc->lk_file.runtime_total_size);
}

static void smc_vmcall_exit(guest_cpu_handle_t gcpu)
{
	guest_cpu_handle_t next_gcpu;
	uint64_t rdi;
	trusty_boot_params_t *trusty_boot_params;

	if(!guest_in_ring0(gcpu))
	{
		return;
	}

	vmcs_read(gcpu->vmcs, VMCS_GUEST_RIP);// update cache
	vmcs_read(gcpu->vmcs, VMCS_EXIT_INSTR_LEN);// update cache
	next_gcpu = schedule_next_gcpu();

	switch (smc_stage)
	{
		case SMC_STAGE_BOOT:
			if(0 == host_cpu_id())
			{
				if(0 == gcpu->guest->id)
				{
					/* From Android */
					/* avoid waring -Wbad-function-cast */
					rdi = gcpu_get_gp_reg(gcpu, REG_RDI);
					trusty_boot_params = (trusty_boot_params_t *)rdi;

#ifdef DEBUG
					VMM_ASSERT_EX(trusty_boot_params, "Invalid trusty boot params\n");
#endif

#if LOADER_STAGE0_SUB == abl
					trusty_desc->lk_file.loadtime_addr = trusty_boot_params->load_base;
					trusty_desc->lk_file.loadtime_size = trusty_boot_params->load_size;
					relocate_trusty_image();
#elif LOADER_STAGE0_SUB == sbl
					trusty_desc->gcpu0_state.rip = trusty_boot_params->entry_point;
					if(trusty->lk_file.runtime_addr != 0)
						print_warn("LK's runtime address should not be filled before here.\n");
					trusty_desc->lk_file.runtime_addr = trusty_boot_params->runtime_addr;
#endif

					setup_trusty_sec_info(dev_sec_info);
					mem_free(dev_sec_info);
					dev_sec_info = NULL;

					vmcs_write(next_gcpu->vmcs, VMCS_GUEST_RIP, trusty_desc->gcpu0_state.rip);
				}else{
					smc_stage = SMC_STAGE_LK_DONE;
					print_info("VMM: Launch Android\n");
				}
			}
			break;

		case SMC_STAGE_LK_DONE:
			D(VMM_ASSERT(gcpu->guest->id == 0))
			print_init(FALSE);
			smc_stage = SMC_STAGE_ANDROID_DONE;

			smc_copy_gp_regs(gcpu, next_gcpu);
			break;

		case SMC_STAGE_ANDROID_DONE:
			smc_copy_gp_regs(gcpu, next_gcpu);
			break;

		default:
			print_trace("Invalid stage:(%d)\n", smc_stage);
			VMM_DEADLOOP();
			break;
	}
}

// set pending interrupt to next gcpu
static void trusty_vmcall_set_pending_intr(guest_cpu_handle_t gcpu)
{
	guest_cpu_handle_t next_gcpu = gcpu->next_same_host_cpu;
	uint8_t vector = (uint8_t)gcpu_get_gp_reg(gcpu, REG_RBX);

	if(!guest_in_ring0(gcpu))
	{
		return;
	}

	gcpu_set_pending_intr(next_gcpu, vector);
}

static void trusty_vmcall_dump_init(guest_cpu_handle_t gcpu)
{
#ifdef MODULE_DEADLOOP
	uint64_t dump_gva;

	D(VMM_ASSERT(gcpu->guest->id == 0));

	if(!guest_in_ring0(gcpu))
	{
		return;
	}

	/* RDI stored the pointer of deadloop_dump_t which allocated by the guest */
	dump_gva = gcpu_get_gp_reg(gcpu, REG_RDI);

	/* register event deadloop */
	if(!deadloop_setup(gcpu, dump_gva))
		return;
#endif
	/* set the return value */
	gcpu_set_gp_reg(gcpu, REG_RAX, 0);
}

static void guest_register_vmcall_services()
{
	vmcall_register(0, TRUSTY_VMCALL_SMC, smc_vmcall_exit);
	vmcall_register(0, TRUSTY_VMCALL_PENDING_INTR, trusty_vmcall_set_pending_intr);
	vmcall_register(0, TRUSTY_VMCALL_DUMP_INIT, trusty_vmcall_dump_init);
	vmcall_register(1, TRUSTY_VMCALL_SMC, smc_vmcall_exit);
	vmcall_register(1, TRUSTY_VMCALL_PENDING_INTR, trusty_vmcall_set_pending_intr);
}

#ifdef AP_START_IN_HLT
static void set_guest0_aps_to_hlt_state(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	D(VMM_ASSERT(gcpu));

	if (gcpu->guest->id == 0)
	{
		if (gcpu->id != 0)
		{
			vmcs_write(gcpu->vmcs, VMCS_GUEST_ACTIVITY_STATE, ACTIVITY_STATE_HLT);
		}
	}
}
#endif

static void trusty_set_gcpu_state(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	if (gcpu->guest->id == 1) {
		gcpu_set_init_state(gcpu, &trusty_desc->gcpu0_state);

		if (gcpu->id != 0) {
			gcpu_set_reset_state(gcpu);
		}
	}
}

static void *sensitive_info;
#ifndef DEBUG //in DEBUG build, access from VMM to LK will be removed. so, only erase sensetive info in RELEASE build
static void trusty_erase_sensetive_info(void)
{
	memset(sensitive_info, 0, PAGE_4K_SIZE);
}
#endif

void trusty_register_deadloop_handler(evmm_desc_t *evmm_desc)
{
	D(VMM_ASSERT_EX(evmm_desc, "evmm_desc is NULL\n"));
	sensitive_info = (void *)evmm_desc->trusty_desc.lk_file.runtime_addr;
#ifndef DEBUG
	register_final_deadloop_handler(trusty_erase_sensetive_info);
#endif
}

void init_trusty_guest(evmm_desc_t *evmm_desc)
{
	trusty_startup_info_t *trusty_para;
	uint32_t cpu_num = 1;
	uint32_t dev_sec_info_size;
#ifdef MODULE_EPT_UPDATE
	uint64_t upper_start;
#endif

	D(VMM_ASSERT_EX(evmm_desc, "evmm_desc is NULL\n"));

	trusty_desc = (trusty_desc_t *)&evmm_desc->trusty_desc;
	D(VMM_ASSERT(trusty_desc));

	dev_sec_info_size = *((uint32_t *)trusty_desc->dev_sec_info);
	VMM_ASSERT_EX(!(dev_sec_info_size & 0x3ULL), "size of trusty boot info is not 32bit aligned!\n");

	trusty_para = (trusty_startup_info_t *)ALIGN_F(trusty_desc->lk_file.runtime_addr + dev_sec_info_size, 8);
	VMM_ASSERT_EX(((uint64_t)trusty_para + sizeof(trusty_startup_info_t)) <
			(trusty_desc->lk_file.runtime_addr + PAGE_4K_SIZE),
			"size of (dev_sec_info+trusty_startup_info) exceeds the reserved 4K size!\n");

	/* Used to check structure in both sides are same */
	trusty_para->size_of_this_struct    = sizeof(trusty_startup_info_t);
	/* Used to set heap of LK */
	trusty_para->mem_size               =
		trusty_desc->lk_file.runtime_total_size;
	trusty_para->calibrate_tsc_per_ms   = tsc_per_ms;
	trusty_para->trusty_mem_base        =
		trusty_desc->lk_file.runtime_addr;
	trusty_desc->gcpu0_state.gp_reg[REG_RDI] = (uint64_t)trusty_para;

	print_trace("Init trusty guest\n");

	/* TODO: refine it later */
#ifdef ENABLE_SGUEST_SMP
	cpu_num = evmm_desc->num_of_cpu;
	trusty_para->sipi_ap_wkup_addr = evmm_desc->sipi_ap_wkup_addr;
#endif
	create_guest(cpu_num, &(evmm_desc->evmm_file));
	event_register(EVENT_GCPU_INIT, trusty_set_gcpu_state);

#ifdef AP_START_IN_HLT
	event_register(EVENT_GCPU_MODULE_INIT, set_guest0_aps_to_hlt_state);
#endif

#ifdef MODULE_EPT_UPDATE
	/* remove the whole memory region from 0 ~ top_of_mem except lk self in Trusty(guest1) EPT */
	/* remove lower */
	gpm_remove_mapping(guest_handle(1), 0, trusty_desc->lk_file.runtime_addr);

	/* remove upper */
	upper_start = trusty_desc->lk_file.runtime_addr + trusty_desc->lk_file.runtime_total_size;
	gpm_remove_mapping(guest_handle(1), upper_start, top_of_memory - upper_start);

	ept_update_install(1);
#endif

#ifdef PACK_LK
	relocate_trusty_image();
	setup_trusty_sec_info(trusty_desc->dev_sec_info);
#else
	/* Copy dev_sec_info from loader to VMM's memory */
	dev_sec_info = mem_alloc(dev_sec_info_size);
	memcpy(dev_sec_info, trusty_desc->dev_sec_info, dev_sec_info_size);
	memset(trusty_desc->dev_sec_info, 0, dev_sec_info_size);

	schedule_next_gcpu_as_init(0);
#endif

#ifdef DMA_FROM_CSE
	vtd_assign_dev(1, DMA_FROM_CSE);
#endif

	guest_register_vmcall_services();

#ifdef MODULE_MSR_ISOLATION
	/* Isolate below MSRs between guests and set initial value to 0 */
	add_to_msr_isolation_list(MSR_STAR, 0, GUESTS_ISOLATION);
	add_to_msr_isolation_list(MSR_LSTAR, 0, GUESTS_ISOLATION);
	add_to_msr_isolation_list(MSR_FMASK, 0, GUESTS_ISOLATION);
	add_to_msr_isolation_list(MSR_KERNEL_GS_BASE, 0, GUESTS_ISOLATION);
#endif
}

