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

#include <mtrrs_abstraction.h>
#include <hw_utils.h>
#include <common_libc.h>
#include "mon_dbg.h"
#include "address.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(MTRRS_ABSTRACTION_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(MTRRS_ABSTRACTION_C, __condition)


#define MTRRS_ABS_NUM_OF_SUB_RANGES 8
#define MTRRS_ABS_NUM_OF_FIXED_RANGE_MTRRS 11
#define MTRRS_ABS_NUM_OF_VAR_RANGE_MTRRS 10
#define MTRRS_ABS_HIGH_ADDR_SHIFT 32
#define MTRRS_ABS_ADDR_BASE_SHIFT 12

typedef union {
	struct {
		uint32_t vcnt:8;
		uint32_t fix:1;
		uint32_t res0:1;
		uint32_t wc:1;
		uint32_t res1:21;
		uint32_t res2:32;
	} bits;
	uint64_t value;
} ia32_mtrr_cap_reg_t;

typedef union {
	struct {
		uint32_t type:8;
		uint32_t res0:2;
		uint32_t fixed_range_enable:1;
		uint32_t enable:1;
		uint32_t res1:20;
		uint32_t res2:32;
	} bits;
	uint64_t value;
} ia32_mtrr_def_type_reg_t;

typedef union {
	uint8_t		type[MTRRS_ABS_NUM_OF_SUB_RANGES];
	uint64_t	value;
} ia32_fixed_range_mtrr_t;

typedef union {
	struct {
		uint32_t type:8;
		uint32_t res0:4;
		uint32_t phys_base_low:20;
		uint32_t phys_base_high:20;
		uint32_t res1:12;
	} bits;
	uint64_t value;
} ia32_mtrr_physbase_reg_t;

typedef union {
	struct {
		uint32_t res0:11;
		uint32_t valid:1;
		uint32_t phys_mask_low:20;
		uint32_t phys_mask_high:20;
		uint32_t res1:12;
	} bits;
	uint64_t value;
} ia32_mtrr_physmask_reg_t;

typedef struct {
	uint32_t	start_addr;
	uint32_t	end_addr;
} mtrrs_abs_fixed_range_desc_t;

/*---------------------------------------------------*/
typedef struct {
	ia32_mtrr_cap_reg_t		ia32_mtrrcap_reg;
	ia32_mtrr_def_type_reg_t	ia32_mtrr_def_type;
	ia32_fixed_range_mtrr_t		ia32_mtrr_fix64k_00000;
	ia32_fixed_range_mtrr_t		ia32_mtrr_fix16k_80000;
	ia32_fixed_range_mtrr_t		ia32_mtrr_fix16k_A0000;
	ia32_fixed_range_mtrr_t		ia32_mtrr_fix4k_C0000;
	ia32_fixed_range_mtrr_t		ia32_mtrr_fix4k_C8000;
	ia32_fixed_range_mtrr_t		ia32_mtrr_fix4k_D0000;
	ia32_fixed_range_mtrr_t		ia32_mtrr_fix4k_D8000;
	ia32_fixed_range_mtrr_t		ia32_mtrr_fix4k_E0000;
	ia32_fixed_range_mtrr_t		ia32_mtrr_fix4k_E8000;
	ia32_fixed_range_mtrr_t		ia32_mtrr_fix4k_F0000;
	ia32_fixed_range_mtrr_t		ia32_mtrr_fix4k_F8000;
	ia32_fixed_range_mtrr_t		ia32_mtrr_fix[
		MTRRS_ABS_NUM_OF_FIXED_RANGE_MTRRS];
	mtrrs_abs_fixed_range_desc_t
					ia32_mtrr_fix_range[
		MTRRS_ABS_NUM_OF_FIXED_RANGE_MTRRS];
	ia32_mtrr_physbase_reg_t
					ia32_mtrr_var_phys_base[
		MTRRS_ABS_NUM_OF_VAR_RANGE_MTRRS];
	ia32_mtrr_physmask_reg_t
					ia32_mtrr_var_phys_mask[
		MTRRS_ABS_NUM_OF_VAR_RANGE_MTRRS];
	boolean_t			is_initialized;
	uint32_t			padding; /* not used */
} mtrrs_abstraction_cached_info_t;

