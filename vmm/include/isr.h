/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _ISR_H_
#define _ISR_H_

/*----------------------------------------------------*
 *  FUNCTION     : isr_setup()
 *  PURPOSE      : Builds ISR wrappers, IDT tables and
 *               : default ISR handlers for all CPUs.
 *  ARGUMENTS    : IN uint8_t number_of_cpus
 *  RETURNS      : void
 *-------------------------------------------------------*/
void isr_setup(void);

#endif   /* _ISR_H_ */
