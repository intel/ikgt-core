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

#include "mttr.h"
#include "hmm.h"
#include "vmm_asm.h"
#include "dbg.h"
#include "gcpu.h"
#include "heap.h"

#define PAGE_64KB_SIZE ((PAGE_4K_SIZE) * 16)
#define PAGE_16KB_SIZE ((PAGE_4K_SIZE) * 4)

#define MTRR_FIX64K_START 0x0ULL
#define MTRR_FIX16K_START 0x80000ULL
#define MTRR_FIX4K_START 0xC0000ULL

#define MTRR_VAR_PHYSMASK_VALID (1ULL<<11)
#define MTRR_VAR_TYPE(base) ((uint8_t)(base))

#define MTRR_FIXED_MAX 0xFFFFFULL
#define MTRR_NUM_OF_FIX 11
#define MTRR_NUM_OF_VAR 10

typedef struct _var_range {
	uint64_t left_size;
	cache_type_t type;
	uint8_t pad[7];
} var_range_t;

static var_range_t var_range[MTRR_NUM_OF_VAR];
static uint64_t cur_addr = 0;

static cache_type_t find_type_in_fixed_range(uint32_t msr_id, uint32_t index)
{
	uint64_t msr_val = asm_rdmsr(msr_id + (index>>3));
	return (uint8_t)(msr_val >> (index<<3));
}

static cache_type_t mtrr_get_fixed_range_memtype(IN uint64_t addr, OUT uint64_t *size)
{
	uint64_t mtrrcap_reg = asm_rdmsr(MSR_MTRRCAP);
	uint64_t mtrr_def_type = asm_rdmsr(MSR_MTRR_DEF_TYPE);

	/* Fixed Range */
	if (!((mtrrcap_reg & MTRRCAP_FIX_SUPPORTED) &&
		(mtrr_def_type & MTRR_FIX_ENABLE) &&
		addr <= MTRR_FIXED_MAX)) {
		return CACHE_TYPE_UNDEFINED;
	}

	/* addr located in FIX64K range */
	if (addr < MTRR_FIX16K_START) {
		*size = PAGE_64KB_SIZE;
		return find_type_in_fixed_range(MSR_MTRR_FIX64K_00000, addr/PAGE_64KB_SIZE);
	}

	/* addr located in FIX16K range */
	if (addr < MTRR_FIX4K_START) {
		addr -= MTRR_FIX16K_START;
		*size = PAGE_16KB_SIZE;
		return find_type_in_fixed_range(MSR_MTRR_FIX16K_80000, addr/PAGE_16KB_SIZE);
	}

	/* addr located in FIX4K range */
	addr -= MTRR_FIX4K_START;
	*size = PAGE_4K_SIZE;
	return find_type_in_fixed_range(MSR_MTRR_FIX4K_C0000, addr/PAGE_4K_SIZE);
}

/* If addr in current variable range, get the left_size of variable range */
static void update_var_range_matched(uint8_t index, uint64_t addr, uint64_t phy_base, uint64_t phy_mask)
{
	uint64_t range_size;

	/* clear valid bit of mask */
	phy_mask &= ~MTRR_VAR_PHYSMASK_VALID;

	/* caculate range size */
	if (phy_mask == 0)
		range_size = top_of_memory;
	else
		range_size = 1ULL << asm_bsf64(phy_mask);

	/* The left_size is: range_size - inner offset of corresponding segment
	 * e.g. assume TOM=0x100000000, base=0x12345000, mask=0xFEF00800.
	 *      so var_range [0x12300000, 0x12400000), [0x13300000, 0x13400000), range_size=0x100000;
	 *      if addr = 0x1335A000, inner_offset=addr&(range_size-1)=0x5A000,
	 *      left size=range_size-inner_offset=0xA6000.  */
	var_range[index].left_size = range_size - (addr & (range_size - 1));
	var_range[index].type = MTRR_VAR_TYPE(phy_base);
}

/* If addr not in current variable range, get the nearest variable range which
 * is higher than addr. */