static mtrrs_abstraction_cached_info_t mtrrs_cached_info;
uint64_t remsize = 0;
uint64_t mtrr_msbs = 0;
/*---------------------------------------------------*/

uint32_t mtrrs_abstraction_get_num_of_variable_range_regs(void)
{
	uint64_t num = mtrrs_cached_info.ia32_mtrrcap_reg.bits.vcnt;

	return (uint32_t)num;
}

INLINE boolean_t mtrrs_abstraction_are_fixed_regs_supported(void)
{
	return mtrrs_cached_info.ia32_mtrrcap_reg.bits.fix != 0;
}

INLINE boolean_t mtrrs_abstraction_are_mtrrs_enabled(void)
{
	return mtrrs_cached_info.ia32_mtrr_def_type.bits.enable != 0;
}

INLINE boolean_t mtrrs_abstraction_are_fixed_ranged_mtrrs_enabled(void)
{
	return mtrrs_cached_info.ia32_mtrr_def_type.bits.fixed_range_enable !=
	       0;
}

INLINE mon_phys_mem_type_t mtrrs_abstraction_get_default_memory_type(void)
{
	uint64_t type = mtrrs_cached_info.ia32_mtrr_def_type.bits.type;

	return (mon_phys_mem_type_t)type;
}

INLINE boolean_t mtrrs_abstraction_is_var_reg_valid(uint32_t index)
{
	return mtrrs_cached_info.ia32_mtrr_var_phys_mask[index].bits.valid != 0;
}

INLINE uint64_t mtrrs_abstraction_get_address_from_reg(uint32_t reg_index)
{
	uint32_t addr_base_low =
		mtrrs_cached_info.ia32_mtrr_var_phys_base[reg_index].
		bits.phys_base_low << MTRRS_ABS_ADDR_BASE_SHIFT;
	uint32_t addr_base_high =
		mtrrs_cached_info.ia32_mtrr_var_phys_base[reg_index].
		bits.phys_base_high;
	uint64_t addr =
		((uint64_t)addr_base_high <<
		MTRRS_ABS_HIGH_ADDR_SHIFT) | addr_base_low;

	return addr;
}

INLINE uint64_t mtrrs_abstraction_get_mask_from_reg(uint32_t reg_index)
{
	uint32_t addr_mask_low =
		mtrrs_cached_info.ia32_mtrr_var_phys_mask[reg_index].
		bits.phys_mask_low << MTRRS_ABS_ADDR_BASE_SHIFT;
	uint32_t addr_mask_high =
		mtrrs_cached_info.ia32_mtrr_var_phys_mask[reg_index].
		bits.phys_mask_high;
	uint64_t addr_mask =
		((uint64_t)addr_mask_high <<
		MTRRS_ABS_HIGH_ADDR_SHIFT) | addr_mask_low;

	return addr_mask;
}

INLINE
boolean_t mtrrs_abstraction_is_addr_covered_by_var_reg(hpa_t address,
						       uint32_t reg_index)
{
	uint64_t phys_base = mtrrs_abstraction_get_address_from_reg(reg_index);
	uint64_t phys_mask = mtrrs_abstraction_get_mask_from_reg(reg_index);
	uint64_t mask_base = phys_base & phys_mask;
	uint64_t mask_target = phys_mask & address;

	if (mask_base == mask_target) {
		remsize =
			(phys_base &
			 phys_mask) + (~(phys_mask | mtrr_msbs)) + 1 - address;
	}
	return mask_base == mask_target;
}

