/*******************************************************************************
 * Copyright (c) 2018 Intel Corporation
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
#include "mam.h"
#include "dbg.h"
#include "hmm.h"
#include "vmm_util.h"
#include "vmm_objects.h"
#include "guest.h"
#include "gpm.h"
#include "heap.h"
#include "event.h"
#include "host_cpu.h"
#include "vtd_acpi.h"

#include "lib/util.h"

#include "modules/vtd.h"

#define VTD_REG_CAP     0x0008
#define VTD_REG_ECAP    0x0010
#define VTD_REG_GCMD    0x0018
#define VTD_REG_GSTS    0x001C
#define VTD_REG_RTADDR  0x0020
#define VTD_REG_CCMD    0x0028

/*
 * ADDR field in Page-Table Entry will be evaluated by hardware only
 * when at least one of the Read(R) and Write(W) fields is set.
 * VT-D spec chapter 9.3 Page-Table Entry
 */
#define VTD_PT_R         (1ULL << 0)
#define VTD_PT_W         (1ULL << 1)
#define VTD_PT_P         (VTD_PT_R | VTD_PT_W)
#define VTD_PT_SP        (1ULL << 7)
#define VTD_PT_SNP       (1ULL << 11)
#define VTD_PT_TM        (1ULL << 62)

typedef union {
	struct {
		uint32_t nd         : 3;
		uint32_t afl        : 1;
		uint32_t rwbf       : 1;
		uint32_t pmlr       : 1;
		uint32_t pmhr       : 1;
		uint32_t cm         : 1;
		uint32_t sagaw      : 5;
		uint32_t rsvd0      : 3;
		uint32_t mgaw       : 6;
		uint32_t zlr        : 1;
		uint32_t isoch      : 1;
		uint32_t fro_low    : 8;
		uint32_t fro_high   : 2;
		uint32_t sllps      : 4;
		uint32_t rsvd1      : 1;
		uint32_t psi        : 1;
		uint32_t nfr        : 8;
		uint32_t mamv       : 6;
		uint32_t dwd        : 1;
		uint32_t drd        : 1;
		uint32_t rsvd2      : 8;
	} bits;
	uint64_t uint64;
} vtd_cap_reg_t;

typedef union {
	struct {
		uint32_t c      : 1;
		uint32_t qi     : 1;
		uint32_t di     : 1;
		uint32_t ir     : 1;
		uint32_t eim    : 1;
		uint32_t ch     : 1;
		uint32_t pt     : 1;
		uint32_t sc     : 1;
		uint32_t ivo    : 10;
		uint32_t rsvd   : 2;
		uint32_t mhmv   : 4;
		uint32_t rsvd1  : 8;
		uint32_t rsvd2;
	} bits;
	uint64_t uint64;
} vtd_ext_cap_reg_t;

typedef struct {
	uint64_t	low;
	uint64_t	high;
} dma_root_entry_t;

typedef struct {
	uint64_t	low;
	uint64_t	high;
} dma_context_entry_t;

typedef struct addr_trans_table {
	mam_handle_t                  address_space;
	uint16_t                      guest_id;
	uint16_t                      padding[3];
	struct addr_trans_table       *next;
} addr_trans_table_t;

typedef struct {
	vtd_engine_t             engine_list[DMAR_MAX_ENGINE];
	uint8_t                  max_leaf;
	uint8_t                  tm;
	uint8_t                  snoop;
	uint8_t                  pad[5];
	dma_root_entry_t         *root_table;
	addr_trans_table_t       trans_table;
	uint64_t                 default_ctx_hpa;
	mam_entry_ops_t          vtd_entry_ops;
} vtd_dma_remapping_t;

typedef union {
	struct {
		uint32_t read  : 1;
		uint32_t write : 1;
		uint32_t snoop : 1;
		uint32_t tm    : 1;
		uint32_t rsvd1 : 28;
	} bits;
	uint32_t uint32;
} vtdpt_attr_t;

/* TM/SNOOP will be update to zero if hardware does not support */
static vtd_dma_remapping_t g_remapping = {
	.max_leaf = MAM_LEVEL_PML4,
	.tm = 1,
	.snoop = 1,
};

static inline
uint64_t vtd_read_reg64(uint64_t base_hva, uint64_t reg)
{
	return *((volatile uint64_t *)(base_hva + reg));
}

