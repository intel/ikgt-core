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

#include "lib/util.h"

#include "modules/acpi.h"
#include "modules/vtd.h"

#define DMAR_SIGNATURE 0x52414d44  //the ASCII vaule for "DMAR"

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

typedef struct {
	acpi_table_header_t header;
	uint8_t             width;
	uint8_t             flags;
	uint8_t             reserved[10];
	uint8_t             remapping_structures[0];
} acpi_dmar_table_t;

typedef struct {
	uint16_t    type;
	uint16_t    length;
} acpi_dmar_header_t;

typedef struct {
	uint8_t     dev;
	uint8_t     func;
} acpi_path_element_t;

typedef struct {
	uint8_t             type;
	uint8_t             length;
	uint16_t            reserved;
	uint8_t             enumeration_id;
	uint8_t             start_bus_num;
	acpi_path_element_t path[0];
} acpi_device_scope_t;

typedef struct {
	acpi_dmar_header_t  header;
	uint8_t             flags;
	uint8_t             reserved;
	uint16_t            segment;
	uint64_t            reg_base_hpa;
	acpi_device_scope_t device_scope[0];
} acpi_dma_hw_unit_t;

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
		uint32_t sps        : 4;
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
	uint64_t         reg_base_hva[DMAR_MAX_ENGINE];
	uint64_t         reg_base_hpa[DMAR_MAX_ENGINE];
	uint8_t          max_leaf;
	uint8_t          tm;
	uint8_t          snoop;
	uint8_t          device_gpu_engine;
	uint8_t          pad[4];
	dma_root_entry_t *root_table;
	mam_handle_t     address_space;
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

static vtd_dma_remapping_t g_remapping;

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

static mam_entry_ops_t* vtd_make_entry_ops(void)
{
	mam_entry_ops_t *entry_ops = NULL;

	entry_ops =  mem_alloc(sizeof(mam_entry_ops_t));

	// Hard code 4-level page-table, leaf should be 3
	entry_ops->max_leaf_level = g_remapping.max_leaf;
	entry_ops->is_leaf        = vtd_addr_trans_is_leaf;
	entry_ops->is_present     = vtd_addr_trans_is_present;
	entry_ops->to_table       = vtd_addr_trans_to_table;
	entry_ops->to_leaf        = vtd_addr_trans_to_leaf;
	entry_ops->leaf_get_attr  = vtd_leaf_get_attr;

	return entry_ops;
}

#ifdef SKIP_DMAR_GPU
// OAM-42091 work round -- start
/*
 * Assumption:
 *	1. If DMAR engine takes charge of GPU, type of device scope should be
 *		0x01: PCI Endpoint Device, bridge is not expected, do not need to
 *		walk through bridge from start bus. And GPU should be the one and
 *		the only device in devoce scope. So device number should be 1.
 *	2. Bus:Dev:Func of GPU equals to 0:2:0.
 */
static boolean_t dmar_engine_takes_charge_of_gcpu(acpi_dma_hw_unit_t *entry)
{
	uint32_t num_of_devices = 0;
	acpi_device_scope_t *device_scope;

	/*
	 * Flags.Bit0: INCLUDE_PCI_ALL. If clear, this remapping hardware unit has
	 * under its scope only devices in the specified Segment that are explicitly
	 * identified through the 'Device Scope' field.
	 *
	 * More details please reference VT Directed IO Specification
	 * Chapter 8.3: DMA Remapping Hardware Uint Definition Structure
	 */
	if (entry->flags & 1) {
		return FALSE;
	}

	device_scope = (acpi_device_scope_t *)entry->device_scope;
	num_of_devices = (device_scope->length - OFFSET_OF(acpi_device_scope_t, path))
		/ sizeof(acpi_path_element_t);

	/*
	 * GPU device is PCI Endpoint Device, walk through bridge is unexpected.
	 * So numbe of device should be 1.
	 * Device type should be 1(PCI Endpoint device)
	 * Bus:Dev:Func of GPU shoule be 0:2:0
	 */

	if ((1 == num_of_devices) &&
		(1 == device_scope->type) &&
		(0 == device_scope->start_bus_num) &&
		(2 == device_scope->path->dev) &&
		(0 == device_scope->path->func)) {
		return TRUE;
	}

	return FALSE;
}
// OAM-42091 work round -- end
#endif

