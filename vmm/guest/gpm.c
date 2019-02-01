/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <vmm_base.h>
#include <hmm.h>
#include <heap.h>
#include <dbg.h>
#include <vmx_cap.h>
#include <vmm_objects.h>
#include <event.h>
#include <guest.h>
#include <mttr.h>
#include <gpm.h>

/****************
** EPT entry ops **
*****************/
#define EPT_R (1ULL << 0)
#define EPT_W (1ULL << 1)
#define EPT_X (1ULL << 2)
#define EPT_EMT (7ULL <<3) //bit[5:3]
#define EPT_PS (1ULL << 7)
#define EPT_VE (1ULL << 63)
#define EPT_ATTR_MASK 0x3F //for r,w,x,emt
#define EPT_P_MASK 0x7 // use to check if presnet

static mam_entry_ops_t g_ept_entry_ops;

static uint32_t ept_max_leaf_level(void)
{
	vmx_ept_vpid_cap_t ept_vpid;

	ept_vpid.uint64 = get_ept_vpid_cap();
	if (ept_vpid.bits.paging_1g)
		return MAM_LEVEL_PDPT;
	if (ept_vpid.bits.paging_2m)
		return MAM_LEVEL_PD;
	return MAM_LEVEL_PT;
}

static boolean_t ept_is_leaf(uint64_t entry, uint32_t level)
{
	if (level == MAM_LEVEL_PT)
		return TRUE;
	if (entry & EPT_P_MASK) // present
		return entry & EPT_PS;
	//not present
	return TRUE;
}

static boolean_t ept_is_present(uint32_t attr)
{
	return (attr & EPT_P_MASK) ? TRUE : FALSE;
}

static void ept_to_table(uint64_t *p_entry)
{
	*p_entry &= MASK64_MID(51,12); // clear attr
	*p_entry |= EPT_R|EPT_W|EPT_X;
}

static void ept_to_leaf(uint64_t *p_entry, uint32_t level, uint32_t attr)
{
	ept_attr_t ept_attr;

	D(VMM_ASSERT(level <= MAM_LEVEL_PML4));

	ept_attr.uint32 = attr;

	if (attr & EPT_P_MASK)
	{
		*p_entry &= MASK64_MID(51,12); // clear attr
		*p_entry |= (uint64_t)(attr & EPT_ATTR_MASK); // set r,w,x,emt
		if (level != MAM_LEVEL_PT)
			*p_entry |= EPT_PS;
	}
	else
	{
		// not present
		*p_entry = 0;
	}
	// #VE is also appliable to not-present
	if (ept_attr.bits.ve) // set #ve
		*p_entry |= EPT_VE;
}

static uint32_t ept_leaf_get_attr(uint64_t leaf_entry, UNUSED uint32_t level)
{
	ept_attr_t ept_attr;

	D(VMM_ASSERT(level <= MAM_LEVEL_PML4));

	if ((leaf_entry & EPT_P_MASK) == 0) // not present
	{
		ept_attr.uint32 = 0;
	}
	else
	{
		ept_attr.uint32 = leaf_entry & EPT_ATTR_MASK; // get r,w,x,emt
	}
	if ((leaf_entry & EPT_VE) == 1)
		ept_attr.bits.ve = 1;
	return ept_attr.uint32;
}

void invalidate_gpm(guest_handle_t guest)
{
	asm_invept(guest->eptp);
	event_raise(NULL, EVENT_GPM_INVALIDATE, guest);
}

void gpm_create_mapping(guest_handle_t guest)
{
	D(VMM_ASSERT(guest));
	// if want to enable #VE, use (1ULL<<63) as attr
	guest->gpa_to_hpa = mam_create_mapping(&g_ept_entry_ops, 0);
}

//the cache in attr[5:3] is valid
void gpm_set_mapping_with_cache(IN guest_handle_t guest,
				IN uint64_t gpa,
				IN uint64_t hpa,
				IN uint64_t size,
				IN uint32_t attr)
{
	event_gpm_set_t event_gpm_set;

	D(VMM_ASSERT_EX(guest, "gpm mapping guest is NULL\n"));
	VMM_ASSERT_EX((gpa & 0xFFFULL) == 0, "gpm mapping gpa 0x%llX isn't 4K page aligned\n", gpa);
	VMM_ASSERT_EX((hpa & 0xFFFULL) == 0, "gpm mapping hpa 0x%llX isn't 4K page aligned\n", hpa);
	VMM_ASSERT_EX((size & 0xFFFULL) == 0, "gpm mapping size 0x%llX isn't 4K page aligned\n", size);

	mam_insert_range(guest->gpa_to_hpa, gpa, hpa, size, attr);

	event_gpm_set.guest = guest;
	event_gpm_set.gpa = gpa;
	event_gpm_set.hpa = hpa;
	event_gpm_set.size = size;
	event_gpm_set.attr.uint32 = attr;
	event_raise(NULL, EVENT_GPM_SET, (void *)&event_gpm_set);
}

