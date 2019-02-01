/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _PERF_CTRL_ISOLATION_H_
#define _PERF_CTRL_ISOLATION_H_

#ifndef MODULE_PERF_CTRL_ISOLATION
#error "MODULE_PERF_CTRL_ISOLATION is not defined"
#endif

void msr_perf_ctrl_isolation_init(void);

#endif /* _PERF_CTRL_ISOLATION_H_ */
