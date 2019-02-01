/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _GCPU_SWITCH_H_
#define _GCPU_SWITCH_H_

/* perform full state save before switching to another guest */
void gcpu_swap_out(guest_cpu_handle_t gcpu);

/* perform state restore after switching from another guest */
void gcpu_swap_in(const guest_cpu_handle_t gcpu);

/*
 * Resume execution.
 * should never returns.
 */
void gcpu_resume(guest_cpu_handle_t gcpu);

#endif   /* _GCPU_SWITCH_H_ */
