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
#ifndef MEMORY_ADDRESS_MAPPER_H
#define MEMORY_ADDRESS_MAPPER_H

#include <mon_defs.h>
#include <lock.h>

typedef uint64_t mam_hav_t;
typedef uint64_t mam_hpv_t;

typedef union {
	struct {
		uint32_t reserved1:9;
		uint32_t avl:3;  /* This bits are available in all types of
				  * entries except VT-d page tables and used in
				  * order to identify type of entry
				  * (mam_entry_type_t) */
		uint32_t addr_low:20;
		uint32_t addr_high:8;
		uint32_t reserved2:24;
	} any_entry;

	struct {
		uint32_t present:1;
		uint32_t attributes:8;
		uint32_t reserved1:3; /* avl */
		uint32_t addr_low:20;
		uint32_t addr_high:8;
		uint32_t reserved2:24;
	} mam_internal_entry;

	struct {
		uint32_t present:1;
		uint32_t writable:1;
		uint32_t user:1;
		uint32_t pwt:1;
		uint32_t pcd:1;
		uint32_t reserved1:2;
		uint32_t ps_or_pat:1;
		uint32_t global:1;
		uint32_t avl:3;
		uint32_t addr_low:20;
		uint32_t addr_high:8;
		uint32_t reserved2:23;
		uint32_t exb:1;
	} page_table_entry;

	struct {
		uint32_t readable:1;
		uint32_t writable:1;
		uint32_t executable:1;
		uint32_t emt:3;
		uint32_t igmt:1;
		uint32_t sp:1;
		uint32_t avl:4;
		uint32_t addr_low:20;
		uint32_t addr_high:8;
		uint32_t reserved1:23;
		uint32_t suppress_ve:1;
	} ept_entry;

	struct {
		uint32_t readable:1;
		uint32_t writable:1;
		uint32_t reserved1:5;
		uint32_t sp:1;
		uint32_t avl_1:1;    /* set for leaf entry */
		uint32_t avl_2:2;    /* MAM_INNER_VTDPT_ENTRY */
		uint32_t snoop:1;
		uint32_t addr_low:20;
		uint32_t addr_high:8;
		uint32_t reserved2:22;
		uint32_t tm:1;
		uint32_t reserved3:1;
	} vtdpt_entry;

	struct {
		struct {
			uint32_t must_be_zero0:9;
			uint32_t avl:3;
			uint32_t must_be_zero1:20;
		} low_part;
		struct {
			uint32_t reason:31;
			uint32_t suppress_ve:1;
		} high_part;
	} invalid_entry;

	uint64_t uint64;
} mam_entry_t;

typedef enum {
	MAM_GENERAL_MAPPING,
	MAM_PAGE_TABLES_COMPLIANT_MAPPING,
	MAM_EPT_COMPLIANT_MAPPING
} mam_mapping_type_t;

typedef enum {
	MAM_PAGE_TABLE_ENTRY = 0x0,     /* 000 */
	MAM_EPT_ENTRY = 0x1,            /* 001 */
	MAM_INTERNAL_ENTRY = 0x2,       /* 010 */
	MAM_VTDPT_ENTRY = 0x3           /* 011 */
} mam_basic_entry_type_t;

#define MAM_INNER_ENTRY_TYPE_MASK 0x3   /* 011 */

#define MAM_LEAF_ENTRY_TYPE_MASK 0x4    /* 100 */

/* Note that this entry type will reside in three avl bits in entry. In oder to
 * check whether it is leaf just check the MSB (third bit) */
typedef enum {
	MAM_INNER_PAGE_TABLE_ENTRY = MAM_PAGE_TABLE_ENTRY,                      /* 000 */
	MAM_INNER_EPT_ENTRY = MAM_EPT_ENTRY,                                    /* 001 */
	MAM_INNER_INTERNAL_ENTRY = MAM_INTERNAL_ENTRY,                          /* 010 */
	MAM_INNER_VTDPT_ENTRY = MAM_VTDPT_ENTRY,                                /* 011 */
	MAM_LEAF_PAGE_TABLE_ENTRY =
		(MAM_PAGE_TABLE_ENTRY | MAM_LEAF_ENTRY_TYPE_MASK),              /* 100 */
	MAM_LEAF_EPT_ENTRY = (MAM_EPT_ENTRY | MAM_LEAF_ENTRY_TYPE_MASK),        /* 101 */
	MAM_LEAF_INTERNAL_ENTRY =
		(MAM_INTERNAL_ENTRY | MAM_LEAF_ENTRY_TYPE_MASK),                /* 110 */
	MAM_LEAF_VTDPT_ENTRY =
		(MAM_VTDPT_ENTRY | MAM_LEAF_ENTRY_TYPE_MASK)                    /* 111 */
} mam_entry_type_t;

typedef enum {
	MAM_OVERWRITE_ADDR_AND_ATTRS,
	MAM_SET_ATTRS,
	MAM_CLEAR_ATTRS,
	/* RTDBEUG */
	MAM_OVERWRITE_ATTRS,
} mam_update_op_t;

/*******************************************************************
*  The adjusted guest address widths corresponding to various bit indices of
*  SAGAW field are:
* 0: 30-bit AGAW (2-level page-table)
* 1: 39-bit AGAW (3-level page-table)
* 2: 48-bit AGAW (4-level page-table)
* 3: 57-bit AGAW (5-level page-table)
* 4: 64-bit AGAW (6-level page-table)
*******************************************************************/
typedef enum {
	MAM_VTDPT_LEVEL_2 = 0x0,
	MAM_VTDPT_LEVEL_3 = 0x1,
	MAM_VTDPT_LEVEL_4 = 0x2,
	MAM_VTDPT_LEVEL_5 = 0x3,
	MAM_VTDPT_LEVEL_6 = 0x4,
} mam_vtdpt_level_t;

