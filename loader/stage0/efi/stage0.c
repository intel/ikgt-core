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
#include "stage0_lib.h"
#include "efi_boot_param.h"

#include "lib/util.h"
#include "lib/serial.h"
#include "lib/image_loader.h"

#ifdef LIB_EFI_SERVICES
#include "lib/efi/efi_services.h"
#endif

static void cleanup_sensetive_data(tos_startup_info_t *p_startup_info)
{
	memset(p_startup_info->seed, 0, BUP_MKHI_BOOTLOADER_SEED_LEN);
	barrier();
}

extern uint64_t return_address;
static uint32_t call_stage1(uint64_t entry, evmm_desc_t *evmm_desc)
{
	uint32_t ret = -1;
	uint64_t *gp_reg;

	save_current_cpu_state(&evmm_desc->guest0_gcpu0_state);

	evmm_desc->guest0_gcpu0_state.rip = (uint64_t)&return_address;
	evmm_desc->guest0_gcpu0_state.rflags = asm_get_rflags();

	gp_reg = &evmm_desc->guest0_gcpu0_state.gp_reg[0];

	/* return value: rax */
	gp_reg[REG_RAX] = 0;

	/* callee saved registers: rbx, rbp, r12~r15 */
	__asm__ __volatile__ ("movq %%rbx, %0" : "=r"(gp_reg[REG_RBX]));
	__asm__ __volatile__ ("movq %%rbp, %0" : "=r"(gp_reg[REG_RBP]));
	__asm__ __volatile__ ("movq %%r12, %0" : "=r"(gp_reg[REG_R12]));
	__asm__ __volatile__ ("movq %%r13, %0" : "=r"(gp_reg[REG_R13]));
	__asm__ __volatile__ ("movq %%r14, %0" : "=r"(gp_reg[REG_R14]));
	__asm__ __volatile__ ("movq %%r15, %0" : "=r"(gp_reg[REG_R15]));

	/* rsp for resume back */
	__asm__ __volatile__ ("movq %%rsp, %0" : "=r"(gp_reg[REG_RSP]));

	/* call into stage1 */
	__asm__ __volatile__ (

		/* call into stage1 */
		"movq %2, %%rdi \n\t"
		"callq  *%1 \n\t"

		/* return address: resume from VMM or return from stage1 */
		".globl return_address \n\t"
		"return_address: \n\t"

		: "=a"(ret)
		: "r"(entry), "r"((uint64_t)evmm_desc)
	);

	return ret;
}

/* Function: stage0_main
 * Description: Called by start() in stage0_entry.S. Jumps to stage1.
 * Calling convention:
 *   rdi, rsi, rdx, rcx, r8, r9, stack1, stack2
 */
uint32_t stage0_main(
	const init_register_t *init_reg,
	uint64_t stage0_base)
{
	evmm_desc_t *evmm_desc = NULL;
	uint64_t (*stage1_main) (evmm_desc_t *xd) = NULL;
	uint32_t ret = -1;
	tos_startup_info_t *tos_startup_info = (void *)init_reg->rdi;

	print_init(FALSE);

	if (!init_efi_services(tos_startup_info->system_table_addr)) {
		print_panic("%s: Failed init efi services\n", __func__);
		goto exit;
	}

	evmm_desc = boot_params_parse(tos_startup_info, stage0_base);

	if (!evmm_desc) {
		print_panic("evmm_desc is NULL\n");
		goto exit;
	}

	if (!check_vmx()) {
		print_panic("VT is not supported\n");
		goto exit;
	}

	if(!file_parse(evmm_desc, stage0_base, 0, EVMM_PKG_BIN_SIZE)) {
		print_panic("file parse failed\n");
		goto exit;
	}

	if (!relocate_elf_image(&(evmm_desc->stage1_file), (uint64_t *)&stage1_main)) {
		print_panic("relocate stage1 image failed\n");
		goto exit;
	}

	if (!efi_enable_disable_aps(FALSE)) {
		print_panic("Failed to disable APs by EFI services!\n");
		goto exit;
	}

	ret = call_stage1((uint64_t)stage1_main, evmm_desc);
	if (ret != 0) {
		print_panic("Failed in stage1\n");
		goto exit;
	}

	if (!efi_enable_disable_aps(TRUE)) {
		print_panic("Failed to enable APs by EFI services!\n");
		ret = -1;
	}
exit:
	/* wipe seed data */
	cleanup_sensetive_data(tos_startup_info);

#ifdef MODULE_TRUSTY_TEE
	if (evmm_desc) {
		memset(evmm_desc->trusty_tee_desc.seed, 0, BUP_MKHI_BOOTLOADER_SEED_LEN);
		barrier();
	}
#endif

	return ret;
}
/* End of file */
