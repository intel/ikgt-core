/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _IDT_H_
#define _IDT_H_

#define VMM_IDT_EXTRA_STACKS 1

/*-------------------------------------------------------*
*  FUNCTION     : idt_load()
*  PURPOSE      : Load IDT descriptor into IDTR on given CPU
*  ARGUMENTS    : void
*  RETURNS      : void
*-------------------------------------------------------*/
void idt_load(void);

/*-------------------------------------------------------*
*  FUNCTION     : idt_setup()
*  PURPOSE      : Build and populate IDT tables, one per CPU
*  ARGUMENTS    : void
*  RETURNS      : void
*-------------------------------------------------------*/
void idt_setup(void);

#endif                          /* _IDT_H_ */
