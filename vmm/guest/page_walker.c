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
#include "gpm.h"
#include "hmm.h"
#include "guest.h"
#include "gcpu.h"
#include "vmm_util.h"
#include "page_walker.h"

#define PAGE_FLAG_P (1ull<<0)
#define PAGE_FLAG_RW (1ull<<1)
#define PAGE_FLAG_US (1ull<<2)
#define PAGE_FLAG_PS (1ull<<7)

#define PF_FLAG_P 0x1
#define PF_FLAG_RW 0x2
#define PF_FLAG_US 0x4
#define PF_FLAG_RSVD 0x8
#define PF_FLAG_PK 0x20

/* When PF_FLAG_P in __ec is set, it means the #PF is
 * caused by "one of the P bit in page structures is 0".
 * in this case, according to IA32 spec, the PF_FLAG_P in
 * __pfec->ec should be cleared.
 * on the other hand, if PF_FLAG_P in __ec is clear, PF_FLAG_P
 * in __pfec->ec should be set to indicate the #PF is NOT caused
 * by "one of the P bit in page structures is 0" */
#define set_pfec(__pfec, __access, __cpl, __ec) { \
	__pfec->is_pf = TRUE;			\
	__pfec->ec = __ec ^ PF_FLAG_P;		\
	if (__cpl == 3)                           \
		__pfec->ec |= PF_FLAG_US;       \
	if (__access & GUEST_CAN_WRITE)		\
		__pfec->ec |= PF_FLAG_RW;}

static uint32_t maxphyaddr;
static boolean_t pse36_supported;
static boolean_t page1GB_supported;

static uint32_t get_cpuid_pse36(void)
{
	cpuid_params_t cpuid_params = {1, 0, 0, 0};

	asm_cpuid(&cpuid_params);

	return (cpuid_params.edx & CPUID_EDX_PSE36);
}

static uint32_t get_cpuid_page1GB(void)
{
	cpuid_params_t cpuid_params = {0x80000001, 0, 0, 0};

	asm_cpuid(&cpuid_params);

	return (cpuid_params.edx & CPUID_EDX_1G_PAGE);
}

void page_walk_init(void)
{
	maxphyaddr = get_max_phy_addr();

	if (maxphyaddr > 52) {
		print_warn("the max physical address(%d) is greater than 52\n", maxphyaddr);
		maxphyaddr = 52;
	}

	if (get_cpuid_pse36()) {
		pse36_supported = TRUE;
	} else {
		pse36_supported = FALSE;
	}

	if (get_cpuid_page1GB()) {
		page1GB_supported = TRUE;
	} else {
		page1GB_supported = FALSE;
	}
}

inline static boolean_t check_us(uint64_t entry, uint32_t cpl)
{
	uint32_t us = entry & PAGE_FLAG_US;

	//user-mode access supervisor address
	if ((cpl == 3) && (us == 0)) {
		return FALSE;
	}
	return TRUE;
}

inline static boolean_t check_smap(boolean_t is_user_addr, uint32_t cpl, uint64_t cr4, uint64_t rflags)
{
	uint32_t ac = rflags & RFLAGS_AC;

	if ((cpl < 3) && is_user_addr && //supervisor-mode access user address
		(cr4 & CR4_SMAP) && (ac == 0)) { //SMAP enable
		return FALSE;
	}
	return TRUE;
}

static boolean_t check_rw(uint64_t entry, uint32_t access, uint32_t cpl, uint64_t cr0)
{
	uint32_t rw = entry & PAGE_FLAG_RW;

	if ((access & GUEST_CAN_WRITE) && (rw == 0)) {
		if (cpl == 3) { //user-mode write access when the R/W flag is 0
			return FALSE;
		} else {
			//supervisor-mode write access when the R/W flag is 0
			if (cr0 & CR0_WP) {
				return FALSE;
			}
		}
	}
	return TRUE;
}

inline static uint32_t get_pkru(void)
{
	uint32_t pkru;
	uint64_t cr4;

	cr4 = asm_get_cr4();
	asm_set_cr4(cr4 | CR4_PKE);
	pkru = asm_get_pkru(); //rdpkru can only be executed when CR4.PKE is set
	asm_set_cr4(cr4);

	return pkru;
}

