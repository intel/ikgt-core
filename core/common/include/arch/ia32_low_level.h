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

#ifndef _IA32_LOW_LEVEL_H_
#define _IA32_LOW_LEVEL_H_

#include "ia32_defs.h"

uint32_t CDECL ia32_read_cr0(void);
void CDECL ia32_write_cr0(uint32_t value);
uint32_t CDECL ia32_read_cr2(void);
uint32_t CDECL ia32_read_cr3(void);
void CDECL ia32_write_cr3(uint32_t value);
uint32_t CDECL ia32_read_cr4(void);
void CDECL ia32_write_cr4(uint32_t value);
void CDECL ia32_write_gdtr(ia32_gdtr_t *p_descriptor);
void CDECL ia32_read_gdtr(ia32_gdtr_t *p_descriptor);
void CDECL ia32_read_idtr(ia32_idtr_t *p_descriptor);
void CDECL ia32_write_idtr(ia32_idtr_t *p_descriptor);
uint16_t CDECL ia32_read_ldtr(void);
uint16_t CDECL ia32_read_tr(void);
void CDECL ia32_read_msr(uint32_t msr_id, uint64_t *p_value);
void CDECL ia32_write_msr(uint32_t msr_id, uint64_t *p_value);
uint32_t CDECL ia32_read_eflags(void);
uint16_t CDECL ia32_read_cs(void);
uint16_t CDECL ia32_read_ds(void);
uint16_t CDECL ia32_read_es(void);
uint16_t CDECL ia32_read_fs(void);
uint16_t CDECL ia32_read_gs(void);
uint16_t CDECL ia32_read_ss(void);

/* CPUID */
/* compiler intrinsic */
extern void __cpuid(int cpu_info[4], int info_type);
#define CPUID_EAX 0
#define CPUID_EBX 1
#define CPUID_ECX 2
#define CPUID_EDX 3
#define ia32_cpuid  __cpuid

/* This function is for use when the compiler intrinsic is not available */
void CDECL ia32_cpu_id(int cpu_info[4], int info_type);

#endif  /* _IA32_LOW_LEVEL_H_ */
