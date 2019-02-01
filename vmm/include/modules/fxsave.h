/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _FXSAVE_H_
#define _FXSAVE_H_

#ifndef MODULE_FXSAVE
#error "MODULE_FXSAVE is not defined"
#endif

/*fxsave will isolate FPU/MMX/SSE registers*/
void fxsave_isolation_init(void);
void fxsave_enable(void);
#endif
