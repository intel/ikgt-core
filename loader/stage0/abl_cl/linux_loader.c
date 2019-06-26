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
#include "linux_loader.h"
#include "ldr_dbg.h"

#include "lib/util.h"

static void setup_linux_gcpu_state(uint64_t boot_params, uint64_t kernel_entry, gcpu_state_t *gcpu_state)
{
	gcpu_state->gp_reg[REG_RAX] = 0;
	gcpu_state->gp_reg[REG_RBX] = 0;
	gcpu_state->gp_reg[REG_RCX] = 0;
	gcpu_state->gp_reg[REG_RDX] = 0;
	gcpu_state->gp_reg[REG_RSI] = boot_params;
	gcpu_state->gp_reg[REG_RDI] = 0;
	gcpu_state->gp_reg[REG_RBP] = 0;
	gcpu_state->gp_reg[REG_RSP] = 0;
	gcpu_state->rip = kernel_entry;
	gcpu_state->rflags = asm_get_rflags();

	save_current_cpu_state(gcpu_state);
}

static boolean_t is_bzimage(void *addr)
{
	setup_header_t *hdr = &(((boot_params_t *)addr)->setup_hdr);

	if (!addr)
		return FALSE;

	if ((hdr->header != HDRS_MAGIC) || (hdr->boot_flag != BZIMAGE_BOOT_FLAGS_MAGIC)) {
		print_panic("%s: addr=%llx, header=%x, boot_flag=%x\n", __func__, (uint64_t)addr, hdr->header, hdr->boot_flag);
		return FALSE;
	}

	return TRUE;
}

boolean_t load_linux_kernel(uint64_t kernel_base,
			    uint64_t kernel_size,
			    uint64_t initrd_base,
			    uint64_t initrd_size,
			    uint64_t kernel_rt_base,
			    uint64_t kernel_rt_size,
			    uint32_t cmdline_addr,
			    uint64_t boot_params_addr,
			    gcpu_state_t *guest0_gcpu0)
{
	setup_header_t *hdr = NULL;
	unsigned long non_real_mode_off = 0;

	/* params check */
	if ((kernel_base == 0) || (kernel_size < sizeof(setup_header_t)) ||
	    (kernel_rt_base == 0) || (kernel_rt_size == 0) ||
	    (boot_params_addr == 0) || (guest0_gcpu0 == NULL)) {
		print_panic("Invalid input params!\n");
		return FALSE;
	}

	if (!is_bzimage((void *)kernel_base)) {
		print_panic("%s: Not a bzImage@%x!\n", __func__, kernel_base);
		return FALSE;
	}

	hdr = &(((boot_params_t *)kernel_base)->setup_hdr);

	/* setup header check and parse */
	if (kernel_rt_size < hdr->syssize * 16) {
		print_panic("No enough memory for kernel\n");
		return FALSE;
	}

	if (hdr->setup_sects == 0) {
		hdr->setup_sects = DEFAULT_SECTOR_NUM;
	}

	if (hdr->setup_sects > MAX_SECTOR_NUM) {
		print_panic("exceed the max sector number, invalid kernel!\n");
		return FALSE;
	}

	/* check boot protocol version 2.05 (support relocatable kernel) */
	if ((hdr->version) < 0x0205) {
		print_panic("boot protocol version < 2.05, not supported right now. ver=0x%",
			hdr->version);
		return FALSE;
	}

	if (!hdr->relocatable_kernel) {
		/* If need to support older kernel, need to move kernel to pref_address. */
		print_panic("Linux protected mode not loaded (old kernel not relocatable)!\n");
		return FALSE;
	}

	/* put cmd_line_ptr after boot_parameters */
	hdr->cmd_line_ptr = cmdline_addr;

	hdr->type_of_loader = 0xFF;

	/* clear CAN_USE_HEAP */
	hdr->loadflags &= ~FLAG_CAN_USE_HEAP;

	if ((initrd_base !=  0) && (initrd_size != 0)) {
		hdr->ramdisk_image = initrd_base;
		hdr->ramdisk_size = initrd_size;
	} else {
		hdr->ramdisk_image = 0;
		hdr->ramdisk_size = 0;
	}

	hdr->code32_start = (uint32_t)kernel_rt_base;

	/* Copy boot_params */
	memset((void *)boot_params_addr, 0, sizeof(boot_params_t));
	memcpy((void *)(boot_params_addr + 0x01f1), hdr, sizeof(setup_header_t));

	/* Copy kernel(non-real-mode) */
	non_real_mode_off = (hdr->setup_sects + 1) * SECTOR_SIZE;
	memcpy((void *)kernel_rt_base, (void *)(kernel_base + non_real_mode_off), hdr->syssize * 16);

	setup_linux_gcpu_state(boot_params_addr, (uint64_t)(kernel_rt_base + 0x200), guest0_gcpu0);

	return TRUE;
}
