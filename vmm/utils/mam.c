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

#include <mam.h>
#include <hmm.h>
#include <heap.h>
#include <dbg.h>
#include <vmm_util.h>
#include <vmx_cap.h>

/* ASSUMPTION: for the memory allocated from vmm_page_alloc(), hva == hpa
** this is correct for pre-os launch. for host CR3, all hva == hpa, except:
**  1. hpa = 0 is remapped to have NULL reference
**  2. 6 stack pages per cpu is remapped to prevent stack overflow
**  3. vmxon and vmptrld page is unmapped to prevent access from non-vmx instruction
** but, in post-os launch, the memory might not be physically continuous,
** it should be re-considered
*/

/* ASSUMPTION: mam is for 4 level table, which takes 9 bits from input address
** as index for each table to support 48 (9*4+12) bits input address
** if want to support other level number, the code should be re-considered
*/

struct mam_t{
	uint64_t* pml4_table;
	mam_entry_ops_t * entry_ops;
	vmm_lock_t lock;
};

#define ENTRY_NUM_IN_TABLE 512

static inline boolean_t is_leaf_supported(mam_entry_ops_t *entry_ops, uint32_t level)
{
	return (level <= (entry_ops->max_leaf_level));
}

#define entry_size(level) (1ULL << (12 + (9 * (level))))
#define src_to_index(src, level) ((uint32_t)((src) >> (12 + (9 * (level)))) & 0x1FF)
#define src_to_offset(src, level) ((src) & MASK64_LOW(12 + (9 * (level))))
#define leaf_addr_mask(level) MASK64_MID(51, (12 + (9 * (level))))

static inline uint64_t leaf_get_addr(uint64_t leaf_entry, mam_entry_ops_t* entry_ops, uint32_t level)
{
	if (entry_ops->is_present(entry_ops->leaf_get_attr(leaf_entry, level)))
		return leaf_entry & leaf_addr_mask(level);
	else // not present
		return 0ULL;
}

static inline uint64_t *table_get_addr(uint64_t table_entry)
{
	return (uint64_t *)(table_entry & MASK64_MID(51, 12));
}

// "attr" provides a chance for EPT to enable #VE for non-present pages
mam_handle_t mam_create_mapping(mam_entry_ops_t *entry_ops, uint32_t attr)
{
	mam_handle_t mam_handle;
	uint32_t i;

	VMM_ASSERT_EX((entry_ops), "%s entry_ops is NULL\n", __FUNCTION__);
	VMM_ASSERT_EX((!entry_ops->is_present(attr)),
		"%s attribute is present\n", __FUNCTION__); // attr must be not present

	mam_handle = (mam_handle_t)mem_alloc(sizeof(struct mam_t));
	mam_handle->pml4_table = (uint64_t*)page_alloc(1);
	mam_handle->entry_ops = entry_ops;
	lock_init(&(mam_handle->lock));

	for (i=0; i<ENTRY_NUM_IN_TABLE; i++)
	{
		entry_ops->to_leaf(mam_handle->pml4_table + i, MAM_LEVEL_PML4, attr);
	}
	return mam_handle;
}

static uint64_t* create_table(uint64_t* p_entry, mam_entry_ops_t *entry_ops)
{
	uint64_t *sub_table = page_alloc(1);

	*p_entry = (uint64_t)sub_table;
	entry_ops->to_table(p_entry);
	return sub_table;
}

static void destroy_table(uint64_t* p_table_entry, mam_entry_ops_t *entry_ops, uint32_t level)
{
	uint32_t i;
	uint64_t *sub_table = (uint64_t*)table_get_addr(*p_table_entry);

	for (i=0; i<ENTRY_NUM_IN_TABLE; i++)
	{
		if (!(entry_ops->is_leaf(sub_table[i], level-1))) // table
			destroy_table(&sub_table[i], entry_ops, level-1);
	}
	page_free((void*)sub_table);
}

static void convert_leaf_to_table(uint64_t* p_leaf_entry, mam_entry_ops_t *entry_ops, uint32_t level)
{
	uint32_t attr = entry_ops->leaf_get_attr(*p_leaf_entry, level);
	uint64_t addr = leaf_get_addr(*p_leaf_entry, entry_ops, level);
	uint64_t *sub_table = create_table(p_leaf_entry, entry_ops);
	uint32_t i;

	for (i=0; i<ENTRY_NUM_IN_TABLE; i++)
	{
		sub_table[i] = addr + i * entry_size(level-1);
		entry_ops->to_leaf(&sub_table[i], level-1, attr);
	}
}

