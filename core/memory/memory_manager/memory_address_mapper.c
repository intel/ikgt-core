/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

/*
 *
 * Abstract:
 *
 * Creates data structures for convenient mapping of memory addresses
 *
 * Concise algorithm description and code walkthrough:
 *
 * The main purpose of this module is to create functionality of mapping
 * addresses from one value to another with 4K granularity. The secondary
 * purpose of this module is to convert the existing mapping to hardware
 * compliant page tables, extended page tables (EPT), VT-d page tables (VTDPT)
 * when requested. In order to make creation of these tables simple, the
 * internal mapping looks very similar to page tables, extended page tables
 * (EPT), or VT-d page tables (VTDPT):
 *
 *  level4        level3        level2        level1
 *  ------        ------        ------        ------
 * |      |      |      |      |      |      |      |
 * |      |      |    --|--+   |      |      |      |
 * |    --|--+   |      |  |   |    --|--+   |      |
 * |      |  |   |      |  |   |      |  |   |values|
 * ------ +-> ------ +-> ------ +-> ------
 *
 * In order to receive the target address to which the source one is mapped,
 * the later is divided to several sections in very similar way as mapping in
 * paging
 *
 * Source address is divided as following:
 *
 * not used level4 idx level3 idx level2 idx level1 idx offset
 * +-------------------------------------------------------------------------+
 * | 16 bits | 9 bits | 9 bits | 9 bits | 9 bits | 12 bits |
 * +-------------------------------------------------------------------------+
 *
 * The purpose of this module is to map memory addresses, so the source and\
 * target addresses can be either virtual or physical ones. The physical
 * address which is currently supported by hardware is up to 40 bits. And
 * virtual address has 48 (0-47) least significant bits which can have any
 * value and 16 most significant ones which must have the same value as bit
 * 47 (sign extension). Hence the four levels of mapping will be enough for
 * any address possible.
 *
 * The most of the code is written in a way similar to "Template Pattern" with
 * usage of "virtual tables" and recursive calls. For example, the algorithm
 * for retrieval target address from some table (which one is not known in
 * compile time)
 * retreive_address_from_table:
 * 1. retrieve index of current entry - the call to "virtual function" which
 *             return the index of current entry according to level (1-4) of
 *              the current table.
 * 2. if leaf entry return the recorded address - the indication whether the
 *    entry is leaf or inner one will be described later.
 * 3. the entry is not leaf
 * 4. retrieve the virtual table of lower level - the call to "virtual function"
 *             which will return the pointer to virtual table of functions
 *             relevant to table of lower level.
 * 5. retrieve the address of the lower level table - the call to "virtual
 *    function" which will return the virtual address (pointer)
 *    of the next table. The entry may be of several types (internal,
 *    page table entry, ept entry, VT-d page tables entry)
 *
 * 6. recursive call to the same function (algorithm) with new virtual table and
 * address (of the table) as parameters
 *
 * In order to retrieve the mapped address, just call the function above with
 *  virtual table of highest level (level4) and virtual address of the highest
 * level table.
 *
 * There are two types of virtual tables. The first one contains pointers to
 * functions relevant only to level of the tables, such as:
 *          - get size covered by entry in current table (4K, 2M, 1G or 512G)
 *          - retrieve index of the entry for current table from given source
 *            address
 *          - retrieve pointer to virtual table of * lower/upper level
 *
 * The pointers to these tables are: MAM_LEVEL1_OPS,
 * MAM_LEVEL2_OPS, MAM_LEVEL3_OPS and MAM_LEVEL4_OPS
 *
 * The second type of virtual tables contain pointer to functions relevant to
 * specific type of entry (internal, page tables entry, ept entry or VT-d page
 * tables entry ). It contains pointers to functions such as:
 *     - check if entry is present
 *     - retrieve address recorded in entry
 *     - retrieve/update attributes recorded in entry
 *     - ...
 * The pointers to these tables are: MAM_INTERNAL_ENTRY_OPS,
 * MAM_PAGE_TABLE_ENTRY_OPS, MAM_EPT_ENTRY_OPS, MAM_VTDPT_ENTRY_OPS
 *
 * According to operation, which must be performed, the appropriate virtual
 * table (table_level vs entry_type) will be used.
 *
 * As was stated earlier, there are four different types of single entry:
 * internal entry (generic), page tables entry, ept entry, and VT-d page table
 * entry. In order to define these entries, the union "mam_entry_t" is used.
 * Note that this is UNION!!!
 *
 * There are 2 bits common for any type of the entry (9-10). These two bits
 * are "available" bits for page table entry, ept entry, and VT-d page table
 * entry according to hardware documentation. They are used in order to
 * specify the baisc type of the entry (internal entry (generic), page tables
 * entry, ept entry, or VT-d page table entry).
 *
 * In addition, there are two other concepts used for entries: "inner entry"
 * and "leaf entry". The "leaf entry" contains final mapping information and
 * "inner entry" contains reference (virtual address of physical one) to lower
 * level table.
 *
 * Bit 11 is "available" for both page table entry and ept entry based on the
 * hardware documentation, this bit is used to tell whether this entry is an
 * ineer entry (clear) or leaf entry (set) for internal entry (generic), page
 * tables entry, and ept entry. But bit 11 is specified for "snoop behavior"
 * in VT-d page table, so we used bit 8 (available in HW) to tell whether this
 * entry is an inner entry (clear) or leaf entry (set) for VT-d page table
 * entry.
 *
 * The combination of the basic and leaf/inner entry type is used to process
 * the information properly among different page table entries.
 *
 * -- */

#include <memory_address_mapper_api.h>
#include <heap.h>
#include <mon_dbg.h>
#include <hw_interlocked.h>
#include <host_memory_manager_api.h>
#include "memory_address_mapper.h"
#include "mam_forward_declarations.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(MEMORY_ADDRESS_MAPPER_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(MEMORY_ADDRESS_MAPPER_C, \
	__condition)

/* for debug */
#define EPT_INNER_ENTRY_RESERVED 0x0000000000000008L

const mam_attributes_t mam_no_attributes = { 0 };

mam_attributes_t mam_rwx_attrs = { 0x7 };
mam_attributes_t mam_rw_attrs = { 0x3 };
mam_attributes_t mam_ro_attrs = { 0x1 };

/*--------------------------------------------------------------------- */

const mam_level_ops_t mam_level1_ops = {
	/* mam_get_size_covered_by_entry_fn */
	mam_get_size_covered_by_level1_entry,
	/* mam_get_entry_index_fn */
	mam_get_level1_entry_index,
	/* mam_get_lower_level_ops_fn */
	mam_get_non_existing_ops,
	/* mam_get_upper_level_ops_fn */
	mam_get_level2_ops,
};

const mam_level_ops_t mam_level2_ops = {
	/* mam_get_size_covered_by_entry_fn */
	mam_get_size_covered_by_level2_entry,
	/* mam_get_entry_index_fn */
	mam_get_level2_entry_index,
	/* mam_get_lower_level_ops_fn */
	mam_get_level1_ops,
	/* mam_get_upper_level_ops_fn */
	mam_get_level3_ops,
};

const mam_level_ops_t mam_level3_ops = {
	/* mam_get_size_covered_by_entry_fn */
	mam_get_size_covered_by_level3_entry,
	/* mam_get_entry_index_fn */
	mam_get_level3_entry_index,
	/* mam_get_lower_level_ops_fn */
	mam_get_level2_ops,
	/* mam_get_upper_level_ops_fn */
	mam_get_level4_ops,
};

const mam_level_ops_t mam_level4_ops = {
	/* mam_get_size_covered_by_entry_fn */
	mam_get_size_covered_by_level4_entry,
	/* mam_get_entry_index_fn */
	mam_get_level4_entry_index,
	/* mam_get_lower_level_ops_fn */
	mam_get_level3_ops,
	/* mam_get_upper_level_ops_fn */
	mam_get_non_existing_ops,
};

#define MAM_LEVEL1_OPS (&mam_level1_ops)
#define MAM_LEVEL2_OPS (&mam_level2_ops)
#define MAM_LEVEL3_OPS (&mam_level3_ops)
#define MAM_LEVEL4_OPS (&mam_level4_ops)

/*--------------------------------------------------------------------- */

const mam_entry_ops_t mam_internal_entry_ops = {
	/* mam_get_address_from_leaf_entry_fn */
	mam_get_address_from_leaf_internal_entry,
	/* mam_get_attributes_from_entry_fn */
	mam_get_attributes_from_internal_entry,
	/* mam_get_table_pointed_by_entry_fn */
	mam_get_table_pointed_by_internal_enty,
	/* mam_is_entry_present_fn */
	mam_is_internal_entry_present,
	/* mam_can_be_leaf_entry_fn */
	mam_can_be_leaf_internal_entry,
	/* mam_update_leaf_entry_fn */
	mam_update_leaf_internal_entry,
	/* mam_update_inner_level_entry_fn */
	mam_update_inner_internal_entry,
	/* mam_update_attributes_in_leaf_entry_fn */
	mam_update_attributes_in_leaf_internal_entry,
	/* mam_get_leaf_entry_type_fn */
	mam_get_leaf_internal_entry_type,
};

const mam_entry_ops_t mam_page_table_entry_ops = {
	/* mam_get_address_from_leaf_entry_fn */
	mam_get_address_from_leaf_page_table_entry,
	/* mam_get_attributes_from_entry_fn */
	mam_get_attributes_from_page_table_entry,
	/* mam_get_table_pointed_by_entry_fn */
	mam_get_table_pointed_by_page_table_entry,
	/* mam_is_entry_present_fn */
	mam_is_page_table_entry_present,
	/* mam_can_be_leaf_entry_fn */
	mam_can_be_leaf_page_table_entry,
	/* mam_update_leaf_entry_fn */
	mam_update_leaf_page_table_entry,
	/* mam_update_inner_level_entry_fn */
	mam_update_inner_page_table_entry,
	/* mam_update_attributes_in_leaf_entry_fn */
	mam_update_attributes_in_leaf_page_table_entry,
	/* mam_get_leaf_entry_type_fn */
	mam_get_leaf_page_table_entry_type,
};

const mam_entry_ops_t mam_ept_entry_ops = {
	/* mam_get_address_from_leaf_entry_fn */
	mam_get_address_from_leaf_ept_entry,
	/* mam_get_attributes_from_entry_fn */
	mam_get_attributes_from_ept_entry,
	/* mam_get_table_pointed_by_entry_fn */
	mam_get_table_pointed_by_ept_entry,
	/* mam_is_entry_present_fn */
	mam_is_ept_entry_present,
	/* mam_can_be_leaf_entry_fn */
	mam_can_be_leaf_ept_entry,
	/* mam_update_leaf_entry_fn */
	mam_update_leaf_ept_entry,
	/* mam_update_inner_level_entry_fn */
	mam_update_inner_ept_entry,
	/* mam_update_attributes_in_leaf_entry_fn */
	mam_update_attributes_in_leaf_ept_entry,
	/* mam_get_leaf_entry_type_fn */
	mam_get_leaf_ept_entry_type,
};

const mam_entry_ops_t mam_vtdpt_entry_ops = {
	/* mam_get_address_from_leaf_entry_fn */
	mam_get_address_from_leaf_vtdpt_entry,
	/* mam_get_attributes_from_entry_fn */
	mam_get_attributes_from_vtdpt_entry,
	/* mam_get_table_pointed_by_entry_fn */
	mam_get_table_pointed_by_vtdpt_entry,
	/* mam_is_entry_present_fn */
	mam_is_vtdpt_entry_present,
	/* mam_can_be_leaf_entry_fn */
	mam_can_be_leaf_vtdpt_entry,
	/* mam_update_leaf_entry_fn */
	mam_update_leaf_vtdpt_entry,
	/* mam_update_inner_level_entry_fn */
	mam_update_inner_vtdpt_entry,
	/* mam_update_attributes_in_leaf_entry_fn */
	mam_update_attributes_in_leaf_vtdpt_entry,
	/* mam_get_leaf_entry_type_fn */
	mam_get_leaf_vtdpt_entry_type,
};

#define MAM_INTERNAL_ENTRY_OPS (&mam_internal_entry_ops)
#define MAM_PAGE_TABLE_ENTRY_OPS (&mam_page_table_entry_ops)
#define MAM_EPT_ENTRY_OPS (&mam_ept_entry_ops)
#define MAM_VTDPT_ENTRY_OPS (&mam_vtdpt_entry_ops)

/*--------------------------------------------------------------------- */

/* including "hw_utils.h" causes compilation problems in UnitTesting */
extern cpu_id_t ASM_FUNCTION hw_cpu_id(void);

INLINE mam_entry_type_t get_mam_entry_type(IN mam_entry_t *entry)
{
	mam_entry_type_t entry_type = (mam_entry_type_t)0;

	/* get inner MAM entry type */
	entry_type =
		(mam_entry_type_t)(entry->any_entry.avl &
				   MAM_INNER_ENTRY_TYPE_MASK);
	if (entry_type != MAM_VTDPT_ENTRY) {
		entry_type = (mam_entry_type_t)(entry->any_entry.avl);
	} else {
		entry_type =
			(mam_entry_type_t)(entry_type |
					   (entry->vtdpt_entry.avl_1 << 2));
	}
	return entry_type;
}

INLINE boolean_t mam_entry_type_is_leaf_entry(IN mam_entry_t *entry)
{
	mam_entry_type_t entry_type = get_mam_entry_type(entry);

	return ((entry_type) & MAM_LEAF_ENTRY_TYPE_MASK) != 0;
}

INLINE mam_hav_t mam_ptr_to_hva(void *ptr)
{
	return (mam_hav_t)ptr;
}

INLINE void *mam_hva_to_ptr(mam_hav_t hva)
{
	return (void *)hva;
}

INLINE mam_hav_t mam_hpa_to_hva(IN mam_hpv_t hpa)
{
	mam_hav_t hva = 0;

	if (!mon_hmm_hpa_to_hva((uint64_t)hpa, (uint64_t *)(&hva))) {
		MON_ASSERT(0);
	}
	return (mam_hav_t)hva;
}

INLINE mam_hpv_t mam_hva_to_hpa(IN mam_hav_t hva)
{
	mam_hpv_t hpa = 0;

	if (!mon_hmm_hva_to_hpa((uint64_t)hva, (uint64_t *)(&hpa))) {
		MON_ASSERT(0);
	}
	return (mam_hpv_t)hpa;
}

INLINE void mam_invalidate_entry(IN mam_entry_t *entry,
				 IN mam_mapping_result_t reason,
				 IN mam_entry_type_t entry_type)
{
	/* When invalidating the entry, the reason for invalidation
	 * resides in high part of the entry when "avl" bits in low entry
	 * should still contain the correct type of the entry */
	entry->invalid_entry.low_part.must_be_zero0 = 0;
	/* make sure that clearing present bits are visible for every cpu; */
	hw_store_fence();
	entry->invalid_entry.low_part.must_be_zero1 = 0;
	entry->invalid_entry.low_part.avl = (uint32_t)entry_type;
	entry->invalid_entry.high_part.reason = reason;
}

