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

#include "vmm_base.h"
#include "dbg.h"
#include "heap.h"
#include "vmx_cap.h"
#include "vmcs.h"
#include "hmm.h"
#include "host_cpu.h"

#include "lib/util.h"
#include "lib/string.h"

#define VMCS_ALWAYS_VALID_COUNT VMCS_ENTRY_INTR_INFO
#define VMCS_INIT_TO_ZERO_FISRT VMCS_EXIT_MSR_STORE_COUNT
#define VMCS_INIT_TO_ZERO_LAST VMCS_ENTRY_ERR_CODE

/*-------------------------------------------------------------------------
 * Since VMCS_INIT_TO_ZERO_FISRT < VMCS_ALWAYS_VALID_COUNT < VMCS_INIT_TO_ZERO_LAST,
 * we only need to make sure that VMCS_INIT_TO_ZERO_LAST does not exceed 64
 *------------------------------------------------------------------------- */
#if VMCS_INIT_TO_ZERO_LAST>63
#error "COUNT is too large"
#endif

#define VMCS_BITMAP_SIZE ((VMCS_FIELD_COUNT+63)/64)
#define DIRTY_CACHE_SIZE 10

struct vmcs_object_t {
	uint64_t hpa;
	uint8_t pad[6];
	uint8_t is_launched;
	uint8_t dirty_count;
	uint32_t dirty_fields[DIRTY_CACHE_SIZE];
	uint64_t valid_bitmap[VMCS_BITMAP_SIZE];
	uint64_t dirty_bitmap[VMCS_BITMAP_SIZE];
	uint64_t cache[VMCS_FIELD_COUNT];
} vmcs_object_t;

/* field encoding and naming */
typedef struct {
	uint32_t		encoding;
	uint32_t		pad;
	const char		*name;
} vmcs_encoding_t;