static void fill_table(uint64_t* table, mam_entry_ops_t *entry_ops, uint32_t level,
	uint64_t src_addr, uint64_t tgt_addr, uint64_t size, uint32_t attr)
{
	uint32_t i;
	uint64_t size_to_fill;

	i = src_to_index(src_addr, level);
	while (size >0)
	{
		D(VMM_ASSERT(i<ENTRY_NUM_IN_TABLE));
		size_to_fill = entry_size(level) - src_to_offset(src_addr, level);
		if (size_to_fill > size)
			size_to_fill = size;
		/* 1. src_start must be level aligned
		** 2. src_end must be level aligned
		** 3. not present (ignore 4,5 below)
		** 4. HW support for big page
		** 5. tgt must be level aligned
		** to be a leaf, it needs (1,2,3) or (1,2,4,5) */
		if ((src_to_offset(src_addr, level) == 0) &&
			(src_to_offset(src_addr+size_to_fill, level) == 0) &&
			((entry_ops->is_present(attr) == FALSE) ||
			(is_leaf_supported(entry_ops, level) &&
			(src_to_offset(tgt_addr, level) == 0))))
		{
			// make leaf
			if (!(entry_ops->is_leaf(table[i], level))) // table
				destroy_table(&table[i], entry_ops, level);
			table[i] = tgt_addr;
			entry_ops->to_leaf(&table[i], level, attr);
		}
		else
		{
			// make sub table
			if (entry_ops->is_leaf(table[i], level))
				convert_leaf_to_table(&table[i], entry_ops, level);
			fill_table(table_get_addr(table[i]), entry_ops,
				level-1, src_addr, tgt_addr, size_to_fill, attr);
		}
		src_addr += size_to_fill;
		tgt_addr += size_to_fill;
		size -= size_to_fill;
		i++;
	}
}

static void update_attr(uint64_t* table, mam_entry_ops_t *entry_ops, uint32_t level,
	uint64_t src_addr, uint64_t size, uint32_t attr_mask, uint32_t attr_value)
{
	uint32_t i;
	uint32_t attr, new_attr;
	uint64_t size_to_update;

	i = src_to_index(src_addr, level);

	while (size >0)
	{
		D(VMM_ASSERT(i<ENTRY_NUM_IN_TABLE));
		size_to_update = entry_size(level) - src_to_offset(src_addr, level);
		if (size_to_update > size)
			size_to_update = size;
		if (entry_ops->is_leaf(table[i], level))
		{
			attr = entry_ops->leaf_get_attr(table[i], level);
			if (entry_ops->is_present(attr)) // present
			{
				new_attr = (attr & (~attr_mask)) | (attr_value & attr_mask);
				if (new_attr != attr) // do nothing for new_attr == attr
				{
					if ((src_to_offset(src_addr, level) == 0) && // whole leaf is covered
						(src_to_offset(src_addr+size_to_update, level) == 0))
					{
						/* it is possible to make new_attr not-present after clearing some bits
						** so, in this to_leaf(), it will because a full not-present entry which means
						** all unnecessary bits are cleared */
						entry_ops->to_leaf(&table[i], level, new_attr);
					}
					else
					{
						/* change it to table. no need to check page size support because
						** current page exists means sub-table page must supported*/
						convert_leaf_to_table(&table[i], entry_ops, level);
						update_attr(table_get_addr(table[i]), entry_ops, level-1,
							src_addr, size_to_update, attr_mask, attr_value);
					}
				}
			}
			// do nothing for not-present
		}
		else // table
		{
			update_attr(table_get_addr(table[i]), entry_ops, level-1,
				src_addr, size_to_update, attr_mask, attr_value);
		}
		src_addr += size_to_update;
		size -= size_to_update;
		i++;
	}
}