INLINE boolean_t mam_is_leaf_entry(IN mam_entry_t *entry)
{
	return mam_entry_type_is_leaf_entry(entry);
}

INLINE uint64_t mam_get_size_covered_by_entry(
	IN const mam_level_ops_t *level_ops)
{
	return level_ops->mam_get_size_covered_by_entry_fn();
}

INLINE uint32_t mam_get_entry_index(IN const mam_level_ops_t *level_ops,
				    IN uint64_t addr)
{
	return level_ops->mam_get_entry_index_fn(addr);
}

INLINE const mam_level_ops_t *mam_get_lower_level_ops(IN const mam_level_ops_t *
						      level_ops)
{
	return level_ops->mam_get_lower_level_ops_fn();
}

INLINE const mam_level_ops_t *mam_get_upper_level_ops(IN const mam_level_ops_t *
						      level_ops)
{
	return level_ops->mam_get_upper_level_ops_fn();
}

INLINE uint64_t mam_get_size_covered_by_table(
	IN const mam_level_ops_t *level_ops)
{
	return mam_get_size_covered_by_entry(level_ops) *
	       MAM_NUM_OF_ENTRIES_IN_TABLE;
}

INLINE uint64_t mam_get_size_covered_by_level1_table(void)
{
	return mam_get_size_covered_by_table(MAM_LEVEL1_OPS);
}

INLINE uint64_t mam_get_size_covered_by_level2_table(void)
{
	return mam_get_size_covered_by_table(MAM_LEVEL2_OPS);
}

INLINE uint64_t mam_get_size_covered_by_level3_table(void)
{
	return mam_get_size_covered_by_table(MAM_LEVEL3_OPS);
}

INLINE uint32_t mam_calculate_pat_index(uint32_t pwt, uint32_t pcd,
					uint32_t pat)
{
	return (pat << MAM_PAT_BIT_POS_IN_PAT_INDEX) |
	       (pcd << MAM_PCD_BIT_POS_IN_PAT_INDEX) |
	       (pwt << MAM_PWT_BIT_POS_IN_PAT_INDEX);
}

INLINE
void mam_calculate_caching_attributes_from_pat_index(uint32_t pat_index,
						     uint32_t *pwt,
						     uint32_t *pcd,
						     uint32_t *pat)
{
	*pwt = ((pat_index >> MAM_PWT_BIT_POS_IN_PAT_INDEX) & 0x1);
	*pcd = ((pat_index >> MAM_PCD_BIT_POS_IN_PAT_INDEX) & 0x1);
	*pat = ((pat_index >> MAM_PAT_BIT_POS_IN_PAT_INDEX) & 0x1);
}

INLINE uint64_t mam_get_address_from_leaf_entry(IN mam_entry_t *entry,
						IN const mam_level_ops_t *level_ops,
						IN const mam_entry_ops_t *entry_ops)
{
	return entry_ops->mam_get_address_from_leaf_entry_fn(entry, level_ops);
}

INLINE mam_attributes_t mam_get_attributes_from_entry(IN mam_entry_t *entry,
						      IN const mam_level_ops_t *
						      level_ops,
						      IN const mam_entry_ops_t *
						      entry_ops)
{
	return entry_ops->mam_get_attributes_from_entry_fn(entry, level_ops);
}

INLINE mam_hav_t mam_get_table_pointed_by_entry(IN mam_entry_t *entry,
						IN const mam_entry_ops_t *entry_ops)
{
	return entry_ops->mam_get_table_pointed_by_entry_fn(entry);
}

INLINE boolean_t mam_is_entry_present(IN mam_entry_t *entry,
				      IN const mam_entry_ops_t *entry_ops)
{
	return entry_ops->mam_is_entry_present_fn(entry);
}

INLINE boolean_t mam_can_be_leaf_entry(IN mam_t *mam,
				       IN const mam_level_ops_t *level_ops,
				       IN uint64_t requested_size,
				       IN uint64_t tgt_addr,
				       IN const mam_entry_ops_t *entry_ops)
{
	return entry_ops->mam_can_be_leaf_entry_fn(mam,
		level_ops,
		requested_size,
		tgt_addr);
}

INLINE void mam_update_leaf_entry(IN mam_entry_t *entry,
				  IN uint64_t addr,
				  IN mam_attributes_t attr,
				  IN const mam_level_ops_t *level_ops,
				  IN const mam_entry_ops_t *entry_ops)
{
	entry_ops->mam_update_leaf_entry_fn(entry, addr, attr, level_ops);
}

INLINE void mam_update_inner_level_entry(IN mam_t *mam,
					 IN mam_entry_t *entry,
					 IN mam_hav_t next_table,
					 IN const mam_level_ops_t *level_ops,
					 IN const mam_entry_ops_t *entry_ops)
{
	entry_ops->mam_update_inner_level_entry_fn(mam, entry, next_table,
		level_ops);
}

INLINE mam_entry_type_t mam_get_leaf_entry_type(const mam_entry_ops_t *entry_ops)
{
	return entry_ops->mam_get_leaf_entry_type_fn();
}

INLINE void mam_update_attributes_in_leaf_entry(mam_entry_t *entry,
						mam_attributes_t attrs,
						const mam_level_ops_t *level_ops,
						const mam_entry_ops_t *entry_ops)
{
	entry_ops->mam_update_attributes_in_leaf_entry_fn(entry,
		attrs,
		level_ops);
}

/*--------------------------------------------------------------------- */
static
uint64_t mam_get_address_from_any_entry(IN mam_entry_t *entry)
{
	uint32_t addr_low;
	uint32_t addr_high;
	uint64_t addr;

	addr_low = entry->any_entry.addr_low << MAM_TABLE_ADDRESS_SHIFT;
	addr_high = entry->any_entry.addr_high;
	addr = ((uint64_t)addr_high << MAM_TABLE_ADDRESS_HIGH_SHIFT) | addr_low;
	return addr;
}

static
void mam_set_address_in_any_entry(IN mam_entry_t *entry, uint64_t addr)
{
	uint32_t addr_low = ((uint32_t)addr) >> MAM_TABLE_ADDRESS_SHIFT;
	uint32_t addr_high = (uint32_t)(addr >> MAM_TABLE_ADDRESS_HIGH_SHIFT);

	MON_ASSERT(addr < MAM_MAX_SUPPORTED_ADDRESS);
	entry->any_entry.addr_low = addr_low;
	entry->any_entry.addr_high = addr_high;
}

static
uint64_t mam_get_address_from_leaf_internal_entry(IN mam_entry_t *entry,
						  IN const mam_level_ops_t *
						  level_ops UNUSED)
{
	MON_ASSERT(entry->any_entry.avl == MAM_LEAF_INTERNAL_ENTRY);

	return mam_get_address_from_any_entry(entry);
}

static
uint64_t mam_get_address_from_leaf_page_table_entry(IN mam_entry_t *entry,
						    IN const mam_level_ops_t *
						    level_ops)
{
	uint64_t addr;

	MON_ASSERT(entry->any_entry.avl == MAM_LEAF_PAGE_TABLE_ENTRY);
	addr = mam_get_address_from_any_entry(entry);

	if (level_ops == MAM_LEVEL2_OPS) {
		/* It is PDE --> PS bit must be set and PAT bit resides in bit 12 */

		MON_ASSERT(entry->page_table_entry.ps_or_pat);
		addr &= (~((uint64_t)0x1 << MAM_TABLE_ADDRESS_SHIFT));
	}

	return addr;
}

static
uint64_t mam_get_address_from_leaf_ept_entry(IN mam_entry_t *entry,
					     IN const mam_level_ops_t *
					     level_ops UNUSED)
{
	MON_ASSERT(entry->any_entry.avl == MAM_LEAF_EPT_ENTRY);

	return mam_get_address_from_any_entry(entry);
}

static
uint64_t mam_get_address_from_leaf_vtdpt_entry(IN mam_entry_t *entry,
					       IN const mam_level_ops_t *
					       level_ops UNUSED)
{
	MON_ASSERT(get_mam_entry_type(entry) == MAM_LEAF_VTDPT_ENTRY);

	return mam_get_address_from_any_entry(entry);
}

static
void mam_update_leaf_internal_entry(IN mam_entry_t *entry,
				    IN uint64_t addr,
				    IN mam_attributes_t attr,
				    IN const mam_level_ops_t *level_ops UNUSED)
{
	MON_ASSERT(ALIGN_BACKWARD(addr, PAGE_4KB_SIZE) == addr);
	entry->uint64 = 0;
	hw_store_fence();
	entry->any_entry.avl = MAM_LEAF_INTERNAL_ENTRY;
	mam_set_address_in_any_entry(entry, addr);
	entry->mam_internal_entry.attributes = attr.uint32;
	hw_store_fence();
	entry->mam_internal_entry.present = 1;
}

static
void mam_update_leaf_page_table_entry(IN mam_entry_t *entry,
				      IN uint64_t addr,
				      IN mam_attributes_t attr,
				      IN const mam_level_ops_t *level_ops)
{
	uint32_t pwt_bit, pcd_bit, pat_bit;

	MON_ASSERT(ALIGN_BACKWARD(addr, PAGE_4KB_SIZE) == addr);

	entry->uint64 = 0;
	hw_store_fence();
	entry->any_entry.avl = MAM_LEAF_PAGE_TABLE_ENTRY;

	/* address */
	mam_set_address_in_any_entry(entry, addr);

	/* attributes */
	entry->page_table_entry.writable = attr.paging_attr.writable;
	entry->page_table_entry.user = attr.paging_attr.user;
	entry->page_table_entry.global = attr.paging_attr.global;
	entry->page_table_entry.exb = (attr.paging_attr.executable) ? 0 : 1;
	mam_calculate_caching_attributes_from_pat_index(
		attr.paging_attr.pat_index,
		&pwt_bit,
		&pcd_bit,
		&pat_bit);
	entry->page_table_entry.pwt = pwt_bit;
	entry->page_table_entry.pcd = pcd_bit;
	if (level_ops == MAM_LEVEL1_OPS) {
		entry->page_table_entry.ps_or_pat = pat_bit;
	} else {
		MON_ASSERT(level_ops == MAM_LEVEL2_OPS);

		/* PS bit must be set if PDE is leaf */
		entry->page_table_entry.ps_or_pat = 1;

		if (pat_bit) {
			/* PAT bit resides in bit 12 */
			entry->page_table_entry.addr_low |= 0x1;
		}
	}
	hw_store_fence();
	entry->page_table_entry.present = 1;
}

static
void mam_update_leaf_ept_entry(IN mam_entry_t *entry,
			       IN uint64_t addr,
			       IN mam_attributes_t attr,
			       IN const mam_level_ops_t *level_ops)
{
	entry->uint64 = 0;
	hw_store_fence();
	entry->any_entry.avl = MAM_LEAF_EPT_ENTRY;

	/* address */
	mam_set_address_in_any_entry(entry, addr);

	/* attributes */
	entry->ept_entry.igmt = attr.ept_attr.igmt;
	entry->ept_entry.emt = attr.ept_attr.emt;
	entry->ept_entry.suppress_ve = attr.ept_attr.suppress_ve;
	if (level_ops != MAM_LEVEL1_OPS) {
		entry->ept_entry.sp = 1;
	}

	hw_store_fence();
	entry->ept_entry.readable = attr.ept_attr.readable;
	entry->ept_entry.writable = attr.ept_attr.writable;
	entry->ept_entry.executable = attr.ept_attr.executable;

	/* MON_LOG(mask_anonymous, level_trace,"Updating leaf ept entry %P \n",
	 * entry->uint64); */
	MON_ASSERT(mam_is_ept_entry_present(entry));
}

static
void mam_update_leaf_vtdpt_entry(IN mam_entry_t *entry,
				 IN uint64_t addr,
				 IN mam_attributes_t attr,
				 IN const mam_level_ops_t *level_ops)
{
	entry->uint64 = 0;
	hw_store_fence();

	entry->vtdpt_entry.avl_2 = MAM_VTDPT_ENTRY;
	entry->vtdpt_entry.avl_1 = MAM_LEAF_ENTRY_TYPE_MASK >> 2;

	/* address */
	mam_set_address_in_any_entry(entry, addr);

	/* attributes */
	if (level_ops != MAM_LEVEL1_OPS) {
		entry->vtdpt_entry.sp = 1;
	}

	hw_store_fence();
	entry->vtdpt_entry.readable = attr.vtdpt_attr.readable;
	entry->vtdpt_entry.writable = attr.vtdpt_attr.writable;
	entry->vtdpt_entry.snoop = attr.vtdpt_attr.snoop;
	entry->vtdpt_entry.tm = attr.vtdpt_attr.tm;

	/* MON_LOG(mask_anonymous, level_trace,"Updating leaf vtdpt entry %P \n",
	 * entry->uint64); */
	MON_ASSERT(mam_is_vtdpt_entry_present(entry));
}

static
mam_attributes_t mam_get_attributes_from_internal_entry(IN mam_entry_t *entry,
							IN const mam_level_ops_t *
							level_ops UNUSED)
{
	mam_attributes_t attrs;

	MON_ASSERT((entry->any_entry.avl == MAM_INNER_INTERNAL_ENTRY)
		|| (entry->any_entry.avl == MAM_LEAF_INTERNAL_ENTRY));
	attrs.uint32 = (uint32_t)entry->mam_internal_entry.attributes;
	return attrs;
}

static
mam_attributes_t mam_get_attributes_from_page_table_entry(IN mam_entry_t *entry,
							  IN const mam_level_ops_t *
							  level_ops)
{
	mam_attributes_t attrs;
	uint32_t pat_bit = 0;
	uint32_t pat_index;

	MON_ASSERT((entry->any_entry.avl == MAM_INNER_PAGE_TABLE_ENTRY)
		|| (entry->any_entry.avl == MAM_LEAF_PAGE_TABLE_ENTRY));

	attrs.uint32 = 0;
	attrs.paging_attr.writable = (uint32_t)entry->page_table_entry.writable;
	attrs.paging_attr.user = (uint32_t)entry->page_table_entry.user;
	/* exb is execution disabled */
	attrs.paging_attr.executable = !((uint32_t)entry->page_table_entry.exb);
	attrs.paging_attr.global = (uint32_t)entry->page_table_entry.global;
	if (level_ops == MAM_LEVEL1_OPS) {
		/* 4K PTE, PAT bit resides in bit 7 */
		pat_bit = (uint32_t)entry->page_table_entry.ps_or_pat;
	} else if ((level_ops == MAM_LEVEL2_OPS) &&
		   (entry->page_table_entry.ps_or_pat)) {
		/* 2M PDE (points to final page) PAT bit resides in bit 12 */
		pat_bit = ((uint32_t)entry->page_table_entry.addr_low & 0x1);
	}

	MON_ASSERT(pat_bit <= 1);

	pat_index =
		mam_calculate_pat_index((uint32_t)entry->page_table_entry.pwt,
			(uint32_t)entry->page_table_entry.pcd, pat_bit);
	attrs.paging_attr.pat_index = pat_index;

	return attrs;
}

