/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _SUSPEND_H_
#define _SUSPEND_H_

#ifndef MODULE_SUSPEND
#error "MODULE_SUSPEND is not defined"
#endif

void suspend_bsp_init(uint32_t sipi_page);
void suspend_ap_init(void);

#endif
