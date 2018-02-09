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
#include "vmm_asm.h"
#include "dbg.h"
#include "guest.h"
#include "gpm.h"
#include "event.h"
#include "hmm.h"
#include "heap.h"
#include "host_cpu.h"
#include "vtd_acpi.h"
#include "vtd_mem_map.h"

#include "lib/util.h"
#include "modules/vtd.h"

#define VTD_REG_CAP     0x0008
#define VTD_REG_ECAP    0x0010
#define VTD_REG_GCMD    0x0018
#define VTD_REG_GSTS    0x001C
#define VTD_REG_RTADDR  0x0020
#define VTD_REG_CCMD    0x0028

/* GCMD_REG Bits */
#define VTD_GCMD_TE   (1ULL << 31)
#define VTD_GCMD_SRTP (1ULL << 30)

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

typedef struct {
	dma_root_entry_t         *root_table;
	uint64_t                 default_ctx_hpa;
} dma_remapping_t;

vtd_engine_t engine_list[DMAR_MAX_ENGINE];
dma_remapping_t g_remapping;

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

static void vtd_get_cap(uint8_t *max_leaf, uint8_t *tm, uint8_t *snoop)
{
	uint32_t i;
	uint32_t idx = 0;
	uint32_t val = 0;
	vtd_cap_reg_t cap = {.uint64 = 0};
	vtd_ext_cap_reg_t ext_cap = {.uint64 = 0};

	/* TM/SNOOP will be update to zero if hardware does not support */
	*max_leaf = MAM_LEVEL_PML4;
	*tm = 1;
	*snoop = 1;

	for (i=0; i<DMAR_MAX_ENGINE; i++) {
		if (!engine_list[i].reg_base_hva)
			break;

		cap.uint64 = vtd_read_reg64(engine_list[i].reg_base_hva,
						VTD_REG_CAP);
		val = ~(cap.bits.sllps);
		D(VMM_ASSERT(val));
		idx = asm_bsf32(val);

		/* Get maxium leaf level, MAM starts from 4KB, VT-D starts from 2MB */
		*max_leaf = MIN(*max_leaf, idx);

		/* Need 4-level page-table support */
		VMM_ASSERT_EX(cap.bits.sagaw & (1ull << 2), "4-level page-table isn't supported by vtd\n");

		/*
		 * Check TM/SNOOP support in external capablity register,
		 * Since they are all used for perfermance enhencement,
		 * and same Context entres/Paging entries are used for all buses,
		 * minimum support will be used.
		 */
		ext_cap.uint64 = vtd_read_reg64(engine_list[i].reg_base_hva,
							VTD_REG_ECAP);
		*tm = *tm & ext_cap.bits.di;
		*snoop = *snoop & ext_cap.bits.sc;
	}
}

static void vtd_send_global_cmd(vtd_engine_t *engine, uint32_t cmd)
{
	volatile uint32_t value;
	value = vtd_read_reg32(engine->reg_base_hva, VTD_REG_GSTS);

	value |= cmd;

	vtd_write_reg32(engine->reg_base_hva, VTD_REG_GCMD, value);

	while (1) {
		value = vtd_read_reg32(engine->reg_base_hva, VTD_REG_GSTS);
		if (value & cmd)
			break;
		else
			asm_pause();
	}
}

static void vtd_guest_setup(UNUSED guest_cpu_handle_t gcpu, void *pv)
{
	guest_handle_t guest = (guest_handle_t)pv;
	uint32_t i;

	/* Remove DMAR engine from Guest mapping */
	for (i=0; i<DMAR_MAX_ENGINE; i++) {
		if (engine_list[i].reg_base_hpa) {
			gpm_remove_mapping(guest,
				engine_list[i].reg_base_hpa, 4096);
		}
	}
}

static void vtd_reactivate_from_s3(UNUSED guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	if (host_cpu_id() == 0)
		vtd_activate();
}

