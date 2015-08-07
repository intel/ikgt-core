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
#include "hw_utils.h"
#include "em64t_defs.h"
#include "hw_vmx_utils.h"

uint8_t hw_read_port_8(uint16_t port)
{
	uint8_t val8;

	__asm__ __volatile__ (
		"in %1, %0"
		: "=a" (val8)
		: "d" (port)
		);

	return val8;
}

uint16_t hw_read_port_16(uint16_t port)
{
	uint16_t val16;

	__asm__ __volatile__ (
		"in %1, %0"
		: "=a" (val16)
		: "d" (port)
		);

	return val16;
}

uint32_t hw_read_port_32(uint16_t port)
{
	uint32_t val32;

	__asm__ __volatile__ (
		"in %1, %0"
		: "=a" (val32)
		: "d" (port)
		);

	return val32;
}

void hw_write_port_8(uint16_t port, uint8_t val8)
{
	__asm__ __volatile__ (
		"out %1, %0"
		:
		: "d" (port), "a" (val8)
		);
}

void hw_write_port_16(uint16_t port, uint16_t val16)
{
	__asm__ __volatile__ (
		"out %1, %0"
		:
		: "d" (port), "a" (val16)
		);
}

void hw_write_port_32(uint16_t port, uint32_t val32)
{
	__asm__ __volatile__ (
		"out %1, %0"
		:
		: "d" (port), "a" (val32)
		);
}

void hw_lidt(void *source)
{
	__asm__ __volatile__ (
		"lidt (%0)"
		:
		: "D" (source)
		);
}

void hw_sidt(void *destination)
{
	__asm__ __volatile__ (
		"sidt (%0)"
		:
		: "D" (destination)
		);
}

void hw_write_msr(uint32_t msr_id, uint64_t value)
{
	__asm__ __volatile__ (
		"movl %0, %%ecx\n"
		"mov  %1, %%rax\n"
		"mov  %1, %%rdx\n"
		"shr $32, %%rdx\n"
		"wrmsr\n"
		:
		: "r" (msr_id), "r" (value)
		: "%rcx", "%rax", "%rdx"
		);
}

uint64_t hw_read_msr(uint32_t msr_id)
{
	uint64_t value;

	__asm__ __volatile__ (
		"movl %1, %%ecx    \n"
		"xor %%rax, %%rax  \n"
		"rdmsr             \n"
		"shl $32, %%rdx    \n"
		"orq  %%rdx, %%rax \n"
		"movq %%rax, %0    \n"
		: "=r" (value)
		: "r" (msr_id)
		: "%rcx", "%rax", "%rdx"
		);

	return value;
}

boolean_t hw_scan_bit_forward(uint32_t *bit_number_ptr, uint32_t bitset)
{
	boolean_t found;

	if (0 != bitset) {
		__asm__ __volatile__ (
			"bsfl  %0, %%eax  \n"
			"movl %%eax, (%1) \n"
			:
			: "r" (bitset), "r" (bit_number_ptr)
			: "%rax"
			);

		found = TRUE;
	} else {
		found = FALSE;
	}

	return found;
}

boolean_t hw_scan_bit_backward(uint32_t *bit_number_ptr, uint32_t bitset)
{
	boolean_t found;

	if (0 != bitset) {
		__asm__ __volatile__ (
			"bsrl  %0, %%eax  \n"
			"movl %%eax, (%1) \n"
			:
			: "r" (bitset), "r" (bit_number_ptr)
			: "%rax"
			);

		found = TRUE;
	} else {
		found = FALSE;
	}

	return found;
}

boolean_t hw_scan_bit_forward64(uint32_t *bit_number_ptr, uint64_t bitset)
{
	boolean_t found;

	if (0 != bitset) {
		__asm__ __volatile__ (
			"bsfq  %0, %%rax  \n"
			"movl %%eax, (%1) \n"
			:
			: "r" (bitset), "r" (bit_number_ptr)
			: "%rax"
			);

		found = TRUE;
	} else {
		found = FALSE;
	}

	return found;
}