static void update_var_range_unmatched(uint8_t index, uint64_t addr, uint64_t phy_base, uint64_t phy_mask)
{
	uint64_t bit;
	uint64_t least_sig_bit;
	uint64_t top_sig_bit;

	var_range[index].type = CACHE_TYPE_UNDEFINED;

	/* Clear valid bit of mask */
	phy_mask &= ~MTRR_VAR_PHYSMASK_VALID;

	least_sig_bit = asm_bsf64(phy_mask);
	top_sig_bit = asm_bsr64(top_of_memory-1);

	/* Pre-process mask, e.g. assume TOM=0x100000000
	 * 	if mask=0xFAF00800, after process, mask=0x05000000 */
	/* Complement high bits with '1' higher than top_of_memory of mask */
	phy_mask |= ~MASK64_LOW(top_sig_bit + 1);
	/* Complement low bits with '1' lower than the least_sig_bit of mask */
	phy_mask |= MASK64_LOW(least_sig_bit);
	/* Reverse mask because asm_bsr64() can only search bit '1' */
	phy_mask = ~phy_mask;

	/* Clear low bits of base */
	phy_base &= ~MASK64_LOW(least_sig_bit);
	/* Clear high bits of base */
	phy_base &= MASK64_LOW(top_sig_bit + 1);
	/* Get max range base */
	phy_base |= phy_mask;

	/* If max range base is less than addr, set left_size to TOM */
	if(phy_base < addr) {
		var_range[index].left_size = top_of_memory - addr;
		return;
	}

	/* Search bit '1' from highest bit of processed mask.
	 * Clear the corresponding bit of phy_base and compare the result to
	 * addr. If result >= addr, clear the bit; else search next '1' of mask. */
	while (phy_mask) {
		bit = asm_bsr64(phy_mask);
		if ((phy_base & ~(1ULL << bit)) >= addr)
			phy_base &= ~(1ULL << bit);
		phy_mask &= ~(1ULL << bit);
	}
	/* Update variable range */
	var_range[index].left_size = phy_base - addr;
}

static void update_var_range(uint8_t index, uint64_t addr, uint64_t phy_base, uint64_t phy_mask)
{

	if ((phy_base & phy_mask) == (addr & phy_mask)) {
		update_var_range_matched(index, addr, phy_base, phy_mask);
	} else {
		update_var_range_unmatched(index, addr, phy_base, phy_mask);
	}
}

/* Determin memory type.
 * Refs: MSR manual, Volume 3: Chapter 11.11.4.1 MTRR Precedences */
static cache_type_t caculate_mem_type(cache_type_t type, cache_type_t type_prev)
{
	if (type == type_prev)
		return type;
	if (type == CACHE_TYPE_UNDEFINED)
		return type_prev;
	if (type_prev == CACHE_TYPE_UNDEFINED)
		return type;
	if (((type == CACHE_TYPE_WT) && (type_prev == CACHE_TYPE_WB)) ||
		((type == CACHE_TYPE_WB) && (type_prev == CACHE_TYPE_WT)))
		return CACHE_TYPE_WT;

	/* For overlaps not defined by the above rules, processor behavior
	 * is undefined. */
	return CACHE_TYPE_ERROR;
}

static cache_type_t mtrr_get_var_range_memtype(IN uint64_t addr, OUT uint64_t *size)
{
	uint8_t i;
	cache_type_t  type = CACHE_TYPE_UNDEFINED;
	uint64_t phy_base, phy_mask;
	uint64_t range_size = top_of_memory - addr;

	uint64_t mtrrcap_reg = asm_rdmsr(MSR_MTRRCAP);
	uint8_t vcnt = MTRRCAP_VCNT(mtrrcap_reg);
	uint64_t mtrr_def_type = asm_rdmsr(MSR_MTRR_DEF_TYPE);

	/* Check each variable range to get proper size and type */
	for(i=0; i<vcnt; i++) {
		phy_base = asm_rdmsr(MSR_MTRR_PHYSBASE0 + i*2);
		phy_mask = asm_rdmsr(MSR_MTRR_PHYSMASK0 + i*2);
		if (phy_mask & MTRR_VAR_PHYSMASK_VALID) {
			if (var_range[i].left_size == 0) {
				update_var_range(i, addr, phy_base, phy_mask);
			}

			/* If memory type is UC, then break the loop because UC can
			 * overwrite all other type */
			if (var_range[i].type == CACHE_TYPE_UC) {
				range_size = var_range[i].left_size;
				type = CACHE_TYPE_UC;
				break;
			}

			range_size = MIN(range_size, var_range[i].left_size);
			type = caculate_mem_type(var_range[i].type, type);
		}
	}

	*size = range_size;

	/* Update left_size of each variable section */
	for(i=0; i<vcnt; i++) {
		if (var_range[i].left_size <= range_size)
			var_range[i].left_size = 0;
		else
			var_range[i].left_size -= range_size;
	}

	/* If no variable memory range matches, use default type */
	if (type == CACHE_TYPE_UNDEFINED)
		return MTRR_DEFAULT_TYPE(mtrr_def_type);

	VMM_ASSERT_EX(type != CACHE_TYPE_ERROR,
		"Invalid memory type overlap in MTRR!\n");

	return type;
}

