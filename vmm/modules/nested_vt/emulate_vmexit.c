/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "gcpu.h"
#include "vmcs.h"
#include "vmexit_cr_access.h"
#include "nested_vt_internal.h"

static void copy_from_hvmcs(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id)
{
	vmcs_write(gcpu->vmcs, field_id, data->hvmcs[field_id]);
}

static void handle_entry_msr_load_count(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	/* TODO: merge with hvmcs */
	vmcs_write(gcpu->vmcs, VMCS_ENTRY_MSR_LOAD_COUNT, data->gvmcs[VMCS_EXIT_MSR_LOAD_COUNT]);
}

static void handle_entry_msr_load_addr(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	/* TODO: merge with hvmcs */
	vmcs_write(gcpu->vmcs, VMCS_ENTRY_MSR_LOAD_ADDR, data->gvmcs[VMCS_EXIT_MSR_LOAD_ADDR]);
}

static void handle_guest_dbgctl(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Clear to 0 according to SDM: Vol3, Chapter 27.5.1 Loading Host Control Registers, Debug Registers, MSRs */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_DBGCTL, 0);
}

static void handle_guest_interruptibility(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_INTERRUPTIBILITY, 0);
}

static void handle_guest_interrupt_status(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_INTERRUPT_STATUS, 0);
}

static void handle_guest_pend_dbg_exception(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Clear to 0 according to SDM: Vol3, Chapter 27.5.5 Updating Non-Register State */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_PEND_DBG_EXCEPTION, 0);
}

static void handle_guest_pat(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_PAT, data->gvmcs[VMCS_HOST_PAT]);
}

static void handle_guest_efer(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_EFER, data->gvmcs[VMCS_HOST_EFER]);
}

static void handle_guest_perf_g_ctrl(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_PERF_G_CTRL, data->gvmcs[VMCS_HOST_PERF_G_CTRL]);
}

static void handle_guest_pdptr0(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* TODO: handle PAE paging if L1 use PAE paging */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_PDPTR0, 0);
}

static void handle_guest_pdptr1(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* TODO: handle PAE paging if L1 use PAE paging */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_PDPTR1, 0);
}

static void handle_guest_pdptr2(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* TODO: handle PAE paging if L1 use PAE paging */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_PDPTR2, 0);
}

static void handle_guest_pdptr3(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* TODO: handle PAE paging if L1 use PAE paging */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_PDPTR3, 0);
}

static void handle_guest_cr0(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_CR0, data->gvmcs[VMCS_HOST_CR0]);
}

static void handle_guest_cr3(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_CR3, data->gvmcs[VMCS_HOST_CR3]);
}

static void handle_guest_cr4(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_CR4, data->gvmcs[VMCS_HOST_CR4]);
}

static void handle_guest_dr7(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set to 0x400 according to SDM: Vol3, Chapter 27.5.1 Loading Host Control Registers, Debug Registers, MSRs */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_DR7, 0x400);
}

static void handle_guest_gdtr_base(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_GDTR_BASE, data->gvmcs[VMCS_HOST_GDTR_BASE]);
}

static void handle_guest_gdtr_limit(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set to 0xFFFF according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_GDTR_LIMIT, 0xFFFF);
}

static void handle_guest_idtr_base(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_IDTR_BASE, data->gvmcs[VMCS_HOST_IDTR_BASE]);
}

static void handle_guest_idtr_limit(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_IDTR_LIMIT, 0xFFFF);
}

static void handle_guest_activity_state(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set active state according to SDM: Vol3, Chapter 27.5.5 Updating Non-Register State */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_ACTIVITY_STATE, ACTIVITY_STATE_ACTIVE);
}

static void handle_guest_sysenter_cs(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_SYSENTER_CS, data->gvmcs[VMCS_HOST_SYSENTER_CS]);
}

static void handle_guest_sysenter_esp(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_SYSENTER_ESP, data->gvmcs[VMCS_HOST_SYSENTER_ESP]);
}

static void handle_guest_sysenter_eip(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_SYSENTER_EIP, data->gvmcs[VMCS_HOST_SYSENTER_EIP]);
}

static void handle_guest_es_sel(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_ES_SEL, data->gvmcs[VMCS_HOST_ES_SEL]);
}

