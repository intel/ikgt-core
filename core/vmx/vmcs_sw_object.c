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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMCS_SW_OBJECT_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMCS_SW_OBJECT_C, __condition)
#include "mon_defs.h"
#include "mon_dbg.h"
#include "memory_allocator.h"
#include "cache64.h"
#include "mon_objects.h"
#include "guest.h"
#include "guest_cpu.h"
#include "gpm_api.h"
#include "host_memory_manager_api.h"
#include "vmcs_api.h"
#include "vmcs_internal.h"


typedef struct {
	vmcs_object_t		vmcs_base[1];
	cache64_object_t	cache;
	guest_cpu_handle_t	gcpu;
	address_t		gpa; /* if !=0 then it's original GPA */
} vmcs_software_object_t;

/*-------------------------- Forward Declarations ----------------------------*/
static
uint64_t vmcs_sw_read(const vmcs_object_t *vmcs, vmcs_field_t field_id);
static
void vmcs_sw_write(vmcs_object_t *vmcs, vmcs_field_t field_id, uint64_t value);
static
void vmcs_sw_flush_to_cpu(const vmcs_object_t *vmcs);
static
boolean_t vmcs_sw_is_dirty(const vmcs_object_t *vmcs);
static
guest_cpu_handle_t vmcs_sw_get_owner(const vmcs_object_t *vmcs);

static
void vmcs_sw_add_msr_to_vmexit_store_list(vmcs_object_t *vmcs,
					  uint32_t msr_index,
					  uint64_t value);
static
void vmcs_sw_add_msr_to_vmexit_load_list(vmcs_object_t *vmcs,
					 uint32_t msr_index,
					 uint64_t value);
static
void vmcs_sw_add_msr_to_vmenter_load_list(vmcs_object_t *vmcs,
					  uint32_t msr_index,
					  uint64_t value);
static
void vmcs_sw_add_msr_to_vmexit_store_and_vmenter_load_lists(vmcs_object_t
							    *vmcs,
							    uint32_t msr_index,
							    uint64_t value);

static
void vmcs_sw_delete_msr_from_vmexit_store_list(vmcs_object_t *vmcs,
					       uint32_t msr_index);
static
void vmcs_sw_delete_msr_from_vmexit_load_list(vmcs_object_t *vmcs,
					      uint32_t msr_index);
static
void vmcs_sw_delete_msr_from_vmenter_load_list(vmcs_object_t *vmcs,
					       uint32_t msr_index);
static
void vmcs_sw_delete_msr_from_vmexit_store_and_vmenter_load_lists(vmcs_object_t
								 *vmcs, uint32_t
								 msr_index);

static
void vmcs_0_flush_to_memory(vmcs_object_t *vmcs);
static
void vmcs_1_flush_to_memory(vmcs_object_t *vmcs);
static
void vmcs_0_destroy(vmcs_object_t *vmcs);
static
void vmcs_1_destroy(vmcs_object_t *vmcs);
static
void vmcs_copy_extra_buffer(void *dst,
			    const vmcs_object_t *vmcs_src,
			    vmcs_field_t field,
			    uint32_t bytes_to_copy);

/*------------------------------ Code ----------------------------------------*/
static
void vmcs_0_copy_msr_list_to_merged(vmcs_object_t *merged_vmcs,
				    vmcs_software_object_t *sw_vmcs,
				    vmcs_field_t address_field,
				    vmcs_field_t count_field,
				    func_vmcs_add_msr_t add_msr_func)
{
	ia32_vmx_msr_entry_t *msr_list_ptr =
		(ia32_vmx_msr_entry_t *)mon_vmcs_read(sw_vmcs->vmcs_base,
			address_field);
	uint32_t msr_list_count = (uint32_t)mon_vmcs_read(sw_vmcs->vmcs_base,
		count_field);
	uint32_t i;

	for (i = 0; i < msr_list_count; i++)
		add_msr_func(merged_vmcs, msr_list_ptr[i].msr_index,
			msr_list_ptr[i].msr_data);
}

