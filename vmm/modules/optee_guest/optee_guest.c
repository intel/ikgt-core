/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

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
#include "optee_info.h"
#include "gcpu_inject_event.h"

#include "lib/util.h"
#include "lib/image_loader.h"

#include "modules/vmcall.h"
#ifdef MODULE_MSR_ISOLATION
#include "modules/msr_isolation.h"
#endif
#ifdef MODULE_IPC
#include "modules/ipc.h"
#endif
#include "modules/optee_guest.h"

#ifdef MODULE_EPT_UPDATE
#include "modules/ept_update.h"
#endif

#ifdef MODULE_VTD
#include "modules/vtd.h"
#endif

typedef enum {
	OPTEE_VMCALL_SMC             = 0x6F707400,
} vmcall_id_t;

enum {
	SMC_STAGE_BOOT = 0,
	SMC_STAGE_OPTEE_DONE,
	SMC_STAGE_REE_DONE
};

enum {
	GUEST_REE = 0,     /* OSloader or Android/Linux/Fuchsia */
	GUEST_OPTEE        /* OP-TEE */
};

#define OPTEE_SHM_SIZE 0x200000

static optee_desc_t *optee_desc;
#ifdef ENABLE_SGUEST_SMP
static uint32_t sipi_ap_wkup_addr;
#endif

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

static void smc_copy_gp_regs(guest_cpu_handle_t gcpu, guest_cpu_handle_t next_gcpu)
{
	uint64_t rdi, rsi, rdx, rbx, rcx;

	rdi = gcpu_get_gp_reg(gcpu, REG_RDI);
	rsi = gcpu_get_gp_reg(gcpu, REG_RSI);
	rdx = gcpu_get_gp_reg(gcpu, REG_RDX);
	rbx = gcpu_get_gp_reg(gcpu, REG_RBX);
	rcx = gcpu_get_gp_reg(gcpu, REG_RCX);
	gcpu_set_gp_reg(next_gcpu, REG_RDI, rdi);
	gcpu_set_gp_reg(next_gcpu, REG_RSI, rsi);
	gcpu_set_gp_reg(next_gcpu, REG_RDX, rdx);
	gcpu_set_gp_reg(next_gcpu, REG_RBX, rbx);
	gcpu_set_gp_reg(next_gcpu, REG_RCX, rcx);
}

static void relocate_optee_image(void)
{
	boolean_t ret = FALSE;
	/* op-tee region: first page is op-tee info, last page is stack. */
	optee_desc->optee_file.runtime_addr += PAGE_4K_SIZE;
	optee_desc->optee_file.runtime_total_size -= PAGE_4K_SIZE;

	ret = relocate_elf_image(&optee_desc->optee_file, &optee_desc->gcpu0_state.rip);

	if (!ret) {
		print_trace("Failed to load ELF file. Try multiboot now!\n");
		ret = relocate_multiboot_image(
                (uint64_t *)optee_desc->optee_file.loadtime_addr,
				optee_desc->optee_file.loadtime_size,
				&optee_desc->gcpu0_state.rip);
	}
	VMM_ASSERT_EX(ret, "Failed to relocate OP-TEE image!\n");

	/* restore op-tee runtime address and total size */
	optee_desc->optee_file.runtime_addr -= PAGE_4K_SIZE;
	optee_desc->optee_file.runtime_total_size += PAGE_4K_SIZE;
}