INLINE boolean_t mttrs_abstraction_is_type_valid(uint64_t type)
{
	switch (type) {
	case MON_PHYS_MEM_UNCACHABLE:
	case MON_PHYS_MEM_WRITE_COMBINING:
	case MON_PHYS_MEM_WRITE_THROUGH:
	case MON_PHYS_MEM_WRITE_PROTECTED:
	case MON_PHYS_MEM_WRITE_BACK:
		return TRUE;
	}
	return FALSE;
}

INLINE boolean_t mtrrs_abstraction_is_IA32_MTRR_DEF_TYPE_valid(uint64_t value)
{
	ia32_mtrr_def_type_reg_t reg;

	reg.value = value;
	return (reg.bits.res0 == 0) && (reg.bits.res1 == 0)
	       && mttrs_abstraction_is_type_valid(reg.bits.type);
}

INLINE boolean_t mtrrs_abstraction_is_IA32_MTRR_PHYSBASE_REG_valid(
	uint64_t value)
{
	ia32_mtrr_physbase_reg_t reg;

	reg.value = value;
	return (reg.bits.res0 == 0) && (reg.bits.res1 == 0)
	       && mttrs_abstraction_is_type_valid(reg.bits.type);
}

INLINE boolean_t mtrrs_abstraction_is_IA32_MTRR_PHYSMASK_REG_valid(
	uint64_t value)
{
	ia32_mtrr_physmask_reg_t reg;

	reg.value = value;
	return (reg.bits.res0 == 0) && (reg.bits.res1 == 0);
}

INLINE boolean_t mtrrs_abstraction_is_IA32_FIXED_RANGE_REG_valid(uint64_t value)
{
	ia32_fixed_range_mtrr_t reg;
	uint32_t i;

	reg.value = value;
	for (i = 0; i < MTRRS_ABS_NUM_OF_SUB_RANGES; i++) {
		if (!mttrs_abstraction_is_type_valid(reg.type[i])) {
			return FALSE;
		}
	}
	return TRUE;
}

boolean_t mon_mtrrs_is_variable_mtrrr_supported(uint32_t msr_id)
{
	/*
	 * IA32_MTRR_PHYSBASE8 - supported only if IA32_MTRR_CAP[7:0] > 8
	 * IA32_MTRR_PHYSMASK8 - supported only if IA32_MTRR_CAP[7:0] > 8
	 * IA32_MTRR_PHYSBASE9 - supported only if IA32_MTRR_CAP[7:0] > 9
	 * IA32_MTRR_PHYSMASK9 - supported only if IA32_MTRR_CAP[7:0] > 9
	 */

	uint32_t index, i;

	/* Check if MSR is within unsupported variable MTRR range */
	if (msr_id >= IA32_MTRR_PHYSBASE8_ADDR
	    && msr_id <= IA32_MTRR_MAX_PHYSMASK_ADDR) {
		for (index = IA32_MTRR_MAX_PHYSMASK_ADDR, i = 1;
		     index > IA32_MTRR_PHYSBASE8_ADDR; index -= 2, i++) {
			if (((index == msr_id) || (index - 1 == msr_id))) {
				if (
					mtrrs_abstraction_get_num_of_variable_range_regs()
					>
					(MTRRS_ABS_NUM_OF_VAR_RANGE_MTRRS -
					 i)) {
					return TRUE;
				} else {
					return FALSE;
				}
			}
		}
		/* dummy added to suppress warning, should never get here */
		return TRUE;
	} else {
		/* MSR is not within unsupported variable MTRR range. */
		return TRUE;
	}
}

