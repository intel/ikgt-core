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
#include "trusty_info.h"
#include "gcpu_inject_event.h"
#include "device_sec_info.h"

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

#ifdef DERIVE_KEY
#include "modules/crypto.h"
#endif

#ifdef MODULE_VIRTUAL_APIC
#include "modules/virtual_apic.h"
#endif

typedef enum {
	TRUSTY_VMCALL_SMC             = 0x74727500,
	TRUSTY_VMCALL_DUMP_INIT       = 0x74727507,
} vmcall_id_t;

enum {
	SMC_STAGE_BOOT = 0,
	SMC_STAGE_LK_DONE,
	SMC_STAGE_ANDROID_DONE
};

enum {
	GUEST_ANDROID = 0, /* OSloader or Android/Linux */
	GUEST_TRUSTY       /* Trusty */
};

static trusty_desc_t *trusty_desc;
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
	uint64_t rdi, rsi, rdx, rbx;

	rdi = gcpu_get_gp_reg(gcpu, REG_RDI);
	rsi = gcpu_get_gp_reg(gcpu, REG_RSI);
	rdx = gcpu_get_gp_reg(gcpu, REG_RDX);
	rbx = gcpu_get_gp_reg(gcpu, REG_RBX);
	gcpu_set_gp_reg(next_gcpu, REG_RDI, rdi);
	gcpu_set_gp_reg(next_gcpu, REG_RSI, rsi);
	gcpu_set_gp_reg(next_gcpu, REG_RDX, rdx);
	gcpu_set_gp_reg(next_gcpu, REG_RBX, rbx);
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

#if defined (PACK_LK) || defined (QEMU_LK)
static void relocate_trusty_image(uint64_t offset)
{
	boolean_t ret = FALSE;
	/* lk region: first page is trusty info, last page is stack. */
	trusty_desc->lk_file.runtime_addr += offset;
	trusty_desc->lk_file.runtime_total_size -= PAGE_4K_SIZE;

	ret = relocate_elf_image(&trusty_desc->lk_file, &trusty_desc->gcpu0_state.rip);

	if (!ret) {
		print_trace("Failed to load ELF file. Try multiboot now!\n");
		ret = relocate_multiboot_image((uint64_t *)trusty_desc->lk_file.loadtime_addr,
				trusty_desc->lk_file.loadtime_size,
				&trusty_desc->gcpu0_state.rip);
	}
	VMM_ASSERT_EX(ret, "Failed to relocate Trusty image!\n");

	/* restore lk runtime address and total size */
	trusty_desc->lk_file.runtime_addr -= offset;
	trusty_desc->lk_file.runtime_total_size += PAGE_4K_SIZE;
}
#endif

#ifdef DERIVE_KEY
static int get_max_svn_index(device_sec_info_v0_t *sec_info)
{
	uint32_t i, max_svn_idx = 0;

	if ((sec_info->num_seeds == 0) || (sec_info->num_seeds > BOOTLOADER_SEED_MAX_ENTRIES))
		return -1;

	for (i = 1; i < sec_info->num_seeds; i ++) {
		if (sec_info->dseed_list[i].cse_svn > sec_info->dseed_list[i - 1].cse_svn) {
			max_svn_idx = i;
		}
	}

	return max_svn_idx;
}

static void key_derive(device_sec_info_v0_t *sec_info)
{
	const char salt[] = "Attestation Keybox Encryption Key";
	const uint8_t *ikm;
	uint8_t *prk;
	uint32_t ikm_len;
	int max_svn_idx;

	max_svn_idx = get_max_svn_index(sec_info);
	if (max_svn_idx < 0) {
		print_info("VMM: failed to get max svn index\n");
		memset(sec_info, 0, sizeof(device_sec_info_v0_t));
		return;
	}

	ikm = sec_info->dseed_list[max_svn_idx].seed;
	ikm_len = 32;

	prk = sec_info->attkb_enc_key;

	if (hmac_sha256((const uint8_t *)salt, sizeof(salt), ikm, ikm_len, prk) != 1) {
		memset(sec_info, 0, sizeof(device_sec_info_v0_t));
		print_panic("VMM: failed to derive key!\n");
	}
}
#endif

