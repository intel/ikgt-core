/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_asm.h"
#include "host_cpu.h"
#include "gcpu.h"
#include "heap.h"
#include "vmcs.h"
#include "vmx_cap.h"
#include "hmm.h"
#include "vmx_asm.h"
#include "stack.h"
#include "scheduler.h"
#include "gdt.h"
#include "vmm_arch.h"

#include "lib/util.h"

/* main save area */
typedef struct {
	uint64_t vmxon_region_hpa;

	uint32_t pending_nmi;
	uint32_t padding;
}  host_cpu_save_area_t;

uint16_t host_cpu_num;

/**************************************************************************
*
* Host CPU model for VMCS
*
**************************************************************************/

/*---------------------------------- globals ------------------------------- */
host_cpu_save_area_t g_host_cpus[MAX_CPU_NUM];

/*-------------------------------------------------------------------------
 *
 * Enable VT on the current CPU
 *
 *------------------------------------------------------------------------- */
void host_cpu_vmx_on(void)
{
	host_cpu_save_area_t *hcpu = NULL;
	uint16_t my_cpu_id = host_cpu_id();

	hcpu = &(g_host_cpus[my_cpu_id]);
	if(hcpu->vmxon_region_hpa == 0)
	{
		hcpu->vmxon_region_hpa = vmcs_alloc();
	}
	/* Enable VMX in CR4 */
	asm_set_cr4(asm_get_cr4() | CR4_VMXE);

	vmx_on(&hcpu->vmxon_region_hpa);
}

uint32_t host_cpu_get_pending_nmi(void)
{
	uint16_t hcpu_id = host_cpu_id();

	return g_host_cpus[hcpu_id].pending_nmi;
}

// nmi is increased 1 by 1
void host_cpu_inc_pending_nmi(void)
{
	uint16_t hcpu_id = host_cpu_id();

	asm_lock_inc32(&(g_host_cpus[hcpu_id].pending_nmi));
}

// several pending_nmis might be consumed together
void host_cpu_dec_pending_nmi(uint32_t num)
{
	uint16_t hcpu_id = host_cpu_id();

	asm_lock_sub32(&(g_host_cpus[hcpu_id].pending_nmi), num);
}

void host_cpu_clear_pending_nmi()
{
	uint16_t hcpu_id = host_cpu_id();

	g_host_cpus[hcpu_id].pending_nmi = 0;
}