static inline
void vtd_write_reg64(uint64_t base_hva, uint64_t reg, uint64_t value)
{
	*((volatile uint64_t *)(base_hva + reg)) = value;
}

static inline
uint32_t vtd_read_reg32(uint64_t base_hva, uint64_t reg)
{
	return *((volatile uint32_t *)(base_hva + reg));
}

static inline
void vtd_write_reg32(uint64_t base_hva, uint64_t reg, uint32_t value)
{
	*((volatile uint32_t *)(base_hva + reg)) = value;
}

static boolean_t vtd_addr_trans_is_leaf(uint64_t entry, uint32_t level)
{
	if (MAM_LEVEL_PT == level)
		return TRUE;

	if (entry & VTD_PT_P)
		return (entry & VTD_PT_SP) ? TRUE : FALSE;

	/* Not present */
	return TRUE;
}

static boolean_t vtd_addr_trans_is_present(uint32_t attr)
{
	return (attr & VTD_PT_P) ? TRUE : FALSE;
}

static void vtd_addr_trans_to_table(uint64_t *p_entry)
{
	*p_entry &= MASK64_MID(51, 12);

	*p_entry |= VTD_PT_P;
}

static void
vtd_addr_trans_to_leaf(uint64_t *p_entry, uint32_t level, uint32_t attr)
{
	if (attr & VTD_PT_P) {
		vtdpt_attr_t vtd_attr;
		vtd_attr.uint32 = attr;

		/* Set R/W in same time */
		*p_entry &= MASK64_MID(51, 12);
		*p_entry |= attr & VTD_PT_P;

		if (level != MAM_LEVEL_PT)
			*p_entry |= VTD_PT_SP;

		if (vtd_attr.bits.snoop)
			*p_entry |= VTD_PT_SNP;

		if (vtd_attr.bits.tm)
			*p_entry |= VTD_PT_TM;
	} else {
		*p_entry = 0;
	}
}

static uint32_t vtd_leaf_get_attr(uint64_t leaf_entry, UNUSED uint32_t level)
{
	vtdpt_attr_t vtd_attr;

	D(VMM_ASSERT(level <= MAM_LEVEL_PML4));

	// Not present
	if ((leaf_entry & VTD_PT_P) == 0)
		return 0;

	// Get read, write, snoop, tm bit
	vtd_attr.uint32 = 0;
	vtd_attr.bits.read  = !!(leaf_entry & VTD_PT_R);
	vtd_attr.bits.write = !!(leaf_entry & VTD_PT_W);
	vtd_attr.bits.snoop = !!(leaf_entry & VTD_PT_SNP);
	vtd_attr.bits.tm    = !!(leaf_entry & VTD_PT_TM);

	return vtd_attr.uint32;
}

static void init_vtd_entry_ops(mam_entry_ops_t *entry_ops)
{
	// Hard code 4-level page-table, leaf should be 3
	entry_ops->max_leaf_level = g_remapping.max_leaf;
	entry_ops->is_leaf        = vtd_addr_trans_is_leaf;
	entry_ops->is_present     = vtd_addr_trans_is_present;
	entry_ops->to_table       = vtd_addr_trans_to_table;
	entry_ops->to_leaf        = vtd_addr_trans_to_leaf;
	entry_ops->leaf_get_attr  = vtd_leaf_get_attr;
}

static void vtd_init_cap(void)
{
	uint32_t i;
	uint32_t idx = 0;
	uint32_t val = 0;
	vtd_cap_reg_t cap = {.uint64 = 0};
	vtd_ext_cap_reg_t ext_cap = {.uint64 = 0};

	for (i=0; i<DMAR_MAX_ENGINE; i++) {
		if (g_remapping.engine_list[i].reg_base_hva) {
			cap.uint64 = vtd_read_reg64(g_remapping.engine_list[i].reg_base_hva,
							VTD_REG_CAP);
			val = ~(cap.bits.sllps);
			D(VMM_ASSERT(val));
			idx = asm_bsf32(val);

			/* Get maxium leaf level, MAM starts from 4KB, VT-D starts from 2MB */
			g_remapping.max_leaf = MIN(g_remapping.max_leaf, idx);

			/* Need 4-level page-table support */
			VMM_ASSERT_EX(cap.bits.sagaw & (1ull << 2), "4-level page-table isn't supported by vtd\n");

			/*
			 * Check TM/SNOOP support in external capablity register,
			 * Since they are all used for perfermance enhencement,
			 * and same Context entres/Paging entries are used for all buses,
			 * minimum support will be used.
			 */
			ext_cap.uint64 = vtd_read_reg64(g_remapping.engine_list[i].reg_base_hva,
							VTD_REG_ECAP);
			g_remapping.tm = g_remapping.tm & ext_cap.bits.di;
			g_remapping.snoop = g_remapping.snoop & ext_cap.bits.sc;
		}
	}
}