static boolean_t check_pk(uint64_t entry, uint32_t cpl, uint32_t access, uint64_t cr0, uint64_t cr4)
{
	uint8_t pk;
	uint32_t pkru;
	uint32_t ad;
	uint32_t wd;
	uint32_t us = entry & PAGE_FLAG_US;

	//access user address and CR4.PKE is set
	if (us && (cr4 & CR4_PKE)) {
		print_warn("protection key is enabled in guest\n");
		pkru = get_pkru();
		pk = (uint8_t)(entry >> 59) & 0xf;
		ad = pkru & (1U << (pk * 2));
		wd = pkru & (1U << (pk * 2 + 1));

		if (ad) { //access disable
			return FALSE;
		}
		if (wd && (access & GUEST_CAN_WRITE)) {
			if (cpl == 3) { //user-mode write access
				return FALSE;
			} else {
				//supervisor-mode write access
				if (cr0 & CR0_WP) {
					return FALSE;
				}
			}
		}
	}
	return TRUE;
}

static boolean_t x86_check_reserved_bits(uint32_t entry, uint32_t level, uint64_t cr4)
{
	if ((level == MAM_LEVEL_PT) || //no reserved bits in PTE
		//no reserved bits in PDE pointer to sub page table
		((cr4 & CR4_PSE) == 0) || ((entry & PAGE_FLAG_PS) == 0)) {
 		return TRUE;
	}
	//PDE pointing to 4M page
	if (pse36_supported) {
		if (entry & (uint32_t)MASK64_MID(21, (MIN(40, maxphyaddr) - 19))) {
			return FALSE;
		}
	} else {
		if (entry & MASK64_MID(21, 13)) { //Reserved bit [21:13]
			return FALSE;
		}
	}
	return TRUE;
}

static boolean_t pae_check_reserved_bits(uint64_t entry, uint32_t level, uint64_t efer)
{
	switch (level) {
	case MAM_LEVEL_PDPT:
		if (entry & MASK64_MID(2, 1)) { //Reverved bits [2:1]
			return FALSE;
		}
		if (entry & MASK64_MID(8, 5)) { //Reverved bits [8:5]
			return FALSE;
		}
		if (entry & MASK64_MID(63, maxphyaddr)) { //Reverved bits [63:M]
			return FALSE;
		}
		break;
	case MAM_LEVEL_PD:
		if (entry & PAGE_FLAG_PS) { //PD (2M page)
			if (entry &  MASK64_MID(20, 13)) { //Reverved bits [20:13]
				return FALSE;
			}
		}
		if (entry & MASK64_MID(62, maxphyaddr)) { //Reverved bits [62:M]
			return FALSE;
		}
		//if IA32_EFER.NXE = 0 and the P flag of a PDE or a PTE is 1, the XD flag(bit 63) is reserved.
		if (((efer & EFER_NXE) == 0) && (entry >> 63)) { //Reverved bits [63]
			return FALSE;
		}
		break;
	case MAM_LEVEL_PT:
		if (entry & MASK64_MID(62, maxphyaddr)) { //Reverved bits [62:M]
			return FALSE;
		}
		//if IA32_EFER.NXE = 0 and the P flag of a PDE or a PTE is 1, the XD flag(bit 63) is reserved.
		if (((efer & EFER_NXE) == 0) && (entry >> 63)) { //Reverved bits [63]
			return FALSE;
		}
		break;
	default:
		return FALSE;
	}

	return TRUE;
}

static boolean_t x64_check_reserved_bits(uint64_t entry, uint32_t level, uint64_t efer)
{
	switch (level) {
	case MAM_LEVEL_PML4:
		if (entry & PAGE_FLAG_PS) { //Reverved bits [7]
			return FALSE;
		}
		break;
	case MAM_LEVEL_PDPT:
		//the PS flag of a PDPTE is reserved and must be 0 if 1-GByte pages are not supported.
		if ((!page1GB_supported) && (entry & PAGE_FLAG_PS)) { //Reverved bits [7]
			return FALSE;
		}
		if (entry & PAGE_FLAG_PS) { // PDPT (1G page)
			if (entry & MASK64_MID(29, 13)) { //Reverved bits [29:13]
				return FALSE;
			}
		}
		break;
	case MAM_LEVEL_PD:
		if (entry & PAGE_FLAG_PS) { // PD (2M page)
			if (entry & MASK64_MID(20, 13)) { //Reverved bits [20:13]
				return FALSE;
			}
		}
		break;
	case MAM_LEVEL_PT:
		break;
	default:
		return FALSE;
	}

	if (entry & MASK64_MID(51, maxphyaddr)) { //Reverved bits [51:M]
		return FALSE;
	}
	//if IA32_EFER.NXE = 0 and the P flag of a paging-structure entry is 1, the XD flag(bit 63) is reserved.
	if (((efer & EFER_NXE) == 0) && (entry >> 63)) { //Reverved bits [63]
		return FALSE;
	}

	return TRUE;
}

