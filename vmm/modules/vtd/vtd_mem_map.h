/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _VTD_MEM_MAP_H_
#define _VTD_MEM_MAP_H_

/* SAGAW field of Capability Register */
#define SAGAW_RESERVED_BIT0    (1U << 0)
#define SAGAW_SUPPORT_3_LVL_PT (1U << 1)
#define SAGAW_SUPPORT_4_LVL_PT (1U << 2)
#define SAGAW_SUPPORT_5_LVL_PT (1U << 3)
#define SAGAW_RESERVED_BIT5    (1U << 4)

typedef union {
	struct {
		uint32_t read  : 1;
		uint32_t write : 1;
		uint32_t rsvd1 : 30;
	} bits;
	uint32_t uint32;
} vtdpt_attr_t;

typedef struct {
	uint64_t hpa;
	uint8_t agaw;
	uint8_t pad[7];
} vtd_trans_table_t;

void set_translation_cap(uint8_t max_leaf, uint8_t tm, uint8_t snoop, uint8_t sagaw);


/* Get mam_handle of a domain by domain_id.
 * This function will create a new domain with domain_id if the domain
 * is not found.
 */
mam_handle_t vtd_get_mam_handle(uint16_t domain_id);


void vtd_get_trans_table(uint16_t domain_id, vtd_trans_table_t *trans_table);

#endif