/* Set up trusty device security info and trusty startup info */
static void setup_trusty_mem(void)
{
#ifdef MODULE_EPT_UPDATE
	uint64_t upper_start;
#endif
#ifndef QEMU_LK
	uint32_t dev_sec_info_size;
	trusty_startup_info_t *trusty_para;
#endif
#ifdef DERIVE_KEY
	device_sec_info_v0_t *sec_info;
#endif

	/* Set trusty memory mapping with RWX(0x7) attribute */
	gpm_set_mapping(guest_handle(GUEST_TRUSTY),
			trusty_desc->lk_file.runtime_addr,
			trusty_desc->lk_file.runtime_addr,
			trusty_desc->lk_file.runtime_total_size,
			0x7);

	gpm_remove_mapping(guest_handle(GUEST_ANDROID),
				trusty_desc->lk_file.runtime_addr,
				trusty_desc->lk_file.runtime_total_size);
	print_trace(
			"Primary guest GPM: remove sguest image base %llx size 0x%x\r\n",
			trusty_desc->lk_file.runtime_addr,
			trusty_desc->lk_file.runtime_total_size);

#ifndef QEMU_LK
	/* Setup trusty boot info */
	dev_sec_info_size = *((uint32_t *)trusty_desc->dev_sec_info);
	memcpy((void *)trusty_desc->lk_file.runtime_addr, trusty_desc->dev_sec_info, dev_sec_info_size);
	memset(trusty_desc->dev_sec_info, 0, dev_sec_info_size);
#endif

#ifdef DERIVE_KEY
	sec_info = (device_sec_info_v0_t *)trusty_desc->lk_file.runtime_addr;
	key_derive(sec_info);
#endif

#ifndef QEMU_LK
	/* Setup trusty startup info */
	trusty_para = (trusty_startup_info_t *)ALIGN_F(trusty_desc->lk_file.runtime_addr + dev_sec_info_size, 8);
	VMM_ASSERT_EX(((uint64_t)trusty_para + sizeof(trusty_startup_info_t)) <
			(trusty_desc->lk_file.runtime_addr + PAGE_4K_SIZE),
			"size of (dev_sec_info+trusty_startup_info) exceeds the reserved 4K size!\n");
	trusty_para->size_of_this_struct    = sizeof(trusty_startup_info_t);
	trusty_para->mem_size               = trusty_desc->lk_file.runtime_total_size;
	trusty_para->calibrate_tsc_per_ms   = tsc_per_ms;
	trusty_para->trusty_mem_base        = trusty_desc->lk_file.runtime_addr;

	/* Set RDI and RSP */
	trusty_desc->gcpu0_state.gp_reg[REG_RDI] = (uint64_t)trusty_para;
	trusty_desc->gcpu0_state.gp_reg[REG_RSP] = trusty_desc->lk_file.runtime_addr + trusty_desc->lk_file.runtime_total_size;
#endif

	/* TODO: refine it later */
#ifdef ENABLE_SGUEST_SMP
	trusty_para->sipi_ap_wkup_addr = sipi_ap_wkup_addr;
#endif

#ifdef MODULE_EPT_UPDATE
	/* remove the whole memory region from 0 ~ top_of_mem except lk self in Trusty(guest1) EPT */
	/* remove lower */
	gpm_remove_mapping(guest_handle(GUEST_TRUSTY), 0, trusty_desc->lk_file.runtime_addr);

	/* remove upper */
	upper_start = trusty_desc->lk_file.runtime_addr + trusty_desc->lk_file.runtime_total_size;
	gpm_remove_mapping(guest_handle(GUEST_TRUSTY), upper_start, top_of_memory - upper_start);

	ept_update_install(GUEST_TRUSTY);
#endif

#ifdef DEBUG //remove VMM's access to LK's memory to make sure VMM will not read/write to LK in runtime
	hmm_unmap_hpa(trusty_desc->lk_file.runtime_addr, trusty_desc->lk_file.runtime_total_size);
#endif
}