static void vtd_init_mapping()
{
	dma_root_entry_t *root_entry;
	dma_context_entry_t *ctx_entry;
	dma_context_entry_t *ctx_table;
	uint32_t i;
	uint64_t slptptr_hpa;
	uint64_t ctx_table_hpa;

	init_vtd_entry_ops(&g_remapping.vtd_entry_ops);

	/* The default translation table is only for Guest[0] */
	g_remapping.trans_table.address_space = mam_create_mapping(&g_remapping.vtd_entry_ops, 0);
	g_remapping.trans_table.guest_id = 0;
	g_remapping.trans_table.next = NULL;

	slptptr_hpa = mam_get_table_hpa(g_remapping.trans_table.address_space);

	ctx_table = (dma_context_entry_t *)page_alloc(1);
	for (i=0; i<256; i++) {
		ctx_entry = &ctx_table[i];
		/* Present(1) = 1*/
		ctx_entry->low  = slptptr_hpa | 0x1;
		/* AW(66:64) = 2; DID(87:72) = 0 */
		ctx_entry->high = 2;
	}

	VMM_ASSERT_EX(hmm_hva_to_hpa((uint64_t)ctx_table, &ctx_table_hpa, NULL),
			"fail to convert hva %p to hpa\n", ctx_table);
	g_remapping.default_ctx_hpa = ctx_table_hpa;

	g_remapping.root_table = (dma_root_entry_t *)page_alloc(1);
	/* Cover all buses */
	for(i=0; i<256; i++) {
		root_entry = &g_remapping.root_table[i];
		/* Same context entry for all PCI bus */
		/* Present(1) = 1 */
		root_entry->low = ctx_table_hpa | 0x1;
		root_entry->high = 0;
	}
}

static void vtd_send_global_cmd(uint32_t bit)
{
	volatile uint32_t value = 0;
	uint32_t i = 0;

	for (i=0; i<DMAR_MAX_ENGINE; i++) {
		if (g_remapping.engine_list[i].reg_base_hva) {
			value = vtd_read_reg32(g_remapping.engine_list[i].reg_base_hva,
						VTD_REG_GSTS);
			value |= 1U << bit;

			vtd_write_reg32(g_remapping.engine_list[i].reg_base_hva,
				VTD_REG_GCMD,
				value);

			while (1) {
				value = vtd_read_reg32(g_remapping.engine_list[i].reg_base_hva,
							VTD_REG_GSTS);
				if (!(value & (1ull << bit)))
					asm_pause();
				else
					break;
			}
		}
	}
}

static void vtd_guest_setup(UNUSED guest_cpu_handle_t gcpu, void *pv)
{
	guest_handle_t guest = (guest_handle_t)pv;
	uint32_t i;

	/* Remove DMAR engine from Guest mapping */
	for (i=0; i<DMAR_MAX_ENGINE; i++) {
		if (g_remapping.engine_list[i].reg_base_hpa) {
			gpm_remove_mapping(guest,
				g_remapping.engine_list[i].reg_base_hpa, 4096);
		}
	}
}

static void vtd_reinit_from_s3(UNUSED guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	if (host_cpu_id() == 0)
		vtd_done();
}

static mam_handle_t vtd_get_mam_handle(uint16_t guest_id)
{
#ifdef MULTI_GUEST_DMA
	addr_trans_table_t *trans_table = &g_remapping.trans_table;
	while (trans_table) {
		if (trans_table->guest_id == guest_id) {
			VMM_ASSERT_EX(trans_table->address_space,
				"VT-D: Address mapping NOT created for Guest[%d]!\n", guest_id);
			return trans_table->address_space;
		}
		trans_table = trans_table->next;
	}

	/* Create new table if address translation table NOT found in the list */
	trans_table = (addr_trans_table_t *)mem_alloc(sizeof(addr_trans_table_t));
	trans_table->guest_id = guest_id;
	trans_table->address_space = mam_create_mapping(&g_remapping.vtd_entry_ops, 0);

	trans_table->next = g_remapping.trans_table.next;
	g_remapping.trans_table.next = trans_table;

	return trans_table->address_space;
#else
	if (guest_id == 0)
		return g_remapping.trans_table.address_space;
	else
		return NULL;
#endif
}

