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

#ifndef _LAPIC_IPI_H
#define _LAPIC_IPI_H

#ifndef LIB_LAPIC_IPI
#error "LIB_LAPIC_IPI is not defined"
#endif

#include "vmm_base.h"

boolean_t lapic_get_id(uint32_t *p_lapic_id);

//broadcast is excluding self, send is specified.
boolean_t broadcast_nmi(void);
boolean_t broadcast_init(void);
boolean_t broadcast_startup(uint32_t vector);
boolean_t send_nmi(uint32_t lapic_id);
boolean_t send_startup(uint32_t lapic_id, uint32_t vector);

#endif
