/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "guest.h"
#include "gcpu.h"
#include "gcpu_state.h"
#include "vmx_cap.h"
#include "vmx_asm.h"
#include "stack.h"
#include "vmm_arch.h"
#include "hmm.h"
#include "vmexit_cr_access.h"
#include "event.h"

#include "lib/util.h"

/*vpid value*/
static volatile uint32_t vpid_value = 0;

void gcpu_set_host_state(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs;

	gdtr64_t gdtr;
	idtr64_t idtr;
	uint64_t gcpu_stack;
	uint16_t cpu;

	D(VMM_ASSERT(gcpu));

	cpu = host_cpu_id();

	vmcs = gcpu->vmcs;

	/*
	 *  Control Registers
	 */
	vmcs_write(vmcs, VMCS_HOST_CR0, asm_get_cr0());
	vmcs_write(vmcs, VMCS_HOST_CR3, asm_get_cr3());
	vmcs_write(vmcs, VMCS_HOST_CR4, asm_get_cr4());

	/*
	 *  EIP, ESP
	 */
	vmcs_write(vmcs, VMCS_HOST_RIP, (uint64_t)vmexit_func);
	gcpu_stack = stack_get_cpu_sp(cpu);
	vmcs_write(vmcs, VMCS_HOST_RSP, gcpu_stack);

	/*
	 *  Base-address fields for FS, GS, TR, GDTR, and IDTR (64 bits each).
	 */
	asm_sgdt(&gdtr);
	vmcs_write(vmcs, VMCS_HOST_GDTR_BASE, gdtr.base);

	asm_sidt(&idtr);
	vmcs_write(vmcs, VMCS_HOST_IDTR_BASE, idtr.base);

	/*
	 *  FS (Selector + Base)
	 */
	D(VMM_ASSERT(asm_get_fs() == 0));
	vmcs_write(vmcs, VMCS_HOST_FS_SEL, 0);
#ifdef STACK_PROTECTOR
	/* used for stack protector */
	vmcs_write(vmcs, VMCS_HOST_FS_BASE, asm_rdmsr(MSR_FS_BASE));
#else
	vmcs_write(vmcs, VMCS_HOST_FS_BASE, 0);
#endif

	/*
	 *  GS (Selector + Base)
	 */
	D(VMM_ASSERT(asm_get_gs() == 0));
	vmcs_write(vmcs, VMCS_HOST_GS_SEL, 0);
	vmcs_write(vmcs, VMCS_HOST_GS_BASE, 0);

	/*
	 *  TR (Selector + Base)
	 */
	vmcs_write(vmcs, VMCS_HOST_TR_SEL, asm_str());
	vmcs_write(vmcs, VMCS_HOST_TR_BASE, get_tss_base(cpu));

	/*
	 *  Selector fields (16 bits each) for the segment registers CS, SS, DS,
	 *  ES, FS, GS, and TR
	 */
	vmcs_write(vmcs, VMCS_HOST_CS_SEL, asm_get_cs());
	vmcs_write(vmcs, VMCS_HOST_SS_SEL, asm_get_ss());
	vmcs_write(vmcs, VMCS_HOST_DS_SEL, asm_get_ds());
	vmcs_write(vmcs, VMCS_HOST_ES_SEL, asm_get_es());

	/*
	 *  MSRS
	 */

	//EXIT_LOAD_IA32_EFER support is checked in vmx_cap_init()
	vmcs_write(vmcs, VMCS_HOST_EFER,asm_rdmsr(MSR_EFER));
	//EXIT_LOAD_IA32_PAT support is checked in vmx_cap_init()
	vmcs_write(vmcs, VMCS_HOST_PAT, asm_rdmsr(MSR_PAT));
}

void gcpu_set_ctrl_state(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs;
	uint32_t ctrl;
	D(VMM_ASSERT(gcpu));

	vmcs = gcpu->vmcs;
	ctrl = get_init_pin();
	vmcs_write(vmcs, VMCS_PIN_CTRL, ctrl);

	ctrl = get_init_proc1();
	vmcs_write(vmcs, VMCS_PROC_CTRL1, ctrl);

	//PROC_SECONDARY_CTRL is checked in vmx_cap_init()
	ctrl = get_init_proc2();
	vmcs_write(vmcs, VMCS_PROC_CTRL2, ctrl);

	ctrl = get_init_entry();
	vmcs_write(vmcs, VMCS_ENTRY_CTRL, ctrl);

	ctrl = get_init_exit();
	vmcs_write(vmcs, VMCS_EXIT_CTRL, ctrl);

	if(get_proctl2_cap(NULL) & PROC2_ENABLE_XSAVE) {
		vmcs_write(vmcs, VMCS_XSS_EXIT_BITMAP, 0);
	}

	if (get_proctl2_cap(NULL) & PROC2_ENABLEC_VPID)
	{
		// The values of VMCS_VPID start from 1
		vmcs_write(vmcs, VMCS_VPID, lock_inc32(&vpid_value));
	}

	/* Set MSR Bitmap: default bitmap is all 0 filled which means no MSR monitored */
	vmcs_write(vmcs, VMCS_MSR_BITMAP, stack_get_zero_page());

	vmcs_write(vmcs, VMCS_LINK_PTR, (uint64_t)-1);
}

