/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _GCPU_STATE_H_
#define _GCPU_STATE_H_

void gcpu_set_host_state(guest_cpu_handle_t gcpu);
void gcpu_set_ctrl_state(guest_cpu_handle_t gcpu);
void gcpu_set_init_state(guest_cpu_handle_t gcpu, const gcpu_state_t *initial_state);
void gcpu_set_reset_state(const guest_cpu_handle_t gcpu);

void prepare_g0gcpu_init_state(const gcpu_state_t *gcpu_state);

void gcpu_set_32bit_state(guest_cpu_handle_t gcpu);
/* x86_cr3 value is from stage0, caller should ensure it is still available before use it */
void gcpu_set_64bit_state(guest_cpu_handle_t gcpu, uint64_t x86_cr3);

#endif   /* _GCPU_STATE_H_ */