/*---------------------------------------------------*/
boolean_t mtrrs_abstraction_bsp_initialize(void)
{
	uint32_t msr_addr;
	uint32_t index;

	mon_memset(&mtrrs_cached_info, 0, sizeof(mtrrs_cached_info));
	mtrrs_cached_info.ia32_mtrrcap_reg.value =
		hw_read_msr(IA32_MTRRCAP_ADDR);
	mtrrs_cached_info.ia32_mtrr_def_type.value =
		hw_read_msr(IA32_MTRR_DEF_TYPE_ADDR);

	if (mtrrs_abstraction_are_fixed_regs_supported()) {
		mtrrs_cached_info.ia32_mtrr_fix[0].value =
			hw_read_msr(IA32_MTRR_FIX64K_00000_ADDR);
		mtrrs_cached_info.ia32_mtrr_fix_range[0].start_addr = 0x0;
		mtrrs_cached_info.ia32_mtrr_fix_range[0].end_addr = 0x7ffff;

		mtrrs_cached_info.ia32_mtrr_fix[1].value =
			hw_read_msr(IA32_MTRR_FIX16K_80000_ADDR);
		mtrrs_cached_info.ia32_mtrr_fix_range[1].start_addr = 0x80000;
		mtrrs_cached_info.ia32_mtrr_fix_range[1].end_addr = 0x9ffff;

		mtrrs_cached_info.ia32_mtrr_fix[2].value =
			hw_read_msr(IA32_MTRR_FIX16K_A0000_ADDR);
		mtrrs_cached_info.ia32_mtrr_fix_range[2].start_addr = 0xa0000;
		mtrrs_cached_info.ia32_mtrr_fix_range[2].end_addr = 0xbffff;

		mtrrs_cached_info.ia32_mtrr_fix[3].value =
			hw_read_msr(IA32_MTRR_FIX4K_C0000_ADDR);
		mtrrs_cached_info.ia32_mtrr_fix_range[3].start_addr = 0xc0000;
		mtrrs_cached_info.ia32_mtrr_fix_range[3].end_addr = 0xc7fff;

		mtrrs_cached_info.ia32_mtrr_fix[4].value =
			hw_read_msr(IA32_MTRR_FIX4K_C8000_ADDR);
		mtrrs_cached_info.ia32_mtrr_fix_range[4].start_addr = 0xc8000;
		mtrrs_cached_info.ia32_mtrr_fix_range[4].end_addr = 0xcffff;

		mtrrs_cached_info.ia32_mtrr_fix[5].value =
			hw_read_msr(IA32_MTRR_FIX4K_D0000_ADDR);
		mtrrs_cached_info.ia32_mtrr_fix_range[5].start_addr = 0xd0000;
		mtrrs_cached_info.ia32_mtrr_fix_range[5].end_addr = 0xd7fff;

		mtrrs_cached_info.ia32_mtrr_fix[6].value =
			hw_read_msr(IA32_MTRR_FIX4K_D8000_ADDR);
		mtrrs_cached_info.ia32_mtrr_fix_range[6].start_addr = 0xd8000;
		mtrrs_cached_info.ia32_mtrr_fix_range[6].end_addr = 0xdffff;

		mtrrs_cached_info.ia32_mtrr_fix[7].value =
			hw_read_msr(IA32_MTRR_FIX4K_E0000_ADDR);
		mtrrs_cached_info.ia32_mtrr_fix_range[7].start_addr = 0xe0000;
		mtrrs_cached_info.ia32_mtrr_fix_range[7].end_addr = 0xe7fff;

		mtrrs_cached_info.ia32_mtrr_fix[8].value =
			hw_read_msr(IA32_MTRR_FIX4K_E8000_ADDR);
		mtrrs_cached_info.ia32_mtrr_fix_range[8].start_addr = 0xe8000;
		mtrrs_cached_info.ia32_mtrr_fix_range[8].end_addr = 0xeffff;

		mtrrs_cached_info.ia32_mtrr_fix[9].value =
			hw_read_msr(IA32_MTRR_FIX4K_F0000_ADDR);
		mtrrs_cached_info.ia32_mtrr_fix_range[9].start_addr = 0xf0000;
		mtrrs_cached_info.ia32_mtrr_fix_range[9].end_addr = 0xf7fff;

		mtrrs_cached_info.ia32_mtrr_fix[10].value =
			hw_read_msr(IA32_MTRR_FIX4K_F8000_ADDR);
		mtrrs_cached_info.ia32_mtrr_fix_range[10].start_addr = 0xf8000;
		mtrrs_cached_info.ia32_mtrr_fix_range[10].end_addr = 0xfffff;
	}

	for (msr_addr = IA32_MTRR_PHYSBASE0_ADDR, index = 0;
	     index < mtrrs_abstraction_get_num_of_variable_range_regs();
	     msr_addr += 2, index++) {
		if (msr_addr > IA32_MTRR_MAX_PHYSMASK_ADDR) {
			MON_LOG(mask_mon,
				level_error,
				"BSP: ERROR: MTRRs Abstraction: Variable MTRRs count > %d",
				MTRRS_ABS_NUM_OF_VAR_RANGE_MTRRS);
			MON_DEADLOOP();
		}

		mtrrs_cached_info.ia32_mtrr_var_phys_base[index].value =
			hw_read_msr(msr_addr);
		mtrrs_cached_info.ia32_mtrr_var_phys_mask[index].value =
			hw_read_msr(msr_addr + 1);
	}

	mtrr_msbs =
		~((uint64_t)(((uint64_t)1 <<
		addr_get_physical_address_size()) - 1));

	mtrrs_cached_info.is_initialized = TRUE;
	return TRUE;
}

