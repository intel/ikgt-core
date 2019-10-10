/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "guest.h"
#include "gcpu.h"
#include "heap.h"
#include "event.h"
#include "gcpu_inject_event.h"

#include "lib/util.h"
#include "modules/ucode_update.h"
#include "modules/msr_monitor.h"

typedef struct ucode_header {
	uint32_t header_ver;
	uint32_t update_ver;
	uint32_t date;
	uint32_t cpu_sig;
	uint32_t chk_sum;
	uint32_t ldr_ver;
	uint32_t cpu_flags;
	uint32_t data_size;
	uint32_t total_size;
	uint32_t rsvd[3];
} ucode_header_t;

static void msr_ucode_update_handler(guest_cpu_handle_t gcpu, uint32_t msr_id UNUSED)
{
	uint64_t val, gva;
	ucode_header_t uhdr;
	pf_info_t pi;
	uint8_t *ucode = NULL;
	boolean_t ret;

	D(VMM_ASSERT(msr_id == MSR_BIOS_UPDT_TRIG));

	val = get_val_for_wrmsr(gcpu);

	/* adjust gva to ucode header */
	gva = val - sizeof(ucode_header_t);

	ret = gcpu_copy_from_gva(gcpu, gva, (uint64_t)&uhdr, sizeof(uhdr), &pi);
	if (!ret) {
		VMM_ASSERT_EX(pi.is_pf, "%s: failed to copy ucode header from gva(0x%llx)\n",
					__func__, gva);
		gcpu_set_cr2(gcpu, pi.cr2);
		gcpu_inject_pf(gcpu, pi.ec);
		goto out;
	}

	ucode = mem_alloc(uhdr.total_size);
	VMM_ASSERT_EX(ucode, "%s: Failed to alloc memory for ucode update field\n", __func__);

	ret = gcpu_copy_from_gva(gcpu, gva, (uint64_t)ucode, uhdr.total_size, &pi);
	if (!ret) {
		VMM_ASSERT_EX(pi.is_pf, "%s: failed to copy ucode from gva(0x%llx)\n",
					__func__, gva);
		gcpu_set_cr2(gcpu, pi.cr2);
		gcpu_inject_pf(gcpu, pi.ec);
		goto out;
	}

	asm_wrmsr(MSR_BIOS_UPDT_TRIG, (uint64_t)ucode + sizeof(uhdr));

out:
	if (ucode)
		mem_free(ucode);
	gcpu_skip_instruction(gcpu);
}

static void ucode_monitor_guest_setup(UNUSED guest_cpu_handle_t gcpu, void *pv)
{
	guest_handle_t guest = (guest_handle_t)pv;

	if (guest->id == 0) {
		monitor_msr_write(guest->id, MSR_BIOS_UPDT_TRIG, msr_ucode_update_handler);
	}
}

/*
 * According to SDM: Vol3 Chapter 25.3 Changes to Instruction Behavior in VMX Non-root Operation
 *     WRMSR: if ECX contains 79H(indicatiing IA32_BIOS_UPDT_TRIG MSR), no microcode update is loaded,
 *            and control passes to next instruction. This implies that microcode updates cannot be
 *            loaded in VMX non-root operation.
 *
 * In order to support microcode updates from guests, eVMM will trap MSR_BIOS_UPDT_TRIG and update
 * the microcode accordingly.
 *
 * More detail references(IA-32 SDM):
 *     Vol3 Chapter 9  Processor Management and Initialization - 9.11 Microcode update facilities
 *     Vol3 Chapter 25 VMX Non-root Operation - 25.3 Changes to Instruction Behavior in VMX Non-root Operation
 *     Vol3 Chapter 32 Virtualization of System Resources - 32.4 Microcode update facility
 */
void ucode_update_init(void)
{
	event_register(EVENT_GUEST_MODULE_INIT, ucode_monitor_guest_setup);
}
