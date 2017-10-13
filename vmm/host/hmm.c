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

#include <vmm_arch.h>
#include <heap.h>
#include <vmm_util.h>
#include <idt.h>
#include <gdt.h>
#include <dbg.h>
#include "hmm.h"
#include "stack.h"
#include "vmm_arch.h"
#include "host_cpu.h"

#include <lib/image_loader.h>
#include "lib/util.h"

#define CR3_ATTR_RW 0x3 //p,w,!x

typedef struct {
	mam_handle_t hva_to_hpa;
	mam_handle_t hpa_to_hva;
} hmm_t;   /* Host Memory Manager */

static hmm_t g_hmm;
uint64_t top_of_memory;
static uint64_t valloc_ptr;
/*-----------------------------------------------------------*/
static uint64_t hmm_page_vmalloc(uint32_t page_num)
{
	uint64_t ptr = valloc_ptr;

	valloc_ptr += page_num * PAGE_4K_SIZE;

	return ptr;
}

static void hmm_update_image_attr(uint64_t base)
{
	image_section_info_t image_section_info;
	cr3_attr_t attr;
	uint64_t section_start;
	uint64_t section_size;
	uint16_t index = 0;

	print_trace("HMM: Updating permissions to VMM image:\n");

	attr.uint32 = CR3_ATTR_RW;

	while (get_image_section((void *)base, index, &image_section_info)) {
		index++;
		if (!image_section_info.valid) {
			continue;
		}

		section_start = (uint64_t)image_section_info.start;
		VMM_ASSERT_EX(((section_start & 0xFFF) == 0),
			"HMM: Section start address 0x%llX isn't 4K page aligned\n", section_start);

		section_size = PAGE_ALIGN_4K(image_section_info.size);

		attr.bits.w = image_section_info.writable;
		attr.bits.x = image_section_info.executable;

		if (attr.uint32 != CR3_ATTR_RW) {
			mam_update_attr(g_hmm.hva_to_hpa, section_start, section_size,
					0x6, attr.uint32);
			print_trace("\tHMM: updated w%d x%d to section [0x%llX - 0x%llX]\n",
					attr.bits.w, attr.bits.x, section_start, section_start + section_size);
		} else {
			print_trace("\tHMM: keep w%d x%d to section [0x%llX - 0x%llX]\n",
					attr.bits.w, attr.bits.x, section_start, section_start + section_size);
		}
	}

}

static void hmm_remap_first_virtual_page(void)
{
	uint64_t virt_page = hmm_page_vmalloc(1);

	mam_insert_range(g_hmm.hva_to_hpa, 0, 0, PAGE_4K_SIZE, 0);

	mam_insert_range(g_hmm.hva_to_hpa, virt_page, 0, PAGE_4K_SIZE, CR3_ATTR_RW);
	mam_insert_range(g_hmm.hpa_to_hva, 0, virt_page, PAGE_4K_SIZE, 0x1);

	print_trace("HMM: Successfully remapped hpa (0) to hva (0x%llX)\n", virt_page);
}

/********************************************
*  Before remap, for each cpu:
*
*  -------------------
*  | exception stack |  hpa=hva
*  -------------------
*  |    cpu stack    |  hpa=hva
*  -------------------
*         ....
*  -------------------
*  | exception stack |  hpa=hva
*  -------------------
*  |    cpu stack    |  hpa=hva
*  -------------------
*  |    zero page    |  hpa=hva
*  -------------------
*
*  After remap, overflow and underflow for both
*  cpu stacks and exception stacks can be detected:
*
*  -------------------
*  | exception stack |  remap to high address
*  -------------------
*  |    cpu stack    |  hpa=hva
*  -------------------
*         ....
*  -------------------
*  | exception stack |  remap to high address
*  -------------------
*  |    cpu stack    |  hpa=hva
*  -------------------
*  |    zero page    |  unmapped
*  -------------------
*
*******************************************/

