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

#ifndef _GCPU_INJECT_EVENT_H_
#define _GCPU_INJECT_EVENT_H_

#include "vmm_objects.h"
#include "vmexit.h"
#include "vmm_arch.h"

void gcpu_reflect_idt_vectoring_info(guest_cpu_handle_t gcpu);
void gcpu_check_nmi_iret(guest_cpu_handle_t gcpu);

void vmexit_nmi_window(guest_cpu_handle_t gcpu);
void vmexit_intr_window(guest_cpu_handle_t gcpu);

void gcpu_inject_exception(guest_cpu_handle_t gcpu, uint8_t vector, uint32_t code);
#define gcpu_inject_db(gcpu) gcpu_inject_exception(gcpu, EXCEPTION_DB, 0)
#define gcpu_inject_ud(gcpu) gcpu_inject_exception(gcpu, EXCEPTION_UD, 0)
#define gcpu_inject_ts(gcpu, code) gcpu_inject_exception(gcpu, EXCEPTION_TS, code)
#define gcpu_inject_np(gcpu, code) gcpu_inject_exception(gcpu, EXCEPTION_NP, code)
#define gcpu_inject_ss(gcpu, code) gcpu_inject_exception(gcpu, EXCEPTION_SS, code)
#define gcpu_inject_gp0(gcpu) gcpu_inject_exception(gcpu, EXCEPTION_GP, 0)
#define gcpu_inject_pf(gcpu, code) gcpu_inject_exception(gcpu, EXCEPTION_PF, code)
#define gcpu_inject_ac(gcpu) gcpu_inject_exception(gcpu, EXCEPTION_AC, 0)

#endif    /* _GCPU_INJECT_EVENT_H_ */
