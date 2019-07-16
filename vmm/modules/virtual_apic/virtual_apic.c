/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "gcpu.h"
#include "guest.h"
#include "vmx_cap.h"
#include "vmm_base.h"
#include "vmm_asm.h"
#include "event.h"
#include "heap.h"
#include "gpm.h"
#include "apic_regs.h"
#include "modules/msr_monitor.h"
#include "modules/vmcall.h"
#include "modules/virtual_apic.h"
#include "modules/instr_decode.h"
#include "lib/util.h"

#ifdef MODULE_INTERRUPT_IPI
#error "MODULE_INTERRUPT_IPI has conflict with MODULE_VIRTUAL_APIC"
#endif

#ifndef MODULE_EXT_INTR
#error "MODULE_EXT_INTR should be used with MODULE_VIRTUAL_APIC"
#endif

#define MSR_X2APIC_BASE 0x800
#define LAPIC_ENABLED  (1ULL << 11)
#define LAPIC_X2_ENABLED  (1ULL << 10)

/* access type for VMExit of virtual apic page access */
#define LN_ACCESS_DATA_READ 	 0x0
#define LN_ACCESS_DATA_WRITE	 0x1
#define LN_ACCESS_INSTR_FETCH	 0x2
#define LN_ACCESS_EVENT_DELIVERY 0x3
#define GP_ACCESS_EVENT_DELIVERY 0xa
#define GP_ACCESS_INSTR_FETCH	 0xf

#define MODULE_POST_INTERRUPT
/* 0xf2 is the vector saved for KVM to deliver posted interrupt IPI.
 * which is also used in eVMM.
 */
#define POST_NOTIFY_VECTOR	0xf2

typedef union {
	struct {
		uint16_t rvi:8; // low byte: the vector with the highest priority that is requesting service.
		uint16_t svi:8; // high byte: the vector with the highest priority that is in service.
	}bits;
	uint16_t uint16;
} g_intr_status_t;

static uint8_t get_highest_pending_intr(irr_t *p_irr)
{
	int seg, off; //if unsgined, seg>=0 is always true, can't cover intr 0x00~0x20.

	VMM_ASSERT(p_irr != NULL);

	for (seg=(APIC_IRR_NR-1); seg>=0; seg--) {
		if (p_irr->intr[seg] != 0) {
			off = (int)asm_bsr32(p_irr->intr[seg]);
			return (uint8_t)(seg*32 + off);
		}
	}

	return 0;
}

void vapic_set_pending_intr(guest_cpu_handle_t gcpu, uint8_t vector)
{
	g_intr_status_t g_intr_status;
	uint64_t vapic_page = vmcs_read(gcpu->vmcs, VMCS_VIRTUAL_APIC_ADDR);
	uint32_t seg, off;

	/* set vIRR */
	seg = (vector/32) << 4;
	off = (vector%32);
	asm_bts64((uint64_t*)(vapic_page + APIC_IRR + seg), off);

	/* update RVI conditionally */
	g_intr_status.uint16 = (uint16_t)vmcs_read(gcpu->vmcs, VMCS_GUEST_INTERRUPT_STATUS);
	if (g_intr_status.bits.rvi < vector) {
		g_intr_status.bits.rvi = vector;
		vmcs_write(gcpu->vmcs, VMCS_GUEST_INTERRUPT_STATUS, (uint64_t)g_intr_status.uint16);
	}
}

uint8_t vapic_get_pending_intr(guest_cpu_handle_t gcpu)
{
	irr_t virr;

	vapic_get_virr(gcpu, &virr);

	return get_highest_pending_intr(&virr);
}

void vapic_clear_pending_intr(guest_cpu_handle_t gcpu, uint8_t vector)
{
	g_intr_status_t g_intr_status;
	uint64_t vapic_page = vmcs_read(gcpu->vmcs, VMCS_VIRTUAL_APIC_ADDR);
	uint32_t seg, off;

	/* clear vIRR */
	seg = (vector/32) << 4;
	off = (vector%32);
	asm_btr64((uint64_t*)(vapic_page + APIC_IRR + seg), off);

	/* update RVI conditionally */
	g_intr_status.uint16 = (uint16_t)vmcs_read(gcpu->vmcs, VMCS_GUEST_INTERRUPT_STATUS);
	if (g_intr_status.bits.rvi <= vector) {
		g_intr_status.bits.rvi = vapic_get_pending_intr(gcpu);
		vmcs_write(gcpu->vmcs, VMCS_GUEST_INTERRUPT_STATUS, (uint64_t)(g_intr_status.uint16));
	}
}

