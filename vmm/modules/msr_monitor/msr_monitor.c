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
#include "vmm_arch.h"
#include "heap.h"
#include "guest.h"
#include "gcpu.h"
#include "gcpu_inject_event.h"
#include "vmcs.h"
#include "vmexit.h"
#include "dbg.h"
#include "hmm.h"
#include "vmx_cap.h"
#include "event.h"
#include "mttr.h"

#include "lib/util.h"

#include "modules/msr_monitor.h"

#define MSR_LOW_FIRST   0
#define MSR_LOW_LAST    0x1FFF
#define MSR_HIGH_FIRST  0xC0000000
#define MSR_HIGH_LAST   0xC0001FFF

typedef struct msr_monitor{
	uint32_t		msr_id;
	uint32_t		pad;
	msr_handler_t		msr_read_handler;
	msr_handler_t		msr_write_handler;
	struct msr_monitor	*next;
} msr_monitor_t;

typedef struct msr_monitor_guest{
	uint16_t			guest_id;
	uint8_t				pad[6];
	uint8_t				*msr_bitmap;
	msr_monitor_t			*msr_list;
	struct msr_monitor_guest	*next;
} msr_monitor_guest_t;

/*---------------------------------Local Data---------------------------------*/
static msr_monitor_guest_t *g_msr_monitor;

/*-------------------------------Code Starts Here-----------------------------*/

inline static msr_monitor_guest_t *msr_monitor_guest_lookup(uint16_t guest_id)
{
	msr_monitor_guest_t *p_guest_msr_mon = g_msr_monitor;
	while (p_guest_msr_mon) {
		if (p_guest_msr_mon->guest_id == guest_id)
			break;
		p_guest_msr_mon = p_guest_msr_mon->next;
	}
	return p_guest_msr_mon;
}

inline static msr_monitor_t *msr_list_lookup(msr_monitor_t *msr_list, uint32_t msr_id)
{
	msr_monitor_t *p_msr_monitor = msr_list;
	while (p_msr_monitor) {
		if (p_msr_monitor->msr_id == msr_id)
			break;
		p_msr_monitor = p_msr_monitor->next;
	}
	return p_msr_monitor;
}

static msr_monitor_guest_t *create_guest_msr_monitor(uint16_t guest_id)
{
	msr_monitor_guest_t *p_guest_msr_mon;
	p_guest_msr_mon = mem_alloc(sizeof(msr_monitor_guest_t));

	/* allocate zero-filled 4K-page to store MSR VMEXIT bitmap */
	p_guest_msr_mon->msr_bitmap = page_alloc(1);
	memset((void *)p_guest_msr_mon->msr_bitmap, 0, PAGE_4K_SIZE);
	p_guest_msr_mon->guest_id = guest_id;
	p_guest_msr_mon->msr_list = NULL;

	/* Update global list: g_msr_monitor always point to latest added member */
	p_guest_msr_mon->next = g_msr_monitor;
	g_msr_monitor = p_guest_msr_mon;

	return p_guest_msr_mon;
}

#ifdef DEBUG
static boolean_t msr_in_vmcs(uint32_t msr_id)
{
	switch (msr_id) {
	case MSR_DEBUGCTL:
	case MSR_SYSENTER_CS:
	case MSR_SYSENTER_ESP:
	case MSR_SYSENTER_EIP:
	case MSR_PAT:
	case MSR_EFER:
	case MSR_FS_BASE:
	case MSR_GS_BASE:
		return TRUE;
	default:
		return FALSE;
	}

}
#endif

/*
 * Currently this function is just used for the MSRs which are NOT isolated
 * between guest and host. so, the MSRs will be read from/write to HW directly.
 * In future, we may need to consult vmcs (like EFER, PAT) or msr isolation
 * module to set/get guest MSR
 */