static void hmm_remap_exception_stack(void)
{
	uint16_t cpu_id;
	uint64_t page;
	uint64_t new_hva;
	uint64_t zero_page;

	print_trace("HMM: Remapping the exception stacks:\n");

	for (cpu_id = 0; cpu_id < host_cpu_num; cpu_id++) {
		/* get the end address of each stack page */
		page = stack_get_exception_sp(cpu_id);
		/* calculate the start address of each stack page */
		page -= PAGE_4K_SIZE;

		/* allocate 3 continuous available virtual pages */
		new_hva = hmm_page_vmalloc(3);
		/* select the middle of these 3 virtual pages to map,
		 * so the virtual address before and after are both not mapped*/
		new_hva += PAGE_4K_SIZE;

		/* unmap the page of exception stack from paging table */
		mam_insert_range(g_hmm.hva_to_hpa, page, 0, PAGE_4K_SIZE, 0);
		/* map the page of exception stack to new virtual page address */
		mam_insert_range(g_hmm.hva_to_hpa, new_hva, page, PAGE_4K_SIZE, CR3_ATTR_RW);
		mam_insert_range(g_hmm.hpa_to_hva, page, new_hva, PAGE_4K_SIZE, 0x1);

		/* The lower and higher pages should remain unmapped in order to
		 * track stack overflow and underflow */
		set_tss_ist(cpu_id, 0,new_hva + PAGE_4K_SIZE);

		print_trace("\tRemapped hva(0x%llX) <--> hpa(0x%llX) to exception stack"
			" for cpu%d\n", new_hva, page, cpu_id);
	}

	/* add zero page as last page in stack */
	zero_page = stack_get_zero_page();
	memset((void *)zero_page, 0 , PAGE_4K_SIZE);
	/* unmap zero page */
	mam_insert_range(g_hmm.hpa_to_hva, zero_page, 0, PAGE_4K_SIZE, 0);
}

/****************
** CR3 entry ops **
*****************/
#define CR3_P (1ULL << 0)
#define CR3_W (1ULL << 1)
#define CR3_PI (3ULL << 3) // bit [4:3]
#define CR3_PS (1ULL << 7)
#define CR3_XD (1ULL << 63)
#define CR3_ATTR_MASK 0x1B //for p,w,pi

static uint32_t cr3_max_leaf_level(void)
{
	cpuid_params_t cpuid_params = {0x80000001, 0, 0, 0};

	asm_cpuid(&cpuid_params);
	if (cpuid_params.edx & CPUID_EDX_1G_PAGE)
		return MAM_LEVEL_PDPT;
	return MAM_LEVEL_PD;
}

static boolean_t cr3_is_leaf(uint64_t entry, uint32_t level)
{
	if (level == MAM_LEVEL_PT)
		return TRUE;
	if (entry & CR3_P) // present
		return entry & CR3_PS;
	//not present
	return TRUE;
}

static boolean_t cr3_is_present(uint32_t attr)
{
	return attr & CR3_P;
}

static void cr3_to_table(uint64_t *p_entry)
{
	*p_entry &= MASK64_MID(51,12); // clear attr
	*p_entry |= CR3_P | CR3_W; //cr3_pat_wb_index=0(msr_pat[0:7]=0x6=WB)
}

static void cr3_to_leaf(uint64_t *p_entry, uint32_t level, uint32_t attr)
{
	cr3_attr_t cr3_attr;

	D(VMM_ASSERT(level <= MAM_LEVEL_PML4));

	cr3_attr.uint32 = attr;

	if (cr3_attr.bits.p)
	{
		*p_entry &= MASK64_MID(51,12); // clear attr
		*p_entry |= (uint64_t)(attr & CR3_ATTR_MASK); // set p,w,pi
		if (cr3_attr.bits.x == 0) // set xd
			*p_entry |= CR3_XD;
		if (level != MAM_LEVEL_PT)
			*p_entry |= CR3_PS;
	}
	else
	{
		// not present
		*p_entry = 0;
	}
}

static uint32_t cr3_leaf_get_attr(uint64_t leaf_entry, UNUSED uint32_t level)
{
	cr3_attr_t cr3_attr;

	D(VMM_ASSERT(level <= MAM_LEVEL_PML4));

	if ((leaf_entry & CR3_P) == 0) // not present
		return 0;
	cr3_attr.uint32 = leaf_entry & CR3_ATTR_MASK; // get p,w,pi
	if ((leaf_entry & CR3_XD) == 0)
		cr3_attr.bits.x = 1;
	return cr3_attr.uint32;
}