/* Set up op-tee device security info and op-tee startup info */
static void setup_optee_mem(void)
{
	optee_startup_info_t *optee_para;
	uint32_t dev_sec_info_size;
#ifdef MODULE_EPT_UPDATE
	uint64_t upper_start;
#endif

	/* Set op-tee memory mapping with RWX(0x7) attribute */
	gpm_set_mapping(guest_handle(GUEST_OPTEE),
			optee_desc->optee_file.runtime_addr,
			optee_desc->optee_file.runtime_addr,
			optee_desc->optee_file.runtime_total_size,
			0x7);

	gpm_remove_mapping(guest_handle(GUEST_REE),
				optee_desc->optee_file.runtime_addr,
				optee_desc->optee_file.runtime_total_size);
	print_trace(
			"Primary guest GPM: remove sguest image base %llx size 0x%x\r\n",
			optee_desc->optee_file.runtime_addr,
			optee_desc->optee_file.runtime_total_size);

	/* Setup op-tee boot info */
	dev_sec_info_size = *((uint32_t *)optee_desc->dev_sec_info);
	memcpy((void *)optee_desc->optee_file.runtime_addr, optee_desc->dev_sec_info, dev_sec_info_size);
	memset(optee_desc->dev_sec_info, 0, dev_sec_info_size);

	/* Setup op-tee startup info */
	optee_para = (optee_startup_info_t *)ALIGN_F(optee_desc->optee_file.runtime_addr + dev_sec_info_size, 8);
	VMM_ASSERT_EX(((uint64_t)optee_para + sizeof(optee_startup_info_t)) <
			(optee_desc->optee_file.runtime_addr + PAGE_4K_SIZE),
			"size of (dev_sec_info+optee_startup_info) exceeds the reserved 4K size!\n");
	optee_para->size_of_this_struct    = sizeof(optee_startup_info_t);
	optee_para->mem_size               = optee_desc->optee_file.runtime_total_size;
	optee_para->calibrate_tsc_per_ms   = tsc_per_ms;
	optee_para->optee_mem_base        = optee_desc->optee_file.runtime_addr;

	/* Set RDI and RSP */
	optee_desc->gcpu0_state.gp_reg[REG_RDI] = (uint64_t)optee_para;
	optee_desc->gcpu0_state.gp_reg[REG_RSP] = optee_desc->optee_file.runtime_addr +
        optee_desc->optee_file.runtime_total_size;

#ifdef MODULE_EPT_UPDATE
	/* remove the whole memory region from 0 ~ top_of_mem except op-tee self in OP-TEE(guest1) EPT */
	/* remove lower */
	gpm_remove_mapping(guest_handle(GUEST_OPTEE), 0, optee_desc->optee_file.runtime_addr);

	/* remove upper */
	upper_start = optee_desc->optee_file.runtime_addr + optee_desc->optee_file.runtime_total_size;
	gpm_remove_mapping(guest_handle(GUEST_OPTEE), upper_start, top_of_memory - upper_start);

	ept_update_install(GUEST_OPTEE);
#endif

#ifdef DEBUG //remove VMM's access to OP-TEE's memory to make sure VMM will not read/write to OP-TEE in runtime
	hmm_unmap_hpa(optee_desc->optee_file.runtime_addr, optee_desc->optee_file.runtime_total_size);
#endif
}

static void parse_optee_boot_param(guest_cpu_handle_t gcpu)
{
	uint64_t rdi;
	optee_boot_params_v0_t *optee_boot_params_v0 = NULL;
	optee_boot_params_v1_t *optee_boot_params_v1 = NULL;

	/* avoid warning -Wbad-function-cast */
	rdi = gcpu_get_gp_reg(gcpu, REG_RDI);
	VMM_ASSERT_EX(rdi, "Invalid op-tee boot params\n");

	/* Different structure pass from OSloader
	 * For v0 structure, runtime_addr is filled in evmm stage0 loader
	 * For v1 structure, runtime_addr is filled here from optee_boot_params_v1 */
	if (optee_desc->optee_file.runtime_addr) {
		optee_boot_params_v0 = (optee_boot_params_v0_t *)rdi;
		optee_desc->optee_file.loadtime_addr = optee_boot_params_v0->load_base;
		optee_desc->optee_file.loadtime_size = optee_boot_params_v0->load_size;
		relocate_optee_image();
	} else {
		optee_boot_params_v1 = (optee_boot_params_v1_t *)rdi;
		optee_desc->gcpu0_state.rip = optee_boot_params_v1->entry_point;
		optee_desc->optee_file.runtime_addr = optee_boot_params_v1->runtime_addr;
	}
}

static void launch_optee(guest_cpu_handle_t gcpu_ree, guest_cpu_handle_t gcpu_optee)
{
	/* Get op-tee info from osloader */
	parse_optee_boot_param(gcpu_ree);
	setup_optee_mem();
	mem_free(optee_desc->dev_sec_info);

	/* Memory mapping is updated for Guest[0] in setup_optee_mem(),
	 * need to do cache invalidation for Guest[0] */
	invalidate_gpm(guest_handle(GUEST_REE));

	gcpu_set_gp_reg(gcpu_optee, REG_RDI, optee_desc->gcpu0_state.gp_reg[REG_RDI]);
	gcpu_set_gp_reg(gcpu_optee, REG_RSP, optee_desc->gcpu0_state.gp_reg[REG_RSP]);
	vmcs_write(gcpu_optee->vmcs, VMCS_GUEST_RIP, optee_desc->gcpu0_state.rip);
}