boolean_t hw_scan_bit_backward64(uint32_t *bit_number_ptr, uint64_t bitset)
{
	boolean_t found;

	if (0 != bitset) {
		__asm__ __volatile__ (
			"bsrq  %0, %%rax  \n"
			"movl %%eax, (%1) \n"
			:
			: "r" (bitset), "r" (bit_number_ptr)
			: "%rax"
			);

		found = TRUE;
	} else {
		found = FALSE;
	}

	return found;
}

uint64_t hw_rdtsc(void)
{
	uint64_t value;

	__asm__ __volatile__ (
		"xor  %%rax, %%rax \n"
		"rdtsc             \n" /* now result in edx:eax */
		"shlq $32, %%rdx   \n"
		"orq  %%rdx, %%rax \n"
		"movq %%rax, %0    \n"
		: "=r" (value)
		:
		: "%rax", "%rdx"
		);

	return value;
}

/*
 *   CR-read accessories
 */
uint64_t hw_read_cr0(void)
{
	uint64_t value;

	__asm__ __volatile__ ("movq %%cr0, %0" : "=r" (value));

	return value;
}

uint64_t hw_read_cr2(void)
{
	uint64_t value;

	__asm__ __volatile__ ("movq %%cr2, %0" : "=r" (value));

	return value;
}

uint64_t hw_read_cr3(void)
{
	uint64_t value;

	__asm__ __volatile__ ("movq %%cr3, %0" : "=r" (value));

	return value;
}

uint64_t hw_read_cr4(void)
{
	uint64_t value;

	__asm__ __volatile__ ("movq %%cr4, %0" : "=r" (value));

	return value;
}

uint64_t hw_read_cr8(void)
{
	uint64_t value;

	__asm__ __volatile__ ("movq %%cr8, %0" : "=r" (value));

	return value;
}

/*
 *   CR-write accessories
 */
void hw_write_cr0(uint64_t data)
{
	__asm__ __volatile__ ("movq %0, %%cr0" : : "r" (data));
}

void hw_write_cr3(uint64_t data)
{
	__asm__ __volatile__ ("movq %0, %%cr3" : : "r" (data));
}

void hw_write_cr4(uint64_t data)
{
	__asm__ __volatile__ ("movq %0, %%cr4" : : "r" (data));
}

void hw_write_cr8(uint64_t data)
{
	__asm__ __volatile__ ("movq %0, %%cr8" : : "r" (data));
}

uint64_t hw_read_dr0(void)
{
	uint64_t value;

	__asm__ __volatile__ ("movq %%dr0, %0" : "=r" (value));

	return value;
}

uint64_t hw_read_dr1(void)
{
	uint64_t value;

	__asm__ __volatile__ ("movq %%dr1, %0" : "=r" (value));

	return value;
}

uint64_t hw_read_dr2(void)
{
	uint64_t value;

	__asm__ __volatile__ ("movq %%dr2, %0" : "=r" (value));

	return value;
}

uint64_t hw_read_dr3(void)
{
	uint64_t value;

	__asm__ __volatile__ ("movq %%dr3, %0" : "=r" (value));

	return value;
}

uint64_t hw_read_dr4(void)
{
	uint64_t value;

	__asm__ __volatile__ ("movq %%dr4, %0" : "=r" (value));

	return value;
}

uint64_t hw_read_dr5(void)
{
	uint64_t value;

	__asm__ __volatile__ ("movq %%dr5, %0" : "=r" (value));

	return value;
}

uint64_t hw_read_dr6(void)
{
	uint64_t value;

	__asm__ __volatile__ ("movq %%dr6, %0" : "=r" (value));

	return value;
}

uint64_t hw_read_dr7(void)
{
	uint64_t value;

	__asm__ __volatile__ ("movq %%dr7, %0" : "=r" (value));

	return value;
}

void hw_write_dr0(uint64_t value UNUSED)
{
	__asm__ __volatile__ ("movq %rdi, %dr0");
}

void hw_write_dr1(uint64_t value UNUSED)
{
	__asm__ __volatile__ ("movq %rdi, %dr1");
}

void hw_write_dr2(uint64_t value UNUSED)
{
	__asm__ __volatile__ ("movq %rdi, %dr2");
}

void hw_write_dr3(uint64_t value UNUSED)
{
	__asm__ __volatile__ ("movq %rdi, %dr3");
}

void hw_write_dr4(uint64_t value UNUSED)
{
	__asm__ __volatile__ ("movq %rdi, %dr4");
}

