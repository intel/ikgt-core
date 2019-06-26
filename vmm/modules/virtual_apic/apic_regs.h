/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _APIC_REGS_H_
#define _APIC_REGS_H_

#define APIC_EOI                  0xB0
#define APIC_EOI_ACK              0x0   // Write this to the EOI register.
#define APIC_IRR                  0x200
#define APIC_IRR_NR               0x8   // Number of 32 bit IRR registers.
#define APIC_ICR_L                0x300
#define APIC_ICR_H                0x310

#endif /* _APIC_REGS_H_ */
