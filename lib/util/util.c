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

#define FEATURE_CONTROL_LOCK            (1ULL << 0)
#define FEATURE_CONTROL_VMX_OUT_SMX     (1ULL << 2)

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

/* Determin TSC frequency(Hz) */
uint64_t determine_nominal_tsc_freq(void)
{
	/*
	 * CPUID: Time Stamp Counter and Nominal Core Crystal Clock Information Leaf
	 * Input:
	 *     EAX: 0x15
	 * Output:
	 *     EAX: denominator of the TSC/"core crystal clock" ratio
	 *     EBX: numerator of the TSC/"core crystal clock" ratio
	 *     ECX: core crystal clock in Hz
	 *     EDX: reserved = 0
	 */
	cpuid_params_t cpuid = {0x15, 0, 0, 0};

	asm_cpuid(&cpuid);

	if ((cpuid.eax == 0) || (cpuid.ebx == 0) || (cpuid.ecx == 0))
		return 0;

	/* TSC frequency = "core crystal clock frequency" * EBX / EAX */
	return (uint64_t)((uint64_t)cpuid.ecx * (uint64_t)cpuid.ebx / (uint64_t)cpuid.eax);
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

void save_current_cpu_state(gcpu_state_t *s)
{
	asm_sgdt(&(s->gdtr));
	asm_sidt(&(s->idtr));
	s->cr0 = asm_get_cr0();
	s->cr3 = asm_get_cr3();
	s->cr4 = asm_get_cr4();

	s->msr_efer = asm_rdmsr(MSR_EFER);

	/* The selector of LDTR in current environment is invalid which indicates
	 * the bootloader is not using LDTR. So set LDTR unusable here. In
	 * future, exception might occur if LDTR is used in bootloader. Then bootloader
	 * will find us since we changed LDTR to 0, and we can fix it for that bootloader. */
	fill_segment(&s->segment[SEG_LDTR], 0, 0, 0x10000, 0);
	/* TSS is used for RING switch, which is usually not used in bootloader since
	 * bootloader always runs in RING0. So we hardcode TR here. In future, #TS
	 * might occur if TSS is used bootloader. Then bootlaoder will find us since we
	 * changed TR to 0, and we can fix it for that bootlaoder. */
	fill_segment(&s->segment[SEG_TR], 0, 0xffffffff, 0x808b, 0);
	/* For segments: get selector from current environment, selector of ES/FS/GS are from DS,
	 * hardcode other fields to make guest launch successful. */
	fill_segment(&s->segment[SEG_CS], 0, 0xffffffff, 0xa09b, asm_get_cs());
	fill_segment(&s->segment[SEG_DS], 0, 0xffffffff, 0xc093, asm_get_ds());
	fill_segment(&s->segment[SEG_ES], 0, 0xffffffff, 0xc093, asm_get_ds());
	fill_segment(&s->segment[SEG_FS], 0, 0xffffffff, 0xc093, asm_get_ds());
	fill_segment(&s->segment[SEG_GS], 0, 0xffffffff, 0xc093, asm_get_ds());
	fill_segment(&s->segment[SEG_SS], 0, 0xffffffff, 0xc093, asm_get_ds());
}