static void try_convert_table_to_leaf(uint64_t* p_table_entry, mam_entry_ops_t *entry_ops,
	uint32_t level, uint64_t src_addr, uint64_t size)
{
	uint32_t i;
	uint64_t size_to_convert;
	uint64_t *sub_table = table_get_addr(*p_table_entry);
	uint32_t sub_level;
	uint32_t attr_0;
	uint64_t tgt_addr_0 = 0;

	sub_level = level-1; // match level to sub_table

	// 1. check affected sub-tables recursively
	if (sub_level > MAM_LEVEL_PT) // no sub-table for PT
	{
		i = src_to_index(src_addr, sub_level);
		while(size)
		{
			D(VMM_ASSERT(i<ENTRY_NUM_IN_TABLE));
			size_to_convert = entry_size(sub_level) - src_to_offset(src_addr, sub_level);
			if (size_to_convert > size)
				size_to_convert = size;
			if (!(entry_ops->is_leaf(sub_table[i], sub_level)))
			{
				try_convert_table_to_leaf(&sub_table[i], entry_ops,
					sub_level, src_addr, size_to_convert);
			}
			src_addr += size_to_convert;
			size -= size_to_convert;
			i++;
		}
	}
	// 2. Do not destory PML4 table
	if (level > MAM_LEVEL_PML4)
		return;
	// 3. check itself
	for (i=0; i<ENTRY_NUM_IN_TABLE; i++)
	{
		if (!(entry_ops->is_leaf(sub_table[i], sub_level)))
			// found a sub-table, which means cannot merge
			return;
	}
	// all entries are leaves in this table
	attr_0 = entry_ops->leaf_get_attr(sub_table[0], sub_level);
	if (entry_ops->is_present(attr_0)) // present
	{
		if (is_leaf_supported(entry_ops, level) == FALSE) // check if present leaf is supported
			return;
		tgt_addr_0 = leaf_get_addr(sub_table[0], entry_ops, sub_level);
		if (src_to_offset(tgt_addr_0, level) !=0) // check tgt_addr alignment
			return;
	}
	for (i=1; i<ENTRY_NUM_IN_TABLE; i++)
	{
		if (attr_0 != entry_ops->leaf_get_attr(sub_table[i], sub_level))
			// found different entry
			return;
		if ((entry_ops->is_present(attr_0)) &&  // check tgt_addr when present
			((tgt_addr_0 + i * entry_size(sub_level))!=
				leaf_get_addr(sub_table[i], entry_ops, sub_level)))
				return;
	}
	// 4. p_table_entry can be merged to leaf, do it
	destroy_table(p_table_entry, entry_ops, level);
	*p_table_entry = tgt_addr_0;
	entry_ops->to_leaf(p_table_entry, level, attr_0);
}

#define MAM_MAX_SRC_ADDR (1ULL << 48)
#define MAM_MAX_TGT_ADDR (1ULL << 52)

void mam_insert_range(mam_handle_t mam_handle, uint64_t src_addr,
	uint64_t tgt_addr, uint64_t size, uint32_t attr)
{
	VMM_ASSERT_EX(mam_handle, "%s mam_handle is NULL\n", __FUNCTION__);
	VMM_ASSERT_EX((src_addr < MAM_MAX_SRC_ADDR),
		"%s source address(0x%llX) is too large\n", __FUNCTION__, src_addr);
	VMM_ASSERT_EX((size <= MAM_MAX_SRC_ADDR),
		"%s size(0x%llx) is too large\n", __FUNCTION__, size);
	VMM_ASSERT_EX((size), "%s size is 0\n",  __FUNCTION__);
	VMM_ASSERT_EX(((src_addr + size) <= MAM_MAX_SRC_ADDR),
		"%s source end address(0x%llX) is too large\n", __FUNCTION__, src_addr + size);
	VMM_ASSERT_EX(((src_addr & 0xFFFULL) == 0),
		"%s source address(0x%llX) isn't 4K page aligned\n", __FUNCTION__, src_addr);
	VMM_ASSERT_EX(((size & 0xFFFULL) == 0),
		"%s size(0x%llx) isn't 4K page aligned\n", __FUNCTION__, size);
	VMM_ASSERT_EX((tgt_addr < MAM_MAX_TGT_ADDR),
		"%s target address(0x%llX) is too large\n", __FUNCTION__, tgt_addr);
	VMM_ASSERT_EX(((tgt_addr & 0xFFFULL) == 0),
		"%s target address(0x%llX) isn't 4K page aligned\n", __FUNCTION__, tgt_addr);

	lock_acquire_write(&(mam_handle->lock));

	fill_table(mam_handle->pml4_table, mam_handle->entry_ops, MAM_LEVEL_PML4,
		src_addr, tgt_addr, size, attr);
	try_convert_table_to_leaf((uint64_t*)&(mam_handle->pml4_table), mam_handle->entry_ops,
		MAM_LEVEL_PML4+1, src_addr, size);

	lock_release(&(mam_handle->lock));
}

void mam_update_attr(mam_handle_t mam_handle, uint64_t src_addr,
	uint64_t size, uint32_t attr_mask, uint32_t attr_value)
{
	VMM_ASSERT_EX(mam_handle, "%s mam_handle is NULL\n", __FUNCTION__);
	VMM_ASSERT_EX((src_addr < MAM_MAX_SRC_ADDR),
		"%s source address(0x%llX) is too large\n", __FUNCTION__, src_addr);
	VMM_ASSERT_EX((size <= MAM_MAX_SRC_ADDR),
		"%s size(0x%llx) is too large\n", __FUNCTION__, size);
	VMM_ASSERT_EX((size), "%s size is 0\n", __FUNCTION__);
	VMM_ASSERT_EX(((src_addr + size) <= MAM_MAX_SRC_ADDR),
		"%s source end address(0x%llX) is too large\n", __FUNCTION__, src_addr + size);
	VMM_ASSERT_EX(((src_addr & 0xFFFULL) == 0),
		"%s source address(0x%llX) isn't 4K page aligned\n", __FUNCTION__, src_addr);
	VMM_ASSERT_EX(((size & 0xFFFULL) == 0),
		"%s size(0x%llx) isn't 4K page aligned\n", __FUNCTION__, size);
	VMM_ASSERT(attr_mask);

	lock_acquire_write(&(mam_handle->lock));

	update_attr(mam_handle->pml4_table, mam_handle->entry_ops, MAM_LEVEL_PML4,
		src_addr, size, attr_mask, attr_value);
	try_convert_table_to_leaf((uint64_t*)&(mam_handle->pml4_table), mam_handle->entry_ops,
		MAM_LEVEL_PML4+1, src_addr, size);

	lock_release(&(mam_handle->lock));
}