void vtd_update_domain_mapping(UNUSED guest_cpu_handle_t gcpu, void *pv)
{
	event_gpm_set_t *event_gpm_set = (event_gpm_set_t *)pv;
	vtdpt_attr_t attr = {.uint32 = 0};
	mam_handle_t mam_handle = NULL;

	attr.bits.read = event_gpm_set->attr.bits.r;
	attr.bits.write = event_gpm_set->attr.bits.w;

#ifndef MULTI_GUEST_DMA
	if (event_gpm_set->guest->id != 0)
		return;
#endif

	mam_handle = vtd_get_mam_handle(gid2did(event_gpm_set->guest->id));
	D(VMM_ASSERT(mam_handle);)

	mam_insert_range(mam_handle,
			event_gpm_set->gpa,
			event_gpm_set->hpa,
			event_gpm_set->size,
			attr.uint32);
}

#define set_ctx_entry(ctx_entry, did, slptptr_hpa) \
{ \
	/* Present(1) = 1 */ \
	(ctx_entry)->low = (slptptr_hpa) | 0x1; \
	/* AW(66:64) = 2; DID(87:72) = did */ \
	(ctx_entry)->high = (((uint64_t)(did)) << 8) | 2; \
}

static void vtd_init_dev_mapping(void)
{
	dma_root_entry_t *root_entry;
	dma_context_entry_t *ctx_entry;
	dma_context_entry_t *ctx_table;
	uint32_t i;
	uint64_t slptptr_hpa;
	uint64_t ctx_table_hpa;

	slptptr_hpa = mam_get_table_hpa(vtd_get_mam_handle(gid2did(0)));

	ctx_table = (dma_context_entry_t *)page_alloc(1);
	for (i=0; i<256; i++) {
		ctx_entry = &ctx_table[i];
		set_ctx_entry(ctx_entry, gid2did(0), slptptr_hpa);
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

#ifdef MULTI_GUEST_DMA
void vtd_assign_dev(uint16_t domain_id, uint16_t dev_id)
{
	dma_root_entry_t *root_entry = NULL;
	dma_context_entry_t *ctx_table = NULL;
	dma_context_entry_t *ctx_entry = NULL;
	uint64_t ctx_table_hpa;
	uint64_t ctx_table_hva;
	uint64_t hpa;
	mam_handle_t mam_handle;

	mam_handle = vtd_get_mam_handle(domain_id);

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
	set_ctx_entry(ctx_entry, domain_id, mam_get_table_hpa(mam_handle));
}
#endif

void vtd_init(void)
{
	uint8_t max_leaf, tm, snoop;

	vtd_dmar_parse(engine_list);

	vtd_get_cap(&max_leaf, &tm, &snoop);
	set_translation_cap(max_leaf, tm, snoop);

	vtd_init_dev_mapping();

	event_register(EVENT_GPM_SET, vtd_update_domain_mapping);
	event_register(EVENT_GUEST_MODULE_INIT, vtd_guest_setup);
	event_register(EVENT_RESUME_FROM_S3, vtd_reactivate_from_s3);
}

static uint64_t get_root_table_hpa(void)
{
	uint64_t hpa, hva;
	hva = (uint64_t)g_remapping.root_table;
	VMM_ASSERT_EX(hmm_hva_to_hpa(hva, &hpa, NULL),
			"fail to convert hva 0x%llX to hpa\n", hva);
	return hpa;
}

static void vtd_enable_dmar(vtd_engine_t *engine, uint64_t rt_hpa)
{
	/* Set Root Table Address Register */
	vtd_write_reg64(engine->reg_base_hva, VTD_REG_RTADDR, (uint64_t)rt_hpa);

	/* Set Root Table Pointer*/
	vtd_send_global_cmd(engine, VTD_GCMD_SRTP);

	/* Translation Enable */
	vtd_send_global_cmd(engine, VTD_GCMD_TE);
}

void vtd_activate(void)
{
	uint32_t i;
	uint64_t rt_hpa;

	asm_wbinvd();

	rt_hpa = get_root_table_hpa();

	for (i=0; i<DMAR_MAX_ENGINE; i++) {
		if (engine_list[i].reg_base_hva) {
			vtd_enable_dmar(&engine_list[i], rt_hpa);
		}
	}
}