static
mam_attributes_t mam_get_attributes_from_ept_entry(IN mam_entry_t *entry,
						   IN const mam_level_ops_t *
						   level_ops UNUSED)
{
	mam_attributes_t attrs;

	MON_ASSERT((entry->any_entry.avl == MAM_INNER_EPT_ENTRY)
		|| (entry->any_entry.avl == MAM_LEAF_EPT_ENTRY));

	attrs.uint32 = 0;
	attrs.ept_attr.readable = (uint32_t)entry->ept_entry.readable;
	attrs.ept_attr.writable = (uint32_t)entry->ept_entry.writable;
	attrs.ept_attr.executable = (uint32_t)entry->ept_entry.executable;
	attrs.ept_attr.igmt = (uint32_t)entry->ept_entry.igmt;
	attrs.ept_attr.emt = (uint32_t)entry->ept_entry.emt;
	attrs.ept_attr.suppress_ve = (uint32_t)entry->ept_entry.suppress_ve;

	if ((entry->any_entry.avl == MAM_INNER_EPT_ENTRY)
	    && (attrs.ept_attr.igmt == 1)) {
		MON_ASSERT(0);
	}
	return attrs;
}

static
mam_attributes_t mam_get_attributes_from_vtdpt_entry(IN mam_entry_t *entry,
						     IN const mam_level_ops_t *
						     level_ops UNUSED)
{
	mam_attributes_t attrs;

	MON_ASSERT((get_mam_entry_type(entry) == MAM_INNER_VTDPT_ENTRY)
		|| (get_mam_entry_type(entry) == MAM_LEAF_VTDPT_ENTRY));

	attrs.uint32 = 0;
	attrs.vtdpt_attr.readable = (uint32_t)entry->vtdpt_entry.readable;
	attrs.vtdpt_attr.writable = (uint32_t)entry->vtdpt_entry.writable;
	attrs.vtdpt_attr.snoop = (uint32_t)entry->vtdpt_entry.snoop;
	attrs.vtdpt_attr.tm = (uint32_t)entry->vtdpt_entry.tm;
	return attrs;
}

static
mam_hav_t mam_get_table_pointed_by_internal_enty(IN mam_entry_t *entry)
{
	MON_ASSERT(entry->any_entry.avl == MAM_INNER_INTERNAL_ENTRY);
	return mam_get_address_from_any_entry(entry);
}

static
mam_hav_t mam_get_table_pointed_by_page_table_entry(IN mam_entry_t *entry)
{
	mam_hpv_t table_hpa;
	mam_hav_t table_hva;

	MON_ASSERT(entry->any_entry.avl == MAM_INNER_PAGE_TABLE_ENTRY);
	table_hpa = mam_get_address_from_any_entry(entry);

	table_hva = mam_hpa_to_hva(table_hpa);

	return table_hva;
}

static
mam_hav_t mam_get_table_pointed_by_ept_entry(IN mam_entry_t *entry)
{
	mam_hpv_t table_hpa;
	mam_hav_t table_hva;

	MON_ASSERT(entry->any_entry.avl == MAM_INNER_EPT_ENTRY);
	table_hpa = mam_get_address_from_any_entry(entry);

	table_hva = mam_hpa_to_hva(table_hpa);

	return table_hva;
}

static
mam_hav_t mam_get_table_pointed_by_vtdpt_entry(IN mam_entry_t *entry)
{
	mam_hpv_t table_hpa;
	mam_hav_t table_hva;

	MON_ASSERT(get_mam_entry_type(entry) == MAM_INNER_VTDPT_ENTRY);
	table_hpa = mam_get_address_from_any_entry(entry);

	table_hva = mam_hpa_to_hva(table_hpa);

	return table_hva;
}

static
boolean_t mam_is_internal_entry_present(IN mam_entry_t *entry)
{
	MON_ASSERT((entry->any_entry.avl == MAM_INNER_INTERNAL_ENTRY)
		|| (entry->any_entry.avl == MAM_LEAF_INTERNAL_ENTRY));

	return entry->mam_internal_entry.present != 0;
}

static
boolean_t mam_is_page_table_entry_present(IN mam_entry_t *entry)
{
	MON_ASSERT((entry->any_entry.avl == MAM_INNER_PAGE_TABLE_ENTRY)
		|| (entry->any_entry.avl == MAM_LEAF_PAGE_TABLE_ENTRY));

	return entry->page_table_entry.present != 0;
}

static
boolean_t mam_is_ept_entry_present(IN mam_entry_t *entry)
{
	MON_ASSERT((entry->any_entry.avl == MAM_INNER_EPT_ENTRY)
		|| (entry->any_entry.avl == MAM_LEAF_EPT_ENTRY));

	return (entry->ept_entry.readable != 0) ||
	       (entry->ept_entry.writable != 0) ||
	       (entry->ept_entry.executable != 0);
}

static
boolean_t mam_is_vtdpt_entry_present(IN mam_entry_t *entry)
{
	MON_ASSERT((get_mam_entry_type(entry) == MAM_INNER_VTDPT_ENTRY)
		|| (get_mam_entry_type(entry) == MAM_LEAF_VTDPT_ENTRY));

	return (entry->vtdpt_entry.readable != 0) ||
	       (entry->vtdpt_entry.writable != 0);
}

static
boolean_t mam_can_be_leaf_internal_entry(IN mam_t *mam UNUSED,
					 IN const mam_level_ops_t *level_ops,
					 uint64_t requested_size,
					 uint64_t tgt_addr UNUSED)
{
	/* tgt_addr can be any value */
	return mam_get_size_covered_by_entry(level_ops) == requested_size;
}

static
boolean_t mam_can_be_leaf_page_table_entry(IN mam_t *mam UNUSED,
					   IN const mam_level_ops_t *level_ops,
					   uint64_t requested_size,
					   uint64_t tgt_addr)
{
	if ((level_ops != MAM_LEVEL1_OPS) && (level_ops != MAM_LEVEL2_OPS)) {
		/* Only PTE and PDE can be leaf entry */
		return FALSE;
	}

	if (mam_get_size_covered_by_entry(level_ops) != requested_size) {
		/* The size doesn't fit */
		return FALSE;
	}

	if (level_ops == MAM_LEVEL2_OPS) {
		if ((tgt_addr & PAGE_2MB_MASK) != 0) {
			return FALSE;
		} else {
			/* tgt_addr is 2M aligned, it's OK */
		}
	} else {
		MON_ASSERT(level_ops == MAM_LEVEL1_OPS);
		MON_ASSERT(requested_size == PAGE_4KB_SIZE);

		/* PTE, that maps 4K is OK */
	}

	return TRUE;
}

static
boolean_t mam_can_be_leaf_ept_entry(IN mam_t *mam,
				    IN const mam_level_ops_t *level_ops,
				    uint64_t requested_size, uint64_t tgt_addr)
{
	if (mam_get_size_covered_by_entry(level_ops) != requested_size) {
		/* The size doesn't fit */
		return FALSE;
	}

	if (level_ops == MAM_LEVEL1_OPS) {
		return TRUE;
	}

	if (level_ops == MAM_LEVEL2_OPS) {
		if ((!(mam->ept_supper_page_support &
		       MAM_EPT_SUPPORT_2MB_PAGE)) ||
		    ((tgt_addr & PAGE_2MB_MASK) != 0)) {
			return FALSE;
		}

		return TRUE;
	}

	if (level_ops == MAM_LEVEL3_OPS) {
		if ((!(mam->ept_supper_page_support &
		       MAM_EPT_SUPPORT_1GB_PAGE)) ||
		    ((tgt_addr & MAM_PAGE_1GB_MASK) != 0)) {
			return FALSE;
		}

		return TRUE;
	}

	MON_ASSERT(level_ops == MAM_LEVEL4_OPS);
	if ((!(mam->ept_supper_page_support & MAM_EPT_SUPPORT_512_GB_PAGE)) ||
	    (tgt_addr & MAM_PAGE_512GB_MASK) != 0) {
		return FALSE;
	}

	return TRUE;
}

static
boolean_t mam_can_be_leaf_vtdpt_entry(IN mam_t *mam,
				      IN const mam_level_ops_t *level_ops,
				      uint64_t requested_size,
				      uint64_t tgt_addr)
{
	if (mam_get_size_covered_by_entry(level_ops) != requested_size) {
		/* The size doesn't fit */
		return FALSE;
	}

	if (level_ops == MAM_LEVEL1_OPS) {
		return TRUE;
	}

	if (level_ops == MAM_LEVEL2_OPS) {
		if ((!(mam->vtdpt_supper_page_support &
		       MAM_VTDPT_SUPPORT_2MB_PAGE)) ||
		    ((tgt_addr & PAGE_2MB_MASK) != 0)) {
			return FALSE;
		}

		return TRUE;
	}

	if (level_ops == MAM_LEVEL3_OPS) {
		if ((!(mam->vtdpt_supper_page_support &
		       MAM_VTDPT_SUPPORT_1GB_PAGE)) ||
		    ((tgt_addr & MAM_PAGE_1GB_MASK) != 0)) {
			return FALSE;
		}

		return TRUE;
	}

	MON_ASSERT(level_ops == MAM_LEVEL4_OPS);
	if ((!(mam->vtdpt_supper_page_support &
	       MAM_VTDPT_SUPPORT_512_GB_PAGE)) ||
	    (tgt_addr & MAM_PAGE_512GB_MASK) != 0) {
		return FALSE;
	}

	return TRUE;
}

static
void mam_update_inner_internal_entry(mam_t *mam, mam_entry_t *entry,
				     mam_hav_t next_table,
				     const mam_level_ops_t *level_ops UNUSED)
{
	mam_attributes_t attrs = mam->inner_level_attributes;

	MON_ASSERT(ALIGN_BACKWARD(next_table, PAGE_4KB_SIZE) == next_table);

	entry->uint64 = 0;
	hw_store_fence();
	entry->any_entry.avl = MAM_INNER_INTERNAL_ENTRY;
	mam_set_address_in_any_entry(entry, next_table);
	entry->mam_internal_entry.attributes = attrs.uint32;
	hw_store_fence();
	entry->mam_internal_entry.present = 1;
}

static
void mam_update_inner_page_table_entry(mam_t *mam, mam_entry_t *entry,
				       mam_hav_t next_table,
				       const mam_level_ops_t *level_ops)
{
	mam_attributes_t attrs = mam->inner_level_attributes;
	mam_hpv_t next_table_hpa;
	uint32_t pwt_bit, pcd_bit, pat_bit;

	/* Entry type must be updated */

	MON_ASSERT(ALIGN_BACKWARD(next_table, PAGE_4KB_SIZE) == next_table);

	next_table_hpa = mam_hva_to_hpa(next_table);

	entry->uint64 = 0;
	hw_store_fence();
	entry->any_entry.avl = MAM_INNER_PAGE_TABLE_ENTRY;

	mam_set_address_in_any_entry(entry, next_table_hpa);

	if ((!mam->is_32bit_page_tables) || (level_ops != MAM_LEVEL3_OPS)) {
		entry->page_table_entry.writable = attrs.paging_attr.writable;
		entry->page_table_entry.user = attrs.paging_attr.user;
		entry->page_table_entry.global = attrs.paging_attr.global;
		entry->page_table_entry.exb =
			(attrs.paging_attr.executable) ? 0 : 1;
	}

	mam_calculate_caching_attributes_from_pat_index(
		attrs.paging_attr.pat_index,
		&pwt_bit,
		&pcd_bit,
		&pat_bit);
	MON_ASSERT(pat_bit == 0);
	entry->page_table_entry.pwt = pwt_bit;
	entry->page_table_entry.pcd = pcd_bit;
	hw_store_fence();
	entry->page_table_entry.present = 1;
}

static
void mam_update_inner_ept_entry(mam_t *mam, mam_entry_t *entry,
				mam_hav_t next_table,
				const mam_level_ops_t *level_ops UNUSED)
{
	mam_attributes_t attrs = mam->inner_level_attributes;
	mam_hpv_t next_table_hpa;

	MON_ASSERT(ALIGN_BACKWARD(next_table, PAGE_4KB_SIZE) == next_table);

	next_table_hpa = mam_hva_to_hpa(next_table);

	entry->uint64 = 0;
	hw_store_fence();
	entry->any_entry.avl = MAM_INNER_EPT_ENTRY;

	mam_set_address_in_any_entry(entry, next_table_hpa);

	MON_ASSERT(attrs.ept_attr.igmt == 0);
	MON_ASSERT(attrs.ept_attr.emt == 0);

	/* igmt and emt remains 0 */
	hw_store_fence();
	entry->ept_entry.readable = attrs.ept_attr.readable;
	entry->ept_entry.writable = attrs.ept_attr.writable;
	entry->ept_entry.executable = attrs.ept_attr.executable;
	entry->ept_entry.suppress_ve = attrs.ept_attr.suppress_ve;

	if (entry->any_entry.avl == MAM_INNER_EPT_ENTRY) {
		MON_ASSERT(((entry->uint64) & EPT_INNER_ENTRY_RESERVED) == 0);
	}
	MON_ASSERT(mam_is_ept_entry_present(entry));
}

static
void mam_update_inner_vtdpt_entry(mam_t *mam, mam_entry_t *entry,
				  mam_hav_t next_table,
				  const mam_level_ops_t *level_ops UNUSED)
{
	mam_attributes_t attrs = mam->inner_level_attributes;
	mam_hpv_t next_table_hpa;

	MON_ASSERT(ALIGN_BACKWARD(next_table, PAGE_4KB_SIZE) == next_table);

	next_table_hpa = mam_hva_to_hpa(next_table);

	entry->uint64 = 0;
	hw_store_fence();
	entry->vtdpt_entry.avl_2 = MAM_INNER_VTDPT_ENTRY;

	mam_set_address_in_any_entry(entry, next_table_hpa);

	hw_store_fence();
	entry->vtdpt_entry.readable = attrs.vtdpt_attr.readable;
	entry->vtdpt_entry.writable = attrs.vtdpt_attr.writable;

	MON_ASSERT(mam_is_vtdpt_entry_present(entry));
}

static
void mam_update_attributes_in_leaf_internal_entry(mam_entry_t *entry,
						  mam_attributes_t attrs,
						  const mam_level_ops_t *
						  level_ops UNUSED)
{
	MON_ASSERT(entry->any_entry.avl == MAM_LEAF_INTERNAL_ENTRY);

	entry->mam_internal_entry.attributes = attrs.uint32;
	MON_ASSERT(mam_is_internal_entry_present(entry));
}