static
void vmcs_0_take_msr_list_from_merged(vmcs_software_object_t *vmcs_0,
				      vmcs_object_t *merged_vmcs,
				      vmcs_field_t address_field,
				      vmcs_field_t count_field)
{
	uint64_t addr_hpa = mon_vmcs_read(merged_vmcs, address_field);
	uint64_t addr_hva;
	uint64_t count_value;

	if (VMCS_INVALID_ADDRESS == addr_hpa) {
		addr_hva = VMCS_INVALID_ADDRESS;
		count_value = 0;
	} else if (!mon_hmm_hpa_to_hva(addr_hpa, &addr_hva)) {
		MON_LOG(mask_anonymous,
			level_trace,
			"%s: Failed translate hpa_t(%P) to hva_t\n",
			__FUNCTION__);
		MON_DEADLOOP();
		addr_hva = VMCS_INVALID_ADDRESS;
		count_value = 0;
	} else {
		count_value = mon_vmcs_read(merged_vmcs, count_field);
		MON_ASSERT(addr_hva ==
			ALIGN_BACKWARD(addr_hva, sizeof(ia32_vmx_msr_entry_t)));
	}

	mon_vmcs_write(vmcs_0->vmcs_base, address_field, addr_hva);
	mon_vmcs_write(vmcs_0->vmcs_base, count_field, count_value);
}