static mam_entry_ops_t* cr3_make_entry_ops(void)
{
	mam_entry_ops_t *entry_ops;

	entry_ops = mem_alloc(sizeof(mam_entry_ops_t));
	entry_ops->max_leaf_level = cr3_max_leaf_level();
	entry_ops->is_leaf = cr3_is_leaf;
	entry_ops->is_present = cr3_is_present;
	entry_ops->to_table = cr3_to_table;
	entry_ops->to_leaf = cr3_to_leaf;
	entry_ops->leaf_get_attr = cr3_leaf_get_attr;
	return entry_ops;
}

/*******************************
** reverse entry ops (for hpa->hva **
*******************************/
#define RVS_P (1ULL << 0)
#define RVS_PS (1ULL << 7)

static boolean_t rvs_is_leaf(uint64_t entry, UNUSED uint32_t level)
{
	if (entry & RVS_P) // present
		return entry & RVS_PS;
	//not present
	return TRUE;
}

static boolean_t rvs_is_present(uint32_t attr)
{
	return attr & RVS_P;
}

static void rvs_to_table(uint64_t *p_entry)
{
	*p_entry &= MASK64_MID(51,12); // clear attr
	*p_entry |= RVS_P;
}

static void rvs_to_leaf(uint64_t *p_entry, UNUSED uint32_t level, uint32_t attr)
{
	if (attr & RVS_P) // present
	{
		*p_entry &= MASK64_MID(51,12); // clear attr
		*p_entry |= RVS_P | RVS_PS;
	}
	else
	{
		*p_entry = 0;
	}
}

static uint32_t rvs_leaf_get_attr(uint64_t leaf_entry, UNUSED uint32_t level)
{
	if (leaf_entry & RVS_P)
		return RVS_P;
	else
		return 0;
}

static mam_entry_ops_t* rvs_make_entry_ops(void)
{
	mam_entry_ops_t *entry_ops;
	entry_ops = mem_alloc(sizeof(mam_entry_ops_t));

	entry_ops->max_leaf_level = MAM_LEVEL_PML4;
	entry_ops->is_leaf = rvs_is_leaf;
	entry_ops->is_present = rvs_is_present;
	entry_ops->to_table = rvs_to_table;
	entry_ops->to_leaf = rvs_to_leaf;
	entry_ops->leaf_get_attr = rvs_leaf_get_attr;
	return entry_ops;
}

void hmm_setup(evmm_desc_t *evmm_desc)
{
	D(VMM_ASSERT_EX(evmm_desc, "HMM: evmm_desc is NULL\n"));

	print_trace("\nHMM: Initializing...\n");

	/* Create HVA -> HPA mapping */
	g_hmm.hva_to_hpa = mam_create_mapping(cr3_make_entry_ops(), 0);
	g_hmm.hpa_to_hva = mam_create_mapping(rvs_make_entry_ops(), 0);
	valloc_ptr = top_of_memory;

	/* Creating initial mapping for range 0 - 4G (+ existing memory above 4G)
	 * with write permissions*/
	mam_insert_range(g_hmm.hva_to_hpa, 0, 0, top_of_memory, CR3_ATTR_RW);
	mam_insert_range(g_hmm.hpa_to_hva, 0, 0, top_of_memory, 0x1);

	print_trace("HMM: Created mapping for range 0 - 0x%llX with "
			"write permissions\n", top_of_memory);

	/* Update permissions for VMM image */
	hmm_update_image_attr(evmm_desc->evmm_file.runtime_addr);

	/* Remap the first virtual page to use hpa(0) page memory */
	hmm_remap_first_virtual_page();

	/* Remap the exception stack to trace each stack overflow and underflow */
	hmm_remap_exception_stack();

	print_trace("\nHMM: Host Memory Manager was successfully initialized\n");
}

#define DEFAULT_PAT_VALUE 0x0007040600070406ULL