static
void mam_update_attributes_in_leaf_page_table_entry(mam_entry_t *entry,
						    mam_attributes_t attrs,
						    const mam_level_ops_t *
						    level_ops)
{
	uint32_t pwt_bit, pcd_bit, pat_bit;

	MON_ASSERT(entry->any_entry.avl == MAM_LEAF_PAGE_TABLE_ENTRY);

	entry->page_table_entry.writable = attrs.paging_attr.writable;
	entry->page_table_entry.user = attrs.paging_attr.user;
	entry->page_table_entry.exb = (attrs.paging_attr.executable) ? 0 : 1;
	entry->page_table_entry.global = attrs.paging_attr.global;
	mam_calculate_caching_attributes_from_pat_index(
		attrs.paging_attr.pat_index,
		&pwt_bit,
		&pcd_bit,
		&pat_bit);
	entry->page_table_entry.pwt = pwt_bit;
	entry->page_table_entry.pcd = pcd_bit;
	if (level_ops == MAM_LEVEL2_OPS) {
		/* PDE, PS bit must be set */
		MON_ASSERT(entry->page_table_entry.ps_or_pat);
		MON_ASSERT(pat_bit <= 1);
		/* PAT bit resides at bit 12 */
		entry->page_table_entry.addr_low |= pat_bit;
	} else {
		MON_ASSERT(level_ops == MAM_LEVEL1_OPS);
		entry->page_table_entry.ps_or_pat = pat_bit;
	}

	MON_ASSERT(mam_is_page_table_entry_present(entry));
}

static
void mam_update_attributes_in_leaf_ept_entry(mam_entry_t *entry,
					     mam_attributes_t attrs,
					     const mam_level_ops_t *level_ops)
{
	entry->ept_entry.readable = attrs.ept_attr.readable;
	entry->ept_entry.writable = attrs.ept_attr.writable;
	entry->ept_entry.executable = attrs.ept_attr.executable;
	entry->ept_entry.igmt = attrs.ept_attr.igmt;
	entry->ept_entry.emt = attrs.ept_attr.emt;
	entry->ept_entry.suppress_ve = attrs.ept_attr.suppress_ve;
	if (level_ops != MAM_LEVEL1_OPS) {
		entry->ept_entry.sp = 1;
	}

	MON_ASSERT(mam_is_ept_entry_present(entry));
}

static
void mam_update_attributes_in_leaf_vtdpt_entry(mam_entry_t *entry,
					       mam_attributes_t attrs,
					       const mam_level_ops_t *level_ops)
{
	entry->vtdpt_entry.readable = attrs.vtdpt_attr.readable;
	entry->vtdpt_entry.writable = attrs.vtdpt_attr.writable;
	entry->vtdpt_entry.snoop = attrs.vtdpt_attr.snoop;
	entry->vtdpt_entry.tm = attrs.vtdpt_attr.tm;
	if (level_ops != MAM_LEVEL1_OPS) {
		entry->vtdpt_entry.sp = 1;
	}

	MON_ASSERT(mam_is_vtdpt_entry_present(entry));
}

static
mam_entry_type_t mam_get_leaf_internal_entry_type(void)
{
	return MAM_LEAF_INTERNAL_ENTRY;
}

static
mam_entry_type_t mam_get_leaf_page_table_entry_type(void)
{
	return MAM_LEAF_PAGE_TABLE_ENTRY;
}

static
mam_entry_type_t mam_get_leaf_ept_entry_type(void)
{
	return MAM_LEAF_EPT_ENTRY;
}

static
mam_entry_type_t mam_get_leaf_vtdpt_entry_type(void)
{
	return MAM_LEAF_VTDPT_ENTRY;
}

/*--------------------------------------------------------------------- */

static
void mam_invalidate_all_entries_in_table(IN mam_hav_t table,
					 IN mam_mapping_result_t reason,
					 IN mam_entry_type_t entry_type)
{
	uint64_t entry_increment = sizeof(mam_entry_t);
	mam_hav_t entry_hva;

	entry_hva = table;
	while (entry_hva <
	       (table + (entry_increment * MAM_NUM_OF_ENTRIES_IN_TABLE))) {
		mam_entry_t *entry = (mam_entry_t *)mam_hva_to_ptr(entry_hva);
		mam_invalidate_entry(entry, reason, entry_type);
		entry_hva += entry_increment;
	}
}

/* -----------------------------------------------------------------------
 * Function: mam_create_table
 * Description: This function creates new table with invalidated entries
 * Input: unmapped_reason - reason why the entries are invalidated
 *        entry_type - type of each entry * Output: HVA of created table.
 * -----------------------------------------------------------------------*/
static
mam_hav_t mam_create_table(IN mam_mapping_result_t unmapped_reason,
			   IN mam_entry_type_t entry_type)
{
	/* allocate single page */
	void *allocated_table_ptr = mon_page_alloc(1);
	mam_hav_t allocated_table_hva = mam_ptr_to_hva(allocated_table_ptr);

	if (allocated_table_ptr != NULL) {
		mam_invalidate_all_entries_in_table(allocated_table_hva,
			unmapped_reason, entry_type);
	}

	return allocated_table_hva;
}

/* -----------------------------------------------------------------------
 * Function: mam_expand_leaf_entry
 * Description: This function expands existing leaf entry to equivalent lower
 *              level table and changes the entry to point to this table
 * Input: mam - main mam_t structure
 *        entry - entry to expand
 *        level_ops - virtual table for relevant table operations
 *        entry_ops - virtual table for relevant (according to type) entry
 *                    operations
 * Output: table - created lower level table.
 * Return value: TRUE in case of success.
 *               FALSE in case of insufficient memory.
 * -----------------------------------------------------------------------*/
static
boolean_t mam_expand_leaf_entry(IN mam_t *mam,
				IN mam_entry_t *entry,
				IN const mam_level_ops_t *level_ops,
				IN const mam_entry_ops_t *entry_ops,
				OUT mam_hav_t *table)
{
	mam_entry_type_t entry_type = get_mam_entry_type(entry);
	/* virtual call */
	uint64_t tgt_addr =
		mam_get_address_from_leaf_entry(entry, level_ops, entry_ops);
	/* virtual call */
	mam_attributes_t attrs =
		mam_get_attributes_from_entry(entry, level_ops, entry_ops);
	/* lower level table with appropriate type of the entries */
	mam_hav_t lower_level_table =
		mam_create_table(MAM_UNKNOWN_MAPPING, entry_type);
	/* virtual call */
	const mam_level_ops_t *lower_level_ops = mam_get_lower_level_ops(
		level_ops);
	mam_hav_t entry_hva;
	uint64_t size_covered_by_lower_level_entry;

	MON_ASSERT(lower_level_ops != NULL);
	if (lower_level_table == mam_ptr_to_hva(NULL)) {
		/* insufficient memory */
		return FALSE;
	}
	/* virtual call */
	size_covered_by_lower_level_entry =
		mam_get_size_covered_by_entry(lower_level_ops);
	entry_hva = lower_level_table;
	while (entry_hva < (lower_level_table + PAGE_4KB_SIZE)) {
		mam_entry_t *entry = mam_hva_to_ptr(entry_hva);

		/* virtual call */
		mam_update_leaf_entry(entry, tgt_addr, attrs, lower_level_ops,
			entry_ops);

		entry_hva += sizeof(mam_entry_t);
		tgt_addr += size_covered_by_lower_level_entry;
	}

	/* virtual call */
	mam_update_inner_level_entry(mam, entry, lower_level_table, level_ops,
		entry_ops);
	*table = lower_level_table;

	return TRUE;
}

/* -----------------------------------------------------------------------
 * Function: mam_try_to_retract_inner_entry_to_leaf
 * Description: This function checks whether it is possible to retract
 *              existing lower level table to single leaf entry.
 * Input: mam - main mam_t structure
 *        entry - entry to expand
 *        level_ops - virtual table for relevant table operations
 *        entry_ops - virtual table for relevant (according to type) entry
 *                    operations
 * -----------------------------------------------------------------------*/
static
void mam_try_to_retract_inner_entry_to_leaf(IN mam_t *mam,
					    IN mam_entry_t *entry_to_retract,
					    IN const mam_level_ops_t *level_ops,
					    IN const mam_entry_ops_t *entry_ops)
{
	mam_hav_t table_pointed_by_entry;
	mam_hav_t entry_hva;
	mam_entry_t *first_entry;
	boolean_t first_entry_present;
	uint64_t tgt_addr = 0;
	mam_attributes_t attr;
	/* virtual * call */
	const mam_level_ops_t *lower_level_ops = mam_get_lower_level_ops(
		level_ops);
	mam_mapping_result_t reason = MAM_UNKNOWN_MAPPING;
	uint32_t entry_index;
	uint64_t size_covered_by_lower_level_entry;

	attr.uint32 = 0;

	/* must be inner level entry */
	MON_ASSERT(!mam_is_leaf_entry(entry_to_retract));
	MON_ASSERT(lower_level_ops != NULL);

	size_covered_by_lower_level_entry =
		mam_get_size_covered_by_entry(lower_level_ops);

	/* virtual call */
	table_pointed_by_entry =
		mam_get_table_pointed_by_entry(entry_to_retract, entry_ops);
	MON_ASSERT(table_pointed_by_entry != mam_ptr_to_hva(NULL));

	/* Retrieve information about first entry */
	first_entry = mam_hva_to_ptr(table_pointed_by_entry);
	first_entry_present = mam_is_entry_present(first_entry, entry_ops);

	if (first_entry_present) {
		uint64_t size_covered_by_entry = mam_get_size_covered_by_entry(
			level_ops);

		if (!mam_is_leaf_entry(first_entry)) {
			/* The entry is not leaf, the retraction is not possible */
			return;
		}

		/* In case when first entry present and leaf, retrieve mapped address
		 * and attributes */

		tgt_addr =
			mam_get_address_from_leaf_entry(first_entry,
				lower_level_ops,
				entry_ops);

		/* Check whether the entry can be retracted to leaf with calculated
		 * tgt_addr (check for alignment) */
		/* virtual call */
		if (!mam_can_be_leaf_entry(mam, level_ops,
			    size_covered_by_entry,
			    tgt_addr, entry_ops)) {
			/* Cannot retract the entry */
			return;
		}
		/* virtual call */
		attr = mam_get_attributes_from_entry(first_entry,
			lower_level_ops,
			entry_ops);
	} else {
		/* When first entry is not present, retrieve the reason why. */
		reason = first_entry->invalid_entry.high_part.reason;
	}

	entry_hva = table_pointed_by_entry + sizeof(mam_entry_t);
	entry_index = 1;
	/* Go all over the entries starting with second one */
	while (entry_hva != (table_pointed_by_entry + PAGE_4KB_SIZE)) {
		mam_entry_t *entry = mam_hva_to_ptr(entry_hva);
		boolean_t curr_entry_is_present = mam_is_entry_present(entry,
			entry_ops);

		if ((curr_entry_is_present != first_entry_present) ||
		    (!mam_is_leaf_entry(entry))) {
			/* If current entry is not present when first entry is present (or
			 * vice versa),
			 * the retraction is not possible.
			 * If any entry is not leaf, retraction is not possible */
			return;
		}

		if (curr_entry_is_present) {
			/* virtual call */
			uint64_t curr_tgt_addr =
				mam_get_address_from_leaf_entry(entry,
					lower_level_ops, entry_ops);
			mam_attributes_t curr_attrs;

			if (curr_tgt_addr !=
			    (tgt_addr +
			     (entry_index *
			      size_covered_by_lower_level_entry))) {
				/* address written to current entry is not sequential to
				 * previous one, retraction is not possible */
				return;
			}

			/* Check whether the attributes are the same */
			curr_attrs =
				mam_get_attributes_from_entry(entry,
					lower_level_ops,
					entry_ops);
			if (curr_attrs.uint32 != attr.uint32) {
				/* the attributes differ, retraction is not possible */
				return;
			}
		} else {
			mam_mapping_result_t curr_reason =
				entry->invalid_entry.high_part.reason;

			/* If the entry is not present check whether the reason is the same
			 * as for first entry */
			if (curr_reason != reason) {
				return;
			}
		}

		entry_hva += sizeof(mam_entry_t);
		entry_index++;
	}

	/* It is possible to retract
	* First: update the entry */
	if (first_entry_present) {
		/* virtual call */
		mam_update_leaf_entry(entry_to_retract,
			tgt_addr,
			attr,
			level_ops,
			entry_ops);
	} else {
		/* virtual call */
		mam_invalidate_entry(entry_to_retract, reason,
			mam_get_leaf_entry_type(entry_ops));
	}

	/* Second: destroy the old (detached) table */
	mam_destroy_table(table_pointed_by_entry);
}

static
uint64_t mam_get_size_covered_by_level1_entry(void)
{
	uint64_t retval = PAGE_4KB_SIZE;

	return retval;
}

static
uint64_t mam_get_size_covered_by_level2_entry(void)
{
	uint64_t retval = mam_get_size_covered_by_level1_table();

	return retval;
}

static
uint64_t mam_get_size_covered_by_level3_entry(void)
{
	uint64_t retval = mam_get_size_covered_by_level2_table();

	return retval;
}

static
uint64_t mam_get_size_covered_by_level4_entry(void)
{
	uint64_t retval = mam_get_size_covered_by_level3_table();

	return retval;
}

static
uint32_t mam_get_level1_entry_index(IN uint64_t address)
{
	uint64_t idx_tmp =
		((address >> MAM_LEVEL1_TABLE_POS) & MAM_ENTRY_INDEX_MASK);

	return (uint32_t)idx_tmp;
}

static
uint32_t mam_get_level2_entry_index(IN uint64_t address)
{
	uint64_t idx_tmp =
		((address >> MAM_LEVEL2_TABLE_POS) & MAM_ENTRY_INDEX_MASK);

	return (uint32_t)idx_tmp;
}

static
uint32_t mam_get_level3_entry_index(IN uint64_t address)
{
	uint64_t idx_tmp =
		((address >> MAM_LEVEL3_TABLE_POS) & MAM_ENTRY_INDEX_MASK);

	return (uint32_t)idx_tmp;
}

static
uint32_t mam_get_level4_entry_index(IN uint64_t address)
{
	uint64_t idx_tmp =
		((address >> MAM_LEVEL4_TABLE_POS) & MAM_ENTRY_INDEX_MASK);

	return (uint32_t)idx_tmp;
}

static
const mam_level_ops_t *mam_get_non_existing_ops(void)
{
	return NULL;
}

static
const mam_level_ops_t *mam_get_level1_ops(void)
{
	return MAM_LEVEL1_OPS;
}

static
const mam_level_ops_t *mam_get_level2_ops(void)
{
	return MAM_LEVEL2_OPS;
}

static
const mam_level_ops_t *mam_get_level3_ops(void)
{
	return MAM_LEVEL3_OPS;
}

static
const mam_level_ops_t *mam_get_level4_ops(void)
{
	return MAM_LEVEL4_OPS;
}

static
const mam_entry_ops_t *mam_get_entry_ops(IN mam_entry_t *entry)
{
	uint32_t entry_type = get_mam_entry_type(entry);

	switch (entry_type) {
	case MAM_INNER_INTERNAL_ENTRY:
	case MAM_LEAF_INTERNAL_ENTRY:
		return MAM_INTERNAL_ENTRY_OPS;

	case MAM_INNER_PAGE_TABLE_ENTRY:
	case MAM_LEAF_PAGE_TABLE_ENTRY:
		return MAM_PAGE_TABLE_ENTRY_OPS;

	case MAM_INNER_EPT_ENTRY:
	case MAM_LEAF_EPT_ENTRY:
		return MAM_EPT_ENTRY_OPS;

	case MAM_INNER_VTDPT_ENTRY:
	case MAM_LEAF_VTDPT_ENTRY:
		return MAM_VTDPT_ENTRY_OPS;

	default:
		MON_LOG(mask_anonymous,
			level_trace,
			"Entry=%P (at %P) type=0x%x\n",
			entry->uint64,
			entry,
			get_mam_entry_type(entry));
		MON_ASSERT(0);
	}
	return NULL;
}