vmcs_object_t *vmcs_0_create(vmcs_object_t *vmcs_origin)
{
	vmcs_software_object_t *vmcs_clone;
	void *io_a_page = NULL;
	void *io_b_page = NULL;
	void *msr_page = NULL;
	vmcs_field_t field_id;

	vmcs_clone = mon_malloc(sizeof(*vmcs_clone));
	if (NULL == vmcs_clone) {
		MON_LOG(mask_anonymous,
			level_trace,
			"[vmcs] %s: Allocation failed\n",
			__FUNCTION__);
		return NULL;
	}

	vmcs_clone->cache = cache64_create(VMCS_FIELD_COUNT);
	if (NULL == vmcs_clone->cache) {
		mon_mfree(vmcs_clone);
		MON_LOG(mask_anonymous,
			level_trace,
			"[vmcs] %s: Allocation failed\n",
			__FUNCTION__);
		return NULL;
	}

	/* allocate VMCS extra pages, which exist at origin VMCS
	 * and write them back into clone vmcs
	 * translation HVA->HPA is not necessary, since
	 * these pages are never applied to hardware */

	io_a_page = mon_page_alloc(1);
	io_b_page = mon_page_alloc(1);
	msr_page = mon_page_alloc(1);
	if (NULL == io_a_page || NULL == io_b_page || NULL == msr_page) {
		MON_LOG(mask_anonymous,
			level_trace,
			"[vmcs] %s: Allocation of extra pages failed\n",
			__FUNCTION__);
		if (NULL != io_a_page) {
			mon_page_free(io_a_page);
		}
		if (NULL != io_b_page) {
			mon_page_free(io_b_page);
		}
		if (NULL != msr_page) {
			mon_page_free(msr_page);
		}
		cache64_destroy(vmcs_clone->cache);

		mon_mfree(vmcs_clone);

		return NULL;
	}

	vmcs_clone->gcpu = vmcs_get_owner(vmcs_origin);
	vmcs_clone->gpa = 0;

	vmcs_clone->vmcs_base->vmcs_read = vmcs_sw_read;
	vmcs_clone->vmcs_base->vmcs_write = vmcs_sw_write;
	vmcs_clone->vmcs_base->vmcs_flush_to_cpu = vmcs_sw_flush_to_cpu;
	vmcs_clone->vmcs_base->vmcs_is_dirty = vmcs_sw_is_dirty;
	vmcs_clone->vmcs_base->vmcs_get_owner = vmcs_sw_get_owner;
	vmcs_clone->vmcs_base->vmcs_flush_to_memory = vmcs_0_flush_to_memory;
	vmcs_clone->vmcs_base->vmcs_destroy = vmcs_0_destroy;

	vmcs_clone->vmcs_base->vmcs_add_msr_to_vmexit_store_list =
		vmcs_sw_add_msr_to_vmexit_store_list;
	vmcs_clone->vmcs_base->vmcs_add_msr_to_vmexit_load_list =
		vmcs_sw_add_msr_to_vmexit_load_list;
	vmcs_clone->vmcs_base->vmcs_add_msr_to_vmenter_load_list =
		vmcs_sw_add_msr_to_vmexit_load_list;
	vmcs_clone->vmcs_base->
	vmcs_add_msr_to_vmexit_store_and_vmenter_load_list =
		vmcs_sw_add_msr_to_vmexit_store_and_vmenter_load_lists;

	vmcs_clone->vmcs_base->vmcs_delete_msr_from_vmexit_store_list =
		vmcs_sw_delete_msr_from_vmexit_store_list;
	vmcs_clone->vmcs_base->vmcs_delete_msr_from_vmexit_load_list =
		vmcs_sw_delete_msr_from_vmexit_load_list;
	vmcs_clone->vmcs_base->vmcs_delete_msr_from_vmenter_load_list =
		vmcs_sw_delete_msr_from_vmenter_load_list;
	vmcs_clone->
	vmcs_base->vmcs_delete_msr_from_vmexit_store_and_vmenter_load_list =
		vmcs_sw_delete_msr_from_vmexit_store_and_vmenter_load_lists;

	vmcs_clone->vmcs_base->level = VMCS_LEVEL_0;
	vmcs_clone->vmcs_base->skip_access_checking = FALSE;
	vmcs_clone->vmcs_base->signature = VMCS_SIGNATURE;

	vmcs_init_all_msr_lists(vmcs_clone->vmcs_base);

	/* copy all fields as is */
	for (field_id = (vmcs_field_t)0; field_id < VMCS_FIELD_COUNT;
	     (vmcs_field_t)++ field_id) {
		if (vmcs_field_is_supported(field_id)) {
			uint64_t value = mon_vmcs_read(vmcs_origin, field_id);
			vmcs_write_nocheck(vmcs_clone->vmcs_base,
				field_id,
				value);
		}
	}

	/* Copy host bitmaps into newly created VMCS#0.
	 * Host HPA must be translated to HVA */
	vmcs_copy_extra_buffer(io_a_page, vmcs_origin, VMCS_IO_BITMAP_ADDRESS_A,
		PAGE_4KB_SIZE);
	vmcs_copy_extra_buffer(io_b_page, vmcs_origin, VMCS_IO_BITMAP_ADDRESS_B,
		PAGE_4KB_SIZE);
	vmcs_copy_extra_buffer(msr_page, vmcs_origin, VMCS_MSR_BITMAP_ADDRESS,
		PAGE_4KB_SIZE);

	/* TODO: Copy MSR lists
	 * vmcs_copy_extra_buffer(msr_vmexit_load_page, vmcs_origin,
	 * VMCS_EXIT_MSR_STORE_ADDRESS, 2*PAGE_4KB_SIZE);
	 * vmcs_copy_extra_buffer(msr_vmexit_store_page, vmcs_origin,
	 * VMCS_EXIT_MSR_LOAD_ADDRESS, 2*PAGE_4KB_SIZE);
	 * vmcs_copy_extra_buffer(msr_vmenter_load_page, vmcs_origin,
	 * VMCS_ENTER_MSR_LOAD_ADDRESS, 2*PAGE_4KB_SIZE); */

	/* Take all MSR lists from merged
	 * Assuming that creation is from merged vmcs */
	MON_ASSERT(vmcs_origin->level == VMCS_MERGED);

	vmcs_0_take_msr_list_from_merged(vmcs_clone, vmcs_origin,
		VMCS_EXIT_MSR_STORE_ADDRESS,
		VMCS_EXIT_MSR_STORE_COUNT);
	vmcs_0_take_msr_list_from_merged(vmcs_clone, vmcs_origin,
		VMCS_EXIT_MSR_LOAD_ADDRESS,
		VMCS_EXIT_MSR_LOAD_COUNT);
	vmcs_0_take_msr_list_from_merged(vmcs_clone, vmcs_origin,
		VMCS_ENTER_MSR_LOAD_ADDRESS,
		VMCS_ENTER_MSR_LOAD_COUNT);

	vmcs_init_all_msr_lists(vmcs_origin);

	MON_ASSERT(vmcs_get_owner(vmcs_origin) != NULL);

	/* Fill anew MSR lists for merged vmcs */
	vmcs_0_copy_msr_list_to_merged(vmcs_origin, vmcs_clone,
		VMCS_EXIT_MSR_STORE_ADDRESS,
		VMCS_EXIT_MSR_STORE_COUNT,
		vmcs_add_msr_to_vmexit_store_list);
	vmcs_0_copy_msr_list_to_merged(vmcs_origin, vmcs_clone,
		VMCS_EXIT_MSR_LOAD_ADDRESS,
		VMCS_EXIT_MSR_LOAD_COUNT,
		vmcs_add_msr_to_vmexit_load_list);
	vmcs_0_copy_msr_list_to_merged(vmcs_origin, vmcs_clone,
		VMCS_ENTER_MSR_LOAD_ADDRESS,
		VMCS_ENTER_MSR_LOAD_COUNT,
		vmcs_add_msr_to_vmenter_load_list);

	/* update extra pages, which are different for vmcs-0. * translation
	 * HVA->HPA is not necessary, since * these pages are never applied to
	 * hardware. */
	mon_vmcs_write(vmcs_clone->vmcs_base, VMCS_IO_BITMAP_ADDRESS_A,
		(uint64_t)io_a_page);
	mon_vmcs_write(vmcs_clone->vmcs_base, VMCS_IO_BITMAP_ADDRESS_B,
		(uint64_t)io_b_page);
	mon_vmcs_write(vmcs_clone->vmcs_base, VMCS_MSR_BITMAP_ADDRESS,
		(uint64_t)msr_page);

	return vmcs_clone->vmcs_base;
}