boolean_t mtrrs_abstraction_ap_initialize(void)
{
	uint32_t msr_addr;
	uint32_t index;

	if (!mtrrs_cached_info.is_initialized) {
		MON_LOG(mask_anonymous, level_error,
			"ERROR: MTRRs Abstraction: Initializing AP before BSP\n");
		goto failed;
	}

	if (mtrrs_cached_info.ia32_mtrrcap_reg.value !=
	    hw_read_msr(IA32_MTRRCAP_ADDR)) {
		MON_LOG(mask_anonymous, level_error,
			"ERROR: MTRRs Abstraction: IA32_MTRRCAP doesn't match\n");
		goto failed;
	}

	if (mtrrs_cached_info.ia32_mtrr_def_type.value !=
	    hw_read_msr(IA32_MTRR_DEF_TYPE_ADDR)) {
		MON_LOG(mask_anonymous,
			level_error,
			"ERROR: MTRRs Abstraction: IA32_MTRR_DEF_TYPE doesn't match\n");
		goto failed;
	}

	if (mtrrs_abstraction_are_fixed_regs_supported()) {
		if ((mtrrs_cached_info.ia32_mtrr_fix[0].value !=
		     hw_read_msr(IA32_MTRR_FIX64K_00000_ADDR))
		    || (mtrrs_cached_info.ia32_mtrr_fix[1].value !=
			hw_read_msr(IA32_MTRR_FIX16K_80000_ADDR))
		    || (mtrrs_cached_info.ia32_mtrr_fix[2].value !=
			hw_read_msr(IA32_MTRR_FIX16K_A0000_ADDR))
		    || (mtrrs_cached_info.ia32_mtrr_fix[3].value !=
			hw_read_msr(IA32_MTRR_FIX4K_C0000_ADDR))
		    || (mtrrs_cached_info.ia32_mtrr_fix[4].value !=
			hw_read_msr(IA32_MTRR_FIX4K_C8000_ADDR))
		    || (mtrrs_cached_info.ia32_mtrr_fix[5].value !=
			hw_read_msr(IA32_MTRR_FIX4K_D0000_ADDR))
		    || (mtrrs_cached_info.ia32_mtrr_fix[6].value !=
			hw_read_msr(IA32_MTRR_FIX4K_D8000_ADDR))
		    || (mtrrs_cached_info.ia32_mtrr_fix[7].value !=
			hw_read_msr(IA32_MTRR_FIX4K_E0000_ADDR))
		    || (mtrrs_cached_info.ia32_mtrr_fix[8].value !=
			hw_read_msr(IA32_MTRR_FIX4K_E8000_ADDR))
		    || (mtrrs_cached_info.ia32_mtrr_fix[9].value !=
			hw_read_msr(IA32_MTRR_FIX4K_F0000_ADDR))
		    || (mtrrs_cached_info.ia32_mtrr_fix[10].value !=
			hw_read_msr(IA32_MTRR_FIX4K_F8000_ADDR))) {
			MON_LOG(mask_anonymous, level_error,
				"ERROR: MTRRs Abstraction: One (or more)"
				" of the fixed range MTRRs doesn't match\n");

			goto failed;
		}
	}

	for (msr_addr = IA32_MTRR_PHYSBASE0_ADDR, index = 0;
	     index < mtrrs_abstraction_get_num_of_variable_range_regs();
	     msr_addr += 2, index++) {
		if (msr_addr > IA32_MTRR_MAX_PHYSMASK_ADDR) {
			MON_LOG(mask_mon,
				level_error,
				"AP: ERROR: MTRRs Abstraction: Variable MTRRs count > %d",
				MTRRS_ABS_NUM_OF_VAR_RANGE_MTRRS);
			MON_DEADLOOP();
		}

		if ((mtrrs_cached_info.ia32_mtrr_var_phys_base[index].value !=
		     hw_read_msr(msr_addr))
		    || (mtrrs_cached_info.ia32_mtrr_var_phys_mask[index].value
			!=
			hw_read_msr(msr_addr + 1))) {
			/*
			 * MTRR MSR registers on the AP processors are not correctly
			 * programmed by BIOS firmware. Just ignore this error, and
			 * xmon programs EPT (Memory Type) based on the BSP’s MTRR values.
			 */
			MON_LOG(mask_anonymous, level_warning,
				"WARN: MTRRs Abstraction: One (or more)"
				" of the variable range MTRRs doesn't match\n");
		}
	}

	return TRUE;
failed:
	MON_ASSERT(0);
	return FALSE;
}