static void handle_guest_seg_base(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id)
{
	/* Clear to 0 for ES/CS/DS/SS/LDTR according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	vmcs_write(gcpu->vmcs, field_id, 0);
}

static void handle_guest_seg_limit(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id)
{
	/* Set to 0xFFFFFFFF for ES/CS/DS/SS/FS/GS according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	vmcs_write(gcpu->vmcs, field_id, 0xFFFFFFFF);
}

static void handle_guest_es_ar(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set segment AR according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	if (data->gvmcs[VMCS_HOST_ES_SEL]) {
		vmcs_write(gcpu->vmcs, VMCS_GUEST_ES_AR, 3 | (1 << 4) | (1 << 7) | (1 << 14) | (1 << 15));
	} else {
		vmcs_write(gcpu->vmcs, VMCS_GUEST_ES_AR, 1 << 16);
	}
}

static void handle_guest_cs_sel(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_CS_SEL, data->gvmcs[VMCS_HOST_CS_SEL]);
}

static void handle_guest_cs_ar(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set segment AR according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_CS_AR, 0xB | (1 << 7) | ((!(data->gvmcs[VMCS_EXIT_CTRL] >> 9)) << 14) | (1 << 15));
}

static void handle_guest_ss_sel(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_SS_SEL, data->gvmcs[VMCS_HOST_SS_SEL]);
}

static void handle_guest_ss_ar(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set segment AR according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	if (data->gvmcs[VMCS_HOST_SS_SEL]) {
		vmcs_write(gcpu->vmcs, VMCS_GUEST_SS_AR, 3 | (1 << 4) | (1 << 7) | (1 << 14) | (1 << 15));
	} else {
		vmcs_write(gcpu->vmcs, VMCS_GUEST_SS_AR, (1 << 14) | (1 << 16));
	}
}

static void handle_guest_ds_sel(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_DS_SEL, data->gvmcs[VMCS_HOST_DS_SEL]);
}

static void handle_guest_ds_ar(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set segment AR according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	if (data->gvmcs[VMCS_HOST_DS_SEL]) {
		vmcs_write(gcpu->vmcs, VMCS_GUEST_DS_AR, 3 | (1 << 4) | (1 << 7) | (1 << 14) | (1 << 15));
	} else {
		vmcs_write(gcpu->vmcs, VMCS_GUEST_DS_AR, 1 << 16);
	}
}

static void handle_guest_fs_sel(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_FS_SEL, data->gvmcs[VMCS_HOST_FS_SEL]);
}

static void handle_guest_fs_base(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_FS_BASE, data->gvmcs[VMCS_HOST_FS_BASE]);
}

static void handle_guest_fs_ar(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set segment AR according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	if (data->gvmcs[VMCS_HOST_FS_SEL]) {
		vmcs_write(gcpu->vmcs, VMCS_GUEST_FS_AR, 3 | (1 << 4) | (1 << 7) | (1 << 14) | (1 << 15));
	} else {
		vmcs_write(gcpu->vmcs, VMCS_GUEST_FS_AR, 1 << 16);
	}
}

static void handle_guest_gs_sel(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_GS_SEL, data->gvmcs[VMCS_HOST_GS_SEL]);
}

static void handle_guest_gs_base(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_GS_BASE, data->gvmcs[VMCS_HOST_GS_BASE]);
}

static void handle_guest_gs_ar(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set segment AR according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	if (data->gvmcs[VMCS_HOST_GS_SEL]) {
		vmcs_write(gcpu->vmcs, VMCS_GUEST_GS_AR, 3 | (1 << 4) | (1 << 7) | (1 << 14) | (1 << 15));
	} else {
		vmcs_write(gcpu->vmcs, VMCS_GUEST_GS_AR, 1 << 16);
	}
}

static void handle_guest_ldtr_sel(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set LDTR according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_LDTR_SEL, 0);
}

static void handle_guest_ldtr_limit(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set LDTR according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_LDTR_LIMIT, 0);
}

static void handle_guest_ldtr_ar(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set LDTR according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_LDTR_AR, 1 << 16);
}

static void handle_guest_tr_sel(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_TR_SEL, data->gvmcs[VMCS_HOST_TR_SEL]);
}

static void handle_guest_tr_base(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_TR_BASE, data->gvmcs[VMCS_HOST_TR_BASE]);
}

static void handle_guest_tr_limit(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set to 0x67 according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_TR_LIMIT, 0x67);
}

static void handle_guest_tr_ar(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Set segment AR according to SDM: Vol3, Chapter 27.5.2 Loading Host Segment and Descriptor-Table Registers */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_CS_AR, 0xB | (1 << 7));
}

static void handle_guest_rsp(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_RSP, data->gvmcs[VMCS_HOST_RSP]);
}

