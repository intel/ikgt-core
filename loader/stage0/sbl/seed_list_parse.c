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
#include "device_sec_info.h"
#include "sbl_boot_param.h"
#include "lib/util.h"

static boolean_t fill_dseed(device_sec_info_v0_t *dev_sec_info, seed_entry_t *entry)
{
	/* The seed_entry with same type/usage are always
	 * arranged by index in order of 0~3.
	 */
	if (entry->index != dev_sec_info->num_seeds) {
		print_panic("Index mismatch!");
		return FALSE;
	}

	if (entry->index >= BOOTLOADER_SEED_MAX_ENTRIES) {
		print_warn("Index exceed max number!");
		return FALSE;
	}

	memcpy(&dev_sec_info->dseed_list[dev_sec_info->num_seeds],
			entry->seed,
			sizeof(seed_info_t));

	dev_sec_info->num_seeds++;

	/* erase original seed in seed entry */
	memset(entry->seed, 0, sizeof(seed_info_t));
	barrier();

	return TRUE;
}

static boolean_t fill_rpmb_seed(device_sec_info_v0_t *dev_sec_info, seed_entry_t *entry)
{
	static uint8_t rpmb_seed_index = 0;
	static uint8_t rpmb_usage;

	/* The seed_entry with same type/usage are always
	 * arranged by index in order of 0~3.
	 */
	if (entry->index != rpmb_seed_index) {
		print_panic("RPMB index mismatch\n");
		return FALSE;
	}

	if (rpmb_seed_index == 0)
		rpmb_usage = entry->usage;
	else if (entry->usage != rpmb_usage) {
		print_panic("RPMB usage mismatch the previous one!\n");
		return FALSE;
	}

	if (entry->index >= RPMB_MAX_PARTITION_NUMBER) {
		print_panic("Index(%u) exceed max number!\n", entry->index);
		return FALSE;
	}

	memcpy(&dev_sec_info->rpmb_key[rpmb_seed_index],
			entry->seed,
			BUP_MKHI_BOOTLOADER_SEED_LEN);

	rpmb_seed_index++;

	return TRUE;
}

void parse_seed_list(device_sec_info_v0_t *dev_sec_info, seed_list_t *seed_list)
{
	uint8_t i;
	seed_entry_t *entry;

	if (!dev_sec_info) {
		print_panic("device sec info is NULL!\n");
		return;
	}

	if (!seed_list) {
		print_panic("Invalid seed_list pointer!\n");
		goto fail;
	}

	if (seed_list->total_seed_count == 0) {
		print_warn("Total seed count is 0!\n");
		goto fail;
	}

	entry = (seed_entry_t *)((uint8_t *)seed_list +
			sizeof(seed_list_t));

	for (i = 0; i < seed_list->total_seed_count; i++) {
		/* retrieve dseed */
		if ((SEED_ENTRY_TYPE_SVNSEED == entry->type) &&
			(SEED_ENTRY_USAGE_DSEED == entry->usage)) {
			if (!fill_dseed(dev_sec_info, entry))
				goto fail;
		} else if (SEED_ENTRY_TYPE_RPMBSEED == entry->type) {
			if (!fill_rpmb_seed(dev_sec_info, entry))
				goto fail;
		}
		entry = (seed_entry_t *)((uint8_t *)entry +
			entry->seed_entry_size);
	}

	if (dev_sec_info->num_seeds == 0) {
		print_warn("No available dseed in seed list\n");
		goto fail;
	}

	return;

fail:
	print_warn("Use fake seed due to no seed or parse seed list failed!\n");
	dev_sec_info->num_seeds = 1;
	memset(&dev_sec_info->dseed_list, 0, sizeof(dev_sec_info->dseed_list));
	memset(&dev_sec_info->rpmb_key[0], 0, sizeof(dev_sec_info->rpmb_key));
	memset(dev_sec_info->dseed_list[0].seed, 0xA5,
			sizeof(dev_sec_info->dseed_list[0].seed));
}