void hmm_enable(void)
{
	uint64_t new_cr3;
	uint64_t pat0;

	pat0 = (asm_rdmsr(MSR_PAT) & 0xFFULL);
	if (((cache_type_t)pat0) != CACHE_TYPE_WB) {
		print_warn("HMM: PAT change: 0x%llx -> 0x%llx\n",
			asm_rdmsr(MSR_PAT), DEFAULT_PAT_VALUE);
		asm_wrmsr(MSR_PAT, DEFAULT_PAT_VALUE);
	}

	asm_wrmsr(MSR_EFER, (asm_rdmsr(MSR_EFER) | EFER_NXE));

	D(VMM_ASSERT(g_hmm.hva_to_hpa));

	/* PCD and PWT bits will be 0; */
	new_cr3 = mam_get_table_hpa(g_hmm.hva_to_hpa);
	print_trace("HMM: New cr3=0x%llX. \n", new_cr3);
	asm_set_cr3(new_cr3);

	print_trace("HMM: Successfully updated CR3 to new value\n");
}


/*-------------------------------------------------------------------------
 * Function: hmm_hva_to_hpa
 *  Description: This function is used in order to convert Host Virtual Address
 *               to Host Physical Address (HVA-->HPA).
 *  Input: hva - host virtual address.
 *  Output: hpa - host physical address.
 *          p_attr - page table entry attribute, can be NULL.
 *  Return Value: TRUE in case the mapping successful (it exists).
 *------------------------------------------------------------------------- */
boolean_t hmm_hva_to_hpa(IN uint64_t hva, OUT uint64_t *p_hpa, OUT cr3_attr_t *p_attr)
{
	mam_handle_t hva_to_hpa = g_hmm.hva_to_hpa;

	D(VMM_ASSERT_EX(p_hpa, "HMM: p_hpa is NULL\n"));

	/* Before hpa/hva mapping is setup, assume 1:1 mapping */
	if (hva_to_hpa == NULL) {
		*p_hpa = hva;
		if (p_attr) {
			p_attr->uint32 = 0x3;
		}
		return TRUE;
	}

	return mam_get_mapping(hva_to_hpa, hva, p_hpa, &p_attr->uint32);
}

/*-------------------------------------------------------------------------
 * Function: hmm_hpa_to_hva
 *  Description: This function is used in order to convert Host Physical Address
 *               to Host Virtual Address (HPA-->HVA), i.e. converting physical
 *               address to pointer.
 *  Input: hpa - host physical address.
 *  Output: p_hva - host virtual address.
 *  Return Value: TRUE in case the mapping successful (it exists).
 *------------------------------------------------------------------------- */
boolean_t hmm_hpa_to_hva(IN uint64_t hpa, OUT uint64_t *p_hva)
{
	mam_handle_t hpa_to_hva = g_hmm.hpa_to_hva;

	D(VMM_ASSERT_EX(p_hva, "HMM: p_hva is NULL\n"));

	/* Before hpa/hva mapping is setup, assume 1:1 mapping */
	if (hpa_to_hva == NULL) {
		*p_hva = hpa;
		return TRUE;
	}

	return mam_get_mapping(hpa_to_hva, hpa, p_hva, NULL);
}

void hmm_unmap_hpa(IN uint64_t hpa, uint64_t size)
{
	mam_handle_t hpa_to_hva;
	mam_handle_t hva_to_hpa;
	uint64_t hva;
	uint64_t size_tmp;
	D(uint64_t hpa_tmp);

	D(VMM_ASSERT(g_hmm.hpa_to_hva));
	D(VMM_ASSERT(g_hmm.hva_to_hpa));
	D(VMM_ASSERT((hpa & 0xFFF) == 0));
	D(VMM_ASSERT((size & 0xFFF) == 0));

	hpa_to_hva = g_hmm.hpa_to_hva;
	hva_to_hpa = g_hmm.hva_to_hpa;
	size_tmp = size;

	while (size_tmp != 0) {
		if (hmm_hpa_to_hva(hpa, &hva)) {
			D(VMM_ASSERT(hmm_hva_to_hpa(hva,
				&hpa_tmp, NULL) && (hpa_tmp == hpa)));
			D(VMM_ASSERT((hva & 0xFFF) == 0));

			mam_insert_range(hva_to_hpa, hva, 0, PAGE_4K_SIZE, 0);
		}

		size_tmp -= PAGE_4K_SIZE;
		hpa += PAGE_4K_SIZE;
	}
	mam_insert_range(hpa_to_hva, hpa, 0, size, 0);

	hw_flash_tlb();
}