static vmcs_encoding_t g_field_data[] = {
	{0x00000000,0,"VMCS_VPID"},
	{0x00002000,0,"VMCS_IO_BITMAP_A"},
	{0x00002002,0,"VMCS_IO_BITMAP_B"},
	{0x00002004,0,"VMCS_MSR_BITMAP"},
	{0x00002010,0,"VMCS_TSC_OFFSET"},
	{0x0000201A,0,"VMCS_EPTP_ADDRESS"},
	{0x0000202C,0,"VMCS_XSS_EXIT_BITMAP"},
	{0x00004000,0,"VMCS_PIN_CTRL"},
	{0x00004002,0,"VMCS_PROC_CTRL1"},
	{0x0000401E,0,"VMCS_PROC_CTRL2"},
	{0x0000400C,0,"VMCS_EXIT_CTRL"},
	{0x00006000,0,"VMCS_CR0_MASK"},
	{0x00006002,0,"VMCS_CR4_MASK"},
	{0x00006004,0,"VMCS_CR0_SHADOW"},
	{0x00006006,0,"VMCS_CR4_SHADOW"},
	{0x00006008,0,"VMCS_CR3_TARGET0"},
	{0x0000600A,0,"VMCS_CR3_TARGET1"},
	{0x0000600C,0,"VMCS_CR3_TARGET2"},
	{0x0000600E,0,"VMCS_CR3_TARGET3"},
	{0x00002800,0,"VMCS_LINK_PTR"},//GUEST
	{0x00006C00,0,"VMCS_HOST_CR0"},
	{0x00006C02,0,"VMCS_HOST_CR3"},
	{0x00006C04,0,"VMCS_HOST_CR4"},
	{0x00000C00,0,"VMCS_HOST_ES_SEL"},
	{0x00000C02,0,"VMCS_HOST_CS_SEL"},
	{0x00000C04,0,"VMCS_HOST_SS_SEL"},
	{0x00000C06,0,"VMCS_HOST_DS_SEL"},
	{0x00000C08,0,"VMCS_HOST_FS_SEL"},
	{0x00006C06,0,"VMCS_HOST_FS_BASE"},
	{0x00000C0A,0,"VMCS_HOST_GS_SEL"},
	{0x00006C08,0,"VMCS_HOST_GS_BASE"},
	{0x00000C0C,0,"VMCS_HOST_TR_SEL"},
	{0x00006C0A,0,"VMCS_HOST_TR_BASE"},
	{0x00006C0C,0,"VMCS_HOST_GDTR_BASE"},
	{0x00006C0E,0,"VMCS_HOST_IDTR_BASE"},
	{0x00006C14,0,"VMCS_HOST_RSP"},
	{0x00006C16,0,"VMCS_HOST_RIP"},
	{0x00004C00,0,"VMCS_HOST_SYSENTER_CS"},
	{0x00006C10,0,"VMCS_HOST_SYSENTER_ESP"},
	{0x00006C12,0,"VMCS_HOST_SYSENTER_EIP"},
	{0x00002C00,0,"VMCS_HOST_PAT"},
	{0x00002C02,0,"VMCS_HOST_EFER"},
	{0x00002C04,0,"VMCS_HOST_PERF_G_CTRL"},
	{0x0000400E,0,"VMCS_EXIT_MSR_STORE_COUNT"},
	{0x00002006,0,"VMCS_EXIT_MSR_STORE_ADDR"},
	{0x00004010,0,"VMCS_EXIT_MSR_LOAD_COUNT"},
	{0x00002008,0,"VMCS_EXIT_MSR_LOAD_ADDR"},
	{0x00004014,0,"VMCS_ENTRY_MSR_LOAD_COUNT"},
	{0x0000200A,0,"VMCS_ENTRY_MSR_LOAD_ADDR"},
	{0x00004004,0,"VMCS_EXCEPTION_BITMAP"},
	{0x0000400A,0,"VMCS_CR3_TARGET_COUNT"},
	{0x00004016,0,"VMCS_ENTRY_INTR_INFO"},
	{0x00004018,0,"VMCS_ENTRY_ERR_CODE"},
	{0x00004012,0,"VMCS_ENTRY_CTRL"},
	{0x0000401A,0,"VMCS_ENTRY_INSTR_LEN"},
	{0x0000482E,0,"VMCS_PREEMPTION_TIMER"},
	{0x00002802,0,"VMCS_GUEST_DBGCTL"},
	{0x00002804,0,"VMCS_GUEST_PAT"},
	{0x00002806,0,"VMCS_GUEST_EFER"},
	{0x00002808,0,"VMCS_GUEST_PERF_G_CTRL"},
	{0x0000280A,0,"VMCS_GUEST_PDPTR0"},
	{0x0000280C,0,"VMCS_GUEST_PDPTR1"},
	{0x0000280E,0,"VMCS_GUEST_PDPTR2"},
	{0x00002810,0,"VMCS_GUEST_PDPTR3"},
	{0x00006800,0,"VMCS_GUEST_CR0"},
	{0x00006802,0,"VMCS_GUEST_CR3"},
	{0x00006804,0,"VMCS_GUEST_CR4"},
	{0x0000681A,0,"VMCS_GUEST_DR7"},
	{0x00006816,0,"VMCS_GUEST_GDTR_BASE"},
	{0x00004810,0,"VMCS_GUEST_GDTR_LIMIT"},
	{0x00006818,0,"VMCS_GUEST_IDTR_BASE"},
	{0x00004812,0,"VMCS_GUEST_IDTR_LIMIT"},
	{0x00004824,0,"VMCS_GUEST_INTERRUPTIBILITY"},
	{0x00004826,0,"VMCS_GUEST_ACTIVITY_STATE"},
	{0x0000482A,0,"VMCS_GUEST_SYSENTER_CS"},
	{0x00006824,0,"VMCS_GUEST_SYSENTER_ESP"},
	{0x00006826,0,"VMCS_GUEST_SYSENTER_EIP"},
	{0x00000800,0,"VMCS_GUEST_ES_SEL"},
	{0x00006806,0,"VMCS_GUEST_ES_BASE"},
	{0x00004800,0,"VMCS_GUEST_ES_LIMIT"},
	{0x00004814,0,"VMCS_GUEST_ES_AR"},
	{0x00000802,0,"VMCS_GUEST_CS_SEL"},
	{0x00006808,0,"VMCS_GUEST_CS_BASE"},
	{0x00004802,0,"VMCS_GUEST_CS_LIMIT"},
	{0x00004816,0,"VMCS_GUEST_CS_AR"},
	{0x00000804,0,"VMCS_GUEST_SS_SEL"},
	{0x0000680A,0,"VMCS_GUEST_SS_BASE"},
	{0x00004804,0,"VMCS_GUEST_SS_LIMIT"},
	{0x00004818,0,"VMCS_GUEST_SS_AR"},
	{0x00000806,0,"VMCS_GUEST_DS_SEL"},
	{0x0000680C,0,"VMCS_GUEST_DS_BASE"},
	{0x00004806,0,"VMCS_GUEST_DS_LIMIT"},
	{0x0000481A,0,"VMCS_GUEST_DS_AR"},
	{0x00000808,0,"VMCS_GUEST_FS_SEL"},
	{0x0000680E,0,"VMCS_GUEST_FS_BASE"},
	{0x00004808,0,"VMCS_GUEST_FS_LIMIT"},
	{0x0000481C,0,"VMCS_GUEST_FS_AR"},
	{0x0000080A,0,"VMCS_GUEST_GS_SEL"},
	{0x00006810,0,"VMCS_GUEST_GS_BASE"},
	{0x0000480A,0,"VMCS_GUEST_GS_LIMIT"},
	{0x0000481E,0,"VMCS_GUEST_GS_AR"},
	{0x0000080C,0,"VMCS_GUEST_LDTR_SEL"},
	{0x00006812,0,"VMCS_GUEST_LDTR_BASE"},
	{0x0000480C,0,"VMCS_GUEST_LDTR_LIMIT"},
	{0x00004820,0,"VMCS_GUEST_LDTR_AR"},
	{0x0000080E,0,"VMCS_GUEST_TR_SEL"},
	{0x00006814,0,"VMCS_GUEST_TR_BASE"},
	{0x0000480E,0,"VMCS_GUEST_TR_LIMIT"},
	{0x00004822,0,"VMCS_GUEST_TR_AR"},
	{0x0000681C,0,"VMCS_GUEST_RSP"},
	{0x0000681E,0,"VMCS_GUEST_RIP"},
	{0x00006820,0,"VMCS_GUEST_RFLAGS"},
	{0x00006822,0,"VMCS_GUEST_PEND_DBG_EXCEPTION"},
	{0x00002400,0,"VMCS_GUEST_PHY_ADDR"},
	{0x0000640A,0,"VMCS_GUEST_LINEAR_ADDR"},
	{0x00004400,0,"VMCS_INSTR_ERROR"},
	{0x00004402,0,"VMCS_EXIT_REASON"},
	{0x00004404,0,"VMCS_EXIT_INT_INFO"},
	{0x00004406,0,"VMCS_EXIT_INT_ERR_CODE"},
	{0x00004408,0,"VMCS_IDT_VECTOR_INFO"},
	{0x0000440A,0,"VMCS_IDT_VECTOR_ERR_CODE"},
	{0x0000440C,0,"VMCS_EXIT_INSTR_LEN"},
	{0x0000440E,0,"VMCS_EXIT_INSTR_INFO"},
	{0x00006400,0,"VMCS_EXIT_QUAL"}
};

