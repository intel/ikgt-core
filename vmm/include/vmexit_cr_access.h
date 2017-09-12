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

#ifndef VMEXIT_CR_ACCESS_H
#define VMEXIT_CR_ACCESS_H

#include "vmm_base.h"

/* write_value: the new value to set to cr
** cr_value: pointer to the cr_value which will be set to VMCS_GUEST_CRX
**     note: handler can change all related bits, not only the masked bit.
**     for example, handler for CR0.PG can also update CR0.PE if they are set to 1 at same time
**     otherwise, if we only set CR0.PG to 1 and leave CR0.PE to 0, after resume, #GP will occur
** return: TRUE mean needs to inject #GP
*/
typedef boolean_t (*cr_write_handler) (guest_cpu_handle_t gcpu, uint64_t write_value, uint64_t* cr_value);
void cr0_write_register(uint16_t guest_id, cr_write_handler handler, uint64_t mask);
void cr4_write_register(uint16_t guest_id, cr_write_handler handler, uint64_t mask);
/* cr0_guest_write() will set value to shadow and call registered handlers to
** update bits from existing VMCS_GUEST_CR0.
** when new CR0 is assigned from host (not guest), a vmcs write to VMCS_GUEST_CR0
** is required before calling cr0_guest_write()
*/
boolean_t cr0_guest_write(guest_cpu_handle_t gcpu, uint64_t write_value);
/* same as cr0_guest_write() */
boolean_t cr4_guest_write(guest_cpu_handle_t gcpu, uint64_t write_value);
void cr_write_init(void);

void cr_write_guest_init(uint16_t guest_id);
void cr_write_gcpu_init(guest_cpu_handle_t gcpu);

void vmexit_cr_access(guest_cpu_handle_t gcpu);

#endif                          /* VMEXIT_CR_ACCESS_H */
