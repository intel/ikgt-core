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

#ifndef _VMX_NMI_H_
#define _VMX_NMI_H_

boolean_t nmi_manager_initialize(cpu_id_t num_of_cores);

static void nmi_raise(cpu_id_t cpu_id);
static void nmi_clear(cpu_id_t cpu_id);
static boolean_t nmi_is_pending(cpu_id_t cpu_id);
void nmi_raise_this(void);
void nmi_clear_this(void);
boolean_t nmi_is_pending_this(void);

/*------------------------------------------------------------------------*
*  FUNCTION : nmi_resume_handler()
*  PURPOSE  : If current CPU is platform NMI owner and unhandled platform NMI
*           : exists on current CPU, sets NMI-Window to get VMEXIT asap.
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : void
*------------------------------------------------------------------------*/
void nmi_resume_handler(guest_cpu_handle_t gcpu);

/*------------------------------------------------------------------------*
*  FUNCTION : nmi_vmexit_handler()
*  PURPOSE  : Process NMI VMEXIT
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : Status which says if VMEXIT was finally handled or
*           : it should be processed by upper layer
*  CALLED   : called as bottom-up local handler
*------------------------------------------------------------------------*/
vmexit_handling_status_t nmi_vmexit_handler(guest_cpu_handle_t gcpu);

/*------------------------------------------------------------------------*
*  FUNCTION : nmi_window_vmexit_handler()
*  PURPOSE  : Process NMI Window VMEXIT
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : Status which says if VMEXIT was finally handled or
*           : it should be processed by upper layer
*  CALLED   : called as bottom-up local handler
*------------------------------------------------------------------------*/
vmexit_handling_status_t nmi_window_vmexit_handler(guest_cpu_handle_t gcpu);

#endif                          /* _VMX_NMI_H_ */
