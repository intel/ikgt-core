/*******************************************************************************
* Copyright (c) 2018 Intel Corporation
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

#ifndef _APIC_REGS_H_
#define _APIC_REGS_H_

#define APIC_EOI                  0xB0
#define APIC_EOI_ACK              0x0   // Write this to the EOI register.
#define APIC_IRR                  0x200
#define APIC_IRR_NR               0x8   // Number of 32 bit IRR registers.
#define APIC_ICR_L                0x300
#define APIC_ICR_H                0x310

#endif /* _APIC_REGS_H_ */