static void handle_guest_rip(guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id UNUSED)
{
	vmcs_write(gcpu->vmcs, VMCS_GUEST_RIP, data->gvmcs[VMCS_HOST_RIP]);
}

static void handle_guest_rflags(guest_cpu_handle_t gcpu, nestedvt_data_t *data UNUSED, vmcs_field_t field_id UNUSED)
{
	/* Clear RFLAGS except bit 1 according to SDM: Vol3, Chapter 27.5.3 Loading Host RIP RSP and RFLAGS */
	vmcs_write(gcpu->vmcs, VMCS_GUEST_RFLAGS, 1 << 1);
}

typedef void (*vmexit_vmcs_handler_t) (guest_cpu_handle_t gcpu, nestedvt_data_t *data, vmcs_field_t field_id);
typedef struct {
	vmexit_vmcs_handler_t handler;
	boolean_t copy_to_gvmcs;
	uint32_t pad;
} vmexit_vmcs_handle_t;

static vmexit_vmcs_handle_t vmexit_vmcs_handle_array[] = {
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_VPID
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_IO_BITMAP_A
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_IO_BITMAP_B
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_MSR_BITMAP
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_TSC_OFFSET
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_VIRTUAL_APIC_ADDR
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_APIC_ACCESS_ADDR
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_POST_INTR_NOTI_VECTOR
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_POST_INTR_DESC_ADDR
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_EOI_EXIT_BITMAP0
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_EOI_EXIT_BITMAP1
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_EOI_EXIT_BITMAP2
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_EOI_EXIT_BITMAP3
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_TPR_THRESHOLD
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_EPTP_ADDRESS
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_XSS_EXIT_BITMAP
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_PIN_CTRL
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_PROC_CTRL1
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_PROC_CTRL2
	{ NULL,                              FALSE, 0 }, //VMCS_EXIT_CTRL
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_CR0_MASK
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_CR4_MASK
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_CR0_SHADOW
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_CR4_SHADOW
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_CR3_TARGET0
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_CR3_TARGET1
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_CR3_TARGET2
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_CR3_TARGET3
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_LINK_PTR
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_CR0
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_CR3
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_CR4
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_ES_SEL
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_CS_SEL
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_SS_SEL
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_DS_SEL
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_FS_SEL
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_FS_BASE
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_GS_SEL
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_GS_BASE
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_TR_SEL
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_TR_BASE
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_GDTR_BASE
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_IDTR_BASE
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_RSP
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_RIP
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_PAT
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_EFER
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_PERF_G_CTRL
	{ NULL,                              FALSE, 0 }, //VMCS_EXIT_MSR_STORE_COUNT
	{ NULL,                              FALSE, 0 }, //VMCS_EXIT_MSR_STORE_ADDR
	{ NULL,                              FALSE, 0 }, //VMCS_EXIT_MSR_LOAD_COUNT
	{ NULL,                              FALSE, 0 }, //VMCS_EXIT_MSR_LOAD_ADDR
	{ handle_entry_msr_load_count,       FALSE, 0 }, //VMCS_ENTRY_MSR_LOAD_COUNT
	{ handle_entry_msr_load_addr,        FALSE, 0 }, //VMCS_ENTRY_MSR_LOAD_ADDR
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_EXCEPTION_BITMAP
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_PAGE_FAULT_ECODE_MASK
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_PAGE_FAULT_ECODE_MATCH
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_SYSENTER_CS
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_SYSENTER_ESP
	{ NULL,                              FALSE, 0 }, //VMCS_HOST_SYSENTER_EIP
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_CR3_TARGET_COUNT
	{ NULL,                              FALSE, 0 }, //VMCS_ENTRY_INTR_INFO
	{ handle_guest_dbgctl,                TRUE, 0 }, //VMCS_GUEST_DBGCTL
	{ handle_guest_interruptibility,      TRUE, 0 }, //VMCS_GUEST_INTERRUPTIBILITY
	{ handle_guest_interrupt_status,      TRUE, 0 }, //VMCS_GUEST_INTERRUPT_STATUS
	{ handle_guest_pend_dbg_exception,    TRUE, 0 }, //VMCS_GUEST_PEND_DBG_EXCEPTION
	{ NULL,                              FALSE, 0 }, //VMCS_ENTRY_ERR_CODE
	{ copy_from_hvmcs,                   FALSE, 0 }, //VMCS_ENTRY_CTRL
	{ NULL,                              FALSE, 0 }, //VMCS_ENTRY_INSTR_LEN
	{ copy_from_hvmcs,                    TRUE, 0 }, //VMCS_PREEMPTION_TIMER
	{ handle_guest_pat,                   TRUE, 0 }, //VMCS_GUEST_PAT
	{ handle_guest_efer,                  TRUE, 0 }, //VMCS_GUEST_EFER
	{ handle_guest_perf_g_ctrl,           TRUE, 0 }, //VMCS_GUEST_PERF_G_CTRL
	{ handle_guest_pdptr0,                TRUE, 0 }, //VMCS_GUEST_PDPTR0
	{ handle_guest_pdptr1,                TRUE, 0 }, //VMCS_GUEST_PDPTR1
	{ handle_guest_pdptr2,                TRUE, 0 }, //VMCS_GUEST_PDPTR2
	{ handle_guest_pdptr3,                TRUE, 0 }, //VMCS_GUEST_PDPTR3
	{ handle_guest_cr0,                   TRUE, 0 }, //VMCS_GUEST_CR0
	{ handle_guest_cr3,                   TRUE, 0 }, //VMCS_GUEST_CR3
	{ handle_guest_cr4,                   TRUE, 0 }, //VMCS_GUEST_CR4
	{ handle_guest_dr7,                   TRUE, 0 }, //VMCS_GUEST_DR7
	{ handle_guest_gdtr_base,             TRUE, 0 }, //VMCS_GUEST_GDTR_BASE
	{ handle_guest_gdtr_limit,            TRUE, 0 }, //VMCS_GUEST_GDTR_LIMIT
	{ handle_guest_idtr_base,             TRUE, 0 }, //VMCS_GUEST_IDTR_BASE
	{ handle_guest_idtr_limit,            TRUE, 0 }, //VMCS_GUEST_IDTR_LIMIT
	{ handle_guest_activity_state,        TRUE, 0 }, //VMCS_GUEST_ACTIVITY_STATE
	{ handle_guest_sysenter_cs,           TRUE, 0 }, //VMCS_GUEST_SYSENTER_CS
	{ handle_guest_sysenter_esp,          TRUE, 0 }, //VMCS_GUEST_SYSENTER_ESP
	{ handle_guest_sysenter_eip,          TRUE, 0 }, //VMCS_GUEST_SYSENTER_EIP
	{ handle_guest_es_sel,                TRUE, 0 }, //VMCS_GUEST_ES_SEL
	{ handle_guest_seg_base,              TRUE, 0 }, //VMCS_GUEST_ES_BASE
	{ handle_guest_seg_limit,             TRUE, 0 }, //VMCS_GUEST_ES_LIMIT
	{ handle_guest_es_ar,                 TRUE, 0 }, //VMCS_GUEST_ES_AR
	{ handle_guest_cs_sel,                TRUE, 0 }, //VMCS_GUEST_CS_SEL
	{ handle_guest_seg_base,              TRUE, 0 }, //VMCS_GUEST_CS_BASE
	{ handle_guest_seg_limit,             TRUE, 0 }, //VMCS_GUEST_CS_LIMIT
	{ handle_guest_cs_ar,                 TRUE, 0 }, //VMCS_GUEST_CS_AR
	{ handle_guest_ss_sel,                TRUE, 0 }, //VMCS_GUEST_SS_SEL
	{ handle_guest_seg_base,              TRUE, 0 }, //VMCS_GUEST_SS_BASE
	{ handle_guest_seg_limit,             TRUE, 0 }, //VMCS_GUEST_SS_LIMIT
	{ handle_guest_ss_ar,                 TRUE, 0 }, //VMCS_GUEST_SS_AR
	{ handle_guest_ds_sel,                TRUE, 0 }, //VMCS_GUEST_DS_SEL
	{ handle_guest_seg_base,              TRUE, 0 }, //VMCS_GUEST_DS_BASE
	{ handle_guest_seg_limit,             TRUE, 0 }, //VMCS_GUEST_DS_LIMIT
	{ handle_guest_ds_ar,                 TRUE, 0 }, //VMCS_GUEST_DS_AR
	{ handle_guest_fs_sel,                TRUE, 0 }, //VMCS_GUEST_FS_SEL
	{ handle_guest_fs_base,               TRUE, 0 }, //VMCS_GUEST_FS_BASE
	{ handle_guest_seg_limit,             TRUE, 0 }, //VMCS_GUEST_FS_LIMIT
	{ handle_guest_fs_ar,                 TRUE, 0 }, //VMCS_GUEST_FS_AR
	{ handle_guest_gs_sel,                TRUE, 0 }, //VMCS_GUEST_GS_SEL
	{ handle_guest_gs_base,               TRUE, 0 }, //VMCS_GUEST_GS_BASE
	{ handle_guest_seg_limit,             TRUE, 0 }, //VMCS_GUEST_GS_LIMIT
	{ handle_guest_gs_ar,                 TRUE, 0 }, //VMCS_GUEST_GS_AR
	{ handle_guest_ldtr_sel,              TRUE, 0 }, //VMCS_GUEST_LDTR_SEL
	{ handle_guest_seg_base,              TRUE, 0 }, //VMCS_GUEST_LDTR_BASE
	{ handle_guest_ldtr_limit,            TRUE, 0 }, //VMCS_GUEST_LDTR_LIMIT
	{ handle_guest_ldtr_ar,               TRUE, 0 }, //VMCS_GUEST_LDTR_AR
	{ handle_guest_tr_sel,                TRUE, 0 }, //VMCS_GUEST_TR_SEL
	{ handle_guest_tr_base,               TRUE, 0 }, //VMCS_GUEST_TR_BASE
	{ handle_guest_tr_limit,              TRUE, 0 }, //VMCS_GUEST_TR_LIMIT
	{ handle_guest_tr_ar,                 TRUE, 0 }, //VMCS_GUEST_TR_AR
	{ handle_guest_rsp,                   TRUE, 0 }, //VMCS_GUEST_RSP
	{ handle_guest_rip,                   TRUE, 0 }, //VMCS_GUEST_RIP
	{ handle_guest_rflags,                TRUE, 0 }, //VMCS_GUEST_RFLAGS
	{ NULL,                               TRUE, 0 }, //VMCS_GUEST_PHY_ADDR
	{ NULL,                               TRUE, 0 }, //VMCS_GUEST_LINEAR_ADDR
	{ NULL,                               TRUE, 0 }, //VMCS_INSTR_ERROR
	{ NULL,                               TRUE, 0 }, //VMCS_EXIT_REASON
	{ NULL,                               TRUE, 0 }, //VMCS_EXIT_INT_INFO
	{ NULL,                               TRUE, 0 }, //VMCS_EXIT_INT_ERR_CODE
	{ NULL,                               TRUE, 0 }, //VMCS_IDT_VECTOR_INFO
	{ NULL,                               TRUE, 0 }, //VMCS_IDT_VECTOR_ERR_CODE
	{ NULL,                               TRUE, 0 }, //VMCS_EXIT_INSTR_LEN
	{ NULL,                               TRUE, 0 }, //VMCS_EXIT_INSTR_INFO
	{ NULL,                               TRUE, 0 }, //VMCS_EXIT_QUAL
};