static cache_type_t mtrr_get_range_memtype(IN uint64_t addr, OUT uint64_t *size)
{
	cache_type_t type;

	if (addr >= top_of_memory)
		return CACHE_TYPE_UNDEFINED;

	/* Fixed Range */
	type = mtrr_get_fixed_range_memtype(addr, size);
	if (type !=  CACHE_TYPE_UNDEFINED) {
		return type;
	}

	/* Variable Range */
	return mtrr_get_var_range_memtype(addr, size);
}

static void mtrr_get_section(mtrr_section_t *mtrr_section)
{
	cache_type_t mtrr_type;
	uint64_t mtrr_size;
	uint64_t mtrr_def_type = asm_rdmsr(MSR_MTRR_DEF_TYPE);

	D(VMM_ASSERT(mtrr_section));

	if (cur_addr >= top_of_memory) {
		mtrr_section->size = 0;
		return;
	}

	/* If MTRRs are disabled, the UC memory type is applied to all of phy mem */
	if (!(mtrr_def_type & MTRR_ENABLE)) {
		mtrr_section->base = 0;
		mtrr_section->size = top_of_memory;
		mtrr_section->type = CACHE_TYPE_UC;
		cur_addr = top_of_memory;
		return;
	}

	mtrr_type = mtrr_get_range_memtype(cur_addr, &mtrr_size);

	mtrr_section->base = cur_addr;
	mtrr_section->size = mtrr_size;
	mtrr_section->type = mtrr_type;

	cur_addr += mtrr_size;
}

static mtrr_section_t *mtrr_list = NULL;
void mtrr_init(void)
{
	mtrr_section_t mtrr_sec;
	mtrr_section_t *mtrr_ptr = NULL;

	D(VMM_ASSERT(mtrr_list == NULL);) //Function mtrr_init() can be called only once.

	for(mtrr_get_section(&mtrr_sec); mtrr_sec.size; mtrr_get_section(&mtrr_sec)) {
		if (!mtrr_list) {
			mtrr_list = mem_alloc(sizeof(mtrr_section_t));
			mtrr_list->base = 0;
			mtrr_list->size = mtrr_sec.size;
			mtrr_list->type = mtrr_sec.type;
			mtrr_ptr = mtrr_list;
			continue;
		}

		/* Note: mtrr_ptr will never be NULL because mtrr_init() will be called only
		 *       once. Check below is to pass the static code scan. */
		if (!mtrr_ptr)
			break;

		if (mtrr_sec.type == mtrr_ptr->type) {
			mtrr_ptr->size += mtrr_sec.size;
		} else {
			mtrr_ptr->next = mem_alloc(sizeof(mtrr_section_t));
			mtrr_ptr = mtrr_ptr->next;

			mtrr_ptr->base = mtrr_sec.base;
			mtrr_ptr->size = mtrr_sec.size;
			mtrr_ptr->type = mtrr_sec.type;
		}
	}
	if (mtrr_ptr)
		mtrr_ptr->next = NULL;
}

mtrr_section_t *get_mtrr_section_list(void)
{
	VMM_ASSERT_EX(mtrr_list, "mtrr list is NULL\n");
	return mtrr_list;
}

#ifdef DEBUG
static uint64_t bsp_mtrrcap_reg;
static uint64_t bsp_mtrr_def_type;
static uint64_t bsp_mtrr_fixed_range[MTRR_NUM_OF_FIX];
static uint64_t bsp_mtrr_var_base[MTRR_NUM_OF_VAR];
static uint64_t bsp_mtrr_var_mask[MTRR_NUM_OF_VAR];

