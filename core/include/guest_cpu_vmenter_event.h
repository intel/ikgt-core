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

#ifndef _GUEST_CPU_VMENTER_EVENT_H_
#define _GUEST_CPU_VMENTER_EVENT_H_

#include "mon_objects.h"

typedef struct {
	ia32_vmx_vmcs_vmenter_interrupt_info_t	interrupt_info;
	uint32_t				instruction_length;
	address_t				error_code;
} vmenter_event_t;

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_inject_event
*  PURPOSE  : Inject interrupt/exception into guest if allowed, otherwise
*           : set NMI/Interrupt window
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*           : vmenter_event_t *p_event
*  RETURNS  : TRUE if event was injected, FALSE
*  NOTES    : no checkings are done for event validity
*---------------------------------------------------------------------------*/
boolean_t gcpu_inject_event(guest_cpu_handle_t gcpu, vmenter_event_t *p_event);

/*---------------------------------------------------------------------------*
*  FUNCTION : mon_gcpu_inject_gp0
*  PURPOSE  : Inject GP with error code 0
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*  RETURNS  : TRUE if event was injected, FALSE
*---------------------------------------------------------------------------*/
boolean_t mon_gcpu_inject_gp0(guest_cpu_handle_t gcpu);

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_inject_fault
*  PURPOSE  : Inject a fault to guest CPU
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*              int vec - fault vector
*              uint32_t code - error code pushed on guest stack
*  RETURNS  : TRUE if event was injected, FALSE
*---------------------------------------------------------------------------*/
boolean_t gcpu_inject_fault(guest_cpu_handle_t gcpu, int vec, uint32_t code);

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_inject_nmi
*  PURPOSE  : Inject NMI into guest if allowed, otherwise set NMI window
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*  RETURNS  : TRUE if event was injected, FALSE
*---------------------------------------------------------------------------*/
boolean_t gcpu_inject_nmi(guest_cpu_handle_t gcpu);

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_inject_double_fault
*  PURPOSE  : Inject Double Fault exception into guest if allowed,
*           :  otherwise set Interruption window
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*  RETURNS  : TRUE if event was injected, FALSE
*---------------------------------------------------------------------------*/
boolean_t gcpu_inject_double_fault(guest_cpu_handle_t gcpu);

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_set_pending_nmi
*  PURPOSE  : Cause NMI VMEXIT be invoked immediately when NMI blocking finished
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*           : boolean_t value
*  RETURNS  : void
*---------------------------------------------------------------------------*/
void gcpu_set_pending_nmi(guest_cpu_handle_t gcpu, boolean_t value);

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_vmexit_exception_resolve
*  PURPOSE  : Called if exception, caused VMEXIT was resolved by MON code
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*  RETURNS  : void
*---------------------------------------------------------------------------*/
void gcpu_vmexit_exception_resolve(guest_cpu_handle_t gcpu);

/*---------------------------------------------------------------------------*
*  FUNCTION : gcpu_vmexit_exception_reflect
*  PURPOSE  : Reflect exception to guest.
*           : Called if exception, caused VMEXIT was caused by Guest SW
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*  RETURNS  : void
*---------------------------------------------------------------------------*/
void gcpu_vmexit_exception_reflect(guest_cpu_handle_t gcpu);

/*---------------------------------------------------------------------------*
*  FUNCTION : mon_gcpu_inject_invalid_opcode_exception
*  PURPOSE  : Inject invalid opcode exception
*  ARGUMENTS: guest_cpu_handle_t gcpu - guest CPU
*  RETURNS  : TRUE if event was injected, FALSE if event was not injected.
*---------------------------------------------------------------------------*/
boolean_t mon_gcpu_inject_invalid_opcode_exception(guest_cpu_handle_t gcpu);

#define gcpu_inject_ts(gcpu, code) \
	gcpu_inject_fault( \
	gcpu, \
	(int)IA32_EXCEPTION_VECTOR_INVALID_TASK_SEGMENT_SELECTOR, \
	code \
	);

#define gcpu_inject_ss(gcpu, code) \
	gcpu_inject_fault( \
	gcpu, \
	(int)IA32_EXCEPTION_VECTOR_STACK_SEGMENT_FAULT, \
	code \
	);

#define gcpu_inject_np(gcpu, code) \
	gcpu_inject_fault( \
	gcpu, \
	(int)IA32_EXCEPTION_VECTOR_SEGMENT_NOT_PRESENT, \
	code \
	);

#define gcpu_inject_db(gcpu) \
	gcpu_inject_fault( \
	gcpu, \
	(int)IA32_EXCEPTION_VECTOR_DEBUG_BREAKPOINT, \
	0 \
	);

#endif    /* _GUEST_CPU_VMENTER_EVENT_H_ */