_Static_assert(sizeof(vmexit_vmcs_handle_array)/sizeof(vmexit_vmcs_handle_t) == VMCS_FIELD_COUNT,
			"Nested VT: vmexit vmcs handle count not aligned with VMCS fields count!");

void emulate_vmexit(guest_cpu_handle_t gcpu)
{
	vmcs_obj_t vmcs;
	nestedvt_data_t *data;
	uint32_t i;

	vmcs = gcpu->vmcs;
	data = get_nestedvt_data(gcpu);

	/* Copy VMCS fields for Layer-1 */
	for (i = 0; i < VMCS_FIELD_COUNT; i++) {
		if (vmexit_vmcs_handle_array[i].copy_to_gvmcs) {
			data->gvmcs[i] = vmcs_read(vmcs, i);
		}
	}

	/* Clear GVMCS fields */
	data->gvmcs[VMCS_ENTRY_INTR_INFO] = 0;
	data->gvmcs[VMCS_ENTRY_ERR_CODE] = 0;
	data->gvmcs[VMCS_ENTRY_INSTR_LEN] = 0;

	/* Trigger all VMCS fields handlers */
	for (i = 0; i < VMCS_FIELD_COUNT; i++) {
		if (vmexit_vmcs_handle_array[i].handler) {
			vmexit_vmcs_handle_array[i].handler(gcpu, data, i);
		}
	}

	/* Update Guest CR0/CR4 */
	cr0_guest_write(gcpu, vmcs_read(vmcs, VMCS_GUEST_CR0));
	cr4_guest_write(gcpu, vmcs_read(vmcs, VMCS_GUEST_CR4));
}