static void print_mtrr()
{
	uint8_t i, vcnt;
	uint64_t mtrrcap_reg, mtrr_def_type;
	mtrrcap_reg = asm_rdmsr(MSR_MTRRCAP);
	mtrr_def_type = asm_rdmsr(MSR_MTRR_DEF_TYPE);

	if (mtrr_def_type & MTRR_ENABLE) {
		print_info("MTRR disabled\n");
		return;
	}

	if ((mtrrcap_reg & MTRRCAP_FIX_SUPPORTED) &&
		(mtrr_def_type & MTRR_FIX_ENABLE)) {
		print_info("\nMTRR FIXED Range:\n");
		print_info("MTRR_FIX64K: 0x%08llx\n", asm_rdmsr(MSR_MTRR_FIX64K_00000));
		print_info("MTRR_FIX16K: 0x%08llx\n", asm_rdmsr(MSR_MTRR_FIX16K_80000));
		print_info("MTRR_FIX16K: 0x%08llx\n", asm_rdmsr(MSR_MTRR_FIX16K_A0000));
		print_info("MTRR_FIX4K:  0x%08llx\n", asm_rdmsr(MSR_MTRR_FIX4K_C0000));
		print_info("MTRR_FIX4K:  0x%08llx\n", asm_rdmsr(MSR_MTRR_FIX4K_C8000));
		print_info("MTRR_FIX4K:  0x%08llx\n", asm_rdmsr(MSR_MTRR_FIX4K_D0000));
		print_info("MTRR_FIX4K:  0x%08llx\n", asm_rdmsr(MSR_MTRR_FIX4K_D8000));
		print_info("MTRR_FIX4K:  0x%08llx\n", asm_rdmsr(MSR_MTRR_FIX4K_E0000));
		print_info("MTRR_FIX4K:  0x%08llx\n", asm_rdmsr(MSR_MTRR_FIX4K_E8000));
		print_info("MTRR_FIX4K:  0x%08llx\n", asm_rdmsr(MSR_MTRR_FIX4K_F0000));
		print_info("MTRR_FIX4K:  0x%08llx\n", asm_rdmsr(MSR_MTRR_FIX4K_F8000));
	} else {
		print_info("MTRR not supported or FIX range disabled\n");
	}

	vcnt = MTRRCAP_VCNT(mtrrcap_reg);
	if (vcnt > MTRR_NUM_OF_VAR) {
		print_warn("MTRR variable range count overflowed!\n");
	}

	print_info("\nMTRR Variable Range, vcnt=%d:\n", vcnt);
	for(i=0; i<vcnt; i++) {
		print_info("MTRR_VAR_BASE[%d]: 0x%016llx\n", i, asm_rdmsr(MSR_MTRR_PHYSBASE0 + i*2));
		print_info("MTRR_VAR_MASK[%d]: 0x%016llx\n", i, asm_rdmsr(MSR_MTRR_PHYSMASK0 + i*2));
	}
}

static void mtrr_bsp_save(void)
{
	uint8_t i, vcnt;
	bsp_mtrrcap_reg = asm_rdmsr(MSR_MTRRCAP);
	bsp_mtrr_def_type = asm_rdmsr(MSR_MTRR_DEF_TYPE);
	vcnt = MTRRCAP_VCNT(bsp_mtrrcap_reg);
	VMM_ASSERT(vcnt <= MTRR_NUM_OF_VAR);

	if ((bsp_mtrrcap_reg & MTRRCAP_FIX_SUPPORTED) &&
		(bsp_mtrr_def_type & MTRR_ENABLE) &&
		(bsp_mtrr_def_type & MTRR_FIX_ENABLE)) {
		bsp_mtrr_fixed_range[0] = asm_rdmsr(MSR_MTRR_FIX64K_00000);
		bsp_mtrr_fixed_range[1] = asm_rdmsr(MSR_MTRR_FIX16K_80000);
		bsp_mtrr_fixed_range[2] = asm_rdmsr(MSR_MTRR_FIX16K_A0000);
		bsp_mtrr_fixed_range[3] = asm_rdmsr(MSR_MTRR_FIX4K_C0000);
		bsp_mtrr_fixed_range[4] = asm_rdmsr(MSR_MTRR_FIX4K_C8000);
		bsp_mtrr_fixed_range[5] = asm_rdmsr(MSR_MTRR_FIX4K_D0000);
		bsp_mtrr_fixed_range[6] = asm_rdmsr(MSR_MTRR_FIX4K_D8000);
		bsp_mtrr_fixed_range[7] = asm_rdmsr(MSR_MTRR_FIX4K_E0000);
		bsp_mtrr_fixed_range[8] = asm_rdmsr(MSR_MTRR_FIX4K_E8000);
		bsp_mtrr_fixed_range[9] = asm_rdmsr(MSR_MTRR_FIX4K_F0000);
		bsp_mtrr_fixed_range[10] = asm_rdmsr(MSR_MTRR_FIX4K_F8000);
	}

	if (bsp_mtrr_def_type & MTRR_ENABLE) {
		for(i=0; i<vcnt; i++) {
			bsp_mtrr_var_base[i] = asm_rdmsr(MSR_MTRR_PHYSBASE0 + i*2);
			bsp_mtrr_var_mask[i] = asm_rdmsr(MSR_MTRR_PHYSMASK0 + i*2);
		}
	}
}