static void parse_trusty_boot_param(guest_cpu_handle_t gcpu)
{
	uint64_t rdi;
	trusty_boot_params_t *trusty_boot_params = NULL;

	/* avoid warning -Wbad-function-cast */
	rdi = gcpu_get_gp_reg(gcpu, REG_RDI);
	VMM_ASSERT_EX(rdi, "Invalid trusty boot params\n");

	/* trusty_boot_params is passed from OSloader */
	trusty_boot_params = (trusty_boot_params_t *)rdi;
	trusty_desc->gcpu0_state.rip = trusty_boot_params->entry_point;
	trusty_desc->lk_file.runtime_addr = trusty_boot_params->runtime_addr;
}

static void launch_trusty(guest_cpu_handle_t gcpu_android, guest_cpu_handle_t gcpu_trusty)
{
	/* Get trusty info from osloader then set trusty startup&sec info */
	parse_trusty_boot_param(gcpu_android);
	setup_trusty_mem();
	mem_free(trusty_desc->dev_sec_info);

	/* Memory mapping is updated for Guest[0] in setup_trusty_info(),
	 * need to do cache invalidation for Guest[0] */
	invalidate_gpm(guest_handle(GUEST_ANDROID));

	gcpu_set_gp_reg(gcpu_trusty, REG_RDI, trusty_desc->gcpu0_state.gp_reg[REG_RDI]);
	gcpu_set_gp_reg(gcpu_trusty, REG_RSP, trusty_desc->gcpu0_state.gp_reg[REG_RSP]);
	vmcs_write(gcpu_trusty->vmcs, VMCS_GUEST_RIP, trusty_desc->gcpu0_state.rip);
}

