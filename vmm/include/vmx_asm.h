/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _VMX_ASM_H_
#define _VMX_ASM_H_

/*------------------------------------------------------------------------*
*  FUNCTION : vmexit_func()
*  PURPOSE  : Called upon VMEXIT. It in turn calls vmexit_common_handler()
*  ARGUMENTS: none
*  RETURNS  : void
*------------------------------------------------------------------------*/
void vmexit_func(void);

/*------------------------------------------------------------------------*
*  FUNCTION : vmentry_func()
*  PURPOSE  : Called upon VMENTER.
*  ARGUMENTS: uint32_t launch - if not zero do VMLAUNCH, otherwise VMRESUME
*  RETURNS  : void
*------------------------------------------------------------------------*/
void vmentry_func(uint32_t launch);

#endif                          /* _VMX_ASM_H_ */
