/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_asm.h"
#include "vmm_arch.h"
#include "vmm_base.h"
#include "efi_types.h"
#include "efi_api.h"
#include "efi_error.h"
#include "protocol/mp_service.h"
#include "lib/util.h"

extern efi_boot_services_t *g_bs;
extern void EFI_API ap_start(void *arg);

typedef void (*hand_over_c_func_t)(uint32_t cpu_id, void *arg, uint64_t old_rsp);
hand_over_c_func_t hand_over_entry;

/* Starts from 1 since BSP will not be counted in efi_ap.S */
volatile uint64_t cpu_num = 1;

static void EFI_API empty_func(efi_event_t event, void *ctx)
{
	(void)event;
	(void)ctx;
}

uint64_t efi_launch_aps(uint64_t c_entry)
{
	efi_guid_t mp_service_guid = EFI_MP_SERVICES_PROTOCOL_GUID;
	efi_status_t ret;
	efi_event_t empty_event;
	efi_mp_services_protocol_t *mp = NULL;
	uintn_t num_processors, num_enabled_processors;
	uint64_t tsc_start;

	if (!c_entry) {
		return 0;
	}
	hand_over_entry = (hand_over_c_func_t)c_entry;

	/* Locate MP_SERVICE protocol */
	ret = g_bs->locate_protocol(&mp_service_guid, NULL, (void **)&mp);
	if ((ret != EFI_SUCCESS) || !mp) {
		return 0;
	}

	/* Get number of logical processors in the platform */
	ret = mp->get_number_of_processors(mp, &num_processors, &num_enabled_processors);
	if (ret != EFI_SUCCESS) {
		return 0;
	}

	if (num_enabled_processors == 1ULL) {
		goto out;
	}

	/* Create an empty event to make CPUs run in non-blocking mode when startup all APs */
	ret = g_bs->create_event(EFI_EVENT_NOTIFY_WAIT, EFI_TPL_CALLBACK, empty_func, NULL, &empty_event);
	if (ret != EFI_SUCCESS) {
		return 0;
	}

	/* Signal APs to startup */
	ret = mp->startup_all_aps(mp, ap_start, FALSE, empty_event, 0, NULL, NULL);
	if (ret != EFI_SUCCESS) {
		return 0;
	}

	/* Check actual started cpu number. Timeout after 100ms */
	tsc_start = asm_rdtsc();
	while (cpu_num != num_enabled_processors) {
		if ((asm_rdtsc() - tsc_start) > tsc_per_ms * 100)
			return 0;
		asm_pause();
	}

out:
	/* Prevent BIOS interrupts */
	asm volatile("cli;");

	return num_enabled_processors;
}