static void smc_vmcall_exit(guest_cpu_handle_t gcpu)
{
	static uint32_t smc_stage;
	guest_cpu_handle_t next_gcpu;

	if(!guest_in_ring0(gcpu))
	{
		return;
	}

	/* Raise event to mitigate L1TF */
	if (GUEST_OPTEE == gcpu->guest->id) {
		event_raise(NULL, EVENT_SWITCH_TO_NONSECURE, NULL);
	} else if (GUEST_REE == gcpu->guest->id) {
		event_raise(NULL, EVENT_SWITCH_TO_SECURE, NULL);
	}

	vmcs_read(gcpu->vmcs, VMCS_GUEST_RIP);// update cache
	vmcs_read(gcpu->vmcs, VMCS_EXIT_INSTR_LEN);// update cache
	next_gcpu = schedule_next_gcpu();

	switch (smc_stage)
	{
		case SMC_STAGE_BOOT:
			if (0 == host_cpu_id())
			{
				if (GUEST_REE == gcpu->guest->id)
				{
					launch_optee(gcpu, next_gcpu);
				} else {
					smc_stage = SMC_STAGE_OPTEE_DONE;
					print_info("VMM: Launch Rich OS\n");
				}
			}
			break;

		case SMC_STAGE_OPTEE_DONE:
			D(VMM_ASSERT(gcpu->guest->id == GUEST_REE))
			print_init(FALSE);
			smc_stage = SMC_STAGE_REE_DONE;

			smc_copy_gp_regs(gcpu, next_gcpu);
			break;

		case SMC_STAGE_REE_DONE:
			smc_copy_gp_regs(gcpu, next_gcpu);
			break;

		default:
			print_trace("Invalid stage:(%d)\n", smc_stage);
			VMM_DEADLOOP();
			break;
	}
}

static void guest_register_vmcall_services()
{
	vmcall_register(GUEST_REE, OPTEE_VMCALL_SMC, smc_vmcall_exit);
	vmcall_register(GUEST_OPTEE, OPTEE_VMCALL_SMC, smc_vmcall_exit);
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

static void optee_set_gcpu_state(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	if (gcpu->guest->id == GUEST_OPTEE) {
		gcpu_set_init_state(gcpu, &optee_desc->gcpu0_state);

		if (gcpu->id != 0) {
			gcpu_set_reset_state(gcpu);
		}
	}
}

void init_optee_guest(evmm_desc_t *evmm_desc)
{
	uint32_t cpu_num = 1;
	uint32_t dev_sec_info_size;
#if !defined(PACK_OPTEE)
	void *dev_sec_info;
#endif

	D(VMM_ASSERT_EX(evmm_desc, "evmm_desc is NULL\n"));

	optee_desc = (optee_desc_t *)&evmm_desc->optee_desc;
	D(VMM_ASSERT(optee_desc));

	dev_sec_info_size = *((uint32_t *)optee_desc->dev_sec_info);
	VMM_ASSERT_EX(!(dev_sec_info_size & 0x3ULL), "size of optee boot info is not 32bit aligned!\n");

	/* reserve shared memory for OP-TEE */
	optee_desc->optee_file.runtime_total_size -= OPTEE_SHM_SIZE;

	print_trace("Init op-tee guest\n");

	/* TODO: refine it later */
#ifdef ENABLE_SGUEST_SMP
	sipi_ap_wkup_addr = evmm_desc->sipi_ap_wkup_addr;
	cpu_num = evmm_desc->num_of_cpu;
#endif
	/* Tee should not have X permission in REE memory. Set it to RW(0x3) */
	create_guest(cpu_num, 0x3);
	event_register(EVENT_GCPU_INIT, optee_set_gcpu_state);

#ifdef AP_START_IN_HLT
	event_register(EVENT_GCPU_MODULE_INIT, set_guest0_aps_to_hlt_state);
#endif

#ifdef PACK_OPTEE
	relocate_optee_image();
	setup_optee_mem();
#else
	/* Copy dev_sec_info from loader to VMM's memory */
	dev_sec_info = mem_alloc(dev_sec_info_size);
	memcpy(dev_sec_info, optee_desc->dev_sec_info, dev_sec_info_size);
	memset(optee_desc->dev_sec_info, 0, dev_sec_info_size);
	optee_desc->dev_sec_info = dev_sec_info;

	schedule_next_gcpu_as_init(0);
#endif

#ifdef DMA_FROM_CSE
	vtd_assign_dev(gid2did(GUEST_OPTEE), DMA_FROM_CSE);
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

