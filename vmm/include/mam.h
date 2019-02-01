/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef MEMORY_ADDRESS_MAPPER_H
#define MEMORY_ADDRESS_MAPPER_H

#include <vmm_base.h>

#define MAM_LEVEL_PT 0
#define MAM_LEVEL_PD 1
#define MAM_LEVEL_PDPT 2
#define MAM_LEVEL_PML4 3

typedef struct {
	uint32_t max_leaf_level;
		// 0: allowed in level 0 -> 4K page
		// 1: allowed in level 0,1 -> 2M page
		// 2: allowed in level 0,1,2 -> 1G page
		// 3: allowed in level 0,1,2,3 -> 512G page
		// >3: not used
	uint32_t padding;
	boolean_t (*is_leaf)(uint64_t entry, uint32_t level);
		// return TRUE when it is not-present
	boolean_t (*is_present)(uint32_t attr);
		// get present from attr
	void (*to_table)(uint64_t *p_entry);
	void (*to_leaf)(uint64_t *p_entry, uint32_t level, uint32_t attr);
	uint32_t (*leaf_get_attr)(uint64_t leaf_entry, uint32_t level);
} mam_entry_ops_t;

typedef struct mam_t* mam_handle_t;

mam_handle_t mam_create_mapping(mam_entry_ops_t *entry_ops, uint32_t attr);

void mam_insert_range(mam_handle_t mam_handle, uint64_t src_addr,
	uint64_t tgt_addr, uint64_t size, uint32_t attr);

void mam_update_attr(mam_handle_t mam_handle, uint64_t src_addr,
	uint64_t size, uint32_t attr_mask, uint32_t attr_value);

boolean_t mam_get_mapping(mam_handle_t mam_handle, uint64_t src_addr,
	uint64_t *p_tgt_addr, uint32_t *p_attr);

uint64_t mam_get_table_hpa(mam_handle_t mam_handle);

typedef boolean_t (*mam_print_filter)(uint64_t src_addr, uint64_t current_entry,
	mam_entry_ops_t *entry_ops, uint32_t level);

#ifdef DEBUG
boolean_t mam_default_filter(uint64_t src_addr, uint64_t current_entry,
	mam_entry_ops_t *entry_ops, uint32_t level);
void mam_print(mam_handle_t mam_handle, mam_print_filter filter);
#endif
#endif
