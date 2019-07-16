/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "gcpu.h"
#include "vmcs.h"
#include "gpm.h"
#include "nested_vt_internal.h"

static void copy_from_gvmcs(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field)
{
	vmcs_write(gcpu->vmcs, vmcs_field, gvmcs[vmcs_field]);
}

static void handle_io_bitmap_a(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	/* TODO: merge with hvmcs */
	vmcs_write(gcpu->vmcs, VMCS_IO_BITMAP_A, gvmcs[VMCS_IO_BITMAP_A]);
}

static void handle_io_bitmap_b(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	/* TODO: merge with hvmcs */
	vmcs_write(gcpu->vmcs, VMCS_IO_BITMAP_B, gvmcs[VMCS_IO_BITMAP_B]);
}

static void handle_msr_bitmap(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	/* TODO: merge with hvmcs */
	vmcs_write(gcpu->vmcs, VMCS_MSR_BITMAP, gvmcs[VMCS_MSR_BITMAP]);
}

static void handle_tsc_offset(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_TSC_OFFSET, gvmcs[VMCS_TSC_OFFSET] + vmcs_read(gcpu->vmcs, VMCS_TSC_OFFSET));
}

static void handle_virtual_apic_addr(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	uint64_t hpa;

	VMM_ASSERT(gpm_gpa_to_hpa(gcpu->guest, gvmcs[VMCS_VIRTUAL_APIC_ADDR], &hpa, NULL));

	vmcs_write(gcpu->vmcs, VMCS_VIRTUAL_APIC_ADDR, hpa);
}

static void handle_post_intr_desc_addr(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	uint64_t hpa;

	VMM_ASSERT(gpm_gpa_to_hpa(gcpu->guest, gvmcs[VMCS_POST_INTR_DESC_ADDR], &hpa, NULL));

	vmcs_write(gcpu->vmcs, VMCS_POST_INTR_DESC_ADDR, hpa);
}

static void handle_eptp_addr(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	uint64_t hpa;

	VMM_ASSERT(gpm_gpa_to_hpa(gcpu->guest, gvmcs[VMCS_EPTP_ADDRESS], &hpa, NULL));

	/* TODO: merge with hvmcs */
	vmcs_write(gcpu->vmcs, VMCS_EPTP_ADDRESS, hpa);
}

static void handle_pin_ctrl(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	/* TODO: merge with hvmcs */
	vmcs_write(gcpu->vmcs, VMCS_PIN_CTRL, gvmcs[VMCS_PIN_CTRL]);
}

static void handle_proc_ctrl1(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	/* TODO: merge with hvmcs */
	vmcs_write(gcpu->vmcs, VMCS_PROC_CTRL1, gvmcs[VMCS_PROC_CTRL1]);
}

static void handle_proc_ctrl2(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	/* TODO: merge with hvmcs */
	vmcs_write(gcpu->vmcs, VMCS_PROC_CTRL2, gvmcs[VMCS_PROC_CTRL2]);
}

static void handle_link_ptr(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	uint64_t hpa;

	VMM_ASSERT(gpm_gpa_to_hpa(gcpu->guest, gvmcs[VMCS_LINK_PTR], &hpa, NULL));

	vmcs_write(gcpu->vmcs, VMCS_LINK_PTR, hpa);
}

static void handle_entry_msr_load_count(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	/* TODO: merge with hvmcs */
	vmcs_write(gcpu->vmcs, VMCS_ENTRY_MSR_LOAD_COUNT, gvmcs[VMCS_ENTRY_MSR_LOAD_COUNT]);
}

static void handle_entry_msr_load_addr(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	uint64_t hpa;

	VMM_ASSERT(gpm_gpa_to_hpa(gcpu->guest, gvmcs[VMCS_ENTRY_MSR_LOAD_ADDR], &hpa, NULL));

	/* TODO: merge with hvmcs */
	vmcs_write(gcpu->vmcs, VMCS_ENTRY_MSR_LOAD_ADDR, hpa);
}

static void handle_entry_ctrl(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	/* TODO: merge with hvmcs */
	vmcs_write(gcpu->vmcs, VMCS_ENTRY_CTRL, gvmcs[VMCS_ENTRY_CTRL]);
}

