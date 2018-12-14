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

#ifndef _VMCS_H_
#define _VMCS_H_
#include "dbg.h"
#include "vmm_asm.h"
#include "vmm_objects.h"

/*-------------------------------------------------------------------------
**the sequence of VMCS fields is:
**1. SW writable, CPU readonly
**2. SW rw, CPU rw
**3. SW readonly, CPU writable
**with this sequence, the VMCS fields in section 1 is always valid since CPU
**will not update them. see vmcs_clear_cache().
**in section 1&2, some VMCS fields should be initialized to 0. so, we collected
**them together. see the dirty_bitmap setting in vmcs_create().
*------------------------------------------------------------------------- */
typedef enum {
	/*SW writable, CPU readonly*/
	VMCS_VPID = 0,
	VMCS_IO_BITMAP_A,
	VMCS_IO_BITMAP_B,
	VMCS_MSR_BITMAP,
	VMCS_TSC_OFFSET,
	VMCS_VIRTUAL_APIC_ADDR,     // for virtual apic
	VMCS_APIC_ACCESS_ADDR,      // for virtual apic
	VMCS_POST_INTR_NOTI_VECTOR, // for virtual apic
	VMCS_POST_INTR_DESC_ADDR,   // for virtual apic
	VMCS_EOI_EXIT_BITMAP0,      // for virtual apic
	VMCS_EOI_EXIT_BITMAP1,      // for virtual apic
	VMCS_EOI_EXIT_BITMAP2,      // for virtual apic
	VMCS_EOI_EXIT_BITMAP3,      // for virtual apic
	VMCS_TPR_THRESHOLD,         // for virtual apic
	VMCS_EPTP_ADDRESS,
	VMCS_XSS_EXIT_BITMAP,
	VMCS_PIN_CTRL,
	VMCS_PROC_CTRL1,
	VMCS_PROC_CTRL2,
	VMCS_EXIT_CTRL,
	VMCS_CR0_MASK,
	VMCS_CR4_MASK,
	VMCS_CR0_SHADOW,
	VMCS_CR4_SHADOW,
	VMCS_CR3_TARGET0,
	VMCS_CR3_TARGET1,
	VMCS_CR3_TARGET2,
	VMCS_CR3_TARGET3,
	VMCS_LINK_PTR,
	VMCS_HOST_CR0,
	VMCS_HOST_CR3,
	VMCS_HOST_CR4,
	VMCS_HOST_ES_SEL,
	VMCS_HOST_CS_SEL,
	VMCS_HOST_SS_SEL,
	VMCS_HOST_DS_SEL,
	VMCS_HOST_FS_SEL,
	VMCS_HOST_FS_BASE,
	VMCS_HOST_GS_SEL,
	VMCS_HOST_GS_BASE,
	VMCS_HOST_TR_SEL,
	VMCS_HOST_TR_BASE,
	VMCS_HOST_GDTR_BASE,
	VMCS_HOST_IDTR_BASE,
	VMCS_HOST_RSP,
	VMCS_HOST_RIP,
	VMCS_HOST_PAT,
	VMCS_HOST_EFER,
	VMCS_HOST_PERF_G_CTRL,
	VMCS_EXIT_MSR_STORE_COUNT,     // init as 0 (see dirty_bitmap in vmcs_create)
	                               // = VMCS_INIT_TO_ZERO_FISRT
	VMCS_EXIT_MSR_STORE_ADDR,      // init as 0 (see dirty_bitmap in vmcs_create)
	VMCS_EXIT_MSR_LOAD_COUNT,      // init as 0 (see dirty_bitmap in vmcs_create)
	VMCS_EXIT_MSR_LOAD_ADDR,       // init as 0 (see dirty_bitmap in vmcs_create)
	VMCS_ENTRY_MSR_LOAD_COUNT,     // init as 0 (see dirty_bitmap in vmcs_create)
	VMCS_ENTRY_MSR_LOAD_ADDR,      // init as 0 (see dirty_bitmap in vmcs_create)
	VMCS_EXCEPTION_BITMAP,         // init as 0 (see dirty_bitmap in vmcs_create)
	VMCS_HOST_SYSENTER_CS,         // init as 0 (see dirty_bitmap in vmcs_create)
	VMCS_HOST_SYSENTER_ESP,        // init as 0 (see dirty_bitmap in vmcs_create)
	VMCS_HOST_SYSENTER_EIP,        // init as 0 (see dirty_bitmap in vmcs_create)
	VMCS_CR3_TARGET_COUNT,         // init as 0 (see dirty_bitmap in vmcs_create)
	/*SW rw, CPU rw*/
	VMCS_ENTRY_INTR_INFO,          // init as 0 (see dirty_bitmap in vmcs_create)
	                               // = VMCS_ALWAYS_VALID_COUNT
	VMCS_GUEST_DBGCTL,             // init as 0 (see dirty_bitmap in vmcs_create)
	VMCS_GUEST_INTERRUPTIBILITY,   // init as 0 (see dirty_bitmap in vmcs_create)
	VMCS_GUEST_INTERRUPT_STATUS,   // for virtual apic
	VMCS_GUEST_PEND_DBG_EXCEPTION, // init as 0 (see dirty_bitmap in vmcs_create)
	VMCS_ENTRY_ERR_CODE,           // init as 0 (see dirty_bitmap in vmcs_create)
	                               // = VMCS_INIT_TO_ZERO_LAST
	VMCS_ENTRY_CTRL,
	VMCS_ENTRY_INSTR_LEN,
	VMCS_PREEMPTION_TIMER,
	VMCS_GUEST_PAT,
	VMCS_GUEST_EFER,
	VMCS_GUEST_PERF_G_CTRL,
	VMCS_GUEST_PDPTR0,
	VMCS_GUEST_PDPTR1,
	VMCS_GUEST_PDPTR2,
	VMCS_GUEST_PDPTR3,
	VMCS_GUEST_CR0,
	VMCS_GUEST_CR3,
	VMCS_GUEST_CR4,
	VMCS_GUEST_DR7,
	VMCS_GUEST_GDTR_BASE,
	VMCS_GUEST_GDTR_LIMIT,
	VMCS_GUEST_IDTR_BASE,
	VMCS_GUEST_IDTR_LIMIT,
	VMCS_GUEST_ACTIVITY_STATE,
	VMCS_GUEST_SYSENTER_CS,
	VMCS_GUEST_SYSENTER_ESP,
	VMCS_GUEST_SYSENTER_EIP,
	VMCS_GUEST_ES_SEL,
	VMCS_GUEST_ES_BASE,
	VMCS_GUEST_ES_LIMIT,
	VMCS_GUEST_ES_AR,
	VMCS_GUEST_CS_SEL,
	VMCS_GUEST_CS_BASE,
	VMCS_GUEST_CS_LIMIT,
	VMCS_GUEST_CS_AR,
	VMCS_GUEST_SS_SEL,
	VMCS_GUEST_SS_BASE,
	VMCS_GUEST_SS_LIMIT,
	VMCS_GUEST_SS_AR,
	VMCS_GUEST_DS_SEL,
	VMCS_GUEST_DS_BASE,
	VMCS_GUEST_DS_LIMIT,
	VMCS_GUEST_DS_AR,
	VMCS_GUEST_FS_SEL,
	VMCS_GUEST_FS_BASE,
	VMCS_GUEST_FS_LIMIT,
	VMCS_GUEST_FS_AR,
	VMCS_GUEST_GS_SEL,
	VMCS_GUEST_GS_BASE,
	VMCS_GUEST_GS_LIMIT,
	VMCS_GUEST_GS_AR,
	VMCS_GUEST_LDTR_SEL,
	VMCS_GUEST_LDTR_BASE,
	VMCS_GUEST_LDTR_LIMIT,
	VMCS_GUEST_LDTR_AR,
	VMCS_GUEST_TR_SEL,
	VMCS_GUEST_TR_BASE,
	VMCS_GUEST_TR_LIMIT,
	VMCS_GUEST_TR_AR,
	VMCS_GUEST_RSP,
	VMCS_GUEST_RIP,
	VMCS_GUEST_RFLAGS,
	/*SW readonly, CPU writable*/
	VMCS_GUEST_PHY_ADDR,
	VMCS_GUEST_LINEAR_ADDR,
	VMCS_INSTR_ERROR,
	VMCS_EXIT_REASON,
	VMCS_EXIT_INT_INFO,
	VMCS_EXIT_INT_ERR_CODE,
	VMCS_IDT_VECTOR_INFO,
	VMCS_IDT_VECTOR_ERR_CODE,
	VMCS_EXIT_INSTR_LEN,
	VMCS_EXIT_INSTR_INFO,
	VMCS_EXIT_QUAL,
	VMCS_FIELD_COUNT
} vmcs_field_t;