static void smc_vmcall_exit(guest_cpu_handle_t gcpu)
{
#ifdef MODULE_VIRTUAL_APIC
	irr_t virr;
	uint8_t vector;
#endif

	static uint32_t smc_stage;
	guest_cpu_handle_t next_gcpu;

	if(!guest_in_ring0(gcpu))
	{
		return;
	}

	/* Raise event to mitigate L1TF */
	if (GUEST_TRUSTY == gcpu->guest->id) {
		event_raise(NULL, EVENT_SWITCH_TO_NONSECURE, NULL);
	} else if (GUEST_ANDROID == gcpu->guest->id) {
		event_raise(NULL, EVENT_SWITCH_TO_SECURE, NULL);
	}

	vmcs_read(gcpu->vmcs, VMCS_GUEST_RIP);// update cache
	vmcs_read(gcpu->vmcs, VMCS_EXIT_INSTR_LEN);// update cache

#ifdef MODULE_VIRTUAL_APIC
	/* get virr for current gcpu before gcpu switch */
	vapic_get_virr(gcpu, &virr);
	vapic_clear_virr(gcpu);
#endif

	next_gcpu = schedule_next_gcpu();

#ifdef MODULE_VIRTUAL_APIC
	vapic_merge_virr(next_gcpu, &virr);

	/* migrate vector bufferred in pending_intr list */
	for(vector=gcpu_get_pending_intr(gcpu); vector>=0x20; vector=gcpu_get_pending_intr(gcpu)) {
		gcpu_set_pending_intr(next_gcpu, vector);
		gcpu_clear_pending_intr(gcpu, vector);
	}
#endif

	switch (smc_stage)
	{
		case SMC_STAGE_BOOT:
			if (0 == host_cpu_id())
			{
				if (GUEST_ANDROID == gcpu->guest->id)
				{
					launch_trusty(gcpu, next_gcpu);
				} else {
					smc_stage = SMC_STAGE_LK_DONE;
					print_info("VMM: Launch Normal World\n");
				}
			}
			break;

		case SMC_STAGE_LK_DONE:
			D(VMM_ASSERT(gcpu->guest->id == GUEST_ANDROID))
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

static void trusty_vmcall_dump_init(guest_cpu_handle_t gcpu)
{
#ifdef MODULE_DEADLOOP
	uint64_t dump_gva;

	D(VMM_ASSERT(gcpu->guest->id == GUEST_ANDROID));

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
	vmcall_register(GUEST_ANDROID, TRUSTY_VMCALL_SMC, smc_vmcall_exit);
	vmcall_register(GUEST_ANDROID, TRUSTY_VMCALL_DUMP_INIT, trusty_vmcall_dump_init);
	vmcall_register(GUEST_TRUSTY, TRUSTY_VMCALL_SMC, smc_vmcall_exit);
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
	if (gcpu->guest->id == GUEST_TRUSTY) {
		gcpu_set_init_state(gcpu, &trusty_desc->gcpu0_state);

		if (gcpu->id != 0) {
			gcpu_set_reset_state(gcpu);
		}
	}
}

static void *sensitive_info;
static void trusty_erase_sensetive_info(void)
{
#ifndef DEBUG //in DEBUG build, access from VMM to LK will be removed. so, only erase sensetive info in RELEASE build
	memset(sensitive_info, 0, PAGE_4K_SIZE);
#endif
}

void trusty_register_deadloop_handler(evmm_desc_t *evmm_desc)
{
	D(VMM_ASSERT_EX(evmm_desc, "evmm_desc is NULL\n"));
	sensitive_info = (void *)evmm_desc->trusty_desc.lk_file.runtime_addr;

	register_final_deadloop_handler(trusty_erase_sensetive_info);
}

void init_trusty_guest(evmm_desc_t *evmm_desc)
{
	uint32_t cpu_num = 1;
	uint32_t dev_sec_info_size;
#if !defined(PACK_LK) && !defined (QEMU_LK)
	void *dev_sec_info;
#endif

	D(VMM_ASSERT_EX(evmm_desc, "evmm_desc is NULL\n"));

	trusty_desc = (trusty_desc_t *)&evmm_desc->trusty_desc;
	D(VMM_ASSERT(trusty_desc));

	dev_sec_info_size = *((uint32_t *)trusty_desc->dev_sec_info);
	VMM_ASSERT_EX(!(dev_sec_info_size & 0x3ULL), "size of trusty boot info is not 32bit aligned!\n");

	print_trace("Init trusty guest\n");

	/* TODO: refine it later */
#ifdef ENABLE_SGUEST_SMP
	sipi_ap_wkup_addr = evmm_desc->sipi_ap_wkup_addr;
	cpu_num = evmm_desc->num_of_cpu;
#endif
	/* Tee should not have X permission in REE memory. Set it to RW(0x3) */
	create_guest(cpu_num, 0x3);
	event_register(EVENT_GCPU_INIT, trusty_set_gcpu_state);

#ifdef AP_START_IN_HLT
	event_register(EVENT_GCPU_MODULE_INIT, set_guest0_aps_to_hlt_state);
#endif

#ifdef PACK_LK
	relocate_trusty_image(PAGE_4K_SIZE);
	setup_trusty_mem();
#elif QEMU_LK
	relocate_trusty_image(0);
	setup_trusty_mem();
#else
	/* Copy dev_sec_info from loader to VMM's memory */
	dev_sec_info = mem_alloc(dev_sec_info_size);
	memcpy(dev_sec_info, trusty_desc->dev_sec_info, dev_sec_info_size);
	memset(trusty_desc->dev_sec_info, 0, dev_sec_info_size);
	trusty_desc->dev_sec_info = dev_sec_info;

	schedule_next_gcpu_as_init(0);
#endif

#ifdef DMA_FROM_CSE
	vtd_assign_dev(gid2did(GUEST_TRUSTY), DMA_FROM_CSE);
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

