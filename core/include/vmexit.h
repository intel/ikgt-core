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

#ifndef _VMEXIT_H_
#define _VMEXIT_H_

#include "vmx_vmcs.h"

typedef enum {
	VMEXIT_NOT_HANDLED,
	VMEXIT_HANDLED,
	VMEXIT_HANDLED_RESUME_LEVEL2
} vmexit_handling_status_t;

typedef vmexit_handling_status_t (*vmexit_handler_t) (guest_cpu_handle_t);

/*------------------------------------------------------------------------*
*  FUNCTION : vmexit_initialize()
*  PURPOSE  : Perform basic vmexit initialization common for all guests
*  ARGUMENTS: void
*  RETURNS  : void
*------------------------------------------------------------------------*/
void vmexit_initialize(void);

/*------------------------------------------------------------------------*
*  FUNCTION : vmexit_guest_initialize()
*  PURPOSE  : Populate guest table, containing specific VMEXIT handlers with
*           : default handlers
*  ARGUMENTS: guest_id_t guest_id
*  RETURNS  : void
*------------------------------------------------------------------------*/
void vmexit_guest_initialize(guest_id_t guest_id);

/*------------------------------------------------------------------------*
*  FUNCTION : vmexit_common_handler()
*  PURPOSE  : Called by vmexit_func() upon each VMEXIT
*  ARGUMENTS: void
*  RETURNS  : void
*------------------------------------------------------------------------*/
void vmexit_common_handler(void);

/*------------------------------------------------------------------------*
*  FUNCTION : vmexit_install_handler
*  PURPOSE  : Install specific VMEXIT handler
*  ARGUMENTS: guest_id_t        guest_id
*           : vmexit_handler_t  handler
*           : uint32_t          reason
*  RETURNS  : mon_status_t
*------------------------------------------------------------------------*/
mon_status_t vmexit_install_handler(guest_id_t guest_id,
				    vmexit_handler_t handler,
				    uint32_t reason);

/* should not be here */
vmexit_handling_status_t vmexit_handler_default(guest_cpu_handle_t);
/*------------------------------------------------------------------------*
*  FUNCTION : vmentry_failure_function
*  PURPOSE  : Called upon VMENTER failure
*  ARGUMENTS: address_t flag - value of processor flags register
*  RETURNS  : void
*  NOTES    : is not VMEXIT
*------------------------------------------------------------------------*/
void vmentry_failure_function(address_t flags);

/*------------------------------------------------------------------------*
*  FUNCTION : vmexit_direct_call_top_down_handler
*  PURPOSE  : Used for specific case. Normally should never be called.
*  ARGUMENTS: self described
*  RETURNS  : void
*------------------------------------------------------------------------*/
void vmexit_direct_call_handler(guest_cpu_handle_t gcpu);

/*-------------------------------------------------------------------------
 * Guest/Guest CPU vmexits control
 *
 * request vmexits for given gcpu or guest
 *
 * Receives 2 bitmasks:
 * For each 1bit in mask check the corresponding request bit. If request bit
 * is 1 - request the vmexit on this bit change, else - remove the
 * previous request for this bit.
 *------------------------------------------------------------------------- */
typedef struct {
	uint64_t	bit_request;
	uint64_t	bit_mask;
} vmexit_control_field_t;

typedef struct {
	vmexit_control_field_t	cr0;
	vmexit_control_field_t	cr4;
	vmexit_control_field_t	exceptions;
	vmexit_control_field_t	pin_ctrls;
	vmexit_control_field_t	proc_ctrls;
	vmexit_control_field_t	proc_ctrls2;
	vmexit_control_field_t	vm_enter_ctrls;
	vmexit_control_field_t	vm_exit_ctrls;
} vmexit_control_t;

#endif                          /* _VMEXIT_H_ */