/*
 * Setup guest CPU initial state
 *
 * Should be called only if initial GCPU state is not Wait-For-Sipi
 */
void gcpu_set_init_state(guest_cpu_handle_t gcpu, const gcpu_state_t *initial_state)
{
	uint32_t idx;
	vmcs_obj_t vmcs;
#ifdef DEBUG
	uint64_t efer;
#endif

	D(VMM_ASSERT(gcpu));
	D(VMM_ASSERT(initial_state));

	/* init segment registers */
	for (idx = SEG_CS; idx < SEG_COUNT; ++idx) {
		gcpu_set_seg(gcpu, (seg_id_t)idx,
			initial_state->segment[idx].selector,
			initial_state->segment[idx].base,
			initial_state->segment[idx].limit,
			initial_state->segment[idx].attributes);
	}

	/* init gp registers */
	for (idx = REG_RAX; idx < REG_GP_COUNT; ++idx)
		gcpu_set_gp_reg(gcpu, (gp_reg_t)idx,
			initial_state->gp_reg[idx]);

	vmcs = gcpu->vmcs;

	vmcs_write(vmcs, VMCS_GUEST_DR7, 0x00000400);

	/* init RSP and RFLAGS */
	vmcs_write(vmcs, VMCS_GUEST_RIP, initial_state->rip);
	vmcs_write(vmcs, VMCS_GUEST_RFLAGS, initial_state->rflags);

	vmcs_write(vmcs, VMCS_GUEST_CR0, initial_state->cr0);
	vmcs_write(vmcs, VMCS_GUEST_CR3, initial_state->cr3);
	vmcs_write(vmcs, VMCS_GUEST_CR4, initial_state->cr4);

	vmcs_write(vmcs, VMCS_GUEST_GDTR_BASE, initial_state->gdtr.base);
	vmcs_write(vmcs, VMCS_GUEST_GDTR_LIMIT, initial_state->gdtr.limit);

	vmcs_write(vmcs, VMCS_GUEST_IDTR_BASE, initial_state->idtr.base);
	vmcs_write(vmcs, VMCS_GUEST_IDTR_LIMIT, initial_state->idtr.limit);

	/* init selected model-specific registers */
	vmcs_write(vmcs, VMCS_GUEST_EFER, initial_state->msr_efer);
	vmcs_write(vmcs, VMCS_GUEST_SYSENTER_CS, 0);
	vmcs_write(vmcs, VMCS_GUEST_SYSENTER_ESP, 0);
	vmcs_write(vmcs, VMCS_GUEST_SYSENTER_EIP, 0);
	vmcs_write(vmcs, VMCS_GUEST_PAT, asm_rdmsr(MSR_PAT));

	/* set cached value to the same in order not to trigger events */
	vmcs_write(vmcs, VMCS_GUEST_ACTIVITY_STATE,
			ACTIVITY_STATE_ACTIVE);

	/* set state in vmenter control fields */
	cr0_guest_write(gcpu, initial_state->cr0);
	cr4_guest_write(gcpu, initial_state->cr4);
	gcpu_update_guest_mode(gcpu);

#ifdef DEBUG
	efer = vmcs_read(vmcs, VMCS_GUEST_EFER);
	if (efer != initial_state->msr_efer)
	{
		print_warn("%s:guest efer value(0x%x) is different with init efer value(0x%x).\n", __FUNCTION__, efer, initial_state->msr_efer);
	}
#endif
}

/*
 * Set Guest CPU states following Power-up, Reset, or INIT
 * Refer to IA32 Manual 9.1.2 Processor Built-In Self-Test
 */