void vapic_get_virr(guest_cpu_handle_t gcpu, irr_t * p_virr)
{
	uint64_t vapic_page;
	uint64_t addr;
	uint8_t i;

	VMM_ASSERT(p_virr != NULL);

	vapic_page = vmcs_read(gcpu->vmcs, VMCS_VIRTUAL_APIC_ADDR);
	addr = (uint64_t)(vapic_page + APIC_IRR);

	for (i=0; i<APIC_IRR_NR; i++) {
		p_virr->intr[i] = *(uint32_t *)(addr);
		addr += 0x0010;
	}
}

void vapic_merge_virr(guest_cpu_handle_t gcpu, irr_t *p_virr)
{
	g_intr_status_t g_intr_status;
	uint64_t vapic_page;
	uint64_t addr;
	uint8_t i;
	uint8_t vector;

	VMM_ASSERT(p_virr != NULL);

	/* merge vIRR unconditionally */
	vapic_page = vmcs_read(gcpu->vmcs, VMCS_VIRTUAL_APIC_ADDR);
	addr = (uint64_t)(vapic_page + APIC_IRR);

	for (i=0; i<APIC_IRR_NR; i++) {
		*(uint32_t *)(addr) |= p_virr->intr[i];
		addr += 0x0010;
	}

	/* merge RVI conditionally */
	vector = get_highest_pending_intr(p_virr);
	g_intr_status.uint16 = (uint16_t)vmcs_read(gcpu->vmcs, VMCS_GUEST_INTERRUPT_STATUS);
	if (g_intr_status.bits.rvi < vector) {
		g_intr_status.bits.rvi = vector;
		vmcs_write(gcpu->vmcs, VMCS_GUEST_INTERRUPT_STATUS, (uint64_t)(g_intr_status.uint16));
	}
}

void vapic_clear_virr(guest_cpu_handle_t gcpu)
{
	uint64_t vapic_page;
	uint16_t int_status;

	/* clear RVI  */
	int_status = (uint16_t)vmcs_read(gcpu->vmcs, VMCS_GUEST_INTERRUPT_STATUS);
	vmcs_write(gcpu->vmcs, VMCS_GUEST_INTERRUPT_STATUS, (uint64_t)(int_status & 0xFF00));

	/* clear vIRR */
	vapic_page = vmcs_read(gcpu->vmcs, VMCS_VIRTUAL_APIC_ADDR);
	memset((uint8_t *)(vapic_page + APIC_IRR), 0, APIC_IRR_NR * 0x10);
}

static inline void vapic_set_reg(uint64_t apic_page, uint32_t offset, uint32_t val)
{
	*((volatile uint32_t *)(apic_page + offset)) = val;
}

static inline void lapic_set_reg(uint32_t offset, uint32_t val)
{
	uint64_t apic_base;
	apic_base = asm_rdmsr(MSR_APIC_BASE);

	if (!(apic_base & LAPIC_ENABLED))
		return;

	if (apic_base & LAPIC_X2_ENABLED) {
		/* x2APIC */
		asm_wrmsr(MSR_X2APIC_BASE + (offset >> 4), val);
	}else {
		/* xAPIC */
		*((volatile uint32_t *)(uint64_t)((apic_base & (~PAGE_4K_MASK)) + offset)) = val;
	}
}

static void setup_virtual_apic_page(guest_cpu_handle_t gcpu)
{
	uint64_t vapic_page_hpa;
	uint64_t *vapic_page = page_alloc(1);

	VMM_ASSERT_EX(hmm_hva_to_hpa((uint64_t)vapic_page, &vapic_page_hpa, NULL),
		"fail to convert hva %p to hpa\n", vapic_page);

	vmcs_write(gcpu->vmcs, VMCS_VIRTUAL_APIC_ADDR, vapic_page_hpa);
	memset(vapic_page, 0, PAGE_4K_SIZE);
	memcpy(vapic_page, (void *)0xFEE00000, PAGE_4K_SIZE);

	asm_wbinvd();
}