//the cache in attr[5:3] is not used. cache will be set automatically
void gpm_set_mapping(IN guest_handle_t guest,
			IN uint64_t gpa,
			IN uint64_t hpa,
			IN uint64_t size,
			IN uint32_t attr)
{
	uint64_t gpa_tmp = gpa;
	uint64_t hpa_tmp = hpa;
	uint64_t size_tmp = size;
	uint64_t cnt;
	ept_attr_t ept_attr;
	event_gpm_set_t event_gpm_set;
	mtrr_section_t *mtrr_ptr = get_mtrr_section_list();

	D(VMM_ASSERT_EX(guest, "gpm mapping guest is NULL\n"));
	VMM_ASSERT_EX((gpa & 0xFFFULL) == 0, "gpm mapping gpa 0x%llX isn't 4K page aligned\n", gpa);
	VMM_ASSERT_EX((hpa & 0xFFFULL) == 0, "gpm mapping hpa 0x%llX isn't 4K page aligned\n", hpa);
	VMM_ASSERT_EX((size & 0xFFFULL) == 0, "gpm mapping size 0x%llX isn't 4K page aligned\n", size);
	VMM_ASSERT_EX(((hpa < (hpa + size)) && ((hpa + size) <= top_of_memory)),
					"gpm mapiing hpa 0x%llX and size 0x%llX is invalid\n", hpa, size);

	ept_attr.uint32 = attr;

	for (; mtrr_ptr; mtrr_ptr = mtrr_ptr->next) {
		if ((hpa_tmp >= mtrr_ptr->base) && (hpa_tmp < (mtrr_ptr->base + mtrr_ptr->size))) {
			ept_attr.bits.emt = mtrr_ptr->type;
			if ((hpa_tmp + size_tmp) <= (mtrr_ptr->base + mtrr_ptr->size)) {
				mam_insert_range(guest->gpa_to_hpa,
							gpa_tmp,
							hpa_tmp,
							size_tmp,
							ept_attr.uint32);
				break;
			} else {
				cnt = mtrr_ptr->size - (hpa_tmp - mtrr_ptr->base);
				mam_insert_range(guest->gpa_to_hpa,
							gpa_tmp,
							hpa_tmp,
							cnt,
							ept_attr.uint32);

				gpa_tmp += cnt;
				hpa_tmp += cnt;
				size_tmp -= cnt;
			}
		} else {
			D(VMM_ASSERT(hpa_tmp > mtrr_ptr->base));
		}
	}

	event_gpm_set.guest = guest;
	event_gpm_set.gpa = gpa;
	event_gpm_set.hpa = hpa;
	event_gpm_set.size = size;
	event_gpm_set.attr.uint32 = attr;
	event_raise(NULL, EVENT_GPM_SET, (void *)&event_gpm_set);
}

/*-------------------------------------------------------------------------
 * Function: gpm_gpa_to_hpa
 *  Description: This function is used in order to convert Guest Physical Address
 *               to Host Physical Address (GPA-->HPA).
 *  Input:  guest - guest handle.
 *	    gpa - guest physical address.
 *  Output: p_hpa - host physical address.
 *          p_attr - ept page table entry attribute, can be NULL.
 *  Return Value: TRUE in case the mapping successful (it exists).
 *------------------------------------------------------------------------- */
boolean_t gpm_gpa_to_hpa(IN guest_handle_t guest,
			IN uint64_t gpa,
			OUT uint64_t *p_hpa,
			OUT ept_attr_t *p_attr)
{
	D(VMM_ASSERT(guest));
	VMM_ASSERT_EX((p_hpa), "p_hpa is NULL\n");

	return mam_get_mapping(guest->gpa_to_hpa, (uint64_t)gpa, p_hpa, &p_attr->uint32);
}

/*-------------------------------------------------------------------------
 * Function: gpm_gpa_to_hva
 *  Description: This function is used in order to convert Guest Physical Address
 *               to Host Virtual Address (GPA-->HVA).
 *  Input:  guest - guest handle.
 *          gpa - guest physical address.
 *          access - access rights, can be read, write, or read and write.
 *  Output: p_hva - host virtual address.
 *  Return Value: TRUE in case the mapping successful (it exists).
 *------------------------------------------------------------------------- */
boolean_t gpm_gpa_to_hva(IN guest_handle_t guest,
			 IN uint64_t gpa,
			 IN uint32_t access,
			 OUT uint64_t *p_hva)
{
	uint64_t hpa;
	ept_attr_t attrs;

	D(VMM_ASSERT(guest));
	VMM_ASSERT_EX(p_hva, "p_hva is NULL\n");
	VMM_ASSERT_EX(((access & (GUEST_CAN_READ | GUEST_CAN_WRITE)) != 0),
				"access is invalid\n");

	if (!gpm_gpa_to_hpa(guest, gpa, &hpa, &attrs)) {
		print_warn("%s: Failed Translation GPA(0x%llX) to HPA\n",
				__FUNCTION__, gpa);
		return FALSE;
	}

	/* check read or wirte access rights */
	if (((access & GUEST_CAN_READ) && (attrs.bits.r == 0)) ||
		((access & GUEST_CAN_WRITE) && (attrs.bits.w == 0))) {
		print_warn("%s: Fail to access HPA with attribute(%d) through"
			" access rights(%d)", __FUNCTION__, attrs.uint32, access);
		return FALSE;
	}

	if (!hmm_hpa_to_hva(hpa, p_hva)) {
		print_warn("%s: Failed Translation HPA(0x%llX) to HVA\n",
				__FUNCTION__, hpa);
		return FALSE;
	}

	return TRUE;
}

void gpm_init(void)
{
	g_ept_entry_ops.max_leaf_level = ept_max_leaf_level();
	g_ept_entry_ops.is_leaf = ept_is_leaf;
	g_ept_entry_ops.is_present = ept_is_present;
	g_ept_entry_ops.to_table = ept_to_table;
	g_ept_entry_ops.to_leaf = ept_to_leaf;
	g_ept_entry_ops.leaf_get_attr = ept_leaf_get_attr;
}