void gcpu_set_reset_state(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs;

	D(VMM_ASSERT(gcpu));
	vmcs = gcpu->vmcs;
	/*------------------ Set Segment Registers ------------------*/
	/* Attribute set bits: Present, R/W, Accessed */
	gcpu_set_seg(gcpu, SEG_CS,
				0xF000, 0xFFFF0000, 0xFFFF, 0x9B);
	gcpu_set_seg(gcpu, SEG_DS, 0, 0, 0xFFFF, 0x93);
	gcpu_set_seg(gcpu, SEG_ES, 0, 0, 0xFFFF, 0x93);
	gcpu_set_seg(gcpu, SEG_FS, 0, 0, 0xFFFF, 0x93);
	gcpu_set_seg(gcpu, SEG_GS, 0, 0, 0xFFFF, 0x93);
	gcpu_set_seg(gcpu, SEG_SS, 0, 0, 0xFFFF, 0x93);

	/* TR/LDTR Attribute set bits: Present, R/W */
	gcpu_set_seg(gcpu, SEG_TR, 0, 0, 0xFFFF, 0x8B);
	gcpu_set_seg(gcpu, SEG_LDTR, 0, 0, 0xFFFF, 0x82);

	/*------------------ Set General Purpose Registers ------------------*/
	gcpu_set_gp_reg(gcpu, REG_RAX, 0);
	gcpu_set_gp_reg(gcpu, REG_RBX, 0);
	gcpu_set_gp_reg(gcpu, REG_RCX, 0);
	gcpu_set_gp_reg(gcpu, REG_RDX, 0xF00);
	gcpu_set_gp_reg(gcpu, REG_RDI, 0);
	gcpu_set_gp_reg(gcpu, REG_RSI, 0);
	gcpu_set_gp_reg(gcpu, REG_RBP, 0);
	gcpu_set_gp_reg(gcpu, REG_RSP, 0);

	vmcs_write(vmcs, VMCS_GUEST_DR7, 0x00000400);

	/* init RSP and RFLAGS */
	vmcs_write(vmcs, VMCS_GUEST_RIP, 0xFFF0);
	vmcs_write(vmcs, VMCS_GUEST_RFLAGS, 2);

	/*------------------ Set Control Registers ------------------*/
	vmcs_write(vmcs, VMCS_GUEST_CR0, 0x60000010);
	vmcs_write(vmcs, VMCS_GUEST_CR3, 0);
	vmcs_write(vmcs, VMCS_GUEST_CR4, 0);

	/*------------------ Set Memory Mgmt Registers ------------------*/
	vmcs_write(vmcs, VMCS_GUEST_GDTR_BASE, 0xFFFF);
	vmcs_write(vmcs, VMCS_GUEST_GDTR_LIMIT, 0xFFFF);

	vmcs_write(vmcs, VMCS_GUEST_IDTR_BASE, 0xFFFF);
	vmcs_write(vmcs, VMCS_GUEST_IDTR_LIMIT, 0xFFFF);

	/*
	 * According to IA32 Manual, MSR state are left unchanged during a INIT
	 */
	vmcs_write(vmcs, VMCS_GUEST_EFER, 0);
	vmcs_write(vmcs, VMCS_GUEST_SYSENTER_CS, 0);
	vmcs_write(vmcs, VMCS_GUEST_SYSENTER_ESP, 0);
	vmcs_write(vmcs, VMCS_GUEST_SYSENTER_EIP, 0);
	vmcs_write(vmcs, VMCS_GUEST_PAT, asm_rdmsr(MSR_PAT));

	/* Put guest CPU into the WAIT-FOR-SIPI state */
	/*  wait-for-SIPI support is checked in vmx_cap_init() */
	vmcs_write(vmcs, VMCS_GUEST_ACTIVITY_STATE,
			ACTIVITY_STATE_WAIT_FOR_SIPI);

	cr0_guest_write(gcpu, 0x60000010);
	cr4_guest_write(gcpu, 0);
	gcpu_update_guest_mode(gcpu);
}

/* The g0gcpu0_state is only used before guest0(Android) boot up */
static const gcpu_state_t *g0gcpu0_state;

static void g0gcpu_set_guest_state(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	D(VMM_ASSERT(gcpu));
	VMM_ASSERT_EX(g0gcpu0_state, "g0gcpu0_state is NULL\n");

	if (gcpu->guest->id == 0) {
		if (gcpu->id == 0)
			gcpu_set_init_state(gcpu, g0gcpu0_state);
		else
			gcpu_set_reset_state(gcpu);
	}
}

void prepare_g0gcpu_init_state(const gcpu_state_t *gcpu_state)
{
	VMM_ASSERT_EX(gcpu_state, "gcpu_state is NULL\n");
	g0gcpu0_state = gcpu_state;

	event_register(EVENT_GCPU_INIT, g0gcpu_set_guest_state);
}