vmcs_obj_t vmcs_create()
{
	vmcs_obj_t p_vmcs;
	uint32_t i ;

	p_vmcs = mem_alloc(sizeof(*p_vmcs));
	memset(p_vmcs, 0, sizeof(*p_vmcs));

	p_vmcs->hpa = vmcs_alloc();
	vmcs_clr_ptr(p_vmcs);
	p_vmcs->dirty_count = VMCS_FIELD_COUNT;
	for (i=0; i<VMCS_BITMAP_SIZE; i++)
		p_vmcs->valid_bitmap[i] = (uint64_t)-1; // all valid

	/*
	 * Note: Some VMCS fields should be initialized to 0 ,so we put them together
	 * from VMCS_INIT_TO_ZERO_FISRT to VMCS_INIT_TO_ZERO_LAST in vmcs_field_t
	 * and update the dirty_bitmap(set dirty bit to 1).Because VMCS_INIT_TO_ZERO_LAST does not exceed 64,
	 * we only update the dirty_bitmap[0].if it is greater than 64, we also should update others.
	 */
	p_vmcs->dirty_bitmap[0] |= MASK64_MID(VMCS_INIT_TO_ZERO_LAST,VMCS_INIT_TO_ZERO_FISRT);
	return p_vmcs;
}

void vmcs_set_ptr(vmcs_obj_t vmcs)
{
	D(VMM_ASSERT(vmcs));
	asm_vmptrld(&vmcs->hpa);
	D(VMM_ASSERT((asm_get_rflags()&VMCS_ERRO_MASK) == 0));
}

void vmcs_clr_ptr(vmcs_obj_t vmcs)
{
	D(VMM_ASSERT(vmcs));
	asm_vmclear(&vmcs->hpa);
	D(VMM_ASSERT((asm_get_rflags()&VMCS_ERRO_MASK) == 0));
}

void vmx_on(uint64_t *addr)
{
	D(VMM_ASSERT(addr));
	asm_vmxon(addr);
	D(VMM_ASSERT((asm_get_rflags()&VMCS_ERRO_MASK) == 0));
}

