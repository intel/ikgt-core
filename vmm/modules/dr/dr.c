/*******************************************************************************
* Copyright (c) 2017 Intel Corporation
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
#include "guest.h"
#include "gcpu.h"
#include "event.h"
#include "vmm_asm.h"
#include "vmcs.h"
#include "lock.h"
#include "dbg.h"
#include "vmx_cap.h"
#include "heap.h"

#include "lib/util.h"

#include "modules/dr.h"

enum {
	REG_DR0 = 0,
	REG_DR1,
	REG_DR2,
	REG_DR3,
	REG_DR6,
	REG_DR_COUNT
};

typedef struct dr_info {
	guest_cpu_handle_t gcpu;
	uint64_t guest_dr[REG_DR_COUNT];
	struct dr_info *next;
} dr_info_t;

static dr_info_t *g_dr;
static vmm_lock_t dr_lock;

static dr_info_t *dr_lookup(guest_cpu_handle_t gcpu)
{
	dr_info_t *p_dr;

	lock_acquire_read(&dr_lock);
	p_dr = g_dr;
	while (p_dr) {
		if (p_dr->gcpu == gcpu)
			break;
		p_dr = p_dr->next;
	}

	lock_release(&dr_lock);
	return p_dr;
}

static void dr_swap_in(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	dr_info_t *dr;

	dr = dr_lookup(gcpu);
	if(dr == NULL)
	{
		/*for first swap in, register it to g_dr, but no need to
		 do restore*/
		dr = (dr_info_t *)mem_alloc(sizeof(dr_info_t));
		dr->gcpu = gcpu;
		lock_acquire_write(&dr_lock);
		dr->next = g_dr;
		g_dr = dr;
		lock_release(&dr_lock);
	}else{
		/*restore dr registers*/
		asm_set_dr0(dr->guest_dr[REG_DR0]);
		asm_set_dr1(dr->guest_dr[REG_DR1]);
		asm_set_dr2(dr->guest_dr[REG_DR2]);
		asm_set_dr3(dr->guest_dr[REG_DR3]);
		asm_set_dr6(dr->guest_dr[REG_DR6]);
	}
}

static void dr_swap_out(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	dr_info_t *dr;

	dr = dr_lookup(gcpu);

	/*it must be swapped in before, so the save
	 for this gcpu must exist*/
	D(VMM_ASSERT(dr);)

	/*save dr registers*/
	dr->guest_dr[REG_DR0] = asm_get_dr0();
	dr->guest_dr[REG_DR1] = asm_get_dr1();
	dr->guest_dr[REG_DR2] = asm_get_dr2();
	dr->guest_dr[REG_DR3] = asm_get_dr3();
	dr->guest_dr[REG_DR6] = asm_get_dr6();
}

/* this module only isolates DR0~DR3, DR6 between GUESTs.
 * for DR7 and DEBUG_CTRL_MSR, they are isolated by VMCS directly
 * for host DRs, DR7 and DEBUG_CTRL_MSR will be set to 0x400
 * and 0x0 in each VMExit, which disables all DRs.
 * So, host DR0~DR3, DR6 are NOT isolated from guests.
 */
void dr_isolation_init()
{
	lock_init(&dr_lock);
	event_register(EVENT_GCPU_SWAPIN, dr_swap_in);
	event_register(EVENT_GCPU_SWAPOUT, dr_swap_out);
}
