/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _MP_INIT_H_
#define _MP_INIT_H_

#ifndef LIB_MP_INIT
#error "LIB_MP_INIT is not defined"
#endif

#include "vmm_base.h"
#include "vmm_arch.h"

void setup_cpu_startup_stack(uint32_t cpu_id, uint32_t esp);
void setup_sipi_page(uint64_t sipi_page, boolean_t need_wakeup_bsp, uint64_t c_entry);

/* Wakeup Application Processors(APs) */
void wakeup_aps(uint32_t sipi_page);

/* Get active cpu number */
uint32_t get_active_cpu_num(void);

uint32_t launch_aps(uint32_t sipi_page, uint8_t expected_cpu_num, uint64_t c_entry);

#endif /* _MP_INIT_H_ */