void vmcs_copy_extra_buffer(void *dst, const vmcs_object_t *vmcs_src,
			    vmcs_field_t field, uint32_t bytes_to_copy)
{
	address_t hpa, hva;

	hpa = mon_vmcs_read(vmcs_src, field);
	if (TRUE == mon_hmm_hpa_to_hva(hpa, &hva)) {
		mon_memcpy(dst, (void *)hva, bytes_to_copy);
	} else {
		mon_memset(dst, 0, PAGE_4KB_SIZE);
	}
}

void vmcs_0_destroy(vmcs_object_t *vmcs)
{
	vmcs_software_object_t *p_vmcs = (vmcs_software_object_t *)vmcs;
	void *page;

	MON_ASSERT(p_vmcs);

	page = (void *)mon_vmcs_read(vmcs, VMCS_IO_BITMAP_ADDRESS_A);
	if (NULL != page) {
		mon_page_free(page);
	}

	page = (void *)mon_vmcs_read(vmcs, VMCS_IO_BITMAP_ADDRESS_B);
	if (NULL != page) {
		mon_page_free(page);
	}

	page = (void *)mon_vmcs_read(vmcs, VMCS_MSR_BITMAP_ADDRESS);
	if (NULL != page) {
		mon_page_free(page);
	}

	vmcs_destroy_all_msr_lists_internal(vmcs, FALSE);

	cache64_destroy(p_vmcs->cache);
}

void vmcs_0_flush_to_memory(vmcs_object_t *vmcs)
{
	MON_ASSERT(vmcs);
}