#ifdef MODULE_POST_INTERRUPT
static void setup_post_interrupt(guest_cpu_handle_t gcpu)
{
	uint64_t post_interrupt_desc_hpa;
	uint64_t *post_interrupt_desc = page_alloc(1);

	VMM_ASSERT_EX(hmm_hva_to_hpa((uint64_t)post_interrupt_desc, &post_interrupt_desc_hpa, NULL),
		"fail to convert hva %p to hpa\n", post_interrupt_desc);

	memset(post_interrupt_desc, 0, PAGE_4K_SIZE);
	vmcs_write(gcpu->vmcs, VMCS_POST_INTR_DESC_ADDR, post_interrupt_desc_hpa);
	vmcs_write(gcpu->vmcs, VMCS_POST_INTR_NOTI_VECTOR, POST_NOTIFY_VECTOR);
}
#endif

static void virtual_apic_gcpu_init(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	uint32_t proc_ctrl, proc2_ctrl;
	uint32_t pin_ctrl;
	uint32_t exit_ctrl;
	uint64_t apic_base;

	proc_ctrl = vmcs_read(gcpu->vmcs, VMCS_PROC_CTRL1);
	proc_ctrl |= (PROC_TPR_SHADOW);
	proc_ctrl |= (PROC_SECONDARY_CTRL);
	vmcs_write(gcpu->vmcs, VMCS_PROC_CTRL1, proc_ctrl);

	proc2_ctrl = vmcs_read(gcpu->vmcs, VMCS_PROC_CTRL2);
	proc2_ctrl |= (PROC2_VAPIC_ACCESSES | PROC2_APIC_REG_VIRTUALIZE | PROC2_VINT_DELIVERY);
	vmcs_write(gcpu->vmcs, VMCS_PROC_CTRL2, proc2_ctrl);

	exit_ctrl = EXIT_ACK_INT_EXIT | vmcs_read(gcpu->vmcs, VMCS_EXIT_CTRL);
	vmcs_write(gcpu->vmcs, VMCS_EXIT_CTRL, exit_ctrl);

	pin_ctrl = (~PIN_PROC_POSTED_INT) & vmcs_read(gcpu->vmcs, VMCS_PIN_CTRL);
	vmcs_write(gcpu->vmcs, VMCS_PIN_CTRL, pin_ctrl);

#ifdef MODULE_POST_INTERRUPT
	pin_ctrl = PIN_PROC_POSTED_INT | vmcs_read(gcpu->vmcs, VMCS_PIN_CTRL);
	vmcs_write(gcpu->vmcs, VMCS_PIN_CTRL, pin_ctrl);
	setup_post_interrupt(gcpu);
	print_trace("Guest%d, GCPU%d Post-int notify vector 0x%x, PIR=0x%llx\n",
                                    gcpu->guest->id, gcpu->id,
                                    vmcs_read(gcpu->vmcs, VMCS_POST_INTR_NOTI_VECTOR),
                                    vmcs_read(gcpu->vmcs, VMCS_POST_INTR_DESC_ADDR));
#endif

	setup_virtual_apic_page(gcpu);

	apic_base = asm_rdmsr(MSR_APIC_BASE);

	vmcs_write(gcpu->vmcs, VMCS_TPR_THRESHOLD, 0);
	vmcs_write(gcpu->vmcs, VMCS_APIC_ACCESS_ADDR, apic_base & (~PAGE_4K_MASK));

	print_trace("Guest%d, GCPU%d vapic inited vpage=%x abase=%llx\n",
                                    gcpu->guest->id, gcpu->id,
                                    vmcs_read(gcpu->vmcs, VMCS_VIRTUAL_APIC_ADDR),
                                    vmcs_read(gcpu->vmcs, VMCS_APIC_ACCESS_ADDR));
}

static void virtual_apic_write(guest_cpu_handle_t gcpu)
{
	vmx_exit_qualification_t qual;
	uint32_t offset;
	uint32_t val;
	uint32_t val_h = 0;
	uint64_t vapic_page = vmcs_read(gcpu->vmcs, VMCS_VIRTUAL_APIC_ADDR);

	qual.uint64 = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);
	offset = qual.uint64 & 0xfff;
	val = *((uint32_t *)(vapic_page+offset));

	/* write ICR_H has no vmexit, so we need to fill in it manually */
	if (offset == APIC_ICR_L) {
		val_h = *((uint32_t *)(vapic_page + APIC_ICR_H));
		lapic_set_reg(APIC_ICR_H, val_h);
	}

	lapic_set_reg(offset, val);
}