/*------------------------------------------------------------------------ */

struct mam_t;
typedef struct mam_t mam_t;

struct mam_level_ops_t;
typedef struct mam_level_ops_t mam_level_ops_t;

struct mam_entry_ops_t;
typedef struct mam_entry_ops_t mam_entry_ops_t;

/*------------------------------------------------------------------------ */

typedef uint64_t (*func_mam_get_size_covered_by_entry_t) (void);
typedef uint32_t (*func_mam_get_entry_index_t) (uint64_t);
typedef const mam_level_ops_t *(*func_mam_get_lower_level_ops_t) (void);
typedef const mam_level_ops_t *(*func_mam_get_upper_level_ops_t) (void);

struct mam_level_ops_t {
	func_mam_get_size_covered_by_entry_t	mam_get_size_covered_by_entry_fn;
	func_mam_get_entry_index_t		mam_get_entry_index_fn;
	func_mam_get_lower_level_ops_t		mam_get_lower_level_ops_fn;
	func_mam_get_upper_level_ops_t		mam_get_upper_level_ops_fn;
};

/*------------------------------------------------------------------------ */

typedef uint64_t (*func_mam_get_address_from_leaf_entry_t) (mam_entry_t *,
							    const
							    mam_level_ops_t *);
typedef mam_attributes_t (*func_mam_get_attributes_from_entry_t) (mam_entry_t *,
								  const
								  mam_level_ops_t
*);
typedef mam_hav_t (*func_mam_get_table_pointed_by_entry_t) (mam_entry_t *);
typedef boolean_t (*func_mam_is_entry_present_t) (mam_entry_t *);
typedef boolean_t (*func_mam_can_be_leaf_entry_t) (mam_t *,
						   const mam_level_ops_t *,
						   uint64_t, uint64_t);
typedef void (*func_mam_update_leaf_entry_t) (mam_entry_t *, uint64_t,
					      mam_attributes_t,
					      const mam_level_ops_t *);
typedef void (*func_mam_update_innter_level_entry_t) (mam_t *, mam_entry_t *,
						      mam_hav_t,
						      const mam_level_ops_t *);
typedef void (*func_mam_update_attributes_in_leaf_entry_t) (mam_entry_t *,
							    mam_attributes_t,
							    const
mam_level_ops_t *);
typedef mam_entry_type_t (*func_mam_get_leaf_entry_type_t) (void);

struct mam_entry_ops_t {
	func_mam_get_address_from_leaf_entry_t
						mam_get_address_from_leaf_entry_fn;
	func_mam_get_attributes_from_entry_t	mam_get_attributes_from_entry_fn;
	func_mam_get_table_pointed_by_entry_t
						mam_get_table_pointed_by_entry_fn;
	func_mam_is_entry_present_t		mam_is_entry_present_fn;
	func_mam_can_be_leaf_entry_t		mam_can_be_leaf_entry_fn;
	func_mam_update_leaf_entry_t		mam_update_leaf_entry_fn;
	func_mam_update_innter_level_entry_t	mam_update_inner_level_entry_fn;
	func_mam_update_attributes_in_leaf_entry_t
						mam_update_attributes_in_leaf_entry_fn;
	func_mam_get_leaf_entry_type_t		mam_get_leaf_entry_type_fn;
};

/*------------------------------------------------------------------------ */

struct mam_t {
	mam_hav_t			first_table;
	mam_level_ops_t			*first_table_ops;
	mam_attributes_t		inner_level_attributes;
	mam_ept_super_page_support_t	ept_supper_page_support;
	mon_lock_t			update_lock;
	volatile uint32_t		update_counter;
	uint32_t			update_on_cpu;
	boolean_t			is_32bit_page_tables;
	mam_vtdpt_super_page_support_t	vtdpt_supper_page_support;
	mam_vtdpt_snoop_behavior_t	vtdpt_snoop_behavior;
	mam_vtdpt_trans_mapping_t	vtdpt_trans_mapping;
	uint8_t				ept_hw_ve_support;
	mam_memory_ranges_iterator_t	last_iterator;
	uint64_t			last_range_size;
};

#define MAM_NUM_OF_ENTRIES_IN_TABLE (PAGE_4KB_SIZE / sizeof(mam_entry_t))
#define MAM_TABLE_ADDRESS_SHIFT 12
#define MAM_TABLE_ADDRESS_HIGH_SHIFT 32
#define MAM_LEVEL1_TABLE_POS 12
#define MAM_LEVEL2_TABLE_POS 21
#define MAM_LEVEL3_TABLE_POS 30
#define MAM_LEVEL4_TABLE_POS 39
#define MAM_ENTRY_INDEX_MASK 0x1ff
#define MAM_PAT_BIT_POS_IN_PAT_INDEX 2
#define MAM_PCD_BIT_POS_IN_PAT_INDEX 1
#define MAM_PWT_BIT_POS_IN_PAT_INDEX 0
#define MAM_NUM_OF_PDPTES_IN_32_BIT_MODE 4
#define MAM_INVALID_ADDRESS (~((uint64_t)0))
#define MAM_PAGE_1GB_MASK 0x3fffffff
#define MAM_PAGE_512GB_MASK ((uint64_t)0x7fffffffff)
#define MAM_INVALID_CPU_ID (~((uint32_t)0))
/* mon only support 48 bit guest virtual address, the high 16 bits
 * should be all zeros */
#define MAM_MAX_SUPPORTED_ADDRESS ((uint64_t)0xffffffffffff)

#endif