static boolean_t x86_gva_to_gpa(IN guest_cpu_handle_t gcpu,
					IN uint32_t gva,
					IN uint32_t access,
					OUT uint64_t *p_gpa,
					OUT pf_ec_t *p_pfec)

{
	uint32_t level;
	uint32_t cpl;
	uint32_t entry;
	uint64_t rflags;
	uint32_t* table_hva;
	uint64_t table_gpa;
	uint64_t cr0;
	uint64_t cr4;
	boolean_t is_user_addr;

	is_user_addr = TRUE;
	level = MAM_LEVEL_PD;
	cr0 = gcpu_get_visible_cr0(gcpu);
	cr4 = gcpu_get_visible_cr4(gcpu);
	rflags = vmcs_read(gcpu->vmcs, VMCS_GUEST_RFLAGS);
	cpl = ((uint32_t)vmcs_read(gcpu->vmcs, VMCS_GUEST_CS_SEL)) & 0x3;

	table_gpa = vmcs_read(gcpu->vmcs, VMCS_GUEST_CR3) & MASK64_MID(31,12);
	while (TRUE) {
		if (!gpm_gpa_to_hva(gcpu->guest, table_gpa, access, (uint64_t *)&table_hva)) {
			print_warn("%s: failed to translate gpa(0x%llx)\n",
				__FUNCTION__, table_gpa);
			p_pfec->is_pf = FALSE; //error in evmm
			return FALSE;
		}
		entry = *(table_hva + ((gva >> (12 + (10*level))) & 0x3FF));
		if (!(entry & PAGE_FLAG_P)) {
			print_warn("%s: #PF was caused by a no-present page\n", __FUNCTION__);
			print_warn("gva=0x%X, entry=0x%x, level=%d\n", gva, entry, level);
			//set error code to 0 means the #PF is caused by any P bit cleared in page structures.
			set_pfec(p_pfec, access, cpl, PAGE_FLAG_P);
			return FALSE;
		}
		if (!x86_check_reserved_bits(entry, level, cr4)) {
			print_warn("%s: #PF was caused by a reserved bit set to 1\n", __FUNCTION__);
			print_warn("gva=0x%X, entry=0x%x, level=%d, cr4=0x%llx\n", gva, entry, level, cr4);
			set_pfec(p_pfec, access, cpl, PF_FLAG_RSVD);
			return FALSE;
		}
		if (!check_us(entry, cpl)) {
			print_warn("%s: #PF was caused by us check\n", __FUNCTION__);
			print_warn("gva=0x%X, entry=0x%x, level=%d, cpl=%d\n", gva, entry, level, cpl);
			set_pfec(p_pfec, access, cpl, 0);
			return FALSE;
		}
		if (!check_rw(entry, access, cpl, cr0)) {
			print_warn("%s: #PF was caused by a write\n", __FUNCTION__);
			print_warn("gva=0x%X, entry=0x%x, level=%d, cpl=%d, cr0=0x%llx\n", gva, entry, level, cpl, cr0);
			set_pfec(p_pfec, access, cpl, 0);
			return FALSE;
		}
		//only the U/S flag(bit 2) is 1 in all of the paging-structure entries, the address is a user-mode address
		is_user_addr = is_user_addr && (entry & PAGE_FLAG_US);
		if ((level == MAM_LEVEL_PT) || ((cr4 & CR4_PSE) && (entry & PAGE_FLAG_PS))) { //4KB page or 4MB page
			if (!check_smap(is_user_addr, cpl, cr4, rflags)) {
				print_warn("%s: #PF was caused by smap check\n", __FUNCTION__);
				print_warn("gva=0x%X, entry=0x%x, level=%d, is_user_addr=%d, cpl=%d, cr4=0x%llx, rflags=0x%llx\n",
					gva, entry, level, is_user_addr, cpl, cr4, rflags);
				set_pfec(p_pfec, access, cpl, 0);
				return FALSE;
			}
			if (level == MAM_LEVEL_PT) { //4KB page
				*p_gpa = ((uint64_t)entry & MASK64_MID(31, 12)) |
					((uint64_t)gva & MASK64_LOW(12));
			} else { //4MB page
				*p_gpa = ((uint64_t)entry & MASK64_MID(31, 22)) |
					(((uint64_t)entry & MASK64_MID(20, 13)) << 19) |
					((uint64_t)gva & MASK64_LOW(22));
			}
			return TRUE;
		} else { //page table
			table_gpa = (uint64_t)entry & MASK64_MID(31, 12);
		}
		level--;
	}
}

