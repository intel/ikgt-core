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
#ifndef _VMEXIT_HANDLER_H_
#define _VMEXIT_HANDLER_H_

void vmexit_triple_fault(guest_cpu_handle_t gcpu);
void vmexit_sipi_event(guest_cpu_handle_t gcpu);
void vmexit_invd(guest_cpu_handle_t gcpu);
void vmexit_msr_read(guest_cpu_handle_t gcpu);
void vmexit_msr_write(guest_cpu_handle_t gcpu);

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_invalid_instruction()
*  PURPOSE	: Handler for invalid instruction
*  ARGUMENTS: gcpu
*  RETURNS	: void
*--------------------------------------------------------------------------*/
void vmexit_invalid_instruction(guest_cpu_handle_t gcpu);

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_xsetbv()
*  PURPOSE	: Handler for xsetbv instruction
*  ARGUMENTS: gcpu
*  RETURNS	: void
*--------------------------------------------------------------------------*/
void vmexit_xsetbv(guest_cpu_handle_t gcpu);

#endif

