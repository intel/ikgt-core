/*******************************************************************************
* Copyright (c) 2017 Intel Corporation
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

#endif /* _MP_INIT_H_ */
