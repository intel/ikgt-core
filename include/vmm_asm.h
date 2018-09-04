/*******************************************************************************
* Copyright (c) 2016 Intel Corporation
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
#ifndef _VMM_ASM_H_
#define _VMM_ASM_H_

#include "vmm_base.h"

/*-------------------------------------------------------------------------
** From http://www.ibiblio.org/gferg/ldp/GCC-Inline-Assembly-HOWTO.html
** AT&T inline asm code contains 4 parts:
** asm ( assembler template
**     : output operands
**     : input operands
**     : list of clobbered registers
**     );
** For the "clobbered registers":
**   If the register is assigned by compiler (with "r"), no need to add
**   "clobbered registers".
**   If we specify the registers in input/outputoperands (with "a", "b",
**   "c", "d", "D", "S", etc), compiler will avoid using these registers.
**   so, it is also no need to add "clobbered registers".
**   So, in most of the asm code here, we don'tneed to specify "clobbered
**   registers"there's one exception, see the comments for "asm_cpuid".
*------------------------------------------------------------------------- */
static inline uint64_t asm_get_cr0(void)
{
	uint64_t value;
	__asm__ __volatile__ (
		"movq %%cr0, %0"
		: "=r" (value)
		);
	return value;
}

static inline uint64_t asm_get_cr2(void)
{
	uint64_t value;
	__asm__ __volatile__ (
		"movq %%cr2, %0"
		: "=r" (value)
		);
	return value;
}

static inline uint64_t asm_get_cr3(void)
{
	uint64_t value;
	__asm__ __volatile__ (
		"movq %%cr3, %0"
		: "=r" (value)
		);
	return value;
}

static inline uint64_t asm_get_cr4(void)
{
	uint64_t value;
	__asm__ __volatile__ (
		"movq %%cr4, %0"
		: "=r" (value)
		);
	return value;
}

static inline uint64_t asm_get_cr8(void)
{
	uint64_t value;
	__asm__ __volatile__ (
		"movq %%cr8, %0"
		: "=r" (value)
		);
	return value;
}

static inline void asm_set_cr0(uint64_t data)
{
	__asm__ __volatile__ (
		"movq %0, %%cr0"
		:: "r" (data)
		);
}

static inline void asm_set_cr2(uint64_t data)
{
	__asm__ __volatile__ (
		"movq %0, %%cr2"
		:: "r" (data)
		);
}

static inline void asm_set_cr3(uint64_t data)
{
	__asm__ __volatile__ (
		"movq %0, %%cr3"
		:: "r" (data)
		);
}

static inline void asm_set_cr4(uint64_t data)
{
	__asm__ __volatile__ (
		"movq %0, %%cr4"
		:: "r" (data)
		);
}

static inline void asm_set_cr8(uint64_t data)
{
	__asm__ __volatile__ (
		"movq %0, %%cr8"
		:: "r" (data)
		);
}

static inline uint8_t asm_in8(uint16_t port)
{
	uint8_t val8;

	__asm__ __volatile__ (
		"inb %1, %0"
		: "=a" (val8)
		: "d" (port)
		);

	return val8;
}

static inline uint16_t asm_in16(uint16_t port)
{
	uint16_t val16;

	__asm__ __volatile__ (
		"inw %1, %0"
		: "=a" (val16)
		: "d" (port)
		);

	return val16;
}

static inline uint32_t asm_in32(uint16_t port)
{
	uint32_t val32;

	__asm__ __volatile__ (
		"inl %1, %0"
		: "=a" (val32)
		: "d" (port)
		);

	return val32;
}

static inline void asm_out8(uint16_t port, uint8_t val8)
{
	__asm__ __volatile__ (
		"outb %1, %0"
		:
		: "d" (port), "a" (val8)
		);
}

static inline void asm_out16(uint16_t port, uint16_t val16)
{
	__asm__ __volatile__ (
		"outw %1, %0"
		:
		: "d" (port), "a" (val16)
		);
}