static
mam_mapping_result_t mam_get_mapping_from_table(
	IN const mam_level_ops_t *level_ops,
	IN mam_hav_t table,
	IN uint64_t src_addr,
	OUT uint64_t *tgt_addr_out,
	OUT mam_attributes_t *
	attributes_out)
{
	uint32_t entry_index;
	mam_hav_t entry_addr;
	mam_entry_t *entry;
	const mam_entry_ops_t *entry_ops;
	mam_hav_t lower_level_table;
	const mam_level_ops_t *lower_level_ops;

	/* Call to virtual function that calculates the entry index within the
	 * table out of requested source address */
	entry_index = mam_get_entry_index(level_ops, src_addr);

	/* Calculate the HVA of the entry */
	entry_addr = table + (entry_index * sizeof(mam_entry_t));

	/* Convert HVA to pointer */
	entry = (mam_entry_t *)mam_hva_to_ptr(entry_addr);

	/* Retrieve the virtual table with appropriate operations according to type
	 * of the entry */
	entry_ops = mam_get_entry_ops(entry);

	if (!mam_is_entry_present(entry, entry_ops)) {
		/* The entry is not present, return the reason */
		return entry->invalid_entry.high_part.reason;
	}

	if (mam_is_leaf_entry(entry)) {
		/* It is the final (leaf) entry, retrieve the mapped address and
		 * attributes from the entry */
		uint64_t tgt_addr;
		mam_attributes_t attrs;
		uint64_t offset;
		uint64_t size_covered_by_entry;

		/* Use virtual functions to retrieve the address and attributes */
		tgt_addr = mam_get_address_from_leaf_entry(entry,
			level_ops,
			entry_ops);
		attrs = mam_get_attributes_from_entry(entry,
			level_ops,
			entry_ops);

		/* Calculate the offset for the requested address */
		size_covered_by_entry =
			mam_get_size_covered_by_entry(level_ops);
		offset = src_addr & (size_covered_by_entry - 1);

		/* Set the output parameters */
		*tgt_addr_out = tgt_addr + offset;
		*attributes_out = attrs;

		return MAM_MAPPING_SUCCESSFUL;
	}

	/* The entry is not leaf (final), there is table pointed by it.
	 * Retrieve the HVA of this table and appropriate ops using virtual
	 * function */

	lower_level_table = mam_get_table_pointed_by_entry(entry, entry_ops);
	lower_level_ops = mam_get_lower_level_ops(level_ops);

	/* Recursive call in order to retrieve the address and attributes */
	return mam_get_mapping_from_table(lower_level_ops, lower_level_table,
		src_addr, tgt_addr_out, attributes_out);
}

/* Note, before destroying table, make sure no reference to the table exists,
 * (no entry points to this table) */
static
void mam_destroy_table(IN mam_hav_t table)
{
	const uint64_t entry_increment = sizeof(mam_entry_t);
	mam_hav_t entry_hva;
	void *table_ptr;
	const mam_entry_ops_t *entry_ops;
	mam_entry_t *first_entry;

	entry_hva = table;
	table_ptr = mam_hva_to_ptr(table);

	/* Retrieve the entry_ops from first entry */
	first_entry = (mam_entry_t *)table_ptr;
	entry_ops = mam_get_entry_ops(first_entry);

	/* Go all over the entries in current table and destroy recursively all
	 * tables pointed by the entries */
	while (entry_hva <
	       (table + (entry_increment * MAM_NUM_OF_ENTRIES_IN_TABLE))) {
		mam_entry_t *entry = (mam_entry_t *)mam_hva_to_ptr(entry_hva);

		if (!mam_is_leaf_entry(entry)) {
			/* There is additional table pointed by this entry, destroy it
			 * recursively */
			/* virtual call */
			mam_hav_t lower_level_table =
				mam_get_table_pointed_by_entry(entry,
					entry_ops);

			/* Recursive call */
			mam_destroy_table(lower_level_table);
		}

		entry_hva += entry_increment;
	}

	mon_memory_free(table_ptr);
}

/* -----------------------------------------------------------------------
 * Function: mam_update_table
 * Description: The function recursively finds the entries that must be
 *              updated and updates it according to provided information
 * Input: mam - main mam_t structure
 *        level_ops - virtual table for relevant table operations
 *        table - HVA of the table to update
 *        first_mapped_address - first source address that is mapped through
 *                               this table
 *        src_addr - source address of range to map
 *        tgt_addr - target address
 *        size - size of range
 *        attrs - attributes update_op - which update operation must be done
 * Return value - TRUE in case of success, FALSE in case of insufficient
 *                memory
 * -----------------------------------------------------------------------*/
static
boolean_t mam_update_table(IN mam_t *mam,
			   IN const mam_level_ops_t *level_ops,
			   IN mam_hav_t table,
			   IN uint64_t first_mapped_address,
			   IN uint64_t src_addr,
			   IN uint64_t tgt_addr,
			   IN uint64_t size,
			   IN mam_attributes_t attrs,
			   IN mam_update_op_t update_op)
{
	uint32_t curr_entry_index;
	uint32_t final_entry_index;
	uint64_t curr_entry_first_mapped_address;
	/* virtual call */
	uint64_t size_covered_by_entry =
		mam_get_size_covered_by_entry(level_ops);
	uint64_t remaining_size;
	const mam_entry_ops_t *entry_ops;
	/* virtual call */
	const mam_level_ops_t *lower_level_ops = mam_get_lower_level_ops(
		level_ops);

	/* Internal checks */
	MON_ASSERT(mam_get_size_covered_by_table(level_ops) >= size);
	MON_ASSERT(first_mapped_address <= src_addr);
	MON_ASSERT((first_mapped_address +
		    mam_get_size_covered_by_table(level_ops)) >=
		(src_addr + size));

	/* Retrieve the indeces of the entries within the table to be updated
	 * virtual call */
	curr_entry_index = mam_get_entry_index(level_ops, src_addr);
	/* virtual call */
	final_entry_index = mam_get_entry_index(level_ops, src_addr + size - 1);

	/* Retrieve the first mapped address of the first entry */
	curr_entry_first_mapped_address =
		first_mapped_address +
		(curr_entry_index * size_covered_by_entry);
	remaining_size = size;

	/* retrieve entry_ops from first entry of the table */
	entry_ops = mam_get_entry_ops(mam_hva_to_ptr(table));

	/* Go over the entries that map the requested range */
	while (curr_entry_index <= final_entry_index) {
		mam_hav_t entry_hva = table +
				      (curr_entry_index * sizeof(mam_entry_t));
		mam_entry_t *entry = (mam_entry_t *)mam_hva_to_ptr(entry_hva);
		uint64_t size_covered_by_curr_entry;
		uint64_t max_possible_size_for_entry =
			curr_entry_first_mapped_address +
			size_covered_by_entry - src_addr;
		boolean_t check_for_retraction = FALSE;
		boolean_t is_entry_present = mam_is_entry_present(entry,
			entry_ops);

		/* choose the minimum between the sizes */
		size_covered_by_curr_entry =
			(remaining_size >
			 max_possible_size_for_entry) ?
			max_possible_size_for_entry :
			remaining_size;

		if (update_op == MAM_OVERWRITE_ADDR_AND_ATTRS) {
			mam_hav_t lower_level_table;

			if (mam_can_be_leaf_entry(mam, level_ops,
				    size_covered_by_curr_entry,
				    tgt_addr, entry_ops)) {
				/* The entry in the current level can be leaf entry */

				MON_ASSERT(
					curr_entry_first_mapped_address ==
					src_addr);

				/* The overwrite, destroy all the subtrees of this entry and
				 * overwrite its contents */
				if (is_entry_present) {
					if (!mam_is_leaf_entry(entry)) {
						/* virtual call */
						lower_level_table =
							mam_get_table_pointed_by_entry(
								entry,
								entry_ops);

						/* first clear the current entry */
						mam_invalidate_entry(entry,
							MAM_UNKNOWN_MAPPING,
							MAM_LEAF_INTERNAL_ENTRY);

						/* only then destroy the lower hierarchy */
						mam_destroy_table(
							lower_level_table);
					}
				}
				/* virtual call */
				mam_update_leaf_entry(entry,
					tgt_addr,
					attrs,
					level_ops,
					entry_ops);
				/* no need for retraction check */
				check_for_retraction = FALSE;
			} else {
				/* Non leaf entry */
				boolean_t res;

				MON_ASSERT(lower_level_ops != NULL);

				if (!is_entry_present) {
					/* The entry is not present, create sub tree (expansion of
					 * non present entry) */

					mam_mapping_result_t reason =
						entry->invalid_entry.high_part.
						reason;
					mam_entry_type_t entry_type =
						get_mam_entry_type(entry);

					lower_level_table = mam_create_table(
						reason,
						entry_type);
					if (lower_level_table ==
					    mam_ptr_to_hva(NULL)) {
						/* ERROR: memory allocation failed */
						MON_LOG(mask_anonymous,
							level_trace,
							"%s (line %d): ERROR: memory allocation has failed\n",
							__FUNCTION__,
							__LINE__);
						return FALSE;
					}

					mam_update_inner_level_entry(mam,
						entry,
						lower_level_table,
						level_ops,
						entry_ops);
					/* no need for retraction check */
					check_for_retraction = FALSE;

					/* The next step is to update lower level table */
				} else if (mam_is_leaf_entry(entry)) {
					/* Currently the entry is leaf, but it can not remain it
					 * any longer, expand the entry to lower level table */

					if (!mam_expand_leaf_entry
						    (mam, entry, level_ops,
						    entry_ops,
						    &lower_level_table)) {
						MON_LOG(mask_anonymous,
							level_trace,
							"%s (line %d): ERROR: memory allocation has failed\n",
							__FUNCTION__,
							__LINE__);
						return FALSE;
					}
					/* no need for retraction check */
					check_for_retraction = FALSE;

					/* The next step is to update lower level table */
				} else {
					/* This is already inner level entry (non-leaf), retrieve
					 * the table pointed by this entry */

					lower_level_table =
						mam_get_table_pointed_by_entry(
							entry,
							entry_ops);
					/* it is possible that lower level table can be retracted
					 * to single entry */
					check_for_retraction = TRUE;

					/* The next step is to update lower level table */
				}

				/* Updating lower level table
				 * Recursive call for lower level table */
				res =
					mam_update_table(mam,
						lower_level_ops,
						lower_level_table,
						curr_entry_first_mapped_address,
						src_addr,
						tgt_addr,
						size_covered_by_curr_entry,
						attrs,
						update_op);
				if (!res) {
					MON_LOG(mask_anonymous,
						level_trace,
						"%s (line %d): ERROR: memory allocation has failed\n",
						__FUNCTION__,
						__LINE__);
					return FALSE;
				}
			}
		} else {
			MON_ASSERT((update_op == MAM_SET_ATTRS)
				|| (update_op == MAM_CLEAR_ATTRS)
				|| (update_op == MAM_OVERWRITE_ATTRS));
			MON_ASSERT((attrs.paging_attr.pat_index == 0)
				|| (attrs.ept_attr.emt == 0));

			if (is_entry_present) {
				/* Only if the entry is present, update of permissions can
				 * change something */

				if (mam_is_leaf_entry(entry)) {
					/* This is leaf entry */

					mam_attributes_t curr_entry_attrs;
					mam_attributes_t new_attrs;
					boolean_t attrs_are_updated = FALSE;

					/* virtual call */
					curr_entry_attrs =
						mam_get_attributes_from_entry(
							entry,
							level_ops,
							entry_ops);

					if (update_op == MAM_SET_ATTRS) {
						/* Check whether the attributes are already set */
						new_attrs.uint32 =
							curr_entry_attrs.uint32
							| attrs.uint32;
						attrs_are_updated =
							(new_attrs.uint32 ==
							 curr_entry_attrs.uint32);
					} else if (update_op ==
						   MAM_OVERWRITE_ATTRS) {
						new_attrs.uint32 =
							curr_entry_attrs.uint32;
						new_attrs.ept_attr.readable =
							attrs.ept_attr.readable;
						new_attrs.ept_attr.writable =
							attrs.ept_attr.writable;
						new_attrs.ept_attr.executable =
							attrs.ept_attr.
							executable;
						new_attrs.ept_attr.suppress_ve =
							attrs.ept_attr.
							suppress_ve;
						attrs_are_updated =
							(new_attrs.uint32 ==
							 curr_entry_attrs.uint32);
					} else {
						MON_ASSERT(
							update_op ==
							MAM_CLEAR_ATTRS);
						/* Check whether the attributes are already cleared */
						new_attrs.uint32 =
							curr_entry_attrs.uint32
							& (~(attrs.uint32));
						attrs_are_updated =
							(new_attrs.uint32 ==
							 curr_entry_attrs.uint32);
					}

					if (!attrs_are_updated) {
						/* The attributess must be updated, */
						/* check whether the entry may remain leaf or must be
						 * expanded */

						if (size_covered_by_curr_entry
						    == size_covered_by_entry) {
							/* The entry may remain leaf */

							MON_ASSERT(curr_entry_first_mapped_address ==
								src_addr);
							mam_update_attributes_in_leaf_entry(
								entry,
								new_attrs,
								level_ops,
								entry_ops);
						} else {
							/* The entry can not remain leaf any longer */

							mam_hav_t
								lower_level_table;
							boolean_t res;

							/* Expand leaf entry to lower level table */
							res =
								mam_expand_leaf_entry(
									mam,
									entry,
									level_ops,
									entry_ops,
									&lower_level_table);
							if (!res) {
								MON_LOG(
									mask_anonymous,
									level_trace,
									"%s (line %d): ERROR: memory allocation"
									" has failed\n",
									__FUNCTION__,
									__LINE__);
								return FALSE;
							}

							/* Update the attributes in lower level table.
							 * Recursive call */
							res =
								mam_update_table(
									mam,
									lower_level_ops,
									lower_level_table,
									curr_entry_first_mapped_address,
									src_addr,
									tgt_addr,
									size_covered_by_curr_entry,
									attrs,
									update_op);
							if (!res) {
								MON_LOG(
									mask_anonymous,
									level_trace,
									"%s (line %d): ERROR: memory allocation"
									" has failed\n",
									__FUNCTION__,
									__LINE__);
								return FALSE;
							}
						}
					}
					/* no need for retraction check */
					check_for_retraction = FALSE;
				} else {
					/* Non leaf entry
					 * Update the attributes in lower level table */

					mam_hav_t lower_level_table;
					boolean_t res;

					/* Retrieve the table pointed by current entry */
					lower_level_table =
						mam_get_table_pointed_by_entry(
							entry,
							entry_ops);

					/* Update the attributes in lower level table. Recursive
					 * call */
					res =
						mam_update_table(mam,
							lower_level_ops,
							lower_level_table,
							curr_entry_first_mapped_address,
							src_addr,
							tgt_addr,
							size_covered_by_curr_entry,
							attrs,
							update_op);
					if (!res) {
						MON_LOG(mask_anonymous,
							level_trace,
							"%s (line %d): ERROR: memory allocation"
							" has failed\n",
							__FUNCTION__,
							__LINE__);
						return FALSE;
					}
					/* Check whether the lower level table can be
					 * retracted to single entry */
					check_for_retraction = TRUE;
				}
			}
		}

		if (check_for_retraction) {
			/* There is a chance that table pointed by current inner level
			 * entry can be retracted to single entry */
			mam_try_to_retract_inner_entry_to_leaf(mam,
				entry,
				level_ops,
				entry_ops);
		}

		curr_entry_index++;
		curr_entry_first_mapped_address += size_covered_by_entry;
		MON_ASSERT(remaining_size >= size_covered_by_curr_entry);
		remaining_size -= size_covered_by_curr_entry;
		src_addr += size_covered_by_curr_entry;
		tgt_addr += size_covered_by_curr_entry;
	}

	return TRUE;
}