static void register_msr_monitor(uint16_t guest_id, uint32_t msr_id, boolean_t block_only,
				msr_handler_t read_handler, msr_handler_t write_handler)
{
	msr_monitor_t *p_msr_mon;
	msr_monitor_guest_t *p_guest_msr_mon;
	uint64_t msr_offset = -1ULL;

#ifdef DEBUG
	/* Assert if no handler */
	VMM_ASSERT(read_handler || write_handler);

	/* TODO: msr_id should not be in msr_isolation list, there is no check
	 *       here. Expand msr monitor code if we want support it later. */

	/* Check if it is MSR which resides in Guest part of VMCS */
	VMM_ASSERT_EX(!msr_in_vmcs(msr_id),
			"Not supported: msr_id in Guest part of VMCS field!\n");
#endif

	/* Get MSR-Bitmap Address offset according to IA32 Manual,
	 * Volume 3, Chapter 24.6.9 "MSR-Bitmap Address" */
	if (msr_id <= MSR_LOW_LAST) {
		msr_offset = msr_id;
	} else if ((MSR_HIGH_FIRST <= msr_id) && (msr_id <= MSR_HIGH_LAST)) {
		msr_offset = msr_id - MSR_HIGH_FIRST + 1024 * 8;
	}

	/* Access a non-exists MSR will cause VM Exit according to IA32 Manual,
	 * Volume 3, Chapter 25.1.3 "Instructions That Cause VM Exits Conditionlly".
	 * So nothing to do to block no-exist MSRs. */
	if (block_only && (msr_offset == -1ULL))
		return;

	p_guest_msr_mon = msr_monitor_guest_lookup(guest_id);

	if (p_guest_msr_mon == NULL)
		p_guest_msr_mon = create_guest_msr_monitor(guest_id);

	if (msr_offset != -1ULL) {
		if (read_handler)
			BITARRAY_SET((uint64_t *)(p_guest_msr_mon->msr_bitmap), msr_offset);

		if (write_handler)
			BITARRAY_SET((uint64_t *)(p_guest_msr_mon->msr_bitmap + 2048), msr_offset);
	}

	if (block_only)
		return;

	p_msr_mon = msr_list_lookup(p_guest_msr_mon->msr_list, msr_id);

	if (!p_msr_mon) {
		p_msr_mon = mem_alloc(sizeof(msr_monitor_t));
		memset(p_msr_mon, 0, sizeof(msr_monitor_t));
		p_msr_mon->next = p_guest_msr_mon->msr_list;
		p_guest_msr_mon->msr_list = p_msr_mon;
		p_msr_mon->msr_id = msr_id;
	}

	if (read_handler) {
		/* For same guest, a MSR monitor should not be re-registered */
		D(VMM_ASSERT(!(p_msr_mon->msr_read_handler)));
		p_msr_mon->msr_read_handler = read_handler;
	}
	if (write_handler) {
		/* For same guest, a MSR monitor should not be re-registered */
		D(VMM_ASSERT(!(p_msr_mon->msr_write_handler)));
		p_msr_mon->msr_write_handler = write_handler;
	}
}

void block_msr_read(uint16_t guest_id, uint32_t msr_id)
{
	register_msr_monitor(guest_id, msr_id, TRUE, (msr_handler_t)1, NULL);
}

void block_msr_write(uint16_t guest_id, uint32_t msr_id)
{
	register_msr_monitor(guest_id, msr_id, TRUE, NULL, (msr_handler_t)1);
}

void block_msr_access(uint16_t guest_id, uint32_t msr_id)
{
	register_msr_monitor(guest_id, msr_id, TRUE, (msr_handler_t)1, (msr_handler_t)1);
}

void monitor_msr_read(uint16_t guest_id, uint32_t msr_id, msr_handler_t handler)
{
	register_msr_monitor(guest_id, msr_id, FALSE, handler, NULL);
}

void monitor_msr_write(uint16_t guest_id, uint32_t msr_id, msr_handler_t handler)
{
	register_msr_monitor(guest_id, msr_id, FALSE, NULL, handler);
}

void monitor_msr_access(uint16_t guest_id, uint32_t msr_id,
			msr_handler_t read_handler, msr_handler_t write_handler)
{
	register_msr_monitor(guest_id, msr_id, FALSE, read_handler, write_handler);
}

uint64_t get_val_for_wrmsr(guest_cpu_handle_t gcpu)
{
	uint64_t msr_value;

	D(VMM_ASSERT(gcpu));

	msr_value = MAKE64(gcpu_get_gp_reg(gcpu, REG_RDX),
				gcpu_get_gp_reg(gcpu, REG_RAX));
	return msr_value;
}

void set_val_for_rdmsr(guest_cpu_handle_t gcpu, uint64_t msr_value)
{
	D(VMM_ASSERT(gcpu));

	gcpu_set_gp_reg(gcpu, REG_RDX, msr_value >> 32);
	gcpu_set_gp_reg(gcpu, REG_RAX, msr_value & MASK64_LOW(32));
}

static void msr_monitor_common_handler(guest_cpu_handle_t gcpu, void *pv)
{
	event_msr_vmexit_t *msr_vmexit = (event_msr_vmexit_t *)pv;
	uint32_t msr_id = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RCX);
	msr_handler_t msr_handler = NULL;
	msr_monitor_t *p_msr_mon;
	msr_monitor_guest_t *p_guest_msr_mon;

	p_guest_msr_mon = msr_monitor_guest_lookup(gcpu->guest->id);

	/* handled=FALSE, #GP will be injected */
	if (!p_guest_msr_mon)
		return;

	p_msr_mon = msr_list_lookup(p_guest_msr_mon->msr_list, msr_id);

	/* handled=FALSE, #GP will be injected */
	if (!p_msr_mon)
		return;

	/* is_write: TRUE for write, FALSE for read */
	if (msr_vmexit->is_write)
		msr_handler = p_msr_mon->msr_write_handler;
	else
		msr_handler = p_msr_mon->msr_read_handler;

	/* handled=FALSE, #GP will be injected */
	if (!msr_handler)
		return;

	msr_handler(gcpu, msr_id);
	msr_vmexit->handled = TRUE;
}

