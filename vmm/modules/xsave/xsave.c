/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "guest.h"
#include "gcpu.h"
#include "host_cpu.h"
#include "dbg.h"
#include "event.h"
#include "vmm_asm.h"
#include "vmm_arch.h"
#include "vmx_cap.h"
#include "heap.h"

#include "lib/util.h"

#include "modules/xsave.h"

typedef struct xsave_info {
	guest_cpu_handle_t gcpu;
	void *xsave_area;
	uint64_t guest_xcr0;
	struct xsave_info *next;
} xsave_info_t;

static xsave_info_t *g_xsave;
static vmm_lock_t xsave_lock;
static uint64_t host_xcr0;

#ifdef DEBUG
static boolean_t xsave_is_supported()
{
	cpuid_params_t cpuid_params = {1, 0 ,0 ,0};
	asm_cpuid(&cpuid_params);
	if ((cpuid_params.ecx & CPUID_ECX_XSAVE) == 0) {
		return FALSE;
	}
	return TRUE;
}
#endif

static xsave_info_t *xsave_lookup(guest_cpu_handle_t gcpu)
{
	xsave_info_t *p_xsave;

	lock_acquire_read(&xsave_lock);
	p_xsave = g_xsave;
	while (p_xsave) {
		if (p_xsave->gcpu == gcpu)
			break;
		p_xsave = p_xsave->next;
	}

	lock_release(&xsave_lock);
	return p_xsave;
}

static uint64_t xsave_get_max_xcr0()
{
	cpuid_params_t cpuid_params = {0xd, 0, 0, 0};

	asm_cpuid(&cpuid_params);

	/*Since PT state is not supported in xcr0. if we set the PT state to 1 in xcr0, it will cause a #GP.
		so, we have to clear this bit.*/
	return  (MAKE64(cpuid_params.edx, cpuid_params.eax) & (~CPUID_XSS));
}

static boolean_t xsave_check_components(uint64_t components)
{
	if ((xsave_get_max_xcr0() & components) == components) {
		return TRUE;
	}
	return FALSE;
}

static inline void xsave_set_xcr0(uint64_t value)
{
	asm_xsetbv(0, value);
}

static inline uint64_t xsave_get_xcr0()
{
	return  asm_xgetbv(0);
}

static void xsave_swap_in(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	xsave_info_t *xsave;

	xsave = xsave_lookup(gcpu);
	if(xsave == NULL)
	{
		xsave = (xsave_info_t *)mem_alloc(sizeof(xsave_info_t));
		/*xsave area needs to be 64 bytes aligned. otherwise, it will cause a #GP*/
		xsave->xsave_area = page_alloc(1);
		memset(xsave->xsave_area, 0, PAGE_4K_SIZE);
		xsave->gcpu = gcpu;
		lock_acquire_write(&xsave_lock);
		xsave->next = g_xsave;
		g_xsave = xsave;
		lock_release(&xsave_lock);
	}else{
		xsave_set_xcr0(host_xcr0);
		asm_xrstor(xsave->xsave_area, host_xcr0);
		xsave_set_xcr0(xsave->guest_xcr0);
	}
}

static void xsave_swap_out(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	xsave_info_t *xsave;

	xsave = xsave_lookup(gcpu);
	D(VMM_ASSERT(xsave));
	D(VMM_ASSERT(xsave->xsave_area));

	xsave->guest_xcr0 = xsave_get_xcr0();
	xsave_set_xcr0(host_xcr0);
	asm_xsave(xsave->xsave_area,host_xcr0);
}

/* CR4.OSXSAVE has already been set in get_init_cr4() if supported*/
void xsave_isolation_init(uint64_t components)
{
	D(VMM_ASSERT_EX(xsave_is_supported(),
		"xsave is not supported\n"));
	VMM_ASSERT_EX((get_cr4_cap(NULL) & CR4_OSXSAVE),
		"OSXSAVE in cr4 is not supported by vmx cr4_may1\n");

	if(components)
	{
		host_xcr0 = components;
		VMM_ASSERT_EX(xsave_check_components(components),
		"components(0x%llX) can not be supported by cpu(0x%llX)\n",
		components, xsave_get_max_xcr0());
	}else{//If components is 0, max xcr0 will be used to isolate all components supported
		host_xcr0 = xsave_get_max_xcr0();
		print_trace("xsave state max =0x%llX\n", host_xcr0);
	}

	lock_init(&xsave_lock, "xsave_lock");
	event_register(EVENT_GCPU_SWAPIN, xsave_swap_in);
	event_register(EVENT_GCPU_SWAPOUT, xsave_swap_out);
}