void hw_write_dr5(uint64_t value UNUSED)
{
	__asm__ __volatile__ ("movq %rdi, %dr5");
}

void hw_write_dr6(uint64_t value UNUSED)
{
	__asm__ __volatile__ ("movq %rdi, %dr6");
}

void hw_write_dr7(uint64_t value UNUSED)
{
	__asm__ __volatile__ ("movq %rdi, %dr7");
}

void hw_invlpg(void *address)
{
	__asm__ __volatile__ (
		"invlpg %0"
		:
		: "m" (address)
		);
}

void hw_wbinvd(void)
{
	__asm__ __volatile__ ("wbinvd");
}

void hw_halt(void)
{
	__asm__ __volatile__ ("hlt");
}

/*
 * VMX primitives
 */
INLINE unsigned char vmx_ret_val(uint64_t flags)
{
	em64t_rflags_t rflags;

	rflags.uint64 = flags;

	if (rflags.bits.cf) {
		return (unsigned char)HW_VMX_FAILED;
	} else if (rflags.bits.zf) {
		return (unsigned char)HW_VMX_FAILED_WITH_STATUS;
	} else {
		return (unsigned char)HW_VMX_SUCCESS;
	}
}

void __vmx_vmptrst(uint64_t *vmcs_physical_address)
{
	__asm__ __volatile__ (
		"vmptrst (%0)     \n"
		:
		: "r" (vmcs_physical_address)
		);
}

unsigned char __vmx_vmptrld(uint64_t *vmcs_physical_address)
{
	uint64_t flags;

	__asm__ __volatile__ (
		"vmptrld  (%1)          \n"
		"pushfq                 \n"
		"popq %0                \n"
		: "=r" (flags)
		: "r" (vmcs_physical_address)
		: "rax"
		);

	return vmx_ret_val(flags);
}

unsigned char __vmx_vmclear(uint64_t *vmcs_physical_address)
{
	uint64_t flags;

	__asm__ __volatile__ (
		"vmclear (%1)           \n"
		"pushfq                 \n"
		"popq %0                \n"
		: "=r" (flags)
		: "r" (vmcs_physical_address)
		: "rax"
		);

	return vmx_ret_val(flags);
}

unsigned char __vmx_vmlaunch(void)
{
	uint64_t flags;

	__asm__ __volatile__ (
		"vmlaunch               \n"
		"pushfq                 \n"
		"popq %0                \n"
		: "=r" (flags)
		);

	return vmx_ret_val(flags);
}

unsigned char __vmx_vmresume(void)
{
	uint64_t flags;

	__asm__ __volatile__ (
		"vmresume               \n"
		"pushfq                 \n"
		"popq %0                \n"
		: "=r" (flags)
		);

	return vmx_ret_val(flags);
}

unsigned char __vmx_vmwrite(size_t field, size_t field_value)
{
	uint64_t flags;

	__asm__ __volatile__ (
		"vmwrite %2, %1         \n"
		"pushfq                 \n"
		"popq %0                \n"
		: "=r" (flags)
		: "r" (field), "r" (field_value)
		);

	return vmx_ret_val(flags);
}

unsigned char __vmx_vmread(size_t field, size_t *field_value)
{
	uint64_t flags;

	__asm__ __volatile__ (
		"vmread %1, (%2)        \n"
		"pushfq                 \n"
		"popq %0                \n"
		: "=r" (flags)
		: "r" (field), "r" (field_value)
		);

	return vmx_ret_val(flags);
}

unsigned char __vmx_on(uint64_t *vmcs_physical_address)
{
	uint64_t flags;

	__asm__ __volatile__ (
		"vmxon  (%1)            \n"
		"pushfq                 \n"
		"popq %0                \n"
		: "=r" (flags)
		: "r" (vmcs_physical_address)
		);

	return vmx_ret_val(flags);
}

void __vmx_off(void)
{
	__asm__ __volatile__ ("vmxoff");
}

/*
 * Interlocked primitives
 *
 * IF accumulator = DEST THEN
 *     ZF = 1;
 *     DEST = SRC;
 * ELSE
 *     ZF = 0;
 *     accumulator = DEST;
 * FI;
 */
