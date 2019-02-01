/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

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