/* According to IA32 Manual: Volume 3, Chapter 11.11.8
 * All processors must have the same MTRR values. Currently no hardware support
 * to maintain consistency between all the processors in MP system. OS must
 * maintain MTRR this consistency. Here to check if all the processors have the
 * same MTRR values. */
static void mtrr_ap_check(void)
{
	uint8_t i, vcnt;
	uint32_t msr_addr;
	VMM_ASSERT(bsp_mtrrcap_reg == asm_rdmsr(MSR_MTRRCAP));
	vcnt = MTRRCAP_VCNT(bsp_mtrrcap_reg);

	VMM_ASSERT(bsp_mtrr_def_type == asm_rdmsr(MSR_MTRR_DEF_TYPE));

	if ((bsp_mtrrcap_reg & MTRRCAP_FIX_SUPPORTED) &&
		(bsp_mtrr_def_type & MTRR_ENABLE) &&
		(bsp_mtrr_def_type & MTRR_FIX_ENABLE)) {
		VMM_ASSERT(bsp_mtrr_fixed_range[0] == asm_rdmsr(MSR_MTRR_FIX64K_00000));
		VMM_ASSERT(bsp_mtrr_fixed_range[1] == asm_rdmsr(MSR_MTRR_FIX16K_80000));
		VMM_ASSERT(bsp_mtrr_fixed_range[2] == asm_rdmsr(MSR_MTRR_FIX16K_A0000));
		VMM_ASSERT(bsp_mtrr_fixed_range[3] == asm_rdmsr(MSR_MTRR_FIX4K_C0000));
		VMM_ASSERT(bsp_mtrr_fixed_range[4] == asm_rdmsr(MSR_MTRR_FIX4K_C8000));
		VMM_ASSERT(bsp_mtrr_fixed_range[5] == asm_rdmsr(MSR_MTRR_FIX4K_D0000));
		VMM_ASSERT(bsp_mtrr_fixed_range[6] == asm_rdmsr(MSR_MTRR_FIX4K_D8000));
		VMM_ASSERT(bsp_mtrr_fixed_range[7] == asm_rdmsr(MSR_MTRR_FIX4K_E0000));
		VMM_ASSERT(bsp_mtrr_fixed_range[8] == asm_rdmsr(MSR_MTRR_FIX4K_E8000));
		VMM_ASSERT(bsp_mtrr_fixed_range[9] == asm_rdmsr(MSR_MTRR_FIX4K_F0000));
		VMM_ASSERT(bsp_mtrr_fixed_range[10] == asm_rdmsr(MSR_MTRR_FIX4K_F8000))
	}

	if (bsp_mtrr_def_type & MTRR_ENABLE) {
		for(msr_addr=MSR_MTRR_PHYSBASE0, i=0; i<vcnt; msr_addr += 2, i++) {
			VMM_ASSERT(bsp_mtrr_var_base[i] == asm_rdmsr(msr_addr));
			VMM_ASSERT(bsp_mtrr_var_mask[i] == asm_rdmsr(msr_addr+1));
		}
	}

	return;
}

void mtrr_check(void)
{
	if (host_cpu_id() == 0) {
		mtrr_bsp_save();
		print_info("------DUMP MTRR for BSP------\n");
		print_mtrr();
		print_info("------    DUMP end     ------\n");
	} else {
		mtrr_ap_check();
	}
}
#endif
