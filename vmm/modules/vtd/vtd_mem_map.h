/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _VTD_MEM_MAP_H_
#define _VTD_MEM_MAP_H_

typedef union {
	struct {
		uint32_t read  : 1;
		uint32_t write : 1;
		uint32_t rsvd1 : 30;
	} bits;
	uint32_t uint32;
} vtdpt_attr_t;

void set_translation_cap(uint8_t max_leaf, uint8_t tm, uint8_t snoop);


/* Get mam_handle of a domain by domain_id.
 * This function will create a new domain with domain_id if the domain
 * is not found.
 */
mam_handle_t vtd_get_mam_handle(uint16_t domain_id);

#endif
