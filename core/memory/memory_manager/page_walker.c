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

#include "file_codes.h"
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(PAGE_WALKER_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(PAGE_WALKER_C, __condition)
#include <mon_defs.h>
#include <mon_dbg.h>
#include <em64t_defs.h>
#include <gpm_api.h>
#include <host_memory_manager_api.h>
#include <guest.h>
#include <guest_cpu.h>
#include <hw_interlocked.h>
#include <page_walker.h>

typedef union {
	union {
		struct {
			uint32_t present:1;
			uint32_t writable:1;
			uint32_t user:1;
			uint32_t pwt:1;
			uint32_t pcd:1;
			uint32_t accessed:1;
			uint32_t dirty:1;
			uint32_t page_size:1;
			uint32_t global:1;
			uint32_t available:3;
			uint32_t addr_base:20;
		} bits;
		uint32_t uint32;
	} non_pae_entry;
	union {
		struct {
			uint32_t present:1;
			uint32_t writable:1;
			uint32_t user:1;
			uint32_t pwt:1;
			uint32_t pcd:1;
			uint32_t accessed:1;
			uint32_t dirty:1;
			uint32_t page_size:1;
			uint32_t global:1;
			uint32_t available:3;
			uint32_t addr_base_low:20;
			uint32_t addr_base_high:20;
			uint32_t avl_or_res:11;
			uint32_t exb_or_res:1;
		} bits;
		uint64_t uint64;
	} pae_lme_entry;
} pw_page_entry_t;

typedef union {
	struct {
		uint32_t present:1;
		uint32_t is_write:1;
		uint32_t is_user:1;
		uint32_t is_reserved:1;
		uint32_t is_fetch:1;
		uint32_t reserved:27;
		uint32_t reserved_high;
	} bits;
	uint64_t uint64;
} pw_pfec_t;

#define PW_NUM_OF_TABLE_ENTRIES_IN_PAE_MODE 512
#define PW_NUM_OF_TABLE_ENTRIES_IN_NON_PAE_MODE 1024
#define PW_INVALID_INDEX ((uint32_t)(~(0)));
#define PW_PAE_ENTRY_INCREMENT PW_SIZE_OF_PAE_ENTRY
#define PW_NON_PAE_ENTRY_INCREMENT 4
#define PW_PDPTE_INDEX_MASK_IN_32_BIT_ADDR (0xc0000000)
#define PW_PDPTE_INDEX_SHIFT  30
#define PW_PDPTE_INDEX_MASK_IN_64_BIT_ADDR ((uint64_t)0x0000007fc0000000)
#define PW_PML4TE_INDEX_MASK ((uint64_t)0x0000ff8000000000)
#define PW_PML4TE_INDEX_SHIFT 39
#define PW_PDE_INDEX_MASK_IN_PAE_MODE (0x3fe00000)
#define PW_PDE_INDEX_SHIFT_IN_PAE_MODE 21
#define PW_PDE_INDEX_MASK_IN_NON_PAE_MODE (0xffc00000)
#define PW_PDE_INDEX_SHIFT_IN_NON_PAE_MODE 22
#define PW_PTE_INDEX_MASK_IN_PAE_MODE  (0x1ff000)
#define PW_PTE_INDEX_MASK_IN_NON_PAE_MODE (0x3ff000)
#define PW_PTE_INDEX_SHIFT  12
#define PW_PDPT_ALIGNMENT 32
#define PW_TABLE_SHIFT 12
#define PW_HIGH_ADDRESS_SHIFT 32
#define PW_2M_PAE_PDE_RESERVED_BITS_IN_ENTRY_LOW_MASK ((uint32_t)0x1fe000)
#define PW_4M_NON_PAE_PDE_RESERVED_BITS_IN_ENTRY_LOW_MASK ((uint32_t)0x3fe000)
#define PW_1G_PAE_PDPTE_RESERVED_BITS_IN_ENTRY_LOW_MASK ((uint32_t)0x3fffe000)

uint32_t pw_reserved_bits_high_mask;

INLINE boolean_t pw_gpa_to_hpa(gpm_handle_t gpm_handle,
			       uint64_t gpa,
			       uint64_t *hpa)
{
	mam_attributes_t attrs;

	return mon_gpm_gpa_to_hpa(gpm_handle, gpa, hpa, &attrs);
}

INLINE boolean_t pw_hpa_to_hva(IN uint64_t hpa, OUT uint64_t *hva)
{
	return mon_hmm_hpa_to_hva(hpa, hva);
}

INLINE void *pw_hva_to_ptr(IN uint64_t hva)
{
	return (void *)hva;
}

INLINE uint64_t pw_retrieve_table_from_cr3(uint64_t cr3, boolean_t is_pae,
					   boolean_t is_lme)
{
	if ((!is_pae) || is_lme) {
		return ALIGN_BACKWARD(cr3, PAGE_4KB_SIZE);
	}

	return ALIGN_BACKWARD(cr3, PW_PDPT_ALIGNMENT);
}

static
void pw_retrieve_indices(IN uint64_t virtual_address, IN boolean_t is_pae,
			 IN boolean_t is_lme, OUT uint32_t *pml4te_index,
			 OUT uint32_t *pdpte_index, OUT uint32_t *pde_index,
			 OUT uint32_t *pte_index)
{
	uint32_t virtual_address_low_32_bit = (uint32_t)virtual_address;

	if (is_pae) {
		if (is_lme) {
			uint64_t pml4te_index_tmp =
				((virtual_address & PW_PML4TE_INDEX_MASK) >>
				 PW_PML4TE_INDEX_SHIFT);
			uint64_t pdpte_index_tmp =
				((virtual_address &
				  PW_PDPTE_INDEX_MASK_IN_64_BIT_ADDR) >>
				 PW_PDPTE_INDEX_SHIFT);

			*pml4te_index = (uint32_t)pml4te_index_tmp;
			*pdpte_index = (uint32_t)pdpte_index_tmp;
		} else {
			*pml4te_index = PW_INVALID_INDEX;
			*pdpte_index =
				((virtual_address_low_32_bit &
				  PW_PDPTE_INDEX_MASK_IN_32_BIT_ADDR) >>
				 PW_PDPTE_INDEX_SHIFT);
			MON_ASSERT(
				*pdpte_index <
				PW_NUM_OF_PDPT_ENTRIES_IN_32_BIT_MODE);
		}
		*pde_index =
			((virtual_address_low_32_bit &
			  PW_PDE_INDEX_MASK_IN_PAE_MODE) >>
			 PW_PDE_INDEX_SHIFT_IN_PAE_MODE);
		MON_ASSERT(*pde_index < PW_NUM_OF_TABLE_ENTRIES_IN_PAE_MODE);
		*pte_index =
			((virtual_address_low_32_bit &
			  PW_PTE_INDEX_MASK_IN_PAE_MODE) >>
			 PW_PTE_INDEX_SHIFT);
		MON_ASSERT(*pte_index < PW_NUM_OF_TABLE_ENTRIES_IN_PAE_MODE);
	} else {
		*pml4te_index = PW_INVALID_INDEX;
		*pdpte_index = PW_INVALID_INDEX;
		*pde_index =
			((virtual_address_low_32_bit &
			  PW_PDE_INDEX_MASK_IN_NON_PAE_MODE) >>
			 PW_PDE_INDEX_SHIFT_IN_NON_PAE_MODE);
		*pte_index =
			((virtual_address_low_32_bit &
			  PW_PTE_INDEX_MASK_IN_NON_PAE_MODE) >>
			 PW_PTE_INDEX_SHIFT);
	}
}

static
pw_page_entry_t *pw_retrieve_table_entry(gpm_handle_t gpm_handle,
					 uint64_t table_gpa,
					 uint32_t entry_index,
					 boolean_t is_pae,
					 boolean_t use_host_page_tables)
{
	uint64_t entry_hpa;
	uint64_t entry_hva;
	uint64_t table_hpa;

	if (use_host_page_tables) {
		table_hpa = table_gpa;
	} else if (!pw_gpa_to_hpa(gpm_handle, table_gpa, &table_hpa)) {
		return NULL;
	}

	if (is_pae) {
		entry_hpa = table_hpa + entry_index * PW_PAE_ENTRY_INCREMENT;
	} else {
		entry_hpa = table_hpa + entry_index *
			    PW_NON_PAE_ENTRY_INCREMENT;
	}

	if (!pw_hpa_to_hva(entry_hpa, &entry_hva)) {
		return NULL;
	}
	return (pw_page_entry_t *)pw_hva_to_ptr(entry_hva);
}

static
void pw_read_entry_value(pw_page_entry_t *fill_to,
			 pw_page_entry_t *fill_from, boolean_t is_pae)
{
	if (is_pae) {
		volatile uint64_t *original_value_ptr =
			(volatile uint64_t *)fill_from;
		uint64_t value1 = *original_value_ptr;
		uint64_t value2 = *original_value_ptr;

		while (value1 != value2) {
			value1 = value2;
			value2 = *original_value_ptr;
		}

		*fill_to = *((pw_page_entry_t *)(&value1));
	} else {
		/* clear the whole entry; */
		fill_to->pae_lme_entry.uint64 = 0;
		fill_to->non_pae_entry.uint32 = fill_from->non_pae_entry.uint32;
	}
}

static
boolean_t pw_is_big_page_pde(pw_page_entry_t *entry, boolean_t is_lme,
			     boolean_t is_pae, boolean_t is_pse)
{
	if (!entry->non_pae_entry.bits.page_size) {
		/* doesn't matter which type "non_pae" or "pae_lme" */
		return FALSE;
	}

	if (is_lme || is_pae) {
		/* ignore pse bit in these cases */
		return TRUE;
	}

	return is_pse;
}

INLINE boolean_t pw_is_1gb_page_pdpte(pw_page_entry_t *entry)
{
	return entry->pae_lme_entry.bits.page_size;
}

static
boolean_t pw_are_reserved_bits_in_pml4te_cleared(pw_page_entry_t *entry,
						 boolean_t is_nxe)
{
	if (entry->pae_lme_entry.bits.addr_base_high &
	    pw_reserved_bits_high_mask) {
		return FALSE;
	}

	if ((!is_nxe) && entry->pae_lme_entry.bits.exb_or_res) {
		return FALSE;
	}
	return TRUE;
}

static
boolean_t pw_are_reserved_bits_in_pdpte_cleared(pw_page_entry_t *entry,
						boolean_t is_nxe,
						boolean_t is_lme)
{
	if (entry->pae_lme_entry.bits.addr_base_high &
	    pw_reserved_bits_high_mask) {
		return FALSE;
	}

	if (!is_lme) {
		if (entry->pae_lme_entry.bits.avl_or_res ||
		    entry->pae_lme_entry.bits.exb_or_res ||
		    entry->pae_lme_entry.bits.writable ||
		    entry->pae_lme_entry.bits.user) {
			return FALSE;
		}
	} else {
		if ((!is_nxe) && entry->pae_lme_entry.bits.exb_or_res) {
			return FALSE;
		}
		if (pw_is_1gb_page_pdpte(entry)) {
			if (entry->pae_lme_entry.uint64 &
			    PW_1G_PAE_PDPTE_RESERVED_BITS_IN_ENTRY_LOW_MASK) {
				return FALSE;
			}
		}
	}

	return TRUE;
}

static
boolean_t pw_are_reserved_bits_in_pde_cleared(pw_page_entry_t *entry,
					      boolean_t is_nxe,
					      boolean_t is_lme,
					      boolean_t is_pae,
					      boolean_t is_pse)
{
	if (is_pae) {
		if (entry->pae_lme_entry.
		    bits.addr_base_high & pw_reserved_bits_high_mask) {
			return FALSE;
		}

		if ((!is_nxe) && entry->pae_lme_entry.bits.exb_or_res) {
			return FALSE;
		}

		if ((!is_lme) && entry->pae_lme_entry.bits.avl_or_res) {
			return FALSE;
		}

		if (pw_is_big_page_pde(entry, is_lme, is_pae, is_pse)) {
			if (entry->pae_lme_entry.uint64 &
			    PW_2M_PAE_PDE_RESERVED_BITS_IN_ENTRY_LOW_MASK) {
				return FALSE;
			}
		}
	} else {
		if (pw_is_big_page_pde(entry, is_lme, is_pae, is_pse) &&
		    entry->non_pae_entry.uint32 &
		    PW_4M_NON_PAE_PDE_RESERVED_BITS_IN_ENTRY_LOW_MASK) {
			return FALSE;
		}
	}

	return TRUE;
}

static
boolean_t pw_are_reserved_bits_in_pte_cleared(pw_page_entry_t *pte,
					      boolean_t is_nxe,
					      boolean_t is_lme,
					      boolean_t is_pae)
{
	if (!is_pae) {
		return TRUE;
	}

	if (pte->pae_lme_entry.bits.addr_base_high &
	    pw_reserved_bits_high_mask) {
		return FALSE;
	}

	if ((!is_lme) && (pte->pae_lme_entry.bits.avl_or_res)) {
		return FALSE;
	}

	if ((!is_nxe) && (pte->pae_lme_entry.bits.exb_or_res)) {
		return FALSE;
	}

	return TRUE;
}

static
boolean_t pw_is_write_access_permitted(pw_page_entry_t *pml4te,
				       pw_page_entry_t *pdpte,
				       pw_page_entry_t *pde,
				       pw_page_entry_t *pte,
				       boolean_t is_user, boolean_t is_wp,
				       boolean_t is_lme, boolean_t is_pae,
				       boolean_t is_pse)
{
	if ((!is_user) && (!is_wp)) {
		return TRUE;
	}

	if (is_lme) {
		MON_ASSERT(pml4te != NULL);
		MON_ASSERT(pdpte != NULL);
		MON_ASSERT(pml4te->pae_lme_entry.bits.present);
		MON_ASSERT(pdpte->pae_lme_entry.bits.present);
		if ((!pml4te->pae_lme_entry.bits.writable) ||
		    (!pdpte->pae_lme_entry.bits.writable)) {
			return FALSE;
		}
	}

	if (pw_is_1gb_page_pdpte(pdpte)) {
		return TRUE;
	}

	MON_ASSERT(pde != NULL);
	MON_ASSERT(pde->non_pae_entry.bits.present);
	if (!pde->non_pae_entry.bits.writable) {
		/* doesn't matter which entry "non_pae" or "pae_lme" is checked */
		return FALSE;
	}

	if (pw_is_big_page_pde(pde, is_lme, is_pae, is_pse)) {
		return TRUE;
	}

	MON_ASSERT(pte != NULL);
	MON_ASSERT(pte->non_pae_entry.bits.present);

	/* doesn't matter which entry "non_pae" or "pae_lme" is checked */
	return pte->non_pae_entry.bits.writable;
}

static
boolean_t pw_is_user_access_permitted(pw_page_entry_t *pml4te,
				      pw_page_entry_t *pdpte,
				      pw_page_entry_t *pde,
				      pw_page_entry_t *pte,
				      boolean_t is_lme,
				      boolean_t is_pae,
				      boolean_t is_pse)
{
	if (is_lme) {
		MON_ASSERT(pml4te != NULL);
		MON_ASSERT(pdpte != NULL);
		MON_ASSERT(pml4te->pae_lme_entry.bits.present);
		MON_ASSERT(pdpte->pae_lme_entry.bits.present);
		if ((!pml4te->pae_lme_entry.bits.user) ||
		    (!pdpte->pae_lme_entry.bits.user)) {
			return FALSE;
		}
	}

	if (pw_is_1gb_page_pdpte(pdpte)) {
		return TRUE;
	}

	MON_ASSERT(pde != NULL);
	MON_ASSERT(pde->non_pae_entry.bits.present);
	if (!pde->non_pae_entry.bits.user) {
		/* doesn't matter which entry "non_pae" or "pae_lme" is checked */
		return FALSE;
	}

	if (pw_is_big_page_pde(pde, is_lme, is_pae, is_pse)) {
		return TRUE;
	}

	MON_ASSERT(pte != NULL);
	MON_ASSERT(pte->non_pae_entry.bits.present);

	/* doesn't matter which entry  "non_pae" or "pae_lme" is checked */
	return pte->non_pae_entry.bits.user;
}

static
boolean_t pw_is_fetch_access_permitted(pw_page_entry_t *pml4te,
				       pw_page_entry_t *pdpte,
				       pw_page_entry_t *pde,
				       pw_page_entry_t *pte,
				       boolean_t is_lme,
				       boolean_t is_pae,
				       boolean_t is_pse)
{
	if (is_lme) {
		MON_ASSERT(pml4te != NULL);
		MON_ASSERT(pdpte != NULL);
		MON_ASSERT(pml4te->pae_lme_entry.bits.present);
		MON_ASSERT(pdpte->pae_lme_entry.bits.present);

		if ((pml4te->pae_lme_entry.bits.exb_or_res) ||
		    (pdpte->pae_lme_entry.bits.exb_or_res)) {
			return FALSE;
		}
	}

	if (pw_is_1gb_page_pdpte(pdpte)) {
		return TRUE;
	}

	MON_ASSERT(pde != NULL);
	MON_ASSERT(pde->pae_lme_entry.bits.present);
	if (pde->pae_lme_entry.bits.exb_or_res) {
		return FALSE;
	}

	if (pw_is_big_page_pde(pde, is_lme, is_pae, is_pse)) {
		return TRUE;
	}

	MON_ASSERT(pte != NULL);
	MON_ASSERT(pte->pae_lme_entry.bits.present);

	return !pte->pae_lme_entry.bits.exb_or_res;
}

static
uint64_t pw_retrieve_phys_addr(pw_page_entry_t *entry, boolean_t is_pae)
{
	MON_ASSERT(entry->non_pae_entry.bits.present);
	if (is_pae) {
		uint32_t addr_low =
			entry->pae_lme_entry.bits.addr_base_low <<
			PW_TABLE_SHIFT;
		uint32_t addr_high = entry->pae_lme_entry.bits.addr_base_high;
		return ((uint64_t)addr_high <<
			PW_HIGH_ADDRESS_SHIFT) | addr_low;
	} else {
		return entry->non_pae_entry.bits.addr_base << PW_TABLE_SHIFT;
	}
}

static
uint64_t pw_retrieve_big_page_phys_addr(pw_page_entry_t *entry,
					boolean_t is_pae, boolean_t is_1gb)
{
	uint64_t base = pw_retrieve_phys_addr(entry, is_pae);

	/* Clean offset bits */
	if (is_pae) {
		if (is_1gb) {
			return ALIGN_BACKWARD(base, PAGE_1GB_SIZE);
		} else {
			return ALIGN_BACKWARD(base, PAGE_2MB_SIZE);
		}
	}

	/* Non-PAE mode */
	return ALIGN_BACKWARD(base, PAGE_4MB_SIZE);
}

static
uint32_t pw_get_big_page_offset(uint64_t virtual_address,
				boolean_t is_pae, boolean_t is_1gb)
{
	if (is_pae) {
		if (is_1gb) {
			/* Take only 30 LSBs */
			return (uint32_t)(virtual_address & PAGE_1GB_MASK);
		} else {
			/* Take only 21 LSBs */
			return (uint32_t)(virtual_address & PAGE_2MB_MASK);
		}
	}

	/* Take 22 LSBs */
	return (uint32_t)(virtual_address & PAGE_4MB_MASK);
}

static
void pw_update_ad_bits_in_entry(pw_page_entry_t *native_entry,
				pw_page_entry_t *old_native_value,
				pw_page_entry_t *new_native_value)
{
	uint32_t cmpxch_result = 0;

	MON_ASSERT(native_entry != NULL);
	MON_ASSERT(old_native_value->non_pae_entry.bits.present);
	MON_ASSERT(new_native_value->non_pae_entry.bits.present);

	if (old_native_value->non_pae_entry.uint32 !=
	    new_native_value->non_pae_entry.uint32) {
		cmpxch_result =
			hw_interlocked_compare_exchange(
				(int32_t volatile *)native_entry,
				old_native_value->
				non_pae_entry.uint32,
				new_native_value->
				non_pae_entry.uint32);
		/* The result is not checked. If the cmpxchg has failed,
		 * it means that the guest entry was changed,
		 * so it is wrong to set status bits on the updated entry */
	}
}

static
void pw_update_ad_bits(pw_page_entry_t *guest_space_pml4te,
		       pw_page_entry_t *pml4te,
		       pw_page_entry_t *guest_space_pdpte,
		       pw_page_entry_t *pdpte,
		       pw_page_entry_t *guest_space_pde,
		       pw_page_entry_t *pde,
		       pw_page_entry_t *guest_space_pte,
		       pw_page_entry_t *pte,
		       boolean_t is_write_access,
		       boolean_t is_lme,
		       boolean_t is_pae,
		       boolean_t is_pse)
{
	pw_page_entry_t pde_before_update;
	pw_page_entry_t pte_before_update;

	if (is_lme) {
		pw_page_entry_t pml4te_before_update;
		pw_page_entry_t pdpte_before_update;

		MON_ASSERT(guest_space_pml4te != NULL);
		MON_ASSERT(pml4te != NULL);
		MON_ASSERT(guest_space_pdpte != NULL);
		MON_ASSERT(pdpte != NULL);

		pml4te_before_update = *pml4te;
		pml4te->pae_lme_entry.bits.accessed = 1;
		pw_update_ad_bits_in_entry(guest_space_pml4te,
			&pml4te_before_update,
			pml4te);

		pdpte_before_update = *pdpte;
		pdpte->pae_lme_entry.bits.accessed = 1;

		if (guest_space_pml4te == guest_space_pdpte) {
			pdpte_before_update.pae_lme_entry.bits.accessed = 1;
		}
		pw_update_ad_bits_in_entry(guest_space_pdpte,
			&pdpte_before_update,
			pdpte);
	}

	if (pw_is_1gb_page_pdpte(pdpte)) {
		return;
	}

	MON_ASSERT(guest_space_pde != NULL);
	MON_ASSERT(pde != NULL);

	pde_before_update = *pde;
	/* doesn't matter which field "non_pae" or "pae_lme" is used */
	pde->non_pae_entry.bits.accessed = 1;
	if ((guest_space_pml4te == guest_space_pde) ||
	    (guest_space_pdpte == guest_space_pde)) {
		/* doesn't matter which field "non_pae" or "pae_lme" is used */
		pde_before_update.non_pae_entry.bits.accessed = 1;
	}

	if (pw_is_big_page_pde(pde, is_lme, is_pae, is_pse)) {
		if (is_write_access) {
			/* doesn't matter which field "non_pae" or "pae_lme" is used */
			pde->non_pae_entry.bits.dirty = 1;
		}
		pw_update_ad_bits_in_entry(guest_space_pde,
			&pde_before_update,
			pde);
		return;
	}

	pw_update_ad_bits_in_entry(guest_space_pde, &pde_before_update, pde);

	MON_ASSERT(guest_space_pte != NULL);
	MON_ASSERT(pte != NULL);

	pte_before_update = *pte;
	/* doesn't matter which field "non_pae" or "pae_lme" is used */
	pte->non_pae_entry.bits.accessed = 1;

	if ((guest_space_pml4te == guest_space_pte) ||
	    (guest_space_pdpte == guest_space_pte) ||
	    (guest_space_pde == guest_space_pte)) {
		/* doesn't matter which field "non_pae" or "pae_lme" is used */
		pte_before_update.non_pae_entry.bits.accessed = 1;
	}

	if (is_write_access) {
		/* doesn't matter which field "non_pae" or "pae_lme" is used */
		pte->non_pae_entry.bits.dirty = 1;
	}

	pw_update_ad_bits_in_entry(guest_space_pte, &pte_before_update, pte);
}

/* perform page walk from given cr3, or if none, current cr3 */
pw_retval_t pw_perform_page_walk(IN guest_cpu_handle_t gcpu,
				 IN uint64_t virt_addr,
				 uint64_t cr3,
				 IN boolean_t is_write,
				 IN boolean_t is_user,
				 IN boolean_t is_fetch,
				 IN boolean_t set_ad_bits,
				 OUT uint64_t *gpa_out,
				 OUT uint64_t *pfec_out)
{
	pw_retval_t retval = PW_RETVAL_SUCCESS;
	pw_pfec_t native_pfec;
	guest_handle_t guest_handle = mon_gcpu_guest_handle(gcpu);
	gpm_handle_t gpm_handle = gcpu_get_current_gpm(guest_handle);
	uint64_t efer_value = gcpu_get_msr_reg(gcpu, IA32_MON_MSR_EFER);
	boolean_t is_nxe = ((efer_value & EFER_NXE) != 0);
	boolean_t is_lme = ((efer_value & EFER_LME) != 0);
	uint64_t cr0 = gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR0);
	uint64_t cr4 = gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR4);
	boolean_t is_wp = ((cr0 & CR0_WP) != 0);
	boolean_t is_pae = ((cr4 & CR4_PAE) != 0);
	boolean_t is_pse = ((cr4 & CR4_PSE) != 0);
	uint64_t gpa = PW_INVALID_GPA;
	uint32_t pml4te_index;
	uint32_t pdpte_index;
	uint32_t pde_index;
	uint32_t pte_index;
	uint64_t first_table;
	uint64_t pml4t_gpa;
	pw_page_entry_t *pml4te_ptr = NULL;
	pw_page_entry_t pml4te_val;
	uint64_t pdpt_gpa;
	pw_page_entry_t *pdpte_ptr = NULL;
	pw_page_entry_t pdpte_val;
	uint64_t pd_gpa;
	pw_page_entry_t *pde_ptr = NULL;
	pw_page_entry_t pde_val;
	uint64_t pt_gpa;
	pw_page_entry_t *pte_ptr = NULL;
	pw_page_entry_t pte_val;
	boolean_t use_host_pt = gcpu_uses_host_page_tables(gcpu);

	pml4te_val.pae_lme_entry.uint64 = 0;
	pdpte_val.pae_lme_entry.uint64 = 0;
	pde_val.pae_lme_entry.uint64 = 0;
	pte_val.pae_lme_entry.uint64 = 0;

	native_pfec.uint64 = 0;
	native_pfec.bits.is_write = (is_write) ? 1 : 0;
	native_pfec.bits.is_user = (is_user) ? 1 : 0;
	native_pfec.bits.is_fetch = (is_pae && is_nxe && is_fetch) ? 1 : 0;

	pw_retrieve_indices(virt_addr,
		is_pae,
		is_lme,
		&pml4te_index,
		&pdpte_index,
		&pde_index,
		&pte_index);
	if (cr3 == 0) { /* use current cr3 */
		cr3 = gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR3);
	}
	first_table = pw_retrieve_table_from_cr3(cr3, is_pae, is_lme);
	if (is_pae) {
		if (is_lme) {
			pml4t_gpa = first_table;

			pml4te_ptr =
				pw_retrieve_table_entry(gpm_handle,
					pml4t_gpa,
					pml4te_index,
					is_pae,
					use_host_pt);
			if (pml4te_ptr == NULL) {
				retval = PW_RETVAL_PHYS_MEM_VIOLATION;
				goto out;
			}

			pw_read_entry_value(&pml4te_val, pml4te_ptr, is_pae);

			if (!pml4te_val.pae_lme_entry.bits.present) {
				retval = PW_RETVAL_PF;
				goto out;
			}

			if (!pw_are_reserved_bits_in_pml4te_cleared(&pml4te_val,
				    is_nxe)) {
				native_pfec.bits.present = 1;
				native_pfec.bits.is_reserved = 1;
				retval = PW_RETVAL_PF;
				goto out;
			}

			pdpt_gpa = pw_retrieve_phys_addr(&pml4te_val, is_pae);
			pdpte_ptr =
				pw_retrieve_table_entry(gpm_handle,
					pdpt_gpa,
					pdpte_index,
					is_pae,
					use_host_pt);
			if (pdpte_ptr == NULL) {
				retval = PW_RETVAL_PHYS_MEM_VIOLATION;
				goto out;
			}
		} else {
			pdpt_gpa = first_table;
			pdpte_ptr =
				pw_retrieve_table_entry(gpm_handle,
					pdpt_gpa,
					pdpte_index,
					is_pae,
					use_host_pt);
			if (pdpte_ptr == NULL) {
				retval = PW_RETVAL_PHYS_MEM_VIOLATION;
				goto out;
			}
		}

		pw_read_entry_value(&pdpte_val, pdpte_ptr, is_pae);

		if (!pdpte_val.pae_lme_entry.bits.present) {
			retval = PW_RETVAL_PF;
			goto out;
		}

		if (!pw_are_reserved_bits_in_pdpte_cleared(&pdpte_val, is_nxe,
			    is_lme)) {
			native_pfec.bits.present = 1;
			native_pfec.bits.is_reserved = 1;
			retval = PW_RETVAL_PF;
			goto out;
		}
	}

	/* 1GB page size */
	if (pw_is_1gb_page_pdpte(&pdpte_val)) {
		uint64_t big_page_addr;
		uint32_t offset_in_big_page;

		/* Retrieve address of the big page in guest space */
		big_page_addr =
			pw_retrieve_big_page_phys_addr(&pdpte_val, is_pae,
				TRUE);
		/* Retrieve offset in page */
		offset_in_big_page = pw_get_big_page_offset(virt_addr,
			is_pae,
			TRUE);
		/* Calculate full guest accessed physical address */
		gpa = big_page_addr + offset_in_big_page;

		if ((is_write) &&
		    (!pw_is_write_access_permitted
			     (&pml4te_val, &pdpte_val, NULL, NULL, is_user,
			     is_wp, is_lme,
			     is_pae, is_pse))) {
			native_pfec.bits.present = 1;
			retval = PW_RETVAL_PF;
			goto out;
		}

		if (is_user &&
		    (!pw_is_user_access_permitted
			     (&pml4te_val, &pdpte_val, NULL, NULL, is_lme,
			     is_pae, is_pse))) {
			native_pfec.bits.present = 1;
			retval = PW_RETVAL_PF;
			goto out;
		}

		if (is_pae &&
		    is_nxe &&
		    is_fetch &&
		    (!pw_is_fetch_access_permitted
			     (&pml4te_val, &pdpte_val, NULL, NULL, is_lme,
			     is_pae, is_pse))) {
			native_pfec.bits.present = 1;
			retval = PW_RETVAL_PF;
			goto out;
		}

		if (set_ad_bits) {
			pw_update_ad_bits(pml4te_ptr,
				&pml4te_val,
				pdpte_ptr,
				&pdpte_val,
				NULL,
				NULL,
				NULL,
				NULL,
				is_write,
				is_lme,
				is_pae,
				is_pse);
		}

		retval = PW_RETVAL_SUCCESS;
		goto out;
	}

	pd_gpa =
		(is_pae) ? pw_retrieve_phys_addr(&pdpte_val,
			is_pae) : first_table;
	pde_ptr =
		pw_retrieve_table_entry(gpm_handle, pd_gpa, pde_index, is_pae,
			use_host_pt);
	if (pde_ptr == NULL) {
		retval = PW_RETVAL_PHYS_MEM_VIOLATION;
		goto out;
	}

	pw_read_entry_value(&pde_val, pde_ptr, is_pae);

	/* doesn't matter which entry "non_pae" or "pae" is checked */
	if (!pde_val.non_pae_entry.bits.present) {
		retval = PW_RETVAL_PF;
		goto out;
	}

	if (!pw_are_reserved_bits_in_pde_cleared
		    (&pde_val, is_nxe, is_lme, is_pae, is_pse)) {
		native_pfec.bits.present = 1;
		native_pfec.bits.is_reserved = 1;
		retval = PW_RETVAL_PF;
		goto out;
	}

	/* 2MB, 4MB page size */
	if (pw_is_big_page_pde(&pde_val, is_lme, is_pae, is_pse)) {
		uint64_t big_page_addr = PW_INVALID_GPA;
		uint32_t offset_in_big_page = 0;

		/* Retrieve address of the big page in guest space */
		big_page_addr = pw_retrieve_big_page_phys_addr(&pde_val,
			is_pae,
			FALSE);
		/* Retrieve offset in page */
		offset_in_big_page = pw_get_big_page_offset(virt_addr,
			is_pae,
			FALSE);
		/* Calculate full guest accessed physical address */
		gpa = big_page_addr + offset_in_big_page;

		if ((is_write) &&
		    (!pw_is_write_access_permitted
			     (&pml4te_val, &pdpte_val, &pde_val, NULL, is_user,
			     is_wp, is_lme,
			     is_pae, is_pse))) {
			native_pfec.bits.present = 1;
			retval = PW_RETVAL_PF;
			goto out;
		}

		if (is_user &&
		    (!pw_is_user_access_permitted
			     (&pml4te_val, &pdpte_val, &pde_val, NULL, is_lme,
			     is_pae,
			     is_pse))) {
			native_pfec.bits.present = 1;
			retval = PW_RETVAL_PF;
			goto out;
		}

		if (is_pae &&
		    is_nxe &&
		    is_fetch &&
		    (!pw_is_fetch_access_permitted
			     (&pml4te_val, &pdpte_val, &pde_val, NULL, is_lme,
			     is_pae,
			     is_pse))) {
			native_pfec.bits.present = 1;
			retval = PW_RETVAL_PF;
			goto out;
		}

		if (set_ad_bits) {
			pw_update_ad_bits(pml4te_ptr,
				&pml4te_val,
				pdpte_ptr,
				&pdpte_val,
				pde_ptr,
				&pde_val,
				NULL,
				NULL,
				is_write,
				is_lme,
				is_pae,
				is_pse);
		}

		retval = PW_RETVAL_SUCCESS;
		goto out;
	}

	/* 4KB page size */
	pt_gpa = pw_retrieve_phys_addr(&pde_val, is_pae);
	pte_ptr =
		pw_retrieve_table_entry(gpm_handle, pt_gpa, pte_index, is_pae,
			use_host_pt);
	if (pte_ptr == NULL) {
		retval = PW_RETVAL_PHYS_MEM_VIOLATION;
		goto out;
	}

	pw_read_entry_value(&pte_val, pte_ptr, is_pae);

	/* doesn't matter which field "non_pae" of "pae_lme" is used */
	if (!pte_val.non_pae_entry.bits.present) {
		retval = PW_RETVAL_PF;
		goto out;
	}

	if (!pw_are_reserved_bits_in_pte_cleared(&pte_val, is_nxe, is_lme,
		    is_pae)) {
		native_pfec.bits.present = 1;
		native_pfec.bits.is_reserved = 1;
		retval = PW_RETVAL_PF;
		goto out;
	}

	/* Retrieve GPA of guest PT */
	gpa = pw_retrieve_phys_addr(&pte_val, is_pae);

	if (is_write &&
	    (!pw_is_write_access_permitted
		     (&pml4te_val, &pdpte_val, &pde_val, &pte_val, is_user,
		     is_wp, is_lme,
		     is_pae, is_pse))) {
		native_pfec.bits.present = 1;
		retval = PW_RETVAL_PF;
		goto out;
	}

	if (is_user &&
	    (!pw_is_user_access_permitted
		     (&pml4te_val, &pdpte_val, &pde_val, &pte_val, is_lme,
		     is_pae,
		     is_pse))) {
		native_pfec.bits.present = 1;
		retval = PW_RETVAL_PF;
		goto out;
	}

	if (is_pae &&
	    is_nxe &&
	    is_fetch &&
	    (!pw_is_fetch_access_permitted
		     (&pml4te_val, &pdpte_val, &pde_val, &pte_val, is_lme,
		     is_pae,
		     is_pse))) {
		native_pfec.bits.present = 1;
		retval = PW_RETVAL_PF;
		goto out;
	}

	if (set_ad_bits) {
		pw_update_ad_bits(pml4te_ptr,
			&pml4te_val,
			pdpte_ptr,
			&pdpte_val,
			pde_ptr,
			&pde_val,
			pte_ptr,
			&pte_val,
			is_write,
			is_lme,
			is_pae,
			is_pse);
	}
	/* add offset */
	gpa |= (virt_addr & PAGE_4KB_MASK);
	/* page walk succeeded */
	retval = PW_RETVAL_SUCCESS;