static inline void vmx_vmwrite(uint64_t field, uint64_t field_value)
{
	asm_vmwrite(field,field_value);
	D(VMM_ASSERT((asm_get_rflags()&VMCS_ERRO_MASK) == 0));
}

static inline void vmx_vmread(uint64_t field, uint64_t *field_value)
{
	*field_value = asm_vmread(field);
	D(VMM_ASSERT((asm_get_rflags()&VMCS_ERRO_MASK) == 0));
}

static inline uint64_t cache_is_valid(vmcs_obj_t vmcs, vmcs_field_t vmcs_field)
{
	uint64_t bitmap_index;
	uint64_t bitmap_bit;

	bitmap_index = (vmcs_field>>6);
	bitmap_bit = (1ull << (vmcs_field & 0x3F));
	return (vmcs->valid_bitmap[bitmap_index]&(bitmap_bit));
}
static inline void cache_set_valid(vmcs_obj_t vmcs, vmcs_field_t vmcs_field)
{
	BITARRAY_SET((uint64_t *)&(vmcs->valid_bitmap[0]),(uint64_t)(vmcs_field));
}

static inline  uint64_t cache_is_dirty(vmcs_obj_t vmcs, vmcs_field_t vmcs_field)
{
	return BITARRAY_GET(&vmcs->dirty_bitmap[0], vmcs_field);
}

static inline  void cache_set_dirty(vmcs_obj_t vmcs, vmcs_field_t vmcs_field)
{
	BITARRAY_SET((uint64_t *)&(vmcs->dirty_bitmap[0]),(uint64_t)(vmcs_field));
}

void vmcs_write(vmcs_obj_t vmcs, vmcs_field_t field_id, uint64_t value)
{
	D(VMM_ASSERT(vmcs));
	D(VMM_ASSERT(field_id < VMCS_FIELD_COUNT));

	vmcs->cache[field_id] = value;
	cache_set_valid(vmcs, field_id); // update valid cache

	if((vmcs->dirty_count < DIRTY_CACHE_SIZE)&&(!cache_is_dirty(vmcs,field_id)))
	{
		vmcs->dirty_fields[vmcs->dirty_count] = field_id;
		vmcs->dirty_count++;
	}
	cache_set_dirty(vmcs, field_id);
}

uint64_t vmcs_read(vmcs_obj_t vmcs, vmcs_field_t field_id)
{
	uint64_t value;

	D(VMM_ASSERT(vmcs));
	D(VMM_ASSERT(field_id < VMCS_FIELD_COUNT));

	if (cache_is_valid(vmcs, field_id))
	{
		return vmcs->cache[field_id];
	}

	D(VMM_ASSERT(asm_vmptrst() == vmcs->hpa));
	vmx_vmread(g_field_data[field_id].encoding, &value);
	vmcs->cache[field_id] = value;
	cache_set_valid(vmcs, field_id);
	return value;
}

/*-------------------------------------------------------------------------
 *
 * Reset all read caching. MUST NOT be called with modifications not flushed to
 * hw
 *
 *------------------------------------------------------------------------- */
void vmcs_clear_cache(vmcs_obj_t vmcs)
{
	uint32_t i;

	D(VMM_ASSERT(vmcs));

	for (i=1; i<VMCS_BITMAP_SIZE; i++)
		vmcs->valid_bitmap[i] = 0;

	/*
	 * Note: currently the number of VMCS fields in section 1 (always valid) does not exceed 64.
	 * So,we only need to update valid_bitmap[0].if it is greater than 64, we also should update others.
	 */
	vmcs->valid_bitmap[0] = MASK64_LOW(VMCS_ALWAYS_VALID_COUNT);
}

void vmcs_clear_all_cache(vmcs_obj_t vmcs)
{
	uint32_t i;

	D(VMM_ASSERT(vmcs));

	for (i=0; i<VMCS_BITMAP_SIZE; i++)
		vmcs->valid_bitmap[i] = 0;
}

