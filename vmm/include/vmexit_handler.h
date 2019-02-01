/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

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