#define VMCS_ERRO_MASK 0x41 //check RFLAGS bit 0 :cf ,bit6:zf

typedef struct vmcs_object_t *vmcs_obj_t;

/*
 *  Functions which are not a part ofg general VMCS API,
 *  but are specific to VMCS applied to real hardware
 */
vmcs_obj_t vmcs_create(void);
void vmcs_clear_cache(vmcs_obj_t vmcs);
void vmcs_clear_all_cache(vmcs_obj_t vmcs);
uint8_t vmcs_is_launched(vmcs_obj_t vmcs);
void vmcs_clear_launched(vmcs_obj_t vmcs);
void vmcs_set_launched(vmcs_obj_t vmcs);
void vmcs_write(vmcs_obj_t vmcs, vmcs_field_t field_id, uint64_t value);
uint64_t vmcs_read(vmcs_obj_t vmcs, vmcs_field_t field_id);
void vmcs_flush(vmcs_obj_t vmcs);
void vmcs_print_all(vmcs_obj_t vmcs);

uint32_t vmcs_dump_all(vmcs_obj_t vmcs, char *buffer, uint32_t size);

void vmcs_set_ptr(vmcs_obj_t vmcs);
void vmcs_clr_ptr(vmcs_obj_t vmcs);
void vmx_on(uint64_t *addr);
#define vmx_off()                  asm_vmxoff()
#endif