static void vtd_update_range(UNUSED guest_cpu_handle_t gcpu, void *pv)
{
	event_gpm_set_t *event_gpm_set = (event_gpm_set_t *)pv;
	vtdpt_attr_t attr = {.uint32 = 0};
	mam_handle_t mam_handle;

	attr.bits.read = event_gpm_set->attr.bits.r;
	attr.bits.write = event_gpm_set->attr.bits.w;
	attr.bits.tm = g_remapping.tm;
	attr.bits.snoop = g_remapping.snoop;

	mam_handle = vtd_get_mam_handle(event_gpm_set->guest->id);
	if (mam_handle) {
		mam_insert_range(mam_handle,
				event_gpm_set->gpa,
				event_gpm_set->hpa,
				event_gpm_set->size,
				attr.uint32);
	}
}

#ifdef MULTI_GUEST_DMA
void vtd_assign_dev(uint16_t guest_id, uint16_t dev_id)
{
	dma_root_entry_t *root_entry = NULL;
	dma_context_entry_t *ctx_table = NULL;
	dma_context_entry_t *ctx_entry = NULL;
	uint64_t ctx_table_hpa;
	uint64_t ctx_table_hva;
	uint64_t hpa;

	if (guest_id == 0) {
		print_warn("VT-D: No need to assign device(id=%d) for Guest[0]\n", dev_id);
		return;
	}

	/* Locate Context-table from Root-table according to device_id(Bus) */
	root_entry = &g_remapping.root_table[dev_id>>8];
	ctx_table_hpa = root_entry->low & (~0x1ULL);
	VMM_ASSERT_EX(hmm_hpa_to_hva(ctx_table_hpa, &ctx_table_hva),
		"Failed to convert hpa(0x%llx) to hva!\n", ctx_table_hpa);

	/* If Context-table equals to default Context-table, create a new
	 * Context-table from default table and reset the root-entry to new
	 * Context-table. */
	if (ctx_table_hpa == g_remapping.default_ctx_hpa) {
		/* Alloc new context-table and copy from default context-table */
		ctx_table = (dma_context_entry_t *)page_alloc(1);
		memcpy((void *)ctx_table, (void *)ctx_table_hva, PAGE_4K_SIZE);

		/* Redirect the root-entry to new context-table */
		VMM_ASSERT_EX(hmm_hva_to_hpa((uint64_t)ctx_table, &hpa, NULL),
			"Failed to convert hva(0x%llx) to hpa!\n", (uint64_t)ctx_table);
		root_entry->low = hpa | 1;
	} else {
		/* The Context-table already created, use it directly */
		ctx_table = (dma_context_entry_t *)ctx_table_hva;
	}

	/* Locate Context-entry from Context-table according to device_id(Dev:Func) */
	ctx_entry = &ctx_table[dev_id & 0xFF];
	ctx_entry->low = mam_get_table_hpa(vtd_get_mam_handle(guest_id)) | 0x1;
}
#endif

void vtd_init(void)
{
	vtd_dmar_parse(g_remapping.engine_list);

	vtd_init_cap();

	vtd_init_mapping();

	event_register(EVENT_GPM_SET, vtd_update_range);
	event_register(EVENT_GUEST_MODULE_INIT, vtd_guest_setup);
	event_register(EVENT_RESUME_FROM_S3, vtd_reinit_from_s3);
}

void vtd_done()
{
	uint32_t i = 0;
	uint64_t hva = 0;
	uint64_t hpa = 0;

	hva = (uint64_t)g_remapping.root_table;
	VMM_ASSERT_EX(hmm_hva_to_hpa(hva, &hpa, NULL),
			"fail to convert hva 0x%llX to hpa\n", hva);

	for (i=0; i<DMAR_MAX_ENGINE; i++) {
		if (g_remapping.engine_list[i].reg_base_hva) {
			vtd_write_reg64(g_remapping.engine_list[i].reg_base_hva,
				VTD_REG_RTADDR,
				(uint64_t)hpa);
		}
	}

	asm_wbinvd();

	/* Interrupt remapping is disabled by default */
	/* set root entry talbe */
	vtd_send_global_cmd(30);
	/* enable translation */
	vtd_send_global_cmd(31);
}