/* -----------------------------------------------------------------------
 * Function: mam_remove_range_from_table
 * Description: The function recursively finds the entries that must
 *              be updated and updates it according to provided information
 * Input: mam - main mam_t structure
 *        level_ops - virtual table for relevant table operations
 *        table - HVA of the table to update
 *        first_mapped_address - first source address that is mapped through
 *                               this table
 *        src_addr - source address of range to remove
 *        size - size of range
 *        reason - reason of removal
 * Return value - TRUE in case of success, FALSE in case of insufficient memory
 * -----------------------------------------------------------------------*/
static
boolean_t mam_remove_range_from_table(IN mam_t *mam,
				      IN const mam_level_ops_t *level_ops,
				      IN mam_hav_t table,
				      IN uint64_t first_mapped_address,
				      IN uint64_t src_addr,
				      IN uint64_t size,
				      IN mam_mapping_result_t reason)
{
	uint32_t curr_entry_index;
	uint32_t final_entry_index;
	uint64_t curr_entry_first_mapped_address;
	/* virtual call */
	uint64_t size_covered_by_entry =
		mam_get_size_covered_by_entry(level_ops);
	uint64_t remaining_size;
	const mam_entry_ops_t *entry_ops;
	/* virtual call */
	const mam_level_ops_t *lower_level_ops = mam_get_lower_level_ops(
		level_ops);

	MON_ASSERT(mam_get_size_covered_by_table(level_ops) >= size);
	MON_ASSERT(first_mapped_address <= src_addr);
	MON_ASSERT((first_mapped_address +
		    mam_get_size_covered_by_table(level_ops)) >=
		(src_addr + size));

	/* Retrieve the indeces of the entries within the table to be updated */
	/* virtual call */
	curr_entry_index = mam_get_entry_index(level_ops, src_addr);
	/* virtual call */
	final_entry_index = mam_get_entry_index(level_ops, src_addr + size - 1);

	/* Retrieve the first mapped address of the first entry */
	curr_entry_first_mapped_address =
		first_mapped_address +
		(curr_entry_index * size_covered_by_entry);
	remaining_size = size;

	/* retrieve entry_ops from first entry of the table */
	entry_ops = mam_get_entry_ops(mam_hva_to_ptr(table));

	/* Go over the relevant entries and update them appropriately */
	while (curr_entry_index <= final_entry_index) {
		mam_hav_t entry_hva = table +
				      (curr_entry_index * sizeof(mam_entry_t));
		mam_entry_t *entry = (mam_entry_t *)mam_hva_to_ptr(entry_hva);
		uint64_t size_covered_by_curr_entry;
		uint64_t max_possible_size_for_entry =
			curr_entry_first_mapped_address +
			size_covered_by_entry - src_addr;
		boolean_t check_for_retraction = FALSE;
		mam_hav_t lower_level_table;
		/* virtual call */
		mam_entry_type_t invalid_entry_type = mam_get_leaf_entry_type(
			entry_ops);

		/* choose the minimum between the sizes */
		size_covered_by_curr_entry =
			(remaining_size >
			 max_possible_size_for_entry) ?
			max_possible_size_for_entry :
			remaining_size;

		/* Check if entry can be leaf */
		if ((curr_entry_first_mapped_address == src_addr) &&
		    (size_covered_by_entry == size_covered_by_curr_entry)) {
			/* The overwrite, destroy all the subtrees of this entry and
			 * overwrite its contents */
			/* virtual call */
			if ((mam_is_entry_present(entry, entry_ops)) &&
			    /* virtual call */
			    (!mam_is_leaf_entry(entry))) {
				/* virtual call */
				lower_level_table =
					mam_get_table_pointed_by_entry(entry,
						entry_ops);

				/* first clear the current entry */
				mam_invalidate_entry(entry,
					reason,
					invalid_entry_type);
				/* second: destroy the lower hierarchy */
				mam_destroy_table(lower_level_table);
			} else {
				mam_invalidate_entry(entry,
					reason,
					invalid_entry_type);
			}
			/* no need for retraction check */
			check_for_retraction = FALSE;
		} else {
			/* Non leaf entry */
			boolean_t res;

			MON_ASSERT(lower_level_ops != NULL);

			if (!mam_is_entry_present(entry, entry_ops)) {
				/* The entry is invalidated already */
				mam_mapping_result_t curr_reason =
					entry->invalid_entry.high_part.reason;

				/* Check whether the requested reason differs with existing one
				 */

				if (curr_reason != reason) {
					/* The reasons differ, expand the existing invalid entry */

					mam_entry_type_t entry_type =
						get_mam_entry_type(entry);

					lower_level_table =
						mam_create_table(curr_reason,
							entry_type);
					if (lower_level_table ==
					    mam_ptr_to_hva(NULL)) {
						/* ERROR: memory allocation failed */
						MON_LOG(mask_anonymous,
							level_trace,
							"%s (line %d): ERROR: memory allocation"
							" has failed\n",
							__FUNCTION__,
							__LINE__);
						return FALSE;
					}

					mam_update_inner_level_entry(mam,
						entry,
						lower_level_table,
						level_ops,
						entry_ops);

					/* Updating lower level table
					 * Recursive call for lower level table */
					res =
						mam_remove_range_from_table(mam,
							lower_level_ops,
							lower_level_table,
							curr_entry_first_mapped_address,
							src_addr,
							size_covered_by_curr_entry,
							reason);
					if (!res) {
						MON_LOG(mask_anonymous,
							level_trace,
							"%s (line %d): ERROR: memory allocation has failed\n",
							__FUNCTION__,
							__LINE__);
						return FALSE;
					}
					/* no need for retraction check */
					check_for_retraction = FALSE;

					/* The next step is to update lower level table */
				} else {
					/* The entry has already the same invalidation reason */
					/* no need for retraction check */
					check_for_retraction = FALSE;
				}
			} else {
				/* The entry is present */

				if (mam_is_leaf_entry(entry)) {
					if (!mam_expand_leaf_entry(mam,
						    entry,
						    level_ops,
						    entry_ops,
						    &lower_level_table)) {
						MON_LOG(mask_anonymous,
							level_trace,
							"%s (line %d): ERROR: memory allocation has failed\n",
							__FUNCTION__,
							__LINE__);
						return FALSE;
					}
					/* no need for retraction check */
					check_for_retraction = FALSE;
					/* The next step is to update lower level table */
				} else {
					lower_level_table =
						mam_get_table_pointed_by_entry(
							entry,
							entry_ops);
					/* it is possible that lower level table can be retracted
					 * to single entry */
					check_for_retraction = TRUE;

					/* The next step is to update lower level table */
				}

				/* Updating lower level table
				 * Recursive call for lower level table */
				res =
					mam_remove_range_from_table(mam,
						lower_level_ops,
						lower_level_table,
						curr_entry_first_mapped_address,
						src_addr,
						size_covered_by_curr_entry,
						reason);
				if (!res) {
					MON_LOG(mask_anonymous,
						level_trace,
						"%s (line %d): ERROR: memory allocation has failed\n",
						__FUNCTION__,
						__LINE__);
					return FALSE;
				}
			}
		}

		if (check_for_retraction) {
			/* There is a chance that lower level table can be retracted to
			 * single entry, check it */
			mam_try_to_retract_inner_entry_to_leaf(mam,
				entry,
				level_ops,
				entry_ops);
		}

		curr_entry_index++;
		curr_entry_first_mapped_address += size_covered_by_entry;
		MON_ASSERT(remaining_size >= size_covered_by_curr_entry);
		remaining_size -= size_covered_by_curr_entry;
		src_addr += size_covered_by_curr_entry;
	}

	return TRUE;
}

/* -----------------------------------------------------------------------
 * Function: mam_convert_entries_in_table
 * Description: The function recursively converts entries in the tables
 *              from one type to another Used in order to convert internal
 *              type to page tables, ept or VT-d page tables entries
 * Input: mam - main mam_t structure
 *        table - HVA of the table to convert
 *        level_ops - virtual table for relevant table operations
 *        new_entry_ops - virtual table for the entries of new type
 * Return value - TRUE in case of success, FALSE in case of failure
 * -----------------------------------------------------------------------*/
static
boolean_t mam_convert_entries_in_table(IN mam_t *mam,
				       IN mam_hav_t table,
				       IN const mam_level_ops_t *level_ops,
				       IN const mam_entry_ops_t *new_entry_ops)
{
	mam_hav_t entry_hva;
	/* virtual call */
	uint64_t size_covered_by_entry =
		mam_get_size_covered_by_entry(level_ops);

	entry_hva = table;
	/* Go over all entries in the table and convert them */
	while (entry_hva < (table + PAGE_4KB_SIZE)) {
		mam_entry_t *entry = mam_hva_to_ptr(entry_hva);
		const mam_entry_ops_t *curr_entry_ops =
			mam_get_entry_ops(entry);
		mam_hav_t lower_level_table;
		const mam_level_ops_t *lower_level_ops;
		boolean_t res;

		/* if the entry is not converted yet */
		if (curr_entry_ops != new_entry_ops) {
			/* virtual call */
			if (!mam_is_entry_present(entry, curr_entry_ops)) {
				/* The entry is invalid, just change the type of the entry */
				mam_mapping_result_t reason =
					entry->invalid_entry.high_part.reason;
				mam_entry_type_t new_entry_type =
					mam_get_leaf_entry_type(new_entry_ops);

				/* suppress #VE for not present memory in Hardware #VE
				 * supported system */
				if (new_entry_ops == MAM_EPT_ENTRY_OPS) {
					entry->invalid_entry.high_part.
					suppress_ve =
						mam->ept_hw_ve_support;
				}

				mam_invalidate_entry(entry,
					reason,
					new_entry_type);
			} else if (mam_is_leaf_entry(entry)) {
				/* This is the leaf entry */
				uint64_t tgt_addr;
				mam_attributes_t attr;

				/* Retrieve the mapping information */
				/* virtual call */
				tgt_addr = mam_get_address_from_leaf_entry(
					entry,
					level_ops,
					curr_entry_ops);
				/* virtual call */
				attr = mam_get_attributes_from_entry(entry,
					level_ops,
					curr_entry_ops);

				if (new_entry_ops == MAM_VTDPT_ENTRY_OPS) {
					attr.vtdpt_attr.snoop =
						mam->vtdpt_snoop_behavior;
					attr.vtdpt_attr.tm =
						mam->vtdpt_trans_mapping;
				}

				/* Check whether entry of new type can remain leaf */
				if (mam_can_be_leaf_entry(mam,
					    level_ops,
					    size_covered_by_entry,
					    tgt_addr,
					    new_entry_ops)) {
					mam_update_leaf_entry(entry,
						tgt_addr,
						attr,
						level_ops,
						new_entry_ops);
				} else {
					/* The entry of new type can not remain leaf, must be
					 * expanded */

					res = mam_expand_leaf_entry(mam,
						entry,
						level_ops,
						curr_entry_ops,
						&lower_level_table);
					if (!res) {
						return FALSE;
					}

					lower_level_ops =
						mam_get_lower_level_ops(
							level_ops);
					MON_ASSERT(lower_level_ops != NULL);

					/* Convert entries in the lower level table. Recursive
					 * call. */
					res = mam_convert_entries_in_table(mam,
						lower_level_table,
						lower_level_ops,
						new_entry_ops);
					if (!res) {
						return FALSE;
					}

					/* Update the current entry with new information */
					mam_update_inner_level_entry(mam,
						entry,
						lower_level_table,
						level_ops,
						new_entry_ops);
				}
			} else {
				/* The entry is of inner level (non-leaf) */

				MON_ASSERT(mam_is_entry_present(entry,
						curr_entry_ops));

				/* Retrieve information of lower level table */
				lower_level_table =
					mam_get_table_pointed_by_entry(entry,
						curr_entry_ops);
				lower_level_ops = mam_get_lower_level_ops(
					level_ops);
				MON_ASSERT(lower_level_ops != NULL);

				/* Convert entries in the lower level table. Recursive call */
				res = mam_convert_entries_in_table(mam,
					lower_level_table,
					lower_level_ops,
					new_entry_ops);
				if (!res) {
					return FALSE;
				}

				/* Update the current entry with new information */
				mam_update_inner_level_entry(mam,
					entry,
					lower_level_table,
					level_ops,
					new_entry_ops);
			}
		}

		MON_ASSERT(mam_get_entry_ops(entry) == new_entry_ops);

		entry_hva += sizeof(mam_entry_t);
	}

	return TRUE;
}

/* -----------------------------------------------------------------------
 * Function: mam_update_first_table_to_cover_requested_range
 * Description: The function adds upper level tables for whole hierarchy
 *              if order to cover requested range
 * Input: mam - main mam_t structure
 *        src_addr - initial address of the range
 *        size - size of the range
 * -----------------------------------------------------------------------*/