static inline void asm_out32(uint16_t port, uint32_t val32)
{
	__asm__ __volatile__ (
		"outl %1, %0"
		:
		: "d" (port), "a" (val32)
		);
}

static inline void asm_lidt(void *source)
{
	__asm__ __volatile__ (
		"lidt (%0)"
		:: "r" (source)
		: "memory"
		);
}

static inline void asm_sidt(void *destination)
{
	__asm__ __volatile__ (
		"sidt (%0)"
		:
		: "r" (destination)
		);
}

static inline uint32_t asm_rdmsr_hl(uint32_t msr_id, uint32_t* p_high)
{
	uint32_t low;
	__asm__ __volatile__ (
		"rdmsr"
		:"=a"(low), "=d"(*p_high)
		: "c"(msr_id)
		);
	return low;
}

static inline uint64_t asm_rdmsr(uint32_t msr_id)
{
	uint32_t high, low;
	low = asm_rdmsr_hl(msr_id, &high);
	return MAKE64(high,low);
}

static inline void asm_wrmsr(uint32_t msr_id,uint64_t value)
{
	__asm__ __volatile__ (
		"wrmsr"
		::"c"(msr_id), "a"(value&0xffffffffull), "d"(value>>32)
		);
}

static inline uint32_t asm_rdtsc_hl( uint32_t* p_high)
{
	uint32_t low;
	__asm__ __volatile__ (
		"rdtsc"
		:"=a"(low), "=d"(*p_high)
		);
	return low;
}

static inline uint64_t asm_rdtsc(void)
{
	uint32_t high, low;
	low = asm_rdtsc_hl( &high);
	return MAKE64(high,low);
}

/*-------------------------------------------------------------------------
** From https://gcc.gnu.org/bugzilla/show_bug.cgi?id=54232
** and https://gcc.gnu.org/bugzilla/show_bug.cgi?id=47602 when
** compile with -fPIC flag, ebx/rbx will be reserved by compiler.
** So, we need to save/restore rbx here manually
*------------------------------------------------------------------------- */
typedef struct {
	uint32_t	eax;
	uint32_t	ebx;
	uint32_t	ecx;
	uint32_t	edx;
} cpuid_params_t;

static inline void asm_cpuid(cpuid_params_t *cpuid_params)
{
	__asm__ __volatile__ (
		"xchgl %%ebx, %1  \n\t"  // save ebx
		"cpuid          \n\t"
		"xchgl %%ebx, %1  \n\t"  // restore ebx
		: "=a" (cpuid_params->eax),
		"=r" (cpuid_params->ebx),
		"=c" (cpuid_params->ecx),
		"=d" (cpuid_params->edx)
		: "a" (cpuid_params->eax),
		"1" (cpuid_params->ebx),
		"c" (cpuid_params->ecx),
		"d" (cpuid_params->edx)
		: "cc"
		);
}

static inline uint32_t asm_lock_cmpxchg32(volatile uint32_t *addr,
					 uint32_t new_value,
					 uint32_t old_value)
{
	uint32_t retval;
	__asm__ __volatile__ (
		"lock cmpxchgl  %2, (%1)"
		: "=a" (retval)
		: "r" (addr), "r" (new_value), "a" (old_value)
		);
	return retval;
}

static inline void asm_lock_inc32(volatile uint32_t *addr)
{
	__asm__ __volatile__ (
		"lock incl (%0)"
		:: "r" (addr)
		);
}

static inline void asm_lock_add32(volatile uint32_t *addr, uint32_t val)
{
	__asm__ __volatile__ (
		"lock addl %1, (%0)"
		:: "r" (addr), "r" (val)
		);
}

static inline void asm_lock_sub32(volatile uint32_t *addr, uint32_t val)
{
	__asm__ __volatile__ (
		"lock subl %1, (%0)"
		:: "r" (addr), "r" (val)
		);
}