int32_t gcc_interlocked_compare_exchange(volatile int32_t *destination,
					 int32_t exchange,
					 int32_t comperand)
{
	int32_t retval;


	__asm__ __volatile__ (
		"lock cmpxchgl  %2, (%1) \n"
		"movl %%eax, %0          \n"
		: "=r" (retval)
		: "r" (destination), "S" (exchange), "a" (comperand)
		);

	return retval;
}

int32_t gcc_interlocked_compare_exchange64(volatile int64_t *destination,
					   int64_t exchange,
					   int64_t comperand)
{
	int32_t retval;


	__asm__ __volatile__ (
		"lock cmpxchgq  %2, (%1) \n"
		"movl %%eax, %0          \n"
		: "=r" (retval)
		: "r" (destination), "S" (exchange), "a" (comperand)
		);

	return retval;
}

int64_t gcc_interlocked_compare_exchange_8(volatile int64_t *destination,
					   int64_t exchange,
					   int64_t comperand)
{
	int64_t retval;

	__asm__ __volatile__ (
		"lock cmpxchgq  %2, (%1) \n"
		"movq %%rax, %0          \n"
		: "=r" (retval)
		: "r" (destination), "S" (exchange), "a" (comperand)
		);

	return retval;
}

int32_t hw_interlocked_decrement(int32_t *minuend)
{
	int32_t orig_value;
	int32_t new_value;

	for (orig_value = *minuend;; orig_value = *minuend) {
		new_value = orig_value - 1;

		if (orig_value == gcc_interlocked_compare_exchange(minuend,
			    new_value,
			    orig_value)) {
			break;
		}
	}
	return new_value;
}

int32_t hw_interlocked_increment(int32_t *addend)
{
	int32_t orig_value;
	int32_t new_value;

	for (orig_value = *addend;; orig_value = *addend) {
		new_value = orig_value + 1;

		if (orig_value == gcc_interlocked_compare_exchange(addend,
			    new_value,
			    orig_value)) {
			break;
		}
	}
	return new_value;
}

int32_t hw_interlocked_increment64(int64_t *addend)
{
	int64_t orig_value;
	int64_t new_value;

	for (orig_value = *addend;; orig_value = *addend) {
		new_value = orig_value + 1;

		if (orig_value == gcc_interlocked_compare_exchange64(addend,
			    new_value,
			    orig_value)) {
			break;
		}
	}
	return new_value;
}

int32_t hw_interlocked_add(volatile int32_t *addend, int32_t value)
{
	int32_t orig_value;
	int32_t new_value;

	for (orig_value = *addend;; orig_value = *addend) {
		new_value = orig_value + value;

		if (orig_value == gcc_interlocked_compare_exchange(addend,
			    new_value,
			    orig_value)) {
			break;
		}
	}
	return new_value;
}

int32_t hw_interlocked_or(volatile int32_t *value, int32_t mask)
{
	int32_t orig_value;

	for (orig_value = *value;; orig_value = *value) {
		if (orig_value == gcc_interlocked_compare_exchange(value,
			    orig_value | mask,
			    orig_value)) {
			break;
		}
	}
	return orig_value;
}

int32_t hw_interlocked_and(volatile int32_t *value, int32_t mask)
{
	int32_t orig_value;

	for (orig_value = *value;; orig_value = *value) {
		if (orig_value == gcc_interlocked_compare_exchange(value,
			    orig_value & mask,
			    orig_value)) {
			break;
		}
	}
	return orig_value;
}

int32_t hw_interlocked_xor(volatile int32_t *value, int32_t mask)
{
	int32_t orig_value;

	for (orig_value = *value;; orig_value = *value) {
		if (orig_value == gcc_interlocked_compare_exchange(value,
			    orig_value ^ mask,
			    orig_value)) {
			break;
		}
	}
	return orig_value;
}

int32_t hw_interlocked_assign(volatile int32_t *target, int32_t new_value)
{
	int32_t ret;

	__asm__ __volatile__ (
		"lock xchgl %2, (%1)    \n"
		"movl %2, %0            \n"
		: "=r" (ret)
		: "r" (target), "r" (new_value)
		);

	return ret;
}

void hw_store_fence(void)
{
	__asm__ __volatile__ ("mfence");
}