void vmcs_sw_write(vmcs_object_t *vmcs, vmcs_field_t field_id, uint64_t value)
{
	vmcs_software_object_t *p_vmcs = (vmcs_software_object_t *)vmcs;

	MON_ASSERT(p_vmcs);
	cache64_write(p_vmcs->cache, value, (uint32_t)field_id);
}

uint64_t vmcs_sw_read(const vmcs_object_t *vmcs, vmcs_field_t field_id)
{
	vmcs_software_object_t *p_vmcs = (vmcs_software_object_t *)vmcs;
	uint64_t value;

	MON_ASSERT(p_vmcs);
	if (FALSE == cache64_read(p_vmcs->cache, &value, (uint32_t)field_id)) {
		value = 0;
	}
	return value;
}

void vmcs_sw_flush_to_cpu(const vmcs_object_t *vmcs)
{
	vmcs_software_object_t *p_vmcs = (vmcs_software_object_t *)vmcs;

	MON_ASSERT(p_vmcs);
	/* just clean dirty bits */
	cache64_flush_dirty(p_vmcs->cache, VMCS_FIELD_COUNT, NULL, NULL);
}

boolean_t vmcs_sw_is_dirty(const vmcs_object_t *vmcs)
{
	vmcs_software_object_t *p_vmcs = (vmcs_software_object_t *)vmcs;

	MON_ASSERT(p_vmcs);
	return cache64_is_dirty(p_vmcs->cache);
}

guest_cpu_handle_t vmcs_sw_get_owner(const vmcs_object_t *vmcs)
{
	vmcs_software_object_t *p_vmcs = (vmcs_software_object_t *)vmcs;

	MON_ASSERT(p_vmcs);
	return p_vmcs->gcpu;
}

static
void vmcs_sw_add_msr_to_vmexit_store_list(vmcs_object_t *vmcs,
					  uint32_t msr_index, uint64_t value)
{
	vmcs_add_msr_to_vmexit_store_list_internal(vmcs, msr_index, value,
		FALSE);
}

static
void vmcs_sw_add_msr_to_vmexit_load_list(vmcs_object_t *vmcs,
					 uint32_t msr_index, uint64_t value)
{
	vmcs_add_msr_to_vmexit_load_list_internal(vmcs, msr_index, value,
		FALSE);
}

static
void vmcs_sw_add_msr_to_vmenter_load_list(vmcs_object_t *vmcs,
					  uint32_t msr_index, uint64_t value)
{
	vmcs_add_msr_to_vmenter_load_list_internal(vmcs, msr_index, value,
		FALSE);
}

static
void vmcs_sw_add_msr_to_vmexit_store_and_vmenter_load_lists(vmcs_object_t
							    *vmcs,
							    uint32_t msr_index,
							    uint64_t value)
{
	vmcs_add_msr_to_vmexit_store_and_vmenter_load_lists_internal(vmcs,
		msr_index,
		value, FALSE);
}

static
void vmcs_sw_delete_msr_from_vmexit_store_list(vmcs_object_t *vmcs,
					       uint32_t msr_index)
{
	vmcs_delete_msr_from_vmexit_store_list_internal(vmcs, msr_index, FALSE);
}

static
void vmcs_sw_delete_msr_from_vmexit_load_list(vmcs_object_t *vmcs,
					      uint32_t msr_index)
{
	vmcs_delete_msr_from_vmexit_load_list_internal(vmcs, msr_index, FALSE);
}

static
void vmcs_sw_delete_msr_from_vmenter_load_list(vmcs_object_t *vmcs,
					       uint32_t msr_index)
{
	vmcs_delete_msr_from_vmenter_load_list_internal(vmcs, msr_index, FALSE);
}

static
void vmcs_sw_delete_msr_from_vmexit_store_and_vmenter_load_lists(vmcs_object_t
								 *vmcs,
								 uint32_t
								 msr_index)
{
	vmcs_delete_msr_from_vmexit_store_and_vmenter_load_lists_internal(vmcs,
		msr_index,
		FALSE);
}
