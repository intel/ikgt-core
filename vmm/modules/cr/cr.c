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
#include "vmm_asm.h"
#include "vmcs.h"
#include "lock.h"
#include "dbg.h"
#include "vmx_cap.h"

#include "modules/cr.h"

enum {
	REG_CR2 = 0,
	REG_CR8,
	REG_CR_COUNT
};

typedef struct cr_info {
	guest_cpu_handle_t gcpu;
	uint64_t guest_cr[REG_CR_COUNT];
	struct cr_info *next;
} cr_info_t;

static cr_info_t *g_cr;
static vmm_lock_t cr_lock;

static cr_info_t *cr_lookup(guest_cpu_handle_t gcpu)
{
	cr_info_t *p_cr;

	lock_acquire_read(&cr_lock);
	p_cr = g_cr;
	while (p_cr) {
		if (p_cr->gcpu == gcpu)
			break;
		p_cr = p_cr->next;
	}

	lock_release(&cr_lock);
	return p_cr;
}

static void cr_swap_in(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	cr_info_t *cr;

	cr = cr_lookup(gcpu);
	if(cr == NULL)
	{
		/*for first swap in, register it to g_cr, but no need to
		 do restore*/
		cr = (cr_info_t *)mem_alloc(sizeof(cr_info_t));
		cr->gcpu = gcpu;
		lock_acquire_write(&cr_lock);
		cr->next = g_cr;
		g_cr = cr;
		lock_release(&cr_lock);
	}else{
		/*restore cr registers*/
		asm_set_cr2(cr->guest_cr[REG_CR2]);
		asm_set_cr8(cr->guest_cr[REG_CR8]);
	}
}

static void cr_swap_out(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	cr_info_t *cr;

	cr = cr_lookup(gcpu);

	/*it must be swapped in before, so the save
	 for this gcpu must exist*/
	D(VMM_ASSERT(cr));

	/*save cr registers*/
	cr->guest_cr[REG_CR2] = asm_get_cr2();
	cr->guest_cr[REG_CR8] = asm_get_cr8();
}

static void cr_set_cr2(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	cr_info_t *cr;
	pf_info_t *pfinfo;

	D(VMM_ASSERT(gcpu));
	D(VMM_ASSERT(pv));

	cr = cr_lookup(gcpu);
	VMM_ASSERT_EX(cr, "cr for guest %d gcpu %d isn't registed\n",
			gcpu->guest->id, gcpu->id);

	/*save cr registers*/
	pfinfo = (pf_info_t *)pv;
	pfinfo->handled = TRUE;
	cr->guest_cr[REG_CR2] = pfinfo->cr2;
}

/* this module only isolates CR2 and CR8 between GUESTs.
 * for CR0, CR3, CR4, they are isolated by VMCS directly
 * for host CR2 and CR8, since it will not impact host and
 * host will not use them, they are NOT isolated from guests.
 */
void cr_isolation_init()
{
	lock_init(&cr_lock, "cr_lock");
	event_register(EVENT_GCPU_SWAPIN, cr_swap_in);
	event_register(EVENT_GCPU_SWAPOUT, cr_swap_out);
	event_register(EVENT_SET_CR2, cr_set_cr2);
}