static
void mam_update_first_table_to_cover_requested_range(IN mam_t *mam,
						     IN uint64_t src_addr,
						     IN uint64_t size)
{
	const mam_entry_ops_t *entry_ops;
	mam_hav_t curr_first_table;

	/* The ranges are supported up to level4 */
	MON_ASSERT(mam_get_size_covered_by_table(MAM_LEVEL4_OPS) >=
		(src_addr + size));

	curr_first_table = mam->first_table;

#ifdef DEBUG
	{
		/* Only internal representation can be extended */
		mam_entry_t *entry_tmp = mam_hva_to_ptr(mam->first_table);
		MON_ASSERT((mam_get_size_covered_by_table(
				    mam->first_table_ops)
			    >=
			    (src_addr + size))
			|| (entry_tmp->any_entry.avl == MAM_INNER_INTERNAL_ENTRY)
			|| (entry_tmp->any_entry.avl ==
			    MAM_LEAF_INTERNAL_ENTRY));
	}
#endif

	/* virtual call */
	entry_ops = mam_get_entry_ops(mam_hva_to_ptr(curr_first_table));
	while (mam_get_size_covered_by_table(mam->first_table_ops) <
	       (src_addr + size)) {
		/* Current first table doesn't cover the requested range */

		mam_hav_t new_first_table;
		mam_entry_t *first_entry_of_new_first_table;
		const mam_level_ops_t *new_first_table_ops;

		/* Create upper level table */
		new_first_table =
			mam_create_table(MAM_UNKNOWN_MAPPING,
				MAM_LEAF_INTERNAL_ENTRY);
		if (new_first_table == mam_ptr_to_hva(NULL)) {
			MON_LOG(mask_anonymous,
				level_trace,
				"%s (line %d): MAM ERROR: failed to allocate memory\n",
				__FUNCTION__,
				__LINE__);
			return;
		}

		/* Update the first entry in newly created table to point to existing
		 * first table */
		first_entry_of_new_first_table =
			mam_hva_to_ptr(new_first_table);
		new_first_table_ops = mam_get_upper_level_ops(
			mam->first_table_ops);

		mam_update_inner_level_entry(mam,
			first_entry_of_new_first_table,
			mam->first_table,
			new_first_table_ops,
			entry_ops);

		MON_ASSERT(new_first_table_ops != NULL);

		/* The newly created table is set to be first one */
		mam->first_table = new_first_table;
		mam->first_table_ops = (mam_level_ops_t *)new_first_table_ops;
	}
}

/* -----------------------------------------------------------------------
 * Function: mam_get_size_of_range
 * Description: The function is used in order to iterate over the ranges.
 * Input: src_addr - source address from which the count starts
 *        level_ops - virtual table for the "per level" functions
 *        table - table to look
 * Output: size - size of the range
 * -----------------------------------------------------------------------*/
static
void mam_get_size_of_range(IN uint64_t src_addr,
			   IN const mam_level_ops_t *level_ops,
			   IN mam_hav_t table, OUT uint64_t *size)
{
	/* virtual call */
	uint32_t start_index = mam_get_entry_index(level_ops, src_addr);
	mam_hav_t entry_hva = table + (start_index * sizeof(mam_entry_t));
	mam_entry_t *first_entry = mam_hva_to_ptr(entry_hva);
	uint32_t i;
	/* virtual call */
	uint64_t size_covered_by_entry =
		mam_get_size_covered_by_entry(level_ops);
	uint64_t size_tmp = size_covered_by_entry;
	uint64_t first_entry_tgt_addr = 0;
	mam_attributes_t first_entry_attrs = MAM_NO_ATTRIBUTES;
	mam_mapping_result_t first_entry_reason = MAM_MAPPING_SUCCESSFUL;
	const mam_entry_ops_t *entry_ops = mam_get_entry_ops(first_entry);
	/* virtual call */
	boolean_t is_first_entry_present = mam_is_entry_present(first_entry,
		entry_ops);

	/* Retrieve the information from the first entry of the range */
	if (is_first_entry_present) {
		if (!mam_is_leaf_entry(first_entry)) {
			/* The entry is of inner level, make a recursive call for the lower
			 * level table
			 * virtual call */
			mam_hav_t lower_level_table =
				mam_get_table_pointed_by_entry(first_entry,
					entry_ops);
			/* virtual call */
			const mam_level_ops_t *lower_level_ops =
				mam_get_lower_level_ops(level_ops);

			MON_ASSERT(lower_level_ops != NULL);
			MON_ASSERT(lower_level_table != mam_ptr_to_hva(NULL));

			/* Recursive call */
			mam_get_size_of_range(src_addr,
				lower_level_ops,
				lower_level_table,
				size);

			/* Output parameter "size" is updated in recursive call */
			return;
		}

		/* The entry is leaf, record the information of the first entry
		 * virtual call */
		first_entry_tgt_addr = mam_get_address_from_leaf_entry(
			first_entry, level_ops, entry_ops);
		/* virtual call */
		first_entry_attrs = mam_get_attributes_from_entry(first_entry,
			level_ops, entry_ops);
	} else {
		/* The entry is not present, record the reason */
		first_entry_reason =
			first_entry->invalid_entry.high_part.reason;
	}

	i = start_index + 1;
	entry_hva += sizeof(mam_entry_t);
	/* Go over the ramaining entries in the table and check whether they can be
	 * part of current range */
	while (i < MAM_NUM_OF_ENTRIES_IN_TABLE) {
		mam_entry_t *entry = mam_hva_to_ptr(entry_hva);

		MON_ASSERT(mam_get_entry_ops(entry) == entry_ops);

		if (is_first_entry_present) {
			uint64_t tgt_addr;
			mam_attributes_t attrs;

			if ((!mam_is_entry_present(entry, entry_ops)) ||
			    /* virtual call */
			    (!mam_is_leaf_entry(entry))) {
				/* end of the range; */
				break;
			}

			/* virtual call */
			tgt_addr = mam_get_address_from_leaf_entry(entry,
				level_ops,
				entry_ops);
			if (tgt_addr !=
			    first_entry_tgt_addr +
			    (size_covered_by_entry * (i - start_index))) {
				/* end of the range; */
				break;
			}

			/* virtual call */
			attrs = mam_get_attributes_from_entry(entry,
				level_ops,
				entry_ops);
			if (first_entry_attrs.uint32 != attrs.uint32) {
				/* end of the range; */
				break;
			}
		} else {
			mam_mapping_result_t curr_reason;

			/* virtual call */
			if (mam_is_entry_present(entry, entry_ops)) {
				/* end of the range; */
				break;
			}

			curr_reason = entry->invalid_entry.high_part.reason;
			if (curr_reason != first_entry_reason) {
				/* end of the range; */
				break;
			}
		}

		i++;
		entry_hva += sizeof(mam_entry_t);
		size_tmp += size_covered_by_entry;
	}

	*size = size_tmp;
}

static
void mam_clear_reserved_bits_in_pdpte(IN mam_entry_t *pdpte)
{
	pdpte->page_table_entry.writable = 0;
	pdpte->page_table_entry.user = 0;
	pdpte->page_table_entry.ps_or_pat = 0;
	pdpte->page_table_entry.global = 0;
	pdpte->page_table_entry.exb = 0;
}

static
void mam_clear_reserved_bits_in_pdpt(IN mam_hav_t pdpt)
{
	uint32_t i;

	for (i = 0; i < MAM_NUM_OF_PDPTES_IN_32_BIT_MODE; i++) {
		mam_hav_t pdpte_hva = pdpt + (i * sizeof(mam_entry_t));
		mam_entry_t *pdpte = mam_hva_to_ptr(pdpte_hva);

		mam_clear_reserved_bits_in_pdpte(pdpte);
	}
}

/*--------------------------------------------------------------------- */

mam_handle_t mam_create_mapping(mam_attributes_t inner_level_attributes)
{
	mam_t *mam = mon_memory_alloc(sizeof(mam_t));
	mam_hav_t first_table;

	/* Consistency checks */
	MON_ASSERT(sizeof(mam_attributes_t) ==
		sizeof(inner_level_attributes.uint32));

	if (mam == NULL) {
		goto return_null;
	}

	first_table =
		mam_create_table(MAM_UNKNOWN_MAPPING, MAM_LEAF_INTERNAL_ENTRY);
	if (first_table == mam_ptr_to_hva(NULL)) {
		goto deallocate_mam;
	}

	mam->first_table = first_table;
	/* No mapping exists */
	mam->first_table_ops = (mam_level_ops_t *)MAM_LEVEL1_OPS;
	mam->inner_level_attributes = inner_level_attributes;
	mam->ept_supper_page_support = 0;
	lock_initialize(&(mam->update_lock));
	mam->update_counter = 0;
	mam->update_on_cpu = 0;
	mam->is_32bit_page_tables = FALSE;
	mam->last_iterator = MAM_INVALID_MEMORY_RANGES_ITERATOR;
	mam->last_range_size = 0;

	return (mam_handle_t)mam;

deallocate_mam:
	mon_memory_free(mam);
return_null:
	return NULL;
}

void mon_mam_destroy_mapping(IN mam_handle_t mam_handle)
{
	mam_t *mam = (mam_t *)mam_handle;
	mam_hav_t first_table = mam->first_table;

	lock_acquire(&(mam->update_lock));
	mam_destroy_table(first_table);
	lock_release(&(mam->update_lock));
	mon_memory_free(mam);
}

mam_mapping_result_t mam_get_mapping(IN mam_handle_t mam_handle,
				     IN uint64_t src_addr,
				     OUT uint64_t *tgt_addr,
				     OUT mam_attributes_t *attrs)
{
	mam_t *mam = (mam_t *)mam_handle;
	const mam_level_ops_t *first_table_ops;
	uint64_t first_table;
	mam_mapping_result_t res;
	uint32_t update_counter1;
	uint32_t update_counter2;

	MON_ASSERT(mam_handle != MAM_INVALID_HANDLE);

	first_table_ops = mam->first_table_ops;
	first_table = mam->first_table;

	/* No range was inserted yet */
	if (first_table_ops == NULL) {
		return MAM_UNKNOWN_MAPPING;
	}

	/* If the src_addr is above the address covered by first table, return
	 * MAM_UNKNOWN_MAPPING */
	if (src_addr >= mam_get_size_covered_by_table(first_table_ops)) {
		return MAM_UNKNOWN_MAPPING;
	}

	/* Make sure that there was no update of inner tables during the query */
	do {
		update_counter1 = mam->update_counter;

		/* Retrieve the mapping */
		res = mam_get_mapping_from_table(first_table_ops,
			first_table,
			src_addr, tgt_addr, attrs);
		update_counter2 = mam->update_counter;
	} while ((update_counter1 != update_counter2) ||
		 (((update_counter2 & 0x1) != 0) &&
		  (hw_cpu_id() != mam->update_on_cpu))
		 /* must be even number in order to exit or query on the same
		  * cpu as update */
		 );

	return res;
}

boolean_t mam_insert_range(IN mam_handle_t mam_handle,
			   IN uint64_t src_addr,
			   IN uint64_t tgt_addr,
			   IN uint64_t size, IN mam_attributes_t attrs)
{
	mam_t *mam = (mam_t *)mam_handle;
	const mam_level_ops_t *first_table_ops = NULL;
	mam_hav_t first_table = 0;
	boolean_t res;

	if (mam_handle == MAM_INVALID_HANDLE) {
		return FALSE;
	}

	lock_acquire(&(mam->update_lock));

	mam->update_on_cpu = hw_cpu_id();
	/* first update (becomes odd number) */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) != 0);

	if ((src_addr & (PAGE_4KB_SIZE - 1)) ||
	    (tgt_addr & (PAGE_4KB_SIZE - 1)) ||
	    (size & (PAGE_4KB_SIZE - 1)) || (size == 0)) {
		/* Must be 4K aligned */
		MON_LOG(mask_anonymous, level_trace,
			"MAM ERROR: %s: Alignment error: src_addr=%P "
			"tgt_addr=%P size=%P\n",
			__FUNCTION__, src_addr, tgt_addr, size);
		res = FALSE;
		goto out;
	}

	if ((src_addr + size) > mam_get_size_covered_by_table(MAM_LEVEL4_OPS)) {
		MON_LOG(mask_anonymous, level_trace,
			"MAM ERROR: %s: Range exceeds permitted limit:"
			" src_addr=%P size=%P\n",
			__FUNCTION__, src_addr, size);
		res = FALSE;
		goto out;
	}

	mam_update_first_table_to_cover_requested_range(mam, src_addr, size);

	first_table = mam->first_table;
	first_table_ops = mam->first_table_ops;
	MON_ASSERT(first_table_ops != NULL);

	if ((src_addr + size) >
	    mam_get_size_covered_by_table(first_table_ops)) {
		MON_LOG(mask_anonymous, level_trace,
			"MAM ERROR: %s: Range exceeds permitted limit (2)\n",
			__FUNCTION__);
		res = FALSE;
		goto out;
	}

	res = mam_update_table(mam,
		first_table_ops,
		first_table,
		0,
		src_addr,
		tgt_addr, size, attrs, MAM_OVERWRITE_ADDR_AND_ATTRS);

	MON_DEBUG_CODE(
		if (!res) {
			MON_LOG(mask_anonymous, level_trace,
				"MAM ERROR: %s: Memory allocation error (2)\n",
				__FUNCTION__);
			}
		);

out:
	/* second update (becomes even number); */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) == 0);
	mam->update_on_cpu = MAM_INVALID_CPU_ID;
	lock_release(&(mam->update_lock));
	return res;
}

boolean_t mam_insert_not_existing_range(IN mam_handle_t mam_handle,
					IN uint64_t src_addr,
					IN uint64_t size,
					IN mam_mapping_result_t reason)
{
	mam_t *mam = (mam_t *)mam_handle;
	const mam_level_ops_t *first_table_ops = NULL;
	uint64_t first_table = 0;
	boolean_t res;

	if (mam_handle == MAM_INVALID_HANDLE) {
		return FALSE;
	}

	lock_acquire(&(mam->update_lock));

	mam->update_on_cpu = hw_cpu_id();
	/* first update (becomes odd number) */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) != 0);

	if ((src_addr & (PAGE_4KB_SIZE - 1)) || (size & (PAGE_4KB_SIZE - 1))) {
		/* Must be 4K aligned */
		res = FALSE;
		goto out;
	}

	if ((reason == MAM_MAPPING_SUCCESSFUL) ||
	    (reason == MAM_UNKNOWN_MAPPING)) {
		res = FALSE;
		goto out;
	}

	if ((src_addr + size) > mam_get_size_covered_by_table(MAM_LEVEL4_OPS)) {
		res = FALSE;
		goto out;
	}

	mam_update_first_table_to_cover_requested_range(mam, src_addr, size);

	first_table = mam->first_table;
	first_table_ops = mam->first_table_ops;
	MON_ASSERT(first_table_ops != NULL);

	if ((src_addr + size) >
	    mam_get_size_covered_by_table(first_table_ops)) {
		MON_LOG(mask_anonymous, level_trace,
			"MAM ERROR: %s: Range exceeds permitted limit (2)\n",
			__FUNCTION__);
		res = FALSE;
		goto out;
	}

	if ((src_addr + size) >
	    mam_get_size_covered_by_table(first_table_ops)) {
		res = FALSE;
		goto out;
	}

	res = mam_remove_range_from_table(mam,
		first_table_ops,
		first_table, 0, src_addr, size, reason);

out:
	/* second update (becomes even number); */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) == 0);
	mam->update_on_cpu = MAM_INVALID_CPU_ID;
	lock_release(&(mam->update_lock));
	return res;
}