boolean_t mam_get_mapping(mam_handle_t mam_handle, uint64_t src_addr,
	uint64_t *p_tgt_addr, uint32_t *p_attr)
{
	uint32_t level = MAM_LEVEL_PML4;
	uint64_t* table;
	mam_entry_ops_t* entry_ops;
	uint32_t attr;
	uint64_t tgt_addr = 0;
	uint32_t i;

	VMM_ASSERT_EX(mam_handle, "%s mam_handle is NULL\n", __FUNCTION__);
	VMM_ASSERT_EX((src_addr < MAM_MAX_SRC_ADDR),
		"%s source address(0x%llX) is too large\n", __FUNCTION__, src_addr);
	VMM_ASSERT_EX(p_tgt_addr, "%s p_tgt_addr is NULL\n", __FUNCTION__);

	lock_acquire_read(&(mam_handle->lock));
	table = mam_handle->pml4_table;
	entry_ops = mam_handle->entry_ops;

	while (1)
	{
		i = src_to_index(src_addr, level);
		if (entry_ops->is_leaf(table[i], level))
		{
			attr = entry_ops->leaf_get_attr(table[i], level);
			if (entry_ops->is_present(attr))
			{
				tgt_addr = leaf_get_addr(table[i], entry_ops, level);
				tgt_addr += src_to_offset(src_addr, level);
			}
			break;
		}
		table = table_get_addr(table[i]);
		level--;
	}

	*p_tgt_addr = tgt_addr;
	if (p_attr)
		*p_attr = attr;

	lock_release(&(mam_handle->lock));
	return entry_ops->is_present(attr);
}

uint64_t mam_get_table_hpa(mam_handle_t mam_handle)
{
	VMM_ASSERT_EX(mam_handle, "%s mam_handle is NULL\n", __FUNCTION__);
	return (uint64_t)(mam_handle->pml4_table);
}

#ifdef DEBUG
boolean_t mam_default_filter(uint64_t src_addr, uint64_t current_entry,
	mam_entry_ops_t *entry_ops, uint32_t level)
{
	static uint64_t previous_raw_attr = 0;
	uint64_t raw_attr;
	uint64_t tgt_addr;

	if (entry_ops->is_leaf(current_entry, level))
	{
		raw_attr = current_entry & (~ MASK64_MID(51,12));
		if (raw_attr != previous_raw_attr)
		{
			previous_raw_attr = raw_attr;
			return TRUE;
		}
		if (entry_ops->is_present(entry_ops->leaf_get_attr(current_entry, level)))
		{
			tgt_addr = leaf_get_addr(current_entry, entry_ops, level);
			if (tgt_addr != src_addr) // print non 1:1 mapping
				return TRUE;
		}
		if ((src_addr <= top_of_memory) &&
			((src_addr & MASK64_LOW(30)) == 0))
			return TRUE;
	}
	else
	{
		return TRUE; // print all tables
	}
	return FALSE;
}

// filter=NULL means print all
static void mam_print_internal(uint64_t* table, mam_entry_ops_t *entry_ops,
		uint32_t level, uint64_t src_addr, mam_print_filter filter)
{
	uint32_t i;
	for (i=0; i<ENTRY_NUM_IN_TABLE; i++)
	{
		if ((filter == NULL) || filter(src_addr, table[i], entry_ops, level))
			vmm_printf("%d %03d 0x%llx 0x%llx\n", level, i, table[i], src_addr);
		if (!(entry_ops->is_leaf(table[i], level))) // sub-table
			mam_print_internal(table_get_addr(table[i]), entry_ops,
				level-1, src_addr, filter);
		src_addr += entry_size(level);
	}
}

void mam_print(mam_handle_t mam_handle, mam_print_filter filter)
{
	VMM_ASSERT_EX(mam_handle, "%s mam_handle is NULL\n", __FUNCTION__);
	lock_acquire_read(&(mam_handle->lock));
	mam_print_internal(mam_handle->pml4_table, mam_handle->entry_ops,
		MAM_LEVEL_PML4, 0, filter);
	lock_release(&(mam_handle->lock));
}
#endif
