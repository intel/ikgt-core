/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "guest.h"
#include "gcpu.h"
#include "event.h"
#include "lib/util.h"

#include "modules/l1tf.h"
#include "modules/ipc.h"

static uint32_t smt_mask, core_mask, package_mask;

typedef struct {
	uint32_t smt_id;
	uint32_t core_id;
	uint32_t package_id;
	uint32_t locked_cpu_num;
	boolean_t switch_to_secure;
} cpu_info_t;

static cpu_info_t g_cpu_info[MAX_CPU_NUM];

static inline uint32_t hw_mt_support(void)
{
	cpuid_params_t leaf_01 = {0x1, 0, 0, 0};
	asm_cpuid(&leaf_01);

	return leaf_01.edx & CPUID_EDX_HTT;
}

static inline uint32_t cpuid_leaf_0b_support(void)
{
	cpuid_params_t leaf_0b = {0xb, 0, 0, 0};
	asm_cpuid(&leaf_0b);

	return leaf_0b.ebx & 0xFFU;
}

static uint32_t find_mask_width(uint32_t max_count)
{
	uint32_t bit_index = 0;
	uint32_t mask_width = 0;

	max_count --;
	if (max_count == 0)
		return 0;

	bit_index = asm_bsr32(max_count);
	mask_width = bit_index + 1;

	return mask_width;
}

#define TYPE_INVALID 0U
#define TYPE_SMT     1U
#define TYPE_CORE    2U
#define LEVEL_TYPE(ecx) (((ecx) >> 8U) & 0xFFU)

/*
 * Reference:
 *     Intel-SDM
 *     Chapter 8.9.4: Algorithm for Three-Level Mappings of APIC_ID
 */
static void derive_cpu_masks(void)
{
	if (cpuid_leaf_0b_support()) {
		uint32_t mask_smt_shift;
		uint32_t mask_core_shift = 0;
		uint32_t core_plus_smt_mask;
		cpuid_params_t leaf_0b = {0xb, 0, 0, 0};
		asm_cpuid(&leaf_0b);

		mask_smt_shift = leaf_0b.eax & 0x1FU;
		smt_mask = ~((-1U) << mask_smt_shift);

		while (LEVEL_TYPE(leaf_0b.ecx)) {
			mask_core_shift = leaf_0b.eax & 0x1FU;
			leaf_0b.ecx &= 0xFF;
			leaf_0b.ecx ++;
			leaf_0b.eax = 0xb;
			asm_cpuid(&leaf_0b);
		}
		core_plus_smt_mask = ~((-1U) << mask_core_shift);

		core_mask = core_plus_smt_mask ^ smt_mask;
		package_mask = (-1U) << mask_core_shift;
	} else {
		uint32_t max_LPIDs_per_package, max_CoreIDs_per_package;
		uint32_t smt_mask_width, core_mask_width;
		cpuid_params_t leaf_01 = {0x1, 0, 0, 0};
		cpuid_params_t leaf_04 = {0x4, 0, 0, 0};

		asm_cpuid(&leaf_01);
		max_LPIDs_per_package = (leaf_01.ebx >> 16U) & 0xFF;

		asm_cpuid(&leaf_04);
		max_CoreIDs_per_package = (leaf_04.eax >> 26U) + 1U;

		smt_mask_width = find_mask_width(max_LPIDs_per_package/max_CoreIDs_per_package);
		core_mask_width = find_mask_width(max_CoreIDs_per_package);

		smt_mask = ((uint8_t)(0xFFU)) ^ ((uint8_t)(0xFFU << smt_mask_width));
		core_mask = ((uint8_t)(0xFFU << smt_mask_width)) ^ ((uint8_t)(0xFFU << (smt_mask_width + core_mask_width)));
		package_mask = (0xFFU) ^ (smt_mask | core_mask);
	}
}

static uint32_t get_x2apic_id(void)
{
	cpuid_params_t leaf_0b = {0xb, 0, 0, 0};
	asm_cpuid(&leaf_0b);

	return leaf_0b.edx;
}

static uint32_t get_init_apic_id(void)
{
	cpuid_params_t leaf_01 = {0x1, 0, 0, 0};
	asm_cpuid(&leaf_01);

	return ((leaf_01.ebx >> 24U) & 0xFFU);
}

static void flush_l1d(UNUSED guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	asm_wrmsr(MSR_FLUSH_CMD, L1D_FLUSH);
}

static void flush_all_cache(UNUSED guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	asm_wbinvd();
}

static void wait_cpu_lock(UNUSED guest_cpu_handle_t gcpu, void *arg)
{
	uint16_t sponsor_cpu_id = (uint16_t)(uint64_t)arg;

	asm_lock_add32(&g_cpu_info[sponsor_cpu_id].locked_cpu_num, 1U);

	while (g_cpu_info[sponsor_cpu_id].switch_to_secure) {
		asm_pause();
	}

	asm_lock_sub32(&g_cpu_info[sponsor_cpu_id].locked_cpu_num, 1U);
}