static void handle_preemption_timer(guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t vmcs_field UNUSED)
{
	/* TODO: merge with hvmcs */
	vmcs_write(gcpu->vmcs, VMCS_PREEMPTION_TIMER, gvmcs[VMCS_PREEMPTION_TIMER]);
}

typedef void (*vmentry_vmcs_handler_t) (guest_cpu_handle_t gcpu, uint64_t *gvmcs, vmcs_field_t field_id);

typedef struct {
	vmentry_vmcs_handler_t handler;
	boolean_t need_save_hvmcs;
	uint32_t pad;
} vmentry_vmcs_handle_t;

static vmentry_vmcs_handle_t vmentry_vmcs_handle_array[] = {
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_VPID
	{ handle_io_bitmap_a,          TRUE, 0 }, //VMCS_IO_BITMAP_A
	{ handle_io_bitmap_b,          TRUE, 0 }, //VMCS_IO_BITMAP_B
	{ handle_msr_bitmap,           TRUE, 0 }, //VMCS_MSR_BITMAP
	{ handle_tsc_offset,           TRUE, 0 }, //VMCS_TSC_OFFSET
	{ handle_virtual_apic_addr,    TRUE, 0 }, //VMCS_VIRTUAL_APIC_ADDR
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_APIC_ACCESS_ADDR
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_POST_INTR_NOTI_VECTOR
	{ handle_post_intr_desc_addr,  TRUE, 0 }, //VMCS_POST_INTR_DESC_ADDR
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_EOI_EXIT_BITMAP0
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_EOI_EXIT_BITMAP1
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_EOI_EXIT_BITMAP2
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_EOI_EXIT_BITMAP3
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_TPR_THRESHOLD
	{ handle_eptp_addr,            TRUE, 0 }, //VMCS_EPTP_ADDRESS
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_XSS_EXIT_BITMAP
	{ handle_pin_ctrl,             TRUE, 0 }, //VMCS_PIN_CTRL
	{ handle_proc_ctrl1,           TRUE, 0 }, //VMCS_PROC_CTRL1
	{ handle_proc_ctrl2,           TRUE, 0 }, //VMCS_PROC_CTRL2
	{ NULL,                       FALSE, 0 }, //VMCS_EXIT_CTRL
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_CR0_MASK
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_CR4_MASK
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_CR0_SHADOW
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_CR4_SHADOW
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_CR3_TARGET0
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_CR3_TARGET1
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_CR3_TARGET2
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_CR3_TARGET3
	{ handle_link_ptr,             TRUE, 0 }, //VMCS_LINK_PTR
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_CR0
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_CR3
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_CR4
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_ES_SEL
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_CS_SEL
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_SS_SEL
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_DS_SEL
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_FS_SEL
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_FS_BASE
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_GS_SEL
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_GS_BASE
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_TR_SEL
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_TR_BASE
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_GDTR_BASE
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_IDTR_BASE
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_RSP
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_RIP
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_PAT
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_EFER
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_PERF_G_CTRL
	{ NULL,                       FALSE, 0 }, //VMCS_EXIT_MSR_STORE_COUNT
	{ NULL,                       FALSE, 0 }, //VMCS_EXIT_MSR_STORE_ADDR
	{ NULL,                       FALSE, 0 }, //VMCS_EXIT_MSR_LOAD_COUNT
	{ NULL,                       FALSE, 0 }, //VMCS_EXIT_MSR_LOAD_ADDR
	{ handle_entry_msr_load_count, TRUE, 0 }, //VMCS_ENTRY_MSR_LOAD_COUNT
	{ handle_entry_msr_load_addr,  TRUE, 0 }, //VMCS_ENTRY_MSR_LOAD_ADDR
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_EXCEPTION_BITMAP
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_PAGE_FAULT_ECODE_MASK
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_PAGE_FAULT_ECODE_MATCH
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_SYSENTER_CS
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_SYSENTER_ESP
	{ NULL,                       FALSE, 0 }, //VMCS_HOST_SYSENTER_EIP
	{ copy_from_gvmcs,             TRUE, 0 }, //VMCS_CR3_TARGET_COUNT
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_ENTRY_INTR_INFO
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_DBGCTL
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_INTERRUPTIBILITY
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_INTERRUPT_STATUS
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_PEND_DBG_EXCEPTION
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_ENTRY_ERR_CODE
	{ handle_entry_ctrl,           TRUE, 0 }, //VMCS_ENTRY_CTRL
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_ENTRY_INSTR_LEN
	{ handle_preemption_timer,     TRUE, 0 }, //VMCS_PREEMPTION_TIMER
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_PAT
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_EFER
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_PERF_G_CTRL
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_PDPTR0
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_PDPTR1
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_PDPTR2
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_PDPTR3
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_CR0
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_CR3
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_CR4
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_DR7
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_GDTR_BASE
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_GDTR_LIMIT
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_IDTR_BASE
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_IDTR_LIMIT
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_ACTIVITY_STATE
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_SYSENTER_CS
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_SYSENTER_ESP
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_SYSENTER_EIP
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_ES_SEL
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_ES_BASE
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_ES_LIMIT
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_ES_AR
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_CS_SEL
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_CS_BASE
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_CS_LIMIT
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_CS_AR
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_SS_SEL
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_SS_BASE
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_SS_LIMIT
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_SS_AR
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_DS_SEL
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_DS_BASE
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_DS_LIMIT
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_DS_AR
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_FS_SEL
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_FS_BASE
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_FS_LIMIT
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_FS_AR
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_GS_SEL
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_GS_BASE
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_GS_LIMIT
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_GS_AR
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_LDTR_SEL
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_LDTR_BASE
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_LDTR_LIMIT
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_LDTR_AR
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_TR_SEL
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_TR_BASE
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_TR_LIMIT
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_TR_AR
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_RSP
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_RIP
	{ copy_from_gvmcs,            FALSE, 0 }, //VMCS_GUEST_RFLAGS
	{ NULL,                       FALSE, 0 }, //VMCS_GUEST_PHY_ADDR
	{ NULL,                       FALSE, 0 }, //VMCS_GUEST_LINEAR_ADDR
	{ NULL,                       FALSE, 0 }, //VMCS_INSTR_ERROR
	{ NULL,                       FALSE, 0 }, //VMCS_EXIT_REASON
	{ NULL,                       FALSE, 0 }, //VMCS_EXIT_INT_INFO
	{ NULL,                       FALSE, 0 }, //VMCS_EXIT_INT_ERR_CODE
	{ NULL,                       FALSE, 0 }, //VMCS_IDT_VECTOR_INFO
	{ NULL,                       FALSE, 0 }, //VMCS_IDT_VECTOR_ERR_CODE
	{ NULL,                       FALSE, 0 }, //VMCS_EXIT_INSTR_LEN
	{ NULL,                       FALSE, 0 }, //VMCS_EXIT_INSTR_INFO
	{ NULL,                       FALSE, 0 }, //VMCS_EXIT_QUAL
};

_Static_assert(sizeof(vmentry_vmcs_handle_array)/sizeof(vmentry_vmcs_handle_t) == VMCS_FIELD_COUNT,
			"Nested VT: vmentry vmcs handle count not aligned with VMCS fields count!");

void emulate_vmentry(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs;
	nestedvt_data_t *data;
	uint64_t *gvmcs;
	uint64_t *hvmcs;
	uint32_t i;

	vmcs = gcpu->vmcs;
	data = get_nestedvt_data(gcpu);
	gvmcs = data->gvmcs;
	hvmcs = data->hvmcs;

	/* Backup VMCS fields for Layer-0 */
	for (i = 0; i < VMCS_FIELD_COUNT; i++) {
		if (vmentry_vmcs_handle_array[i].need_save_hvmcs) {
			hvmcs[i] = vmcs_read(vmcs, i);
		}
	}

	/* Trigger all VMCS fields handlers */
	for (i = 0; i < VMCS_FIELD_COUNT; i++) {
		if (vmentry_vmcs_handle_array[i].handler) {
			vmentry_vmcs_handle_array[i].handler(gcpu, gvmcs, i);
		}
	}
}