static inline void asm_lock_or32(volatile uint32_t *addr, uint32_t val)
{
	__asm__ __volatile__ (
		"lock orl %1, (%0)"
		:: "r" (addr), "r" (val)
		);
}

static inline void asm_lock_and32(volatile uint32_t *addr, uint32_t val)
{
	__asm__ __volatile__ (
		"lock andl %1, (%0)"
		:: "r" (addr), "r" (val)
		);
}

static inline void asm_mfence(void)
{
	__asm__ __volatile__ ("mfence");
}

static inline void asm_pause(void)
{
	__asm__ __volatile__ ("pause");
}

static inline void asm_wbinvd(void)
{
	__asm__ __volatile__ ("wbinvd");
}

static inline void asm_hlt(void)
{
	__asm__ __volatile__ ("hlt");
}

/*caller must make sure the bitmap is not 0. If bitmap is 0, return value is not expected*/
static inline uint32_t asm_bsf32(uint32_t bitmap)
{
	uint32_t bit_idx;
	__asm__ __volatile__ (
		"bsfl  %1, %0"
		: "=r" (bit_idx)
		: "r" (bitmap)
		);
	return bit_idx;
}

/*caller must make sure the bitmap is not 0. If bitmap is 0, return value is not expected*/
static inline uint32_t asm_bsr32(uint32_t bitmap)
{
	uint32_t bit_idx;
	__asm__ __volatile__ (
		"bsrl  %1, %0"
		: "=r" (bit_idx)
		: "r" (bitmap)
		);
	return bit_idx;
}

/*caller must make sure the bitmap is not 0. If bitmap is 0, return value is not expected*/
static inline uint64_t asm_bsf64(uint64_t bitmap)
{
	uint64_t bit_idx;
	__asm__ __volatile__ (
		"bsfq  %1, %0"
		: "=r" (bit_idx)
		: "r" (bitmap)
		);
	return bit_idx;
}

/*caller must make sure the bitmap is not 0. If bitmap is 0, return value is not expected*/
static inline uint64_t asm_bsr64(uint64_t bitmap)
{
	uint64_t bit_idx;
	__asm__ __volatile__ (
		"bsrq  %1, %0"
		: "=r" (bit_idx)
		: "r" (bitmap)
		);
	return bit_idx;
}

static inline void asm_btr64(uint64_t *bitmap, uint64_t offset)
{
	__asm__ __volatile__ (
		"btrq %0, (%1)"
		:: "r" (offset), "r" (bitmap)
		: "memory"
	);
}

static inline void asm_bts64(uint64_t *bitmap, uint64_t offset)
{
	__asm__ __volatile__ (
		"btsq %0, (%1)"
		:: "r" (offset), "r" (bitmap)
		: "memory"
	);
}

static inline uint8_t asm_bt64(uint64_t *bitmap, uint64_t offset)
{
	uint8_t value = 0;
	__asm__ __volatile__ (
		"btq %1, (%2)	\n"
		"adc $0, %0	\n" /* CF stores the value of the bit */
		: "+r" (value)
		: "r" (offset), "r" (bitmap)
		: "cc", "memory"
	);

	return value;
}

static inline uint64_t asm_get_dr0(void)
{
	uint64_t value;
	__asm__ __volatile__ (
		"movq %%dr0, %0"
		: "=r" (value)
		);
	return value;
}

static inline uint64_t asm_get_dr1(void)
{
	uint64_t value;
	__asm__ __volatile__ (
		"movq %%dr1, %0"
		: "=r" (value)
		);
	return value;
}

static inline uint64_t asm_get_dr2(void)
{
	uint64_t value;
	__asm__ __volatile__ (
		"movq %%dr2, %0"
		: "=r" (value)
		);
	return value;
}

static inline uint64_t asm_get_dr3(void)
{
	uint64_t value;
	__asm__ __volatile__ (
		"movq %%dr3, %0"
		: "=r" (value)
		);
	return value;
}

static inline uint64_t asm_get_dr6(void)
{
	uint64_t value;
	__asm__ __volatile__ (
		"movq %%dr6, %0"
		: "=r" (value)
		);
	return value;
}

