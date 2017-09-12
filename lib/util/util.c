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

#include "vmm_base.h"
#include "vmm_asm.h"
#include "vmm_arch.h"

#include "lib/util.h"

/* TSC in KHz */
uint64_t tsc_per_ms;

/* with tests we found that, using "stosb" to set 1 page is
 * a little bit quicker than "stosq" in most cases
 */
void memset(void *dest, uint8_t val, uint64_t count)
{
	__asm__ __volatile__ (
		"cld        \n\t"
		"rep stosb  \n\t"
		:: "D" (dest), "a" (val), "c" (count)
		);
	return;
}

/* with tests we found that, using "movsb" to copy 1 page is
 * a little bit quicker than "movsq" in most cases
 */
void memcpy(void *dest, const void *src, uint64_t count)
{
	if (dest < src) {
		__asm__ __volatile__ (
			"cld        \n\t"
			"rep movsb  \n\t"
			:: "D" (dest), "S" (src), "c" (count)
			);
	}else {
		__asm__ __volatile__ (
			"std        \n\t"
			"rep movsb  \n\t"
			"cld        \n\t"
			:: "D" ((uint64_t)dest + count - 1), "S" ((uint64_t)src + count - 1), "c" (count)
			);
	}
	return;
}

uint32_t lock_inc32(volatile uint32_t *addr)
{
	uint32_t retval = 1;

	__asm__ __volatile__ (
		"lock xadd  %2, (%1)"
		: "=a" (retval)
		: "r" (addr) ,"a" (retval)
		);

	return retval + 1;
}

#define FEATURE_CONTROL_LOCK            (1 << 0)
#define FEATURE_CONTROL_VMX_OUT_SMX     (1 << 2)

boolean_t check_vmx(void)
{
	//CPUID[EAX=1] should have VMX feature == 1
	cpuid_params_t cpuid_params = {1, 0, 0, 0};
	uint64_t feature_msr;

	asm_cpuid(&cpuid_params);
	if ((cpuid_params.ecx & CPUID_ECX_VMX) == 0) {
		return FALSE;
	}

	/* MSR_FEATURE_CONTROL should have
	 * either enable_vmx_outside_smx == 1 or
	 * Lock == 0 */

	feature_msr = asm_rdmsr(MSR_FEATURE_CONTROL);
	if (feature_msr & FEATURE_CONTROL_LOCK)
	{
		if((feature_msr & FEATURE_CONTROL_VMX_OUT_SMX) == 0)
			return FALSE;
	}else{
		feature_msr |= FEATURE_CONTROL_VMX_OUT_SMX
			| FEATURE_CONTROL_LOCK;
		asm_wrmsr(MSR_FEATURE_CONTROL, feature_msr);
	}
	return TRUE;
}

void wait_us(uint64_t us)
{
	uint64_t end_tsc;

	end_tsc = asm_rdtsc() + (us * tsc_per_ms / 1000ULL);

	while (asm_rdtsc() < end_tsc)
		asm_pause();
}

#ifdef STACK_PROTECTOR
uint64_t get_stack_cookie_value(void)
{
	uint64_t cookie;

	__asm__ volatile (
		"mov %%fs:0x28, %0\n\t"
		: "=r"(cookie)
	);

	return cookie;
}
#endif
