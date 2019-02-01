/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _VMEXIT_TASK_SWITCH_H_
#define _VMEXIT_TASK_SWITCH_H_

#ifndef MODULE_VMEXIT_TASK_SWITCH
#error "MODULE_VMEXIT_TASK_SWITCH is not defined"
#endif

void vmexit_task_switch_init(void);
#endif