static boolean_t pae_gva_to_gpa(IN guest_cpu_handle_t gcpu,
					IN uint32_t gva,
					IN uint32_t access,
					OUT uint64_t *p_gpa,
					OUT pf_ec_t *p_pfec)

{
	uint32_t level;
	uint32_t cpl;
	uint64_t rflags;
	uint64_t entry;
	uint64_t* table_hva;
	uint64_t table_gpa;
	uint64_t cr0;
	uint64_t cr4;
	uint64_t efer;
	boolean_t is_user_addr;

	is_user_addr = TRUE;
	level = MAM_LEVEL_PDPT;
	cr0 = gcpu_get_visible_cr0(gcpu);
	cr4 = gcpu_get_visible_cr4(gcpu);
	rflags = vmcs_read(gcpu->vmcs, VMCS_GUEST_RFLAGS);
	cpl = ((uint32_t)vmcs_read(gcpu->vmcs, VMCS_GUEST_CS_SEL)) & 0x3;
	efer = vmcs_read(gcpu->vmcs, VMCS_GUEST_EFER);

	table_gpa = vmcs_read(gcpu->vmcs, VMCS_GUEST_CR3) & MASK64_MID(31,5);
	while (TRUE) {
		if (!gpm_gpa_to_hva(gcpu->guest, table_gpa, access, (uint64_t *)&table_hva)) {
			print_warn("%s: failed to translate gpa(0x%llx)\n",
				__FUNCTION__, table_gpa);
			p_pfec->is_pf = FALSE; //error in evmm
			return FALSE;
		}
		entry = *(table_hva + ((gva >> (12 + (9*level))) & 0x1FF));
		if (!(entry & PAGE_FLAG_P)) {
			print_warn("%s: #PF was caused by a no-present page\n", __FUNCTION__);
			print_warn("gva=0x%X, entry=0x%llx, level=%d\n", gva, entry, level);
			//P bit in error code is 0 means the #PF is caused by any P bit cleared in page structures.
			set_pfec(p_pfec, access, cpl, PAGE_FLAG_P);
			return FALSE;
		}
		if (!pae_check_reserved_bits(entry, level, efer)) {
			print_warn("%s: #PF was caused by a reserved bit set to 1\n", __FUNCTION__);
			print_warn("gva=0x%X, entry=0x%llx, level=%d, efer=0x%llx\n",
				gva, entry, level, efer);
			set_pfec(p_pfec, access, cpl, PF_FLAG_RSVD);
			return FALSE;
		}
		if (level != MAM_LEVEL_PDPT) { //there's no rw, us bits in PAE's PDPT entry
			if (!check_us(entry, cpl)) {
				print_warn("%s: #PF was caused by us check\n", __FUNCTION__);
				print_warn("gva=0x%X, entry=0x%llx, level=%d, cpl=%d\n", gva, entry, level, cpl);
				set_pfec(p_pfec, access, cpl, 0);
				return FALSE;
			}
			if (!check_rw(entry, access, cpl, cr0)) {
				print_warn("%s: #PF was caused by a write\n", __FUNCTION__);
				print_warn("gva=0x%X, entry=0x%llx, level=%d, cpl=%d, cr0=0x%llx\n", gva, entry, level, cpl, cr0);
				set_pfec(p_pfec, access, cpl, 0);
				return FALSE;
			}
			//only the U/S flag(bit 2) is 1 in all of the paging-structure entries, the address is a user-mode address
			is_user_addr = is_user_addr && (entry & PAGE_FLAG_US);
		}
		if ((level == MAM_LEVEL_PT) || (entry & PAGE_FLAG_PS)) {
			if (!check_smap(is_user_addr, cpl, cr4, rflags)) {
				print_warn("%s: #PF was caused by smap check\n", __FUNCTION__);
				print_warn("gva=0x%X, entry=0x%llx, level=%d, is_user_addr=%d, cpl=%d, cr4=0x%llx, rflags=0x%llx\n",
					gva, entry, level, is_user_addr, cpl, cr4, rflags);
				set_pfec(p_pfec, access, cpl, 0);
				return FALSE;
			}

			*p_gpa = (entry & MASK64_MID(51, 12+9*level)) |
				((uint64_t)gva & MASK64_LOW(12+9*level));
			return TRUE;
		} else {
			table_gpa = entry & MASK64_MID(51, 12);
		}
		level--;
	}
}

