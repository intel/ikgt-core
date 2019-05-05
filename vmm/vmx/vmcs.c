/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

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

#define str(x) #x
#define CONSTRUCT_VMCS_ENCODING(id, encoding) [id] = {encoding, 0, str(id)}

static vmcs_encoding_t g_field_data[] = {
	CONSTRUCT_VMCS_ENCODING(VMCS_VPID,                      0x00000000),
	CONSTRUCT_VMCS_ENCODING(VMCS_IO_BITMAP_A,               0x00002000),
	CONSTRUCT_VMCS_ENCODING(VMCS_IO_BITMAP_B,               0x00002002),
	CONSTRUCT_VMCS_ENCODING(VMCS_MSR_BITMAP,                0x00002004),
	CONSTRUCT_VMCS_ENCODING(VMCS_TSC_OFFSET,                0x00002010),
	CONSTRUCT_VMCS_ENCODING(VMCS_VIRTUAL_APIC_ADDR,         0x00002012), // for virtual apic
	CONSTRUCT_VMCS_ENCODING(VMCS_APIC_ACCESS_ADDR,          0x00002014), // for virtual apic
	CONSTRUCT_VMCS_ENCODING(VMCS_POST_INTR_NOTI_VECTOR,     0x00000002), // for virtual apic
	CONSTRUCT_VMCS_ENCODING(VMCS_POST_INTR_DESC_ADDR,       0x00002016), // for virtual apic
	CONSTRUCT_VMCS_ENCODING(VMCS_EOI_EXIT_BITMAP0,          0x0000201C), // for virtual apic
	CONSTRUCT_VMCS_ENCODING(VMCS_EOI_EXIT_BITMAP1,          0x0000201E), // for virtual apic
	CONSTRUCT_VMCS_ENCODING(VMCS_EOI_EXIT_BITMAP2,          0x00002020), // for virtual apic
	CONSTRUCT_VMCS_ENCODING(VMCS_EOI_EXIT_BITMAP3,          0x00002022), // for virtual apic
	CONSTRUCT_VMCS_ENCODING(VMCS_TPR_THRESHOLD,             0x0000401C), // for virtual apic
	CONSTRUCT_VMCS_ENCODING(VMCS_EPTP_ADDRESS,              0x0000201A),
	CONSTRUCT_VMCS_ENCODING(VMCS_XSS_EXIT_BITMAP,           0x0000202C),
	CONSTRUCT_VMCS_ENCODING(VMCS_PIN_CTRL,                  0x00004000),
	CONSTRUCT_VMCS_ENCODING(VMCS_PROC_CTRL1,                0x00004002),
	CONSTRUCT_VMCS_ENCODING(VMCS_PROC_CTRL2,                0x0000401E),
	CONSTRUCT_VMCS_ENCODING(VMCS_EXIT_CTRL,                 0x0000400C),
	CONSTRUCT_VMCS_ENCODING(VMCS_CR0_MASK,                  0x00006000),
	CONSTRUCT_VMCS_ENCODING(VMCS_CR4_MASK,                  0x00006002),
	CONSTRUCT_VMCS_ENCODING(VMCS_CR0_SHADOW,                0x00006004),
	CONSTRUCT_VMCS_ENCODING(VMCS_CR4_SHADOW,                0x00006006),
	CONSTRUCT_VMCS_ENCODING(VMCS_CR3_TARGET0,               0x00006008),
	CONSTRUCT_VMCS_ENCODING(VMCS_CR3_TARGET1,               0x0000600A),
	CONSTRUCT_VMCS_ENCODING(VMCS_CR3_TARGET2,               0x0000600C),
	CONSTRUCT_VMCS_ENCODING(VMCS_CR3_TARGET3,               0x0000600E),
	CONSTRUCT_VMCS_ENCODING(VMCS_LINK_PTR,                  0x00002800),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_CR0,                  0x00006C00),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_CR3,                  0x00006C02),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_CR4,                  0x00006C04),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_ES_SEL,               0x00000C00),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_CS_SEL,               0x00000C02),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_SS_SEL,               0x00000C04),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_DS_SEL,               0x00000C06),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_FS_SEL,               0x00000C08),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_FS_BASE,              0x00006C06),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_GS_SEL,               0x00000C0A),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_GS_BASE,              0x00006C08),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_TR_SEL,               0x00000C0C),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_TR_BASE,              0x00006C0A),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_GDTR_BASE,            0x00006C0C),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_IDTR_BASE,            0x00006C0E),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_RSP,                  0x00006C14),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_RIP,                  0x00006C16),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_PAT,                  0x00002C00),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_EFER,                 0x00002C02),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_PERF_G_CTRL,          0x00002C04),
	CONSTRUCT_VMCS_ENCODING(VMCS_EXIT_MSR_STORE_COUNT,      0x0000400E),
	CONSTRUCT_VMCS_ENCODING(VMCS_EXIT_MSR_STORE_ADDR,       0x00002006),
	CONSTRUCT_VMCS_ENCODING(VMCS_EXIT_MSR_LOAD_COUNT,       0x00004010),
	CONSTRUCT_VMCS_ENCODING(VMCS_EXIT_MSR_LOAD_ADDR,        0x00002008),
	CONSTRUCT_VMCS_ENCODING(VMCS_ENTRY_MSR_LOAD_COUNT,      0x00004014),
	CONSTRUCT_VMCS_ENCODING(VMCS_ENTRY_MSR_LOAD_ADDR,       0x0000200A),
	CONSTRUCT_VMCS_ENCODING(VMCS_EXCEPTION_BITMAP,          0x00004004),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_SYSENTER_CS,          0x00004C00),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_SYSENTER_ESP,         0x00006C10),
	CONSTRUCT_VMCS_ENCODING(VMCS_HOST_SYSENTER_EIP,         0x00006C12),
	CONSTRUCT_VMCS_ENCODING(VMCS_CR3_TARGET_COUNT,          0x0000400A),
	CONSTRUCT_VMCS_ENCODING(VMCS_ENTRY_INTR_INFO,           0x00004016),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_DBGCTL,              0x00002802),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_INTERRUPTIBILITY,    0x00004824),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_INTERRUPT_STATUS,    0x00000810), //for virtual apic
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_PEND_DBG_EXCEPTION,  0x00006822),
	CONSTRUCT_VMCS_ENCODING(VMCS_ENTRY_ERR_CODE,            0x00004018),
	CONSTRUCT_VMCS_ENCODING(VMCS_ENTRY_CTRL,                0x00004012),
	CONSTRUCT_VMCS_ENCODING(VMCS_ENTRY_INSTR_LEN,           0x0000401A),
	CONSTRUCT_VMCS_ENCODING(VMCS_PREEMPTION_TIMER,          0x0000482E),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_PAT,                 0x00002804),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_EFER,                0x00002806),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_PERF_G_CTRL,         0x00002808),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_PDPTR0,              0x0000280A),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_PDPTR1,              0x0000280C),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_PDPTR2,              0x0000280E),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_PDPTR3,              0x00002810),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_CR0,                 0x00006800),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_CR3,                 0x00006802),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_CR4,                 0x00006804),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_DR7,                 0x0000681A),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_GDTR_BASE,           0x00006816),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_GDTR_LIMIT,          0x00004810),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_IDTR_BASE,           0x00006818),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_IDTR_LIMIT,          0x00004812),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_ACTIVITY_STATE,      0x00004826),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_SYSENTER_CS,         0x0000482A),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_SYSENTER_ESP,        0x00006824),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_SYSENTER_EIP,        0x00006826),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_ES_SEL,              0x00000800),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_ES_BASE,             0x00006806),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_ES_LIMIT,            0x00004800),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_ES_AR,               0x00004814),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_CS_SEL,              0x00000802),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_CS_BASE,             0x00006808),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_CS_LIMIT,            0x00004802),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_CS_AR,               0x00004816),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_SS_SEL,              0x00000804),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_SS_BASE,             0x0000680A),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_SS_LIMIT,            0x00004804),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_SS_AR,               0x00004818),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_DS_SEL,              0x00000806),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_DS_BASE,             0x0000680C),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_DS_LIMIT,            0x00004806),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_DS_AR,               0x0000481A),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_FS_SEL,              0x00000808),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_FS_BASE,             0x0000680E),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_FS_LIMIT,            0x00004808),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_FS_AR,               0x0000481C),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_GS_SEL,              0x0000080A),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_GS_BASE,             0x00006810),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_GS_LIMIT,            0x0000480A),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_GS_AR,               0x0000481E),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_LDTR_SEL,            0x0000080C),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_LDTR_BASE,           0x00006812),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_LDTR_LIMIT,          0x0000480C),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_LDTR_AR,             0x00004820),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_TR_SEL,              0x0000080E),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_TR_BASE,             0x00006814),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_TR_LIMIT,            0x0000480E),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_TR_AR,               0x00004822),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_RSP,                 0x0000681C),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_RIP,                 0x0000681E),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_RFLAGS,              0x00006820),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_PHY_ADDR,            0x00002400),
	CONSTRUCT_VMCS_ENCODING(VMCS_GUEST_LINEAR_ADDR,         0x0000640A),
	CONSTRUCT_VMCS_ENCODING(VMCS_INSTR_ERROR,               0x00004400),
	CONSTRUCT_VMCS_ENCODING(VMCS_EXIT_REASON,               0x00004402),
	CONSTRUCT_VMCS_ENCODING(VMCS_EXIT_INT_INFO,             0x00004404),
	CONSTRUCT_VMCS_ENCODING(VMCS_EXIT_INT_ERR_CODE,         0x00004406),
	CONSTRUCT_VMCS_ENCODING(VMCS_IDT_VECTOR_INFO,           0x00004408),
	CONSTRUCT_VMCS_ENCODING(VMCS_IDT_VECTOR_ERR_CODE,       0x0000440A),
	CONSTRUCT_VMCS_ENCODING(VMCS_EXIT_INSTR_LEN,            0x0000440C),
	CONSTRUCT_VMCS_ENCODING(VMCS_EXIT_INSTR_INFO,           0x0000440E),
	CONSTRUCT_VMCS_ENCODING(VMCS_EXIT_QUAL,                 0x00006400)
};

