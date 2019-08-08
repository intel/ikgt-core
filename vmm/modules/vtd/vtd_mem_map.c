/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "mam.h"
#include "dbg.h"
#include "hmm.h"
#include "vmm_objects.h"
#include "guest.h"
#include "gpm.h"
#include "heap.h"
#include "vtd_mem_map.h"

/*
 * ADDR field in Page-Table Entry will be evaluated by hardware only
 * when at least one of the Read(R) and Write(W) fields is set.
 * VT-D spec chapter 9.3 Page-Table Entry
 */
#define VTD_PT_R         (1ULL << 0)
#define VTD_PT_W         (1ULL << 1)
#define VTD_PT_P         (VTD_PT_R | VTD_PT_W)
#define VTD_PT_SP        (1ULL << 7)
#define VTD_PT_SNP       (1ULL << 11)
#define VTD_PT_TM        (1ULL << 62)

typedef struct _domain {
	mam_handle_t                  mam_handle;
	uint16_t                      domain_id;
	uint16_t                      padding[3];
	struct _domain                *next;
} domain_t;

static uint8_t g_tm;
static uint8_t g_snoop;
static uint8_t g_sagaw;
static domain_t *g_domain_list;

static boolean_t vtd_addr_trans_is_leaf(uint64_t entry, uint32_t level)
{
	if (MAM_LEVEL_PT == level)
		return TRUE;

	if (entry & VTD_PT_P)
		return (entry & VTD_PT_SP) ? TRUE : FALSE;

	/* Not present */
	return TRUE;
}

static boolean_t vtd_addr_trans_is_present(uint32_t attr)
{
	return (attr & VTD_PT_P) ? TRUE : FALSE;
}

static void vtd_addr_trans_to_table(uint64_t *p_entry)
{
	*p_entry &= MASK64_MID(51, 12);

	*p_entry |= VTD_PT_P;
}

static void
vtd_addr_trans_to_leaf(uint64_t *p_entry, uint32_t level, uint32_t attr)
{
	if (attr & VTD_PT_P) {
		/* Set R/W in same time */
		*p_entry &= MASK64_MID(51, 12);
		*p_entry |= attr & VTD_PT_P;

		if (level != MAM_LEVEL_PT)
			*p_entry |= VTD_PT_SP;

		if (g_snoop)
			*p_entry |= VTD_PT_SNP;

		if (g_tm)
			*p_entry |= VTD_PT_TM;
	} else {
		*p_entry = 0;
	}
}

static uint32_t vtd_leaf_get_attr(uint64_t leaf_entry, UNUSED uint32_t level)
{
	vtdpt_attr_t vtd_attr;

	D(VMM_ASSERT(level <= MAM_LEVEL_PML4));

	// Not present
	if ((leaf_entry & VTD_PT_P) == 0)
		return 0;

	// Get read, write bit
	vtd_attr.uint32 = 0;
	vtd_attr.bits.read  = !!(leaf_entry & VTD_PT_R);
	vtd_attr.bits.write = !!(leaf_entry & VTD_PT_W);

	return vtd_attr.uint32;
}

static mam_entry_ops_t vtd_entry_ops = {
	.max_leaf_level = 0xFF,
	.is_leaf = vtd_addr_trans_is_leaf,
	.is_present = vtd_addr_trans_is_present,
	.to_table = vtd_addr_trans_to_table,
	.to_leaf = vtd_addr_trans_to_leaf,
	.leaf_get_attr = vtd_leaf_get_attr
};

static domain_t *find_domain(uint16_t domain_id)
{
	domain_t *domain = g_domain_list;
	while (domain) {
		if (domain->domain_id == domain_id) {
			VMM_ASSERT_EX(domain->mam_handle,
				"VT-D: Address mapping NOT created for Guest[%d]!\n", domain_id);
			return domain;
		}
		domain = domain->next;
	}
	return NULL;
}

static domain_t *create_domain(uint16_t domain_id)
{
	domain_t *domain;

	D(VMM_ASSERT_EX(vtd_entry_ops.max_leaf_level != 0xFF,
			"max_leaf_level is NOT set!"));

	domain = (domain_t *)mem_alloc(sizeof(domain_t));
	domain->domain_id = domain_id;
	domain->mam_handle = mam_create_mapping(&vtd_entry_ops, 0);

	domain->next = g_domain_list;
	g_domain_list = domain;

	return domain;
}

mam_handle_t vtd_get_mam_handle(uint16_t domain_id)
{
	domain_t *domain;

	domain = find_domain(domain_id);

	if (!domain)
		domain = create_domain(domain_id);

	return domain->mam_handle;
}

void set_translation_cap(uint8_t max_leaf, uint8_t tm, uint8_t snoop, uint8_t sagaw)
{
	vtd_entry_ops.max_leaf_level = max_leaf;
	g_tm = tm;
	g_snoop = snoop;
	g_sagaw = sagaw;
}

#define PAGE_TABLE_3_LVL_AGAW 0x1U
#define PAGE_TABLE_4_LVL_AGAW 0x2U
void vtd_get_trans_table(uint16_t domain_id, vtd_trans_table_t *trans_table)
{
	uint64_t hpa;
	uint64_t hva;
	mam_handle_t mam_handle;

	mam_handle = vtd_get_mam_handle(domain_id);
	hpa = mam_get_table_hpa(mam_handle);

	/*
	 * The cap must support 3 or 4 level page table and already asserted in
	 * vtd_calculate_trans_cap().
	 */
	if (g_sagaw & SAGAW_SUPPORT_4_LVL_PT) {
		trans_table->hpa = hpa;
		trans_table->agaw = PAGE_TABLE_4_LVL_AGAW;
	} else {
		VMM_ASSERT(hmm_hpa_to_hva(hpa, &hva));
		/* Use the first entry of PML4 as 3-level page-table address */
		trans_table->hpa =  *((uint64_t *)hva) & MASK64_MID(51, 12);
		trans_table->agaw = PAGE_TABLE_3_LVL_AGAW;
	}
}