static boolean_t x64_gva_to_gpa(IN guest_cpu_handle_t gcpu,
					IN uint64_t gva,
					IN uint32_t access,
					OUT uint64_t *p_gpa,
					OUT pf_ec_t *p_pfec)

{
	uint32_t level;
	uint32_t cpl;
	uint64_t rflags;
	uint64_t entry;
	uint64_t* table_hva;
	uint64_t table_gpa;
	uint64_t cr0;
	uint64_t cr4;
	uint64_t efer;
	boolean_t is_user_addr;

	is_user_addr = TRUE;
	level = MAM_LEVEL_PML4;
	cr0 = gcpu_get_visible_cr0(gcpu);
	cr4 = gcpu_get_visible_cr4(gcpu);
	rflags = vmcs_read(gcpu->vmcs, VMCS_GUEST_RFLAGS);
	cpl = ((uint32_t)vmcs_read(gcpu->vmcs, VMCS_GUEST_CS_SEL)) & 0x3;
	efer = vmcs_read(gcpu->vmcs, VMCS_GUEST_EFER);

	table_gpa = vmcs_read(gcpu->vmcs, VMCS_GUEST_CR3) & MASK64_MID(51, 12);
	while (TRUE) {
		if (!gpm_gpa_to_hva(gcpu->guest, table_gpa, access, (uint64_t *)&table_hva)) {
			print_warn("%s: failed to translate gpa (0x%llx).\n",
				__FUNCTION__, table_gpa);
			p_pfec->is_pf = FALSE;
			return FALSE;
		}
		entry = *(table_hva + ((uint32_t)(gva >> (12 + (9*level))) & 0x1FF));
		if (!(entry & PAGE_FLAG_P)) {
			print_warn("%s: #PF was caused by a no-present page\n", __FUNCTION__);
			print_warn("gva=0x%llX, entry=0x%llx, level=%d\n", gva, entry, level);
			//P bit in error code is 0 means the #PF is caused by any P bit cleared in page structures.
			set_pfec(p_pfec, access, cpl, PAGE_FLAG_P);
			return FALSE;
		}
		if (!x64_check_reserved_bits(entry, level, efer)) {
			print_warn("%s: #PF was caused by a reserved bit set to 1\n", __FUNCTION__);
			print_warn("gva=0x%llX, entry=0x%llx, level=%d, efer=0x%llx\n", gva, entry, level, efer);
			set_pfec(p_pfec, access, cpl, PF_FLAG_RSVD);
			return FALSE;
		}
		if (!check_us(entry, cpl)) {
			print_warn("%s: #PF was caused by us check\n", __FUNCTION__);
			print_warn("gva=0x%llX, entry=0x%llx, level=%d, cpl=%d\n", gva, entry, level, cpl);
			set_pfec(p_pfec, access, cpl, 0);
			return FALSE;
		}
		if (!check_rw(entry, access, cpl, cr0)) {
			print_warn("%s: #PF was caused by a write\n", __FUNCTION__);
			print_warn("gva=0x%llX, entry=0x%llx, level=%d, cpl=%d, cr0=0x%llx\n", gva, entry, level, cpl, cr0);
			set_pfec(p_pfec, access, cpl, 0);
			return FALSE;
		}
		//only the U/S flag(bit 2) is 1 in all of the paging-structure entries, the address is a user-mode address
		is_user_addr = is_user_addr && (entry & PAGE_FLAG_US);
		if ((level == MAM_LEVEL_PT) || (entry & PAGE_FLAG_PS)) {
			if (!check_smap(is_user_addr, cpl, cr4, rflags)) {
				print_warn("%s: #PF was caused by smap check\n", __FUNCTION__);
				print_warn("gva=0x%llX, entry=0x%llx, level=%d, is_user_addr=%d, cpl=%d, cr4=0x%llx, rflags=0x%llx\n",
					gva, entry, level, is_user_addr, cpl, cr4, rflags);
				set_pfec(p_pfec, access, cpl, 0);
				return FALSE;
			}
			if (!check_pk(entry, cpl, access, cr0, cr4)) {
				print_warn("%s: #PF was caused by protection keys\n", __FUNCTION__);
				print_warn("gva=0x%llX, entry=0x%llx, level=%d, cpl=%d, access=%d, cr0=0x%llx, cr4=0x%llx\n",
					gva, entry, level, cpl, access, cr0, cr4);
				set_pfec(p_pfec, access, cpl, PF_FLAG_PK);
				return FALSE;
			}

			*p_gpa = (entry & MASK64_MID(51, 12+9*level)) |
				(gva & MASK64_LOW(12+9*level));
			return TRUE;
		} else {
			table_gpa = entry & MASK64_MID(51, 12);
		}
		level--;
	}
}