mon_phys_mem_type_t mtrrs_abstraction_get_memory_type(hpa_t address)
{
	uint32_t index;
	uint32_t var_mtrr_match_bitmap;
	mon_phys_mem_type_t type = MON_PHYS_MEM_UNDEFINED;
	uint64_t remsize_back = 0, range_base = 0;
	mon_phys_mem_type_t type_back = MON_PHYS_MEM_UNDEFINED;

	remsize = 0;
	MON_ASSERT(mtrrs_cached_info.is_initialized);

	if (!mtrrs_abstraction_are_mtrrs_enabled()) {
		return MON_PHYS_MEM_UNCACHABLE;
	}

	if (mtrrs_abstraction_are_fixed_regs_supported() &&
	    mtrrs_abstraction_are_fixed_ranged_mtrrs_enabled() &&
	    (address <=
	     mtrrs_cached_info.ia32_mtrr_fix_range
	     [MTRRS_ABS_NUM_OF_FIXED_RANGE_MTRRS - 1].end_addr)) {
		/* Find proper fixed range MTRR */
		for (index = 0; index < MTRRS_ABS_NUM_OF_FIXED_RANGE_MTRRS;
		     index++) {
			if (address <=
			    mtrrs_cached_info.ia32_mtrr_fix_range[index].
			    end_addr) {
				/* Find proper sub-range */
				uint64_t offset =
					address -
					mtrrs_cached_info.ia32_mtrr_fix_range[
						index].start_addr;
				uint32_t size =
					mtrrs_cached_info.ia32_mtrr_fix_range[
						index].end_addr + 1 -
					mtrrs_cached_info.ia32_mtrr_fix_range[
						index].start_addr;
				uint32_t sub_range_size = size /
							  MTRRS_ABS_NUM_OF_SUB_RANGES;
				uint64_t sub_range_index = offset /
							   sub_range_size;
				remsize =
					(sub_range_index +
					 1) * sub_range_size - offset;
				MON_ASSERT(
					(size % MTRRS_ABS_NUM_OF_SUB_RANGES) == 0);
				MON_ASSERT(
					sub_range_index < MTRRS_ABS_NUM_OF_SUB_RANGES);
				return (mon_phys_mem_type_t)mtrrs_cached_info.
				       ia32_mtrr_fix[index].type[sub_range_index];
			}
		}
		/* mustn't reach here */
		MON_ASSERT(0);
	}

	var_mtrr_match_bitmap = 0;

	for (index = 0;
	     index < mtrrs_abstraction_get_num_of_variable_range_regs();
	     index++) {
		if (index >= MTRRS_ABS_NUM_OF_VAR_RANGE_MTRRS) {
			break;
		}

		if (!mtrrs_abstraction_is_var_reg_valid(index)) {
			continue;
		}

		if (mtrrs_abstraction_is_addr_covered_by_var_reg(address,
			    index)) {
			type = (mon_phys_mem_type_t)
			       mtrrs_cached_info.ia32_mtrr_var_phys_base[index].
			       bits.type;
			BIT_SET(var_mtrr_match_bitmap, type);

			if (remsize_back > 0) {
				if (type == MON_PHYS_MEM_UNCACHABLE
				    || type_back == MON_PHYS_MEM_UNCACHABLE) {
					if (type_back !=
					    MON_PHYS_MEM_UNCACHABLE) {
						remsize_back = remsize;
					}
					if (type != MON_PHYS_MEM_UNCACHABLE) {
						remsize = 0;
					}
					if (type == MON_PHYS_MEM_UNCACHABLE
					    && type_back ==
					    MON_PHYS_MEM_UNCACHABLE) {
						remsize_back =
							(remsize_back >
							 remsize) ? remsize_back
							:
							remsize;
					}

					type_back = MON_PHYS_MEM_UNCACHABLE;
					remsize = 0;
				} else {
					remsize_back =
						(remsize_back >
						 remsize) ? remsize :
						remsize_back;
					type_back = type;
					remsize = 0;
				}
			} else {
				remsize_back = remsize;
				remsize = 0;
				type_back = type;
			}
		} else {
			range_base = mtrrs_abstraction_get_address_from_reg(
				index);

			if (address < range_base && address + remsize_back >
			    range_base) {
				remsize_back = range_base - address;
			}
		}
	}
	remsize = remsize_back;

	if (0 == var_mtrr_match_bitmap) {
		/* not described by any MTRR, return default memory type */
		return mtrrs_abstraction_get_default_memory_type();
	} else if (IS_POW_OF_2(var_mtrr_match_bitmap)) {
		/* described by single MTRR, type contains the proper value */
		return type;
	} else if (BIT_GET(var_mtrr_match_bitmap, MON_PHYS_MEM_UNCACHABLE)) {
		/* fall in multiple ranges, UC wins */
		return MON_PHYS_MEM_UNCACHABLE;
	} else
	if ((BIT_VALUE64(MON_PHYS_MEM_WRITE_THROUGH) |
	     BIT_VALUE64(MON_PHYS_MEM_WRITE_BACK))
	    == var_mtrr_match_bitmap) {
		/* fall in WT + WB, WT wins. */
		return MON_PHYS_MEM_WRITE_THROUGH;
	}

	/* improper MTRR setting */
	MON_LOG(mask_anonymous, level_error,
		"FATAL: MTRRs Abstraction: Overlapping variable MTRRs"
		" have confilting types\n");
	MON_DEADLOOP();
	return MON_PHYS_MEM_UNDEFINED;
}