_Static_assert(sizeof(g_field_data)/sizeof(vmcs_encoding_t) == VMCS_FIELD_COUNT, "VMCS field NOT aligned!");

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

	if ((vmcs->dirty_count < DIRTY_CACHE_SIZE)&&(!cache_is_dirty(vmcs,field_id))) {
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

	if (cache_is_valid(vmcs, field_id)) {
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

	if (vmcs->dirty_count <= DIRTY_CACHE_SIZE) {
		for (i=0; i<vmcs->dirty_count; i++) {
			field_id = vmcs->dirty_fields[i];
			vmx_vmwrite(g_field_data[field_id].encoding, vmcs->cache[field_id]);
		}
		// clear dirty cache/bitmap
		vmcs->dirty_count = 0;
		for (bitmap_idx=0; bitmap_idx<VMCS_BITMAP_SIZE; bitmap_idx++)
			vmcs->dirty_bitmap[bitmap_idx] = 0;
	} else {
		for (bitmap_idx=0; bitmap_idx<VMCS_BITMAP_SIZE; bitmap_idx++) {
			while (vmcs->dirty_bitmap[bitmap_idx]) {
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
	UNUSED uint16_t hcpu_id = host_cpu_id();
	vmcs_field_t i =0;
	UNUSED uint64_t u64;

	D(VMM_ASSERT(vmcs));

	for (i = 0; i < VMCS_FIELD_COUNT; ++i) {
		u64 = asm_vmread(g_field_data[i].encoding);
		if ((asm_get_rflags()&VMCS_ERRO_MASK) == 0) {
			if (cache_is_valid(vmcs, i)) {
				print_info("%d %40s (0x%04X) = hw_value(0x%llX),cache_value(0x%llX)\n",hcpu_id, g_field_data[i].name,g_field_data[i].encoding,u64,vmcs->cache[i]);
			} else {
				print_info("%d %40s (0x%04X) = hw_value(0x%llX),invalid cache\n",hcpu_id, g_field_data[i].name,g_field_data[i].encoding,u64);
			}
		} else {
			if (cache_is_valid(vmcs, i)) {
				print_info("%d %40s (0x%04X) = hw read fail,cache_value(0x%llX)\n",hcpu_id, g_field_data[i].name,g_field_data[i].encoding,vmcs->cache[i]);
			} else {
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
		if ((asm_get_rflags()&VMCS_ERRO_MASK) == 0) {
			if (cache_is_valid(vmcs, i)) {
				length += vmm_sprintf_s(cur_buf, left_size, "%d %40s (0x%04X) = hw_value(0x%llX),cache_value(0x%llX)\n",
						hcpu_id, g_field_data[i].name,g_field_data[i].encoding,u64,vmcs->cache[i]);
			} else {
				length += vmm_sprintf_s(cur_buf, left_size, "%d %40s (0x%04X) = hw_value(0x%llX),invalid cache\n",
						hcpu_id, g_field_data[i].name,g_field_data[i].encoding,u64);
			}
		} else {
			if (cache_is_valid(vmcs, i)) {
				length += vmm_sprintf_s(cur_buf, left_size, "%d %40s (0x%04X) = hw read fail,cache_value(0x%llX)\n",
						hcpu_id, g_field_data[i].name,g_field_data[i].encoding,vmcs->cache[i]);

			} else {
				length += vmm_sprintf_s(cur_buf, left_size, "%d %40s (0x%04X) = hw read fail,invalid cache\n",
						hcpu_id, g_field_data[i].name,g_field_data[i].encoding);

			}
		}
	}
	return length;
}

#define ENC_M_BITS      0x6000
#define ENC_64_WIDTH    0x2000
#define IS_ENCODING_64BIT(enc)   ((enc & (ENC_M_BITS)) == (ENC_64_WIDTH))
vmcs_field_t enc2id(uint32_t vmcs_encoding)
{
	uint32_t        encoding;
	vmcs_field_t    cur_field;

	encoding = vmcs_encoding;

	if (IS_ENCODING_HIGH_TYPE(encoding)) {
		if (!IS_ENCODING_64BIT(encoding)) {
			print_panic("VMCS Encoding %P does not map to a known HIGH type encoding\n", encoding);
			return -1;
		}

		encoding = encoding & (~ENC_HIGH_TYPE_BIT); // remove high type bit
	}

	/* search though all supported fields */
	for (cur_field = (vmcs_field_t)0; cur_field < VMCS_FIELD_COUNT; ++cur_field) {
		if (encoding == g_field_data[cur_field].encoding) {
			return cur_field;
		}
	}

	print_panic("VMCS Encoding %P is unknown\n", vmcs_encoding);
	return -1;

}