out:
	if (gpa_out != NULL) {
		*gpa_out = gpa;
	}

	if ((retval == PW_RETVAL_PF) && (pfec_out != NULL)) {
		*pfec_out = native_pfec.uint64;
	}
	return retval;
}

boolean_t pw_is_pdpt_in_32_bit_pae_mode_valid(IN guest_cpu_handle_t gcpu,
					      IN void *pdpt_ptr)
{
	uint64_t efer_value;
	boolean_t is_nxe;
	boolean_t is_lme;
	hva_t pdpte_hva = (hva_t)pdpt_ptr;
	hva_t final_pdpte_hva =
		pdpte_hva +
		(PW_NUM_OF_PDPT_ENTRIES_IN_32_BIT_MODE *
		 PW_PAE_ENTRY_INCREMENT);

	efer_value = gcpu_get_msr_reg(gcpu, IA32_MON_MSR_EFER);
	is_nxe = ((efer_value & EFER_NXE) != 0);
	is_lme = ((efer_value & EFER_LME) != 0);

	for (pdpte_hva = (hva_t)pdpt_ptr; pdpte_hva < final_pdpte_hva;
	     pdpte_hva += PW_PAE_ENTRY_INCREMENT) {
		pw_page_entry_t *pdpte = (pw_page_entry_t *)pdpte_hva;

		if (!pdpte->pae_lme_entry.bits.present) {
			continue;
		}

		if (!pw_are_reserved_bits_in_pdpte_cleared(pdpte, is_nxe,
			    is_lme)) {
			return FALSE;
		}
	}

	return TRUE;
}
