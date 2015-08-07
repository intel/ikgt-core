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

#include "mon_defs.h"
#include "ia32_defs.h"
#include "ia32_low_level.h"

/****************************************************************************
*
* Register usage
*
* Caller-saved and scratch:
*    eax
*    edx
*    ecx
*
* Callee-saved
*    ebp
*    ebx
*    esi
*    edi
*    esp
*
****************************************************************************/
uint32_t CDECL ia32_read_cr0(void)
{
	__asm__ __volatile__ ("movl %cr0, %eax");
}

void CDECL ia32_write_cr0(uint32_t value)
{
	__asm__ __volatile__ ("movl %0, %%cr0" : : "r" (value));
}

uint32_t CDECL ia32_read_cr2(void)
{
	__asm__ __volatile__ ("movl %cr2, %eax");
}

uint32_t CDECL ia32_read_cr3(void)
{
	__asm__ __volatile__ ("movl %cr3, %eax");
}

void CDECL ia32_write_cr3(uint32_t value)
{
	__asm__ __volatile__ ("movl %0, %%cr3" : : "r" (value));
}

uint32_t CDECL ia32_read_cr4(void)
{
	__asm__ __volatile__ ("movl %cr4, %eax");
}

void CDECL ia32_write_cr4(uint32_t value)
{
	__asm__ __volatile__ ("movl %0, %%cr4" : : "r" (value));
}

void CDECL ia32_read_gdtr(ia32_gdtr_t *p_descriptor)
{
	__asm__ __volatile__ ("sgdt %0" : : "m" (*p_descriptor));
}

void CDECL ia32_write_gdtr(ia32_gdtr_t *p_descriptor)
{
	__asm__ __volatile__ ("lgdt %0" : : "m" (*p_descriptor));
}

void CDECL ia32_read_idtr(ia32_idtr_t *p_descriptor)
{
	__asm__ __volatile__ ("sidt %0" : : "m" (*p_descriptor));
}

void CDECL ia32_write_idtr(ia32_idtr_t *p_descriptor)
{
	__asm__ __volatile__ ("lidt %0" : : "m" (*p_descriptor));
}

uint16_t CDECL ia32_read_ldtr(void)
{
	__asm__ __volatile__ ("sldt %ax");
}

uint16_t CDECL ia32_read_tr(void)
{
	__asm__ __volatile__ ("str %ax");
}

void CDECL ia32_read_msr(uint32_t msr_id, uint64_t *p_value)
{
	__asm__ __volatile__ (
		"rdmsr"
		: "=A" (*p_value)
		: "c" (msr_id)
		);
}

void CDECL ia32_write_msr(uint32_t msr_id, uint64_t *p_value)
{
	__asm__ __volatile__ (
		"wrmsr"
		:
		: "c" (msr_id), "A" (*p_value)
		);
}

uint32_t CDECL ia32_read_eflags(void)
{
	__asm__ __volatile__ ("pushfl; popl %eax");
}

uint16_t CDECL ia32_read_cs(void)
{
	__asm__ __volatile__ ("mov %cs, %ax");
}

uint16_t CDECL ia32_read_ds(void)
{
	__asm__ __volatile__ ("mov %ds, %ax");
}

uint16_t CDECL ia32_read_es(void)
{
	__asm__ __volatile__ ("mov %es, %ax");
}

uint16_t CDECL ia32_read_fs(void)
{
	__asm__ __volatile__ ("mov %fs, %ax");
}

uint16_t CDECL ia32_read_gs(void)
{
	__asm__ __volatile__ ("mov %gs, %ax");
}

uint16_t CDECL ia32_read_ss(void)
{
	__asm__ __volatile__ ("mov %ss, %ax");
}

void CDECL ia32_cpu_id(int cpu_info[4], int info_type)
{
	int eax, ebx, ecx, edx, ebx_save;
	__asm__ __volatile__ (
		"movl %%ebx, %5\n\t"
		"movl %4, %%eax\n\t"
		"cpuid\n\t"
		"movl %%eax, %0\n\t"
		"movl %%ebx, %1\n\t"
		"movl %%ecx, %2\n\t"
		"movl %%edx, %3\n\t"
		"movl %5, %%ebx"
		: "=m" (eax), "=m" (ebx), "=m" (ecx), "=m" (edx)
		: "m" (info_type), "m" (ebx_save)
		: "eax", "ecx", "edx"
		);

	cpu_info[CPUID_EAX] = eax;
	cpu_info[CPUID_EBX] = ebx;
	cpu_info[CPUID_ECX] = ecx;
	cpu_info[CPUID_EDX] = edx;
}