boolean_t mam_add_permissions_to_existing_mapping(IN mam_handle_t mam_handle,
						  IN uint64_t src_addr,
						  IN uint64_t size,
						  IN mam_attributes_t attrs)
{
	mam_t *mam = (mam_t *)mam_handle;
	const mam_level_ops_t *first_table_ops = NULL;
	mam_hav_t first_table = 0;
	boolean_t res;

	if (mam_handle == MAM_INVALID_HANDLE) {
		return FALSE;
	}

	lock_acquire(&(mam->update_lock));

	mam->update_on_cpu = hw_cpu_id();
	/* first update (becomes odd number) */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) != 0);

	if ((src_addr & (PAGE_4KB_SIZE - 1)) || (size & (PAGE_4KB_SIZE - 1))) {
		/* Must be 4K aligned */
		res = FALSE;
		goto out;
	}

	first_table = mam->first_table;
	first_table_ops = mam->first_table_ops;
	MON_ASSERT(first_table_ops != NULL);

	res = mam_update_table(mam,
		first_table_ops,
		first_table,
		0,
		src_addr,
		MAM_INVALID_ADDRESS, size, attrs, MAM_SET_ATTRS);

out:
	/* second update (becomes even number); */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) == 0);
	mam->update_on_cpu = MAM_INVALID_CPU_ID;
	lock_release(&(mam->update_lock));
	return res;
}

boolean_t mam_remove_permissions_from_existing_mapping(
	IN mam_handle_t mam_handle,
	IN uint64_t src_addr,
	IN uint64_t size,
	IN mam_attributes_t attrs)
{
	mam_t *mam = (mam_t *)mam_handle;
	const mam_level_ops_t *first_table_ops = NULL;
	mam_hav_t first_table = 0;
	boolean_t res;

	if (mam_handle == MAM_INVALID_HANDLE) {
		return FALSE;
	}

	lock_acquire(&(mam->update_lock));

	mam->update_on_cpu = hw_cpu_id();
	/* first update (becomes odd number) */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) != 0);

	if ((src_addr & (PAGE_4KB_SIZE - 1)) || (size & (PAGE_4KB_SIZE - 1))) {
		/* Must be 4K aligned */
		res = FALSE;
		goto out;
	}

	first_table = mam->first_table;
	first_table_ops = mam->first_table_ops;
	MON_ASSERT(first_table_ops != NULL);

	res = mam_update_table(mam,
		first_table_ops,
		first_table,
		0,
		src_addr,
		MAM_INVALID_ADDRESS, size, attrs, MAM_CLEAR_ATTRS);
out:
	/* second update (becomes even number); */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) == 0);
	mam->update_on_cpu = MAM_INVALID_CPU_ID;
	lock_release(&(mam->update_lock));
	return res;
}

boolean_t mon_mam_overwrite_permissions_in_existing_mapping(
	IN mam_handle_t mam_handle,
	IN uint64_t src_addr,
	IN uint64_t size,
	IN mam_attributes_t attrs)
{
	mam_t *mam = (mam_t *)mam_handle;
	const mam_level_ops_t *first_table_ops = NULL;
	mam_hav_t first_table = 0;
	boolean_t res;

	if (mam_handle == MAM_INVALID_HANDLE) {
		return FALSE;
	}

	lock_acquire(&(mam->update_lock));

	mam->update_on_cpu = hw_cpu_id();
	/* first update (becomes odd number) */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) != 0);

	if ((src_addr & (PAGE_4KB_SIZE - 1)) || (size & (PAGE_4KB_SIZE - 1))) {
		/* Must be 4K aligned */
		res = FALSE;
		goto out;
	}

	first_table = mam->first_table;
	first_table_ops = mam->first_table_ops;
	MON_ASSERT(first_table_ops != NULL);

	res = mam_update_table(mam,
		first_table_ops,
		first_table,
		0,
		src_addr,
		MAM_INVALID_ADDRESS,
		size, attrs, MAM_OVERWRITE_ATTRS);
out:
	/* second update (becomes even number); */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) == 0);
	mam->update_on_cpu = MAM_INVALID_CPU_ID;
	lock_release(&(mam->update_lock));
	return res;
}

boolean_t mam_convert_to_64bit_page_tables(IN mam_handle_t mam_handle,
					   OUT uint64_t *pml4t_hpa)
{
	mam_t *mam = (mam_t *)mam_handle;
	mam_hpv_t first_table_hpa;
	boolean_t res;

	if (mam_handle == MAM_INVALID_HANDLE) {
		return FALSE;
	}

	lock_acquire(&(mam->update_lock));

	mam->update_on_cpu = hw_cpu_id();
	/* first update (becomes odd number) */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) != 0);

	/* the first table must be level4 table (PML4T) */
	mam_update_first_table_to_cover_requested_range(mam, 0,
		mam_get_size_covered_by_table
			(MAM_LEVEL4_OPS));

	if (mam->first_table_ops != MAM_LEVEL4_OPS) {
		MON_LOG(mask_anonymous,
			level_trace,
			"MAM ERROR: %s: Memory allocation error (1)\n",
			__FUNCTION__);
		res = FALSE;
		goto out;
	}

	if (!mam_convert_entries_in_table
		    (mam, mam->first_table, mam->first_table_ops,
		    MAM_PAGE_TABLE_ENTRY_OPS)) {
		res = FALSE;
		goto out;
	}

	first_table_hpa = mam_hva_to_hpa(mam->first_table);
	*pml4t_hpa = (uint64_t)first_table_hpa;

	res = TRUE;

out:
	/* second update (becomes even number); */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) == 0);
	mam->update_on_cpu = MAM_INVALID_CPU_ID;
	lock_release(&(mam->update_lock));
	return res;
}

boolean_t mam_convert_to_32bit_pae_page_tables(IN mam_handle_t mam_handle,
					       OUT uint32_t *pdpt_hpa)
{
	mam_t *mam = (mam_t *)mam_handle;
	mam_hpv_t first_table_hpa;
	boolean_t res;

	if (mam_handle == MAM_INVALID_HANDLE) {
		return FALSE;
	}

	lock_acquire(&(mam->update_lock));

	mam->update_on_cpu = hw_cpu_id();
	/* first update (becomes odd number) */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) != 0);

	/* the first table must be level3 table (PDPT) */
	mam_update_first_table_to_cover_requested_range(mam, 0,
		(uint64_t)4 GIGABYTES);

	if (mam->first_table_ops != MAM_LEVEL3_OPS) {
		MON_LOG(mask_anonymous,
			level_trace,
			"MAM ERROR: %s: Memory allocation error (1)\n",
			__FUNCTION__);
		res = FALSE;
		goto out;
	}

	if (!mam_convert_entries_in_table
		    (mam, mam->first_table, mam->first_table_ops,
		    MAM_PAGE_TABLE_ENTRY_OPS)) {
		res = FALSE;
		goto out;
	}

	mam_clear_reserved_bits_in_pdpt(mam->first_table);
	mam->is_32bit_page_tables = TRUE;

	first_table_hpa = mam_hva_to_hpa(mam->first_table);
	*pdpt_hpa = (uint32_t)first_table_hpa;
	res = TRUE;

out:
	/* second update (becomes even number); */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) == 0);
	mam->update_on_cpu = MAM_INVALID_CPU_ID;
	lock_release(&(mam->update_lock));
	return res;
}

mam_memory_ranges_iterator_t
mam_get_memory_ranges_iterator(IN mam_handle_t mam_handle)
{
	mam_t *mam = (mam_t *)mam_handle;
	const mam_level_ops_t *first_table_ops;

	if (mam_handle == MAM_INVALID_HANDLE) {
		return MAM_INVALID_MEMORY_RANGES_ITERATOR;
	}

	mam->last_iterator = MAM_INVALID_MEMORY_RANGES_ITERATOR;
	mam->last_range_size = 0;

	first_table_ops = mam->first_table_ops;

	if (first_table_ops == NULL) {
		return MAM_INVALID_MEMORY_RANGES_ITERATOR;
	}

	return (mam_memory_ranges_iterator_t)0;
}

mam_memory_ranges_iterator_t
mam_get_range_details_from_iterator(IN mam_handle_t mam_handle,
				    IN mam_memory_ranges_iterator_t iter,
				    OUT uint64_t *src_addr,
				    OUT uint64_t *size)
{
	mam_t *mam = (mam_t *)mam_handle;
	const mam_level_ops_t *first_table_ops;
	mam_hav_t first_table;
	uint64_t next_iter;
	uint32_t update_counter1;
	uint32_t update_counter2;

	*src_addr = ~((uint64_t)0x0);
	*size = 0;

	if (mam_handle == MAM_INVALID_HANDLE) {
		return MAM_INVALID_MEMORY_RANGES_ITERATOR;
	}

	if (iter == MAM_INVALID_MEMORY_RANGES_ITERATOR) {
		return MAM_INVALID_MEMORY_RANGES_ITERATOR;
	}

	first_table_ops = mam->first_table_ops;
	first_table = mam->first_table;

	MON_ASSERT(iter < mam_get_size_covered_by_table(first_table_ops));

	do {
		update_counter1 = mam->update_counter;
		*src_addr = (uint64_t)iter;
		mam_get_size_of_range((uint64_t)iter,
			first_table_ops,
			first_table,
			size);
		MON_ASSERT(*size > 0);

		next_iter = iter + *size;

		if (next_iter >=
		    mam_get_size_covered_by_table(first_table_ops)) {
			next_iter = MAM_INVALID_MEMORY_RANGES_ITERATOR;
		}

		update_counter2 = mam->update_counter;
	} while ((update_counter1 != update_counter2) ||
		/* must be even number */
		((update_counter2 & 0x1) != 0));

	mam->last_iterator = iter;
	mam->last_range_size = *size;

	return (mam_memory_ranges_iterator_t)next_iter;
}

uint64_t mam_get_range_start_address_from_iterator(IN mam_handle_t mam_handle
						   UNUSED,
						   IN mam_memory_ranges_iterator_t
						   iter)
{
	if (iter == MAM_INVALID_MEMORY_RANGES_ITERATOR) {
		return ~((uint64_t)0);
	}
	return (uint64_t)iter;
}

boolean_t mon_mam_convert_to_ept(IN mam_handle_t mam_handle,
				 IN mam_ept_super_page_support_t ept_super_page_support,
				 IN mam_ept_supported_gaw_t ept_supported_gaw,
				 IN boolean_t ept_hw_ve_support,
				 OUT uint64_t *first_table_hpa)
{
	mam_t *mam = (mam_t *)mam_handle;
	uint64_t requested_size = 0;
	const mam_level_ops_t *required_level_ops = NULL;
	boolean_t res;

	if (mam_handle == MAM_INVALID_HANDLE) {
		return FALSE;
	}

	lock_acquire(&(mam->update_lock));
	mam->update_on_cpu = hw_cpu_id();
	/* first update (becomes odd number) */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) != 0);

	if (ept_supported_gaw == MAM_EPT_21_BITS_GAW) {
		requested_size = 2 MEGABYTES;
		required_level_ops = MAM_LEVEL1_OPS;
	} else if (ept_supported_gaw == MAM_EPT_30_BITS_GAW) {
		requested_size = (uint64_t)1 GIGABYTE;
		required_level_ops = MAM_LEVEL2_OPS;
	} else if (ept_supported_gaw == MAM_EPT_39_BITS_GAW) {
		requested_size = (uint64_t)512 GIGABYTES;
		required_level_ops = MAM_LEVEL3_OPS;
	} else {
		MON_ASSERT(ept_supported_gaw == MAM_EPT_48_BITS_GAW);
		requested_size = mam_get_size_covered_by_table(MAM_LEVEL4_OPS);
		required_level_ops = MAM_LEVEL4_OPS;
	}

	mam_update_first_table_to_cover_requested_range(mam, 0, requested_size);

	if (mam->first_table_ops != required_level_ops) {
		res = FALSE;
		goto out;
	}

	/* Update super page support to what is requested */
	mam->ept_supper_page_support = ept_super_page_support;

	mam->ept_hw_ve_support = (uint8_t)ept_hw_ve_support;

	if (!mam_convert_entries_in_table
		    (mam, mam->first_table, mam->first_table_ops,
		    MAM_EPT_ENTRY_OPS)) {
		res = FALSE;
		goto out;
	}

	/* Update super page support to default lowest value */
	mam->ept_supper_page_support = 0;

	*first_table_hpa = mam_hva_to_hpa(mam->first_table);
	res = TRUE;

out:
	/* second update (becomes even number); */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) == 0);
	mam->update_on_cpu = MAM_INVALID_CPU_ID;
	lock_release(&(mam->update_lock));
	return res;
}

boolean_t mam_convert_to_vtdpt(IN mam_handle_t mam_handle,
			       IN mam_vtdpt_super_page_support_t
			       vtdpt_super_page_support,
			       IN mam_vtdpt_snoop_behavior_t vtdpt_snoop_behavior,
			       IN mam_vtdpt_trans_mapping_t vtdpt_trans_mapping,
			       IN uint32_t sagaw_bit_index,
			       OUT uint64_t *first_table_hpa)
{
	mam_t *mam = (mam_t *)mam_handle;
	uint64_t requested_size = 0;
	const mam_level_ops_t *required_level_ops = NULL;
	boolean_t res;
	mam_vtdpt_level_t vtdpt_level = (mam_vtdpt_level_t)sagaw_bit_index;

	if (mam_handle == MAM_INVALID_HANDLE) {
		return FALSE;
	}

	lock_acquire(&(mam->update_lock));
	mam->update_on_cpu = hw_cpu_id();
	/* first update (becomes odd number) */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) != 0);

	switch (vtdpt_level) {
	case MAM_VTDPT_LEVEL_2:
		requested_size = (uint64_t)mam_get_size_covered_by_table(
			MAM_LEVEL2_OPS);
		required_level_ops = MAM_LEVEL2_OPS;
		break;
	case MAM_VTDPT_LEVEL_3:
		requested_size = (uint64_t)mam_get_size_covered_by_table(
			MAM_LEVEL3_OPS);
		required_level_ops = MAM_LEVEL3_OPS;
		break;
	case MAM_VTDPT_LEVEL_4:
		requested_size =
			(uint64_t)mam_get_size_covered_by_table(MAM_LEVEL4_OPS);
		;
		required_level_ops = MAM_LEVEL4_OPS;
		break;
	default:
		MON_LOG(mask_anonymous, level_error,
			"ERROR: %s: unsupported page table level %d!\n",
			__FUNCTION__, vtdpt_level);
		MON_DEADLOOP();
	}

	mam_update_first_table_to_cover_requested_range(mam, 0, requested_size);

	if (mam->first_table_ops != required_level_ops) {
		res = FALSE;
		goto out;
	}

	/* Update super page support to what is requested */
	mam->vtdpt_supper_page_support = vtdpt_super_page_support;
	/* Update snoop behavior attribute */
	mam->vtdpt_snoop_behavior = vtdpt_snoop_behavior;
	/* Update transient mapping attibute */
	mam->vtdpt_trans_mapping = vtdpt_trans_mapping;
	if (!mam_convert_entries_in_table
		    (mam, mam->first_table, mam->first_table_ops,
		    MAM_VTDPT_ENTRY_OPS)) {
		res = FALSE;
		goto out;
	}

	*first_table_hpa = mam_hva_to_hpa(mam->first_table);
	res = TRUE;

out:
	/* second update (becomes even number); */
	mam->update_counter++;
	MON_ASSERT((mam->update_counter & 0x1) == 0);
	mam->update_on_cpu = MAM_INVALID_CPU_ID;
	lock_release(&(mam->update_lock));

	return res;
}