void vmcs_flush(vmcs_obj_t vmcs)
{
	uint32_t i;
	uint32_t field_id;
	uint32_t bitmap_idx;
	uint32_t dirty_bit;

	D(VMM_ASSERT(vmcs));

	if (vmcs->dirty_count <= DIRTY_CACHE_SIZE)
	{
		for (i=0; i<vmcs->dirty_count; i++)
		{
			field_id = vmcs->dirty_fields[i];
			vmx_vmwrite(g_field_data[field_id].encoding, vmcs->cache[field_id]);
		}
		// clear dirty cache/bitmap
		vmcs->dirty_count = 0;
		for (bitmap_idx=0; bitmap_idx<VMCS_BITMAP_SIZE; bitmap_idx++)
			vmcs->dirty_bitmap[bitmap_idx] = 0;
	}
	else
	{
		for (bitmap_idx=0; bitmap_idx<VMCS_BITMAP_SIZE; bitmap_idx++)
		{
			while(vmcs->dirty_bitmap[bitmap_idx]){
				dirty_bit = asm_bsf64(vmcs->dirty_bitmap[bitmap_idx]);
				field_id = dirty_bit+bitmap_idx*64;
				vmx_vmwrite(g_field_data[field_id].encoding, vmcs->cache[field_id]);
				BITARRAY_CLR((uint64_t *)&(vmcs->dirty_bitmap[bitmap_idx]),(uint64_t)dirty_bit);
			}
		}
	}
}

uint8_t vmcs_is_launched(vmcs_obj_t obj)
{
	D(VMM_ASSERT(obj));
	return obj->is_launched;
}

void vmcs_set_launched(vmcs_obj_t obj)
{
	D(VMM_ASSERT(obj));
	obj->is_launched = 1;
}

void vmcs_clear_launched(vmcs_obj_t obj)
{
	D(VMM_ASSERT(obj));
	obj->is_launched = 0;
}

void vmcs_print_all(vmcs_obj_t vmcs)
{
	uint16_t hcpu_id = host_cpu_id();
	vmcs_field_t i =0;
	uint64_t u64;

	D(VMM_ASSERT(vmcs));

	for (i = 0; i < VMCS_FIELD_COUNT; ++i) {
		u64 = asm_vmread(g_field_data[i].encoding);
		if((asm_get_rflags()&VMCS_ERRO_MASK) == 0)
		{
			if(cache_is_valid(vmcs, i))
			{
				print_info("%d %40s (0x%04X) = hw_value(0x%llX),cache_value(0x%llX)\n",hcpu_id, g_field_data[i].name,g_field_data[i].encoding,u64,vmcs->cache[i]);
			}else{
				print_info("%d %40s (0x%04X) = hw_value(0x%llX),invalid cache\n",hcpu_id, g_field_data[i].name,g_field_data[i].encoding,u64);
			}
		}else{
			if(cache_is_valid(vmcs, i))
			{
				print_info("%d %40s (0x%04X) = hw read fail,cache_value(0x%llX)\n",hcpu_id, g_field_data[i].name,g_field_data[i].encoding,vmcs->cache[i]);
			}else{
				print_info("%d %40s (0x%04X) = hw read fail,invalid cache\n",hcpu_id, g_field_data[i].name,g_field_data[i].encoding);
			}
		}
	}
}

uint32_t vmcs_dump_all(vmcs_obj_t vmcs, char *buffer, uint32_t size)
{
	uint16_t hcpu_id = host_cpu_id();
	vmcs_field_t i =0;
	uint32_t length = 0;
	uint64_t u64;
	char *cur_buf;
	uint32_t left_size;

	D(VMM_ASSERT(vmcs));

	for (i = 0; i < VMCS_FIELD_COUNT; ++i) {
		cur_buf = (char *)(buffer + length);
		left_size = size - length;
		u64 = asm_vmread(g_field_data[i].encoding);
		if((asm_get_rflags()&VMCS_ERRO_MASK) == 0) {
			if(cache_is_valid(vmcs, i)) {
				length += vmm_sprintf_s(cur_buf, left_size, "%d %40s (0x%04X) = hw_value(0x%llX),cache_value(0x%llX)\n",
						hcpu_id, g_field_data[i].name,g_field_data[i].encoding,u64,vmcs->cache[i]);
			}else {
				length += vmm_sprintf_s(cur_buf, left_size, "%d %40s (0x%04X) = hw_value(0x%llX),invalid cache\n",
						hcpu_id, g_field_data[i].name,g_field_data[i].encoding,u64);

			}
		}else {
			if(cache_is_valid(vmcs, i)) {
				length += vmm_sprintf_s(cur_buf, left_size, "%d %40s (0x%04X) = hw read fail,cache_value(0x%llX)\n",
						hcpu_id, g_field_data[i].name,g_field_data[i].encoding,vmcs->cache[i]);

			}else {
				length += vmm_sprintf_s(cur_buf, left_size, "%d %40s (0x%04X) = hw read fail,invalid cache\n",
						hcpu_id, g_field_data[i].name,g_field_data[i].encoding);

			}
		}
	}
	return length;
}