static void dmar_parse_drhd(acpi_dma_hw_unit_t *entry)
{
	static uint32_t dmar_engine = 0;
	uint32_t val = 0;
	uint32_t idx = 0;
	uint64_t hva = 0;
	vtd_cap_reg_t cap = {.uint64 = 0};
	vtd_ext_cap_reg_t ext_cap = {.uint64 = 0};

	VMM_ASSERT_EX((dmar_engine < DMAR_MAX_ENGINE),
			"too many dmar engines\n");

	VMM_ASSERT_EX(hmm_hpa_to_hva(entry->reg_base_hpa, &hva),
			"fail to convert hpa 0x%llX to hva", entry->reg_base_hpa);

	g_remapping.reg_base_hva[dmar_engine] = hva;
	g_remapping.reg_base_hpa[dmar_engine++] = entry->reg_base_hpa;

	/* Get maxium leaf level, MAM starts from 4KB, VT-D starts from 2MB */
	cap.uint64 = vtd_read_reg64(hva, VTD_REG_CAP);
	val = ~(cap.bits.sps);
	D(VMM_ASSERT(val));
	idx = asm_bsf32(val);

	g_remapping.max_leaf = MIN(g_remapping.max_leaf, idx);

	/* Need 4-level page-table support */
	VMM_ASSERT_EX(cap.bits.sagaw & (1ull << 2), "4-level page-table isn't supported by vtd\n");

	/*
	 * Check TM/SNOOP support in external capablity register,
	 * Since they are all used for perfermance enhencement,
	 * and same Context entres/Paging entries are used for all buses,
	 * minimum support will be used.
	 */
	ext_cap.uint64 = vtd_read_reg64(hva, VTD_REG_ECAP);
	g_remapping.tm = g_remapping.tm & ext_cap.bits.di;
	g_remapping.snoop = g_remapping.snoop & ext_cap.bits.sc;


#ifdef SKIP_DMAR_GPU
	// OAM-42091 work round -- start
	/*
	 * We need to figure out which engine takes charge in gpu
	 * This is work round for OAM-42091, if this OAM-42091 addressed,
	 * this snippet of code should be removed.
	 */
	if (dmar_engine_takes_charge_of_gcpu(entry)) {
		g_remapping.device_gpu_engine = dmar_engine - 1;
	}
	// OAM-42091 work round -- end
#endif
}

static void dmar_parse(acpi_dmar_table_t *dmar)
{
	uint32_t remaining = 0;
	uint32_t cur_offset = 0;
	acpi_dmar_header_t *entry;

	remaining = dmar->header.length - sizeof(acpi_dmar_table_t);

	while (remaining > 0) {
		entry = (acpi_dmar_header_t *)(dmar->remapping_structures + cur_offset);

		switch(entry->type) {
			/* DMAR type hardware uint */
			case 0:
				dmar_parse_drhd((acpi_dma_hw_unit_t *)entry);
				break;

			/* Just take care of DMAR hw uint */
			default:
				break;
		}

		remaining -= entry->length;
		cur_offset += entry->length;
	}
}

static void vtd_init_mapping()
{
	dma_root_entry_t *root_entry;
	dma_context_entry_t *context_entry;
	dma_context_entry_t *context_table;
	uint32_t i;
	uint64_t slptptr_hpa;
	uint64_t ctp_hpa;

	g_remapping.address_space = mam_create_mapping(vtd_make_entry_ops(), 0);

	slptptr_hpa = mam_get_table_hpa(g_remapping.address_space);

	context_table = (dma_context_entry_t *)page_alloc(1);
	for (i=0; i<256; i++) {
		context_entry = &context_table[i];
		/* Present(1) = 1*/
		context_entry->low  = slptptr_hpa | 0x1;
		/* AW(66:64) = 2; DID(87:72) = 0 */
		context_entry->high = 2;
	}

	VMM_ASSERT_EX(hmm_hva_to_hpa((uint64_t)context_table, &ctp_hpa, NULL),
			"fail to convert hva %p to hpa\n", context_table);

	g_remapping.root_table = (dma_root_entry_t *)page_alloc(1);
	/* Cover all buses */
	for(i=0; i<256; i++) {
		root_entry = &g_remapping.root_table[i];
		/* Same context entry for all PCI bus */
		/* Present(1) = 1 */
		root_entry->low = ctp_hpa | 0x1;
		root_entry->high = 0;
	}
}