static void msr_misc_enable_write_handler(guest_cpu_handle_t gcpu,
					UNUSED uint32_t msr_id)
{
	uint64_t msr_value;
	D(VMM_ASSERT(MSR_MISC_ENABLE == msr_id));

	msr_value = get_val_for_wrmsr(gcpu);

	/* Limit CPUID MAXVAL */
	msr_value &= ~(1ULL << 22);

	asm_wrmsr(MSR_MISC_ENABLE, msr_value);
	gcpu_skip_instruction(gcpu);
}

#ifdef DEBUG
static void msr_mtrr_write_handler(guest_cpu_handle_t gcpu, uint32_t msr_id)
{
	uint64_t msr_value;
	/* MSR_MTRRCAP is read only mtrr */
	VMM_ASSERT(msr_id != MSR_MTRRCAP);
	msr_value = get_val_for_wrmsr(gcpu);

	print_warn("VMM: %s:MSR_ID=0x%x, CUR_VAL=0x%08llx --> TO_VAL=0x%08llx\n",
			__func__, msr_id, asm_rdmsr(msr_id), msr_value);
	asm_wrmsr(msr_id, msr_value);
	gcpu_skip_instruction(gcpu);
}

static void msr_mtrrs_monitor(uint16_t guest_id)
{
	uint8_t i;
	uint32_t msr_addr;

	monitor_msr_write(guest_id, MSR_MTRRCAP, msr_mtrr_write_handler);
	monitor_msr_write(guest_id, MSR_MTRR_DEF_TYPE, msr_mtrr_write_handler);

	/* Fixed range MTRRs */
	monitor_msr_write(guest_id, MSR_MTRR_FIX64K_00000, msr_mtrr_write_handler);

	msr_addr = MSR_MTRR_FIX16K_80000;
	for (i=0; i<2; i++)
		monitor_msr_write(guest_id, msr_addr+i, msr_mtrr_write_handler);

	msr_addr = MSR_MTRR_FIX4K_C0000;
	for (i=0; i<8; i++)
		monitor_msr_write(guest_id, msr_addr+i, msr_mtrr_write_handler);

	/* Variable range MTRRs */
	for (msr_addr = MSR_MTRR_PHYSBASE0, i = 0;
		i < MTRRCAP_VCNT(asm_rdmsr(MSR_MTRRCAP));
		msr_addr += 2, i++) {

		/* Register all MTRR PHYSBASE */
		monitor_msr_write(guest_id, msr_addr, msr_mtrr_write_handler);

		/* Register all MTRR PHYSMASK */
		monitor_msr_write(guest_id, msr_addr + 1, msr_mtrr_write_handler);
	}
}
#endif

static void guest_msr_monitor_setup(UNUSED guest_cpu_handle_t gcpu, void *pv)
{
	guest_handle_t guest = (guest_handle_t)pv;
	uint32_t msr_id;

	D(VMM_ASSERT(guest));

	print_trace("[MSR] Setup for Guests[%d]\n", guest->id);

	/* Block access to VMX related MSRs */
	for (msr_id = MSR_VMX_FIRST; msr_id <= MSR_VMX_LAST; ++msr_id)
		block_msr_access(guest->id, msr_id);

	/* IA32 spec Volume2, Chapter6.3, GETSEC[SENTER]
	 * IA32_FEATURE_CONTROL is only available on SMX or VMX enabled
	 * processors. Otherwise, it is treated as reserved. */
	block_msr_access(guest->id, MSR_FEATURE_CONTROL);
	monitor_msr_write(guest->id, MSR_MISC_ENABLE, msr_misc_enable_write_handler);

#ifdef DEBUG
	msr_mtrrs_monitor(guest->id);
#endif
}

static void msr_monitor_gcpu_init(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	uint64_t msr_bitmap;
	vmcs_obj_t p_vmcs = gcpu->vmcs;
	msr_monitor_guest_t *p_guest_msr_mon;

	D(VMM_ASSERT(gcpu));

	print_trace("[MSR] Activated on GCPU[%d]\n", gcpu->id);

	p_guest_msr_mon = msr_monitor_guest_lookup(gcpu->guest->id);

	/* If p_guest_msr_mon is NULL, it means no MSR monitored during BOOT TIME.
	 * In future, if MSR monitor is required in RUNTIME, code here must be expanded.*/
	if(!p_guest_msr_mon)
		return;
	D(VMM_ASSERT(p_guest_msr_mon->msr_bitmap));

	msr_bitmap = (uint64_t)p_guest_msr_mon->msr_bitmap;
	hmm_hva_to_hpa(msr_bitmap, &msr_bitmap, NULL);
	vmcs_write(p_vmcs, VMCS_MSR_BITMAP, msr_bitmap);
}

void msr_monitor_init(void)
{
	event_register(EVENT_GUEST_MODULE_INIT, guest_msr_monitor_setup);
	event_register(EVENT_GCPU_MODULE_INIT, msr_monitor_gcpu_init);
	event_register(EVENT_MSR_ACCESS, msr_monitor_common_handler);
}