static inline uint64_t asm_get_dr7(void)
{
	uint64_t value;
	__asm__ __volatile__ (
		"movq %%dr7, %0"
		: "=r" (value)
		);
	return value;
}

static inline void asm_set_dr0(uint64_t data)
{
	__asm__ __volatile__ (
		"movq %0, %%dr0"
		:: "r" (data)
		);
}

static inline void asm_set_dr1(uint64_t data)
{
	__asm__ __volatile__ (
		"movq %0, %%dr1"
		:: "r" (data)
		);
}

static inline void asm_set_dr2(uint64_t data)
{
	__asm__ __volatile__ (
		"movq %0, %%dr2"
		:: "r" (data)
		);
}

static inline void asm_set_dr3(uint64_t data)
{
	__asm__ __volatile__ (
		"movq %0, %%dr3"
		:: "r" (data)
		);
}

static inline void asm_set_dr6(uint64_t data)
{
	__asm__ __volatile__ (
		"movq %0, %%dr6"
		:: "r" (data)
		);
}

static inline void asm_set_dr7(uint64_t data)
{
	__asm__ __volatile__ (
		"movq %0, %%dr7"
		:: "r" (data)
		);
}

static inline void asm_sgdt(void *p)
{
	__asm__ __volatile__ (
		"sgdt (%0)"
		:: "r" (p)
		: "memory"
		);
}

static inline void asm_lgdt(void *p)
{
	__asm__ __volatile__ (
		"lgdt (%0)"
		:: "r" (p)
		);
}

static inline uint64_t asm_sldt(void)
{
	uint64_t value;
	__asm__ __volatile__ (
		"sldt (%0)"
		: "=r" (value)
		);
	return value;
}

static inline uint16_t asm_str(void)
{
	uint16_t value;
	__asm__ __volatile__ (
		"str %0"
		: "=r" (value)
		);
	return value;
}

static inline void asm_ltr(uint16_t data)
{
	__asm__ __volatile__ (
		"ltr %0"
		::"r" (data)
		);
}

static inline void asm_lldt(uint16_t data)
{
	__asm__ __volatile__ (
		"lldt %0"
		::"r" (data)
		);
}

static inline uint16_t asm_get_cs(void)
{
	uint16_t value;
	__asm__ __volatile__ (
		"movw %%cs, %0"
		: "=r" (value));
	return value;
}

static inline uint16_t asm_get_ds(void)
{
	uint16_t value;
	__asm__ __volatile__ (
		"movw %%ds, %0"
		: "=r" (value));
	return value;
}

static inline uint16_t asm_get_es(void)
{
	uint16_t value;
	__asm__ __volatile__ (
		"movw %%es, %0"
		: "=r" (value));
	return value;
}

static inline uint16_t asm_get_fs(void)
{
	uint16_t value;
	__asm__ __volatile__ (
		"movw %%fs, %0"
		: "=r" (value));
	return value;
}

static inline uint16_t asm_get_gs(void)
{
	uint16_t value;
	__asm__ __volatile__ (
		"movw %%gs, %0"
		: "=r" (value));
	return value;
}

static inline uint16_t asm_get_ss(void)
{
	uint16_t value;
	__asm__ __volatile__ (
		"movw %%ss, %0"
		: "=r" (value));
	return value;
}

static inline void asm_set_ds(uint16_t data)
{
	__asm__ __volatile__ (
		"movw %0, %%ds"
		:: "r" (data)
		);
}

static inline void asm_set_es(uint16_t data)
{
	__asm__ __volatile__ (
		"movw %0, %%es"
		:: "r" (data)
		);
}

static inline void asm_set_fs(uint16_t data)
{
	__asm__ __volatile__ (
		"movw %0, %%fs"
		:: "r" (data)
		);
}

static inline void asm_set_gs(uint16_t data)
{
	__asm__ __volatile__ (
		"movw %0, %%gs"
		:: "r" (data)
		);
}