static void set_pending_intr_to_gcpu(guest_cpu_handle_t gcpu, void *pv)
{
	uint8_t vector;
	boolean_t *handled = (boolean_t *)pv;

	vmcs_write(gcpu->vmcs, VMCS_PROC_CTRL1, vmcs_read(gcpu->vmcs, VMCS_PROC_CTRL1) & ~(PROC_INT_WINDOW_EXIT));

	for(vector = gcpu_get_pending_intr(gcpu); vector >= 0x20; vector = gcpu_get_pending_intr(gcpu)) {
		/* set vIRR and update RVI */
		vapic_set_pending_intr(gcpu, vector);
		/* clear interrupt bufferred */
		gcpu_clear_pending_intr(gcpu, vector);
		/* perform EOI to local APIC */
		lapic_set_reg(APIC_EOI, APIC_EOI_ACK);
	}

	*handled = TRUE;
}

static void virtual_apic_access(guest_cpu_handle_t gcpu)
{
	vmx_exit_qualification_t qual;
	uint64_t hva;
	pf_ec_t pfec;
   	pf_info_t pfinfo;
	uint8_t instr[17] = {0};

	uint64_t val;
	uint32_t reg_id;
	uint32_t op_size;

	uint64_t vapic_page = vmcs_read(gcpu->vmcs, VMCS_VIRTUAL_APIC_ADDR);
	uint64_t g_rip = vmcs_read(gcpu->vmcs, VMCS_GUEST_RIP);

	if (FALSE == gcpu_gva_to_hva(gcpu, g_rip, GUEST_CAN_READ, &hva, &pfec)) {
		print_panic("vapic access: fail to convert gva to hva!\n");
		return;
	}

	if (FALSE == gcpu_copy_from_gva(gcpu, g_rip, (uint64_t)instr, 17, &pfinfo)) {
		print_panic("vapic access: fail to copy instruction from gva!\n");
		return;
	}

	qual.uint64 = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);

	switch (qual.apic_access.access_type) {
	case LN_ACCESS_DATA_READ:
		if (TRUE == decode_mov_from_mem(gcpu, &reg_id, &op_size)) {
			val = *((volatile uint64_t *)(vapic_page + qual.apic_access.offset));
			gcpu_set_gp_reg(gcpu, reg_id, val);
			gcpu_skip_instruction(gcpu);
		}
		break;

	case LN_ACCESS_DATA_WRITE:
		if (TRUE == decode_mov_to_mem(gcpu, &val, &op_size)) {
			vapic_set_reg(vapic_page, qual.apic_access.offset, val);
			lapic_set_reg(qual.apic_access.offset, val);
			gcpu_skip_instruction(gcpu);
		}
		break;

	case LN_ACCESS_INSTR_FETCH:
	case LN_ACCESS_EVENT_DELIVERY:
	case GP_ACCESS_EVENT_DELIVERY:
	case GP_ACCESS_INSTR_FETCH:
		print_info("unsupport qual %llx\n", qual.uint64);
		D(VMM_ASSERT(0));
		break;
	default:
		print_info("wrong qual %11x\n", qual.uint64);
		D(VMM_ASSERT(0));
		break;
	}
}

static void msr_apic_base_write_handler(guest_cpu_handle_t gcpu, uint32_t msr_id)
{
	uint64_t msr_value = get_val_for_wrmsr(gcpu);

	vmcs_write(gcpu->vmcs, VMCS_APIC_ACCESS_ADDR, msr_value & (~PAGE_4K_MASK));

	asm_wrmsr(msr_id, msr_value);

	gcpu_skip_instruction(gcpu);
}

void virtual_apic_init(void)
{
	event_register(EVENT_GCPU_MODULE_INIT, virtual_apic_gcpu_init);

	monitor_msr_write(0, MSR_APIC_BASE, msr_apic_base_write_handler);
	monitor_msr_write(1, MSR_APIC_BASE, msr_apic_base_write_handler);

	vmexit_install_handler(virtual_apic_write, REASON_56_APIC_WRITE);
	vmexit_install_handler(virtual_apic_access, REASON_44_APIC_ACCESS);

	event_register(EVENT_INJECT_INTR, set_pending_intr_to_gcpu);
}