static void vtd_send_global_cmd(uint32_t bit)
{
	volatile uint32_t value = 0;
	uint32_t i = 0;

	for (i=0; i<DMAR_MAX_ENGINE; i++) {

#ifdef SKIP_DMAR_GPU
// OAM-42091 work round -- start
		if (i == g_remapping.device_gpu_engine)
			continue;
// OAM-42091 work round -- end
#endif
		if (g_remapping.reg_base_hva[i]) {
			value = vtd_read_reg32(g_remapping.reg_base_hva[i],
						VTD_REG_GSTS);
			value |= 1U << bit;

			vtd_write_reg32(g_remapping.reg_base_hva[i],
				VTD_REG_GCMD,
				value);

			while (1) {
				value = vtd_read_reg32(g_remapping.reg_base_hva[i],
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
		if (g_remapping.reg_base_hpa[i]) {
			gpm_remove_mapping(guest,
				g_remapping.reg_base_hpa[i], 4096);
		}
	}
}

static void vtd_reinit_from_s3(UNUSED guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	if (host_cpu_id() == 0)
		vtd_done();
}

static void vtd_update_range(UNUSED guest_cpu_handle_t gcpu, void *pv)
{
	event_gpm_set_t *event_gpm_set = (event_gpm_set_t *)pv;
	vtdpt_attr_t attr = {.uint32 = 0};

	attr.bits.read = event_gpm_set->attr.bits.r;
	attr.bits.write = event_gpm_set->attr.bits.w;
	attr.bits.tm = g_remapping.tm;
	attr.bits.snoop = g_remapping.snoop;

	if (event_gpm_set->guest->id == 0) {
		mam_insert_range(g_remapping.address_space,
				event_gpm_set->gpa,
				event_gpm_set->hpa,
				event_gpm_set->size,
				attr.uint32);
	}
}

void vtd_init(void)
{
	acpi_dmar_table_t *acpi_dmar = NULL;

	acpi_dmar = (acpi_dmar_table_t *)acpi_locate_table(DMAR_SIGNATURE);
	VMM_ASSERT_EX(acpi_dmar, "acpi_dmar is NULL\n");
	print_info("VTD is detected.\n");

	memset(&g_remapping, 0, sizeof(vtd_dma_remapping_t));

	/* TM/SNOOP will be update to zero if hardware does not support */
	g_remapping.tm       = 1;
	g_remapping.snoop    = 1;
	g_remapping.max_leaf = MAM_LEVEL_PML4;
	/* DMAR engine starts from 0, set an invalid value first */
	g_remapping.device_gpu_engine = 0xFF;

	dmar_parse(acpi_dmar);
	/* Hide VT-D ACPI table */
	memset((void *)&acpi_dmar->header.signature, 0, acpi_dmar->header.length);

	vtd_init_mapping();

	event_register(EVENT_GPM_SET, vtd_update_range);
	event_register(EVENT_GUEST_MODULE_INIT, vtd_guest_setup);
	event_register(EVENT_RESUME_FROM_S3, vtd_reinit_from_s3);

#ifdef SKIP_DMAR_GPU
	/*
	 * Add print info here to check VT-D GPU work round easy.
	 * DMAR engine 0 takes charge of GPU
	 */
	print_info("VT-D: SKIP_DMAR_GPU is on\n");
	print_info("\tSkip DMAR engine:%d\n", g_remapping.device_gpu_engine);

	if (0 != g_remapping.device_gpu_engine) {
		print_info("*****************************************************\n");
		print_info("!!CAUTION!!:\t");
		print_info("\tDAMR ENGINE(%d) FOR GPU IS UNEXPECTED\n", g_remapping.device_gpu_engine);
		print_info("*****************************************************\n");
	}

#endif
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
		if (g_remapping.reg_base_hva[i]) {
			vtd_write_reg64(g_remapping.reg_base_hva[i],
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