/*-------------------------------------------------------------------------
 * Function: gcpu_gva_to_gpa
 *  Description: This function is used in order to convert Guest Virtual Address
 *               to Guest physical Address (GVA-->GPA).
 *  Input:  gcpu - guest cpu handle.
 *          gva - guest virtual address.
 *          access - access rights, can be read, write, or read and write
 *  Output: p_gpa - guest physical address, it is valid when return true.
 *          p_pfec - page fault error code, it is valid when return false.
 *  Return Value: TRUE in case the mapping successful (it exists).
 *------------------------------------------------------------------------- */
boolean_t gcpu_gva_to_gpa(IN guest_cpu_handle_t gcpu,
				IN uint64_t gva,
				IN uint32_t access,
				OUT uint64_t *p_gpa,
				OUT pf_ec_t *p_pfec)
{
	uint64_t cr0;
	uint64_t cr4;
	uint64_t efer;

	D(VMM_ASSERT(gcpu);)
	VMM_ASSERT_EX(p_gpa, "%s: p_gpa is NULL\n", __FUNCTION__);
	VMM_ASSERT_EX(p_pfec, "%s: p_pfec is NULL\n", __FUNCTION__);
	VMM_ASSERT_EX(((access & (GUEST_CAN_READ | GUEST_CAN_WRITE)) != 0),
		"%s: access(%d) is invalid\n", __FUNCTION__, access);
	VMM_ASSERT_EX(maxphyaddr,
		"%s: page_walk_init() must be called before here\n", __FUNCTION__);

	cr0 = gcpu_get_visible_cr0(gcpu);
	if ((cr0 & CR0_PG) == 0) { // PG(31) is clear, no paging
		*p_gpa = gva;
		return TRUE;
	}
	cr4 = gcpu_get_visible_cr4(gcpu);
	if ((cr4 & CR4_PAE) == 0) { // PAE(5) is clear, 32 bit paging
			return x86_gva_to_gpa(gcpu, gva, access, p_gpa, p_pfec);
	} else {
		efer = vmcs_read(gcpu->vmcs, VMCS_GUEST_EFER);
		if (efer & EFER_LME) { // LME(10) is set
			return x64_gva_to_gpa(gcpu, gva, access, p_gpa, p_pfec);
		}
		else
			return pae_gva_to_gpa(gcpu, gva, access, p_gpa, p_pfec);
	}

	//print_panic("%s: never reach here\n", __FUNCTION__);
	//p_pfec->is_pf = FALSE;

	//return FALSE;
}