static void deschedule_cpus(UNUSED guest_cpu_handle_t gcpu, UNUSED void *arg)
{
	uint16_t hcpu_id;
	uint32_t i;
	uint32_t target_cpu_num = 0;

	hcpu_id = host_cpu_id();

	/* TODO: consider multiple pre-launched TEE in future */

	/* Wait until all target CPUs handled last event */
	while (g_cpu_info[hcpu_id].locked_cpu_num != 0U) {
		asm_pause();
	}

	g_cpu_info[hcpu_id].switch_to_secure = TRUE;

	for (i=0; i < host_cpu_num; i++) {
		if (i == hcpu_id)
			continue;
		if ((g_cpu_info[i].smt_id != g_cpu_info[hcpu_id].smt_id) &&
			(g_cpu_info[i].core_id == g_cpu_info[hcpu_id].core_id) &&
			(g_cpu_info[i].package_id == g_cpu_info[hcpu_id].package_id)) {
				target_cpu_num ++;
				ipc_exec_on_host_cpu(i, wait_cpu_lock, (void *)(uint64_t)hcpu_id);
		}
	}

	/* Wait until all target CPUs enter Host */
	while (g_cpu_info[hcpu_id].locked_cpu_num != target_cpu_num) {
		asm_pause();
	}
}

static void reschedule_cpus(UNUSED guest_cpu_handle_t gcpu, UNUSED void *arg)
{
	g_cpu_info[host_cpu_id()].switch_to_secure = FALSE;
}

static void mds_buff_overwrite(UNUSED guest_cpu_handle_t gcpu, UNUSED void *arg)
{
	__asm__ __volatile__ (
		"sub $8, %rsp     \n\t"
		"mov %ds, (%rsp)  \n\t"
		"verw (%rsp)      \n\t"
		"add $8, %rsp     \n\t"
	);
}

static void init_cpu_info(uint16_t hcpu_id)
{
	uint32_t apic_id;

	if (cpuid_leaf_0b_support()) {
		apic_id = get_x2apic_id();
	} else {
		apic_id = get_init_apic_id();
	}

	D(VMM_ASSERT(hcpu_id < MAX_CPU_NUM));

	g_cpu_info[hcpu_id].smt_id = apic_id & smt_mask;
	g_cpu_info[hcpu_id].core_id = apic_id & core_mask;
	g_cpu_info[hcpu_id].package_id = apic_id & package_mask;
}

/*
 * Since eVMM does not store any secrets, so the mitigation event only triggered when
 * do guest switch between TEE(SECURE) and NON-TEE(NON-SECURE).
 */
static void register_mitigation_event(void)
{
	uint64_t arch_cap_msr;
	boolean_t need_mitigate_l1tf;
	boolean_t need_mitigate_mds;
	cpuid_params_t cpuid_param = {7, 0, 0, 0}; //eax=7, ecx=0

	asm_cpuid(&cpuid_param);

	if (cpuid_param.edx & CPUID_EDX_ARCH_CAP) {
		arch_cap_msr = asm_rdmsr(MSR_ARCH_CAP);
		need_mitigate_l1tf = !(arch_cap_msr & RDCL_NO);
		need_mitigate_mds = !(arch_cap_msr & MDS_NO);

		if (need_mitigate_l1tf) {
			if (cpuid_param.edx & CPUID_EDX_L1D_FLUSH) {
				event_register(EVENT_SWITCH_TO_NONSECURE, flush_l1d);
			} else {
				event_register(EVENT_SWITCH_TO_NONSECURE, flush_all_cache);
			}
		}

		if (need_mitigate_mds) {
			if (cpuid_param.edx & CPUID_EDX_MD_CLEAR) {
				event_register(EVENT_SWITCH_TO_NONSECURE, mds_buff_overwrite);
			} else {
				print_warn("MD_CLEAR is not enumerated! Please update microcode!\n");
			}
		}

		if (need_mitigate_l1tf || need_mitigate_mds) {
			if (hw_mt_support()) {
				derive_cpu_masks();
				event_register(EVENT_SWITCH_TO_SECURE, deschedule_cpus);
				event_register(EVENT_SWITCH_TO_NONSECURE, reschedule_cpus);
			}
		}
	} else {
		print_warn("IA32_ARCH_CAPABILITIES is not supported!");
	}
}

void l1tf_init(void)
{
	uint16_t hcpu_id = host_cpu_id();

	if (hcpu_id == 0U)
		register_mitigation_event();

	if (hw_mt_support())
		init_cpu_info(hcpu_id);
}
