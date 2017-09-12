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

#ifndef _LAPIC_ID_H
#define _LAPIC_ID_H

#ifndef MODULE_LAPIC_ID
#error "MODULE_LAPIC_ID is not defined"
#endif

#include "vmm_base.h"

/*IA32 spec, volume3, chapter 10 APIC->Local APIC->Local APIC ID
  Local APIC ID usually not be changed*/
uint32_t get_lapic_id(uint16_t hcpu);

// need to be called for all cpus
void lapic_id_init(void);

#endif