static inline void asm_set_ss(uint16_t data)
{
	__asm__ __volatile__ (
		"movw %0, %%ss"
		:: "r" (data)
		);
}

static inline uint64_t asm_get_rflags(void)
{
	uint64_t flags;
	__asm__ __volatile__ (
		"pushfq                 \n"
		"popq %0                \n"
		: "=r" (flags)
		);
	return flags;
}

static inline uint64_t asm_vmptrst(void)
{
	uint64_t vmcs_addr;
	__asm__ __volatile__ (
		"vmptrst (%0)"
		:: "r" (&vmcs_addr)
		: "memory"
		);
	return vmcs_addr;
}

static inline void asm_vmptrld(uint64_t *addr)
{
	__asm__ __volatile__ (
		"vmptrld  (%0)"
		:: "r" (addr)
		);
}

static inline void asm_vmclear(uint64_t *addr)
{
	__asm__ __volatile__ (
		"vmclear (%0)"
		:: "r" (addr)
		);
}

static inline void asm_vmwrite(uint64_t field, uint64_t field_value)
{
	__asm__ __volatile__ (
		"vmwrite %1, %0"
		:: "r" (field), "r" (field_value)
		);
}

static inline uint64_t asm_vmread(uint64_t field)
{
	uint64_t field_value;
	__asm__ __volatile__ (
		"vmread %1, %0"
		: "=r" (field_value)
		: "r" (field)
		);
	return field_value;
}

static inline void asm_vmxon(uint64_t *addr)
{
	__asm__ __volatile__ (
		"vmxon  (%0)"
		:: "r" (addr)
		);
}

static inline void asm_vmxoff(void)
{
	__asm__ __volatile__ (
		"vmxoff"
		);
}

static inline void asm_fxsave(void *addr)
{
	__asm__ __volatile__ (
		"fxsave  (%0)"
		:: "r" (addr)
		: "memory"
		);
}

static inline void asm_fxrstor(void *addr)
{
	__asm__ __volatile__ (
		"fxrstor  (%0)"
		:: "r" (addr)
		);
}

static inline void asm_xsave(void *addr, uint64_t mask)
{
	__asm__ __volatile__ (
		"xsave  (%0)"
		:: "r" (addr), "a"(mask&0xffffffffull), "d"(mask>>32)
		: "memory"
		);
}

static inline void asm_xrstor(void *addr, uint64_t mask)
{
	__asm__ __volatile__ (
		"xrstor  (%0)"
		:: "r" (addr), "a"(mask&0xffffffffull), "d"(mask>>32)
		);
}

static inline void asm_xsetbv(uint64_t idx, uint64_t data)
{
	__asm__ __volatile__ (
		"xsetbv "
		:: "c" (idx), "a"(data&0xffffffffull), "d"(data>>32)
		);
}

static inline uint32_t asm_xgetbv_hl(uint32_t idx, uint32_t* p_high)
{
	uint32_t low;

	__asm__ __volatile__ (
		"xgetbv "
		: "=a"(low), "=d"(*p_high)
		: "c" (idx)
		);
	return low;
}

static inline uint64_t asm_xgetbv(uint32_t idx)
{
	uint32_t high, low;
	low = asm_xgetbv_hl(idx, &high);
	return MAKE64(high,low);
}

static inline void asm_invept(uint64_t eptp)
{
	struct {
		uint64_t eptp, gpa;
	} operand = {eptp, 0};

	__asm__ __volatile__ (
		"invept (%%rax), %%rcx"
		: :"a" (&operand), "c" (1) //the "1" means single-context invalidation
		);
}

static inline uint32_t asm_get_pkru(void)
{
	uint32_t value;
	__asm__ __volatile__ (
		"xorl %%ecx, %%ecx \n\t"
		".byte 0x0f, 0x01, 0xee " //rdpkru
		: "=a" (value)
		::"edx", "ecx"
		);
	return value;
}
#endif