mon_phys_mem_type_t mtrrs_abstraction_get_range_memory_type(hpa_t address,
							    OUT uint64_t *size,
							    uint64_t totalsize)
{
	mon_phys_mem_type_t first_page_mem_type, mem_type;
	uint64_t range_size = 0;

	remsize = 0;

	first_page_mem_type = mtrrs_abstraction_get_memory_type(address);

	for (mem_type = first_page_mem_type;
	     (mem_type == first_page_mem_type) && (range_size < totalsize);
	     mem_type =
		     mtrrs_abstraction_get_memory_type(address + range_size)) {
		if (remsize != 0) {
			range_size += remsize;
		} else {
			range_size += 4 KILOBYTES;
		}
	}
	if (size != NULL) {
		*size = range_size;
	}
	return first_page_mem_type;
}

boolean_t mtrrs_abstraction_track_mtrr_update(uint32_t mtrr_index,
					      uint64_t value)
{
	if (mtrr_index == IA32_MTRR_DEF_TYPE_ADDR) {
		if (!mtrrs_abstraction_is_IA32_MTRR_DEF_TYPE_valid(value)) {
			return FALSE;
		}
		mtrrs_cached_info.ia32_mtrr_def_type.value = value;
		return TRUE;
	}
	if ((mtrr_index >= IA32_MTRR_FIX64K_00000_ADDR)
	    && (mtrr_index <= IA32_MTRR_FIX4K_F8000_ADDR)) {
		uint32_t fixed_index = (~((uint32_t)0));
		switch (mtrr_index) {
		case IA32_MTRR_FIX64K_00000_ADDR:
			fixed_index = 0;
			break;
		case IA32_MTRR_FIX16K_80000_ADDR:
			fixed_index = 1;
			break;
		case IA32_MTRR_FIX16K_A0000_ADDR:
			fixed_index = 2;
			break;
		case IA32_MTRR_FIX4K_C0000_ADDR:
			fixed_index = 3;
			break;
		case IA32_MTRR_FIX4K_C8000_ADDR:
			fixed_index = 4;
			break;
		case IA32_MTRR_FIX4K_D0000_ADDR:
			fixed_index = 5;
			break;
		case IA32_MTRR_FIX4K_D8000_ADDR:
			fixed_index = 6;
			break;
		case IA32_MTRR_FIX4K_E0000_ADDR:
			fixed_index = 7;
			break;
		case IA32_MTRR_FIX4K_E8000_ADDR:
			fixed_index = 8;
			break;
		case IA32_MTRR_FIX4K_F0000_ADDR:
			fixed_index = 9;
			break;
		case IA32_MTRR_FIX4K_F8000_ADDR:
			fixed_index = 10;
			break;
		default:
			MON_ASSERT(0);
			return FALSE;
		}

		if (!mtrrs_abstraction_is_IA32_FIXED_RANGE_REG_valid(value)) {
			return FALSE;
		}
		mtrrs_cached_info.ia32_mtrr_fix[fixed_index].value = value;
		return TRUE;
	}

	if ((mtrr_index >= IA32_MTRR_PHYSBASE0_ADDR)
	    && (mtrr_index <= IA32_MTRR_MAX_PHYSMASK_ADDR)) {
		boolean_t is_phys_base = ((mtrr_index % 2) == 0);
		if (is_phys_base) {
			uint32_t phys_base_index =
				(mtrr_index - IA32_MTRR_PHYSBASE0_ADDR) / 2;
			if (!mtrrs_abstraction_is_IA32_MTRR_PHYSBASE_REG_valid(
				    value)) {
				return FALSE;
			}
			mtrrs_cached_info.ia32_mtrr_var_phys_base[
				phys_base_index].value =
				value;
		} else {
			uint32_t phys_mask_index =
				(mtrr_index - IA32_MTRR_PHYSMASK0_ADDR) / 2;
			if (!mtrrs_abstraction_is_IA32_MTRR_PHYSMASK_REG_valid(
				    value)) {
				return FALSE;
			}
			mtrrs_cached_info.ia32_mtrr_var_phys_mask[
				phys_mask_index].value =
				value;
		}
		return TRUE;
	}

	return FALSE;
}
