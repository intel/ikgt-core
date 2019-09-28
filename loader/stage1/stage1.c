/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_asm.h"
#include "vmm_base.h"
#include "vmm_arch.h"
#include "evmm_desc.h"
#include "ldr_dbg.h"

#include "lib/image_loader.h"
#ifdef LIB_MP_INIT
#include "lib/mp_init.h"
#endif
#include "lib/util.h"

#ifdef LIB_EFI_SERVICES
#include "lib/efi/efi_services.h"
#endif

/* If CPU_NUM is defined, it must be equal to MAX_CPU_NUM */
#ifdef CPU_NUM
#if CPU_NUM != MAX_CPU_NUM
#error "Bad CPU_NUM definition!"
#endif
#endif

typedef void (* vmm_entry_t)(uint32_t cpuid, void *evmm_desc);
static evmm_desc_t *evmm_desc;
static vmm_entry_t vmm_main;

#if (CPU_NUM != 1)
#ifdef MODULE_APS_STATE
static void g0_ap_setup(gcpu_state_t *ap_state, uint64_t rsp, uint64_t rip)
{
	ap_state->gp_reg[REG_RAX] = 0ULL;
	ap_state->gp_reg[REG_RBX] = 0ULL;
	ap_state->gp_reg[REG_RCX] = 0ULL;
	ap_state->gp_reg[REG_RDX] = 0ULL;
	ap_state->gp_reg[REG_RSI] = 0ULL;
	ap_state->gp_reg[REG_RDI] = 0ULL;
	ap_state->gp_reg[REG_RBP] = 0ULL;
	ap_state->gp_reg[REG_RSP] = rsp;
	ap_state->rip = rip;
	ap_state->rflags = asm_get_rflags();

	save_current_cpu_state(ap_state);
}

/*
 * Initial AP setup in protected mode - should never return
 * Parameter:
 *     cpu_id  - cpu_id of current cpu
 *     ret_rsp - the APs' initial state is set to state of this
 *               function's caller.
 */
static void ap_continue(uint32_t cpu_id, uint64_t ret_rsp)
{
	void *ret_addr;

	if (cpu_id == MAX_CPU_NUM) {
		printf("Actual cpu number is larger than MAX_CPU_NUM(%d).\n", MAX_CPU_NUM);
		__STOP_HERE__;
	}

	if (ret_rsp) {
		ret_addr = RETURN_ADDRESS();
		g0_ap_setup(&evmm_desc->guest0_aps_state[cpu_id - 1], ret_rsp, (uint64_t)ret_addr);
	}

	/* launch vmm on ap */
	vmm_main(cpu_id, (void *)evmm_desc);
	printf("ap(%d) launch vmm fail.\n", cpu_id);
	__STOP_HERE__;
}
#else
/*
 * Initial AP setup in protected mode - should never return
 * Parameter:
 *     cpu_id  - cpu_id of current cpu
 */
static void ap_continue(uint32_t cpu_id)
{
	if (cpu_id == MAX_CPU_NUM) {
		printf("Actual cpu number is larger than MAX_CPU_NUM(%d).\n", MAX_CPU_NUM);
		__STOP_HERE__;
	}

	/* launch vmm on ap */
	vmm_main(cpu_id, (void *)evmm_desc);
	printf("ap(%d) launch vmm fail.\n", cpu_id);
	__STOP_HERE__;
}
#endif
#endif

void stage1_main(evmm_desc_t *xd)
{
#if (CPU_NUM != 1)
	uint32_t num_of_cpu;
#endif

#ifdef LIB_EFI_SERVICES
	if (!init_efi_services(xd->system_table_base)) {
		print_panic("%s: Failed init efi services\n", __func__);
		return;
	}

	if (xd->top_of_mem == 0) {
		xd->top_of_mem = efi_get_tom();
	}
#endif

	print_init(FALSE);

	if (xd->top_of_mem == 0)
		return;

	if (xd->tsc_per_ms == 0)
		xd->tsc_per_ms = determine_nominal_tsc_freq()/1000ULL;

	tsc_per_ms = xd->tsc_per_ms;
	if (tsc_per_ms == 0) {
		print_panic("%s: Invalid TSC frequency!\n", __func__);
		return;
	}

#ifdef CPU_NUM
	if ((xd->num_of_cpu != 0) &&
		(xd->num_of_cpu != CPU_NUM)) {
		print_warn("xd->num_of_cpu is not 0 and not equal to CPU_NUM, it will be ignored!\n");
	}
	xd->num_of_cpu = CPU_NUM;
#else
#ifndef START_AP_BY_EFI_MP_SERVICE
	if (xd->num_of_cpu == 0)
		print_warn("xd->num_of_cpu is 0, will 100ms delay to enumarate APs!\n");
#endif
#endif

#ifdef MODULE_TEMPLATE_TEE
	if (evmm_desc->x64_cr3 == 0)
		evmm_desc->x64_cr3 = asm_get_cr3();
#endif

	/* Load evmm image */
	if (!relocate_elf_image(&(xd->evmm_file), (uint64_t *)&vmm_main)) {
		print_panic("relocate evmm file fail\n");
		return;
	}
	evmm_desc = xd;

/* From https://gcc.gnu.org/onlinedocs/cpp/Defined.html,
 * we know if CPU_NUM is not defined, it will be interpreted
 * as having the value zero.so #if (CPU_NUM == 1) means that
 * #if defined (CPU_NUM) && (CPU_NUM == 1). So #if (CPU_NUM != 1)
 * means CPU_NUM is not defined or defined but not equal to 1.*/
#if (CPU_NUM != 1)
#ifdef START_AP_BY_EFI_MP_SERVICE
	num_of_cpu = (uint32_t)efi_launch_aps((uint64_t)ap_continue);
	if ((num_of_cpu == 0) || (num_of_cpu > MAX_CPU_NUM)) {
		print_panic("%s: (EFI)Failed to launch all APs\n", __func__);
		return;
	}
	xd->num_of_cpu = num_of_cpu;
#else
	num_of_cpu = launch_aps(xd->sipi_ap_wkup_addr, xd->num_of_cpu, (uint64_t)ap_continue);
	if (num_of_cpu > MAX_CPU_NUM) {
		print_panic("%s: (Legacy)Failed to launch all APs\n", __func__);
		return;
	}
	if (xd->num_of_cpu == 0)
		xd->num_of_cpu = num_of_cpu;
#endif
#endif

	print_info("Loader: Launch VMM\n");

	/* launch vmm on BSP */
	vmm_main(0, evmm_desc);
	/*should never return here!*/
	return;
}
/* End of file */
