/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmx_cap.h"
#include "heap.h"
#include "hmm.h"
#include "host_cpu.h"
#include "dbg.h"
#include "vmm_arch.h"

#include "lib/util.h"

/****************************************************************************
*
* struct define
*
****************************************************************************/
typedef struct  {
	uint32_t init_pin;
	uint32_t init_proc1;
	uint32_t init_proc2;
	uint32_t init_exit;
	uint32_t init_entry;
	uint32_t init_pad;//add for aligning 64bit
	uint64_t init_cr0;
	uint64_t init_cr4;
} vmx_cap_t;

static vmx_cap_t g_vmx_cap;

/*------------------------------ macros            ------------------------ */
/*
 * VMX MSR Indexes
 */
#define MSR_VMX_BASIC                  0x480
#define MSR_VMX_PINBASED_CTRLS         0x481
#define MSR_VMX_PROCBASED_CTRLS        0x482
#define MSR_VMX_PROCBASED_CTRLS2       0x48B
#define MSR_VMX_EXIT_CTRLS             0x483
#define MSR_VMX_ENTRY_CTRLS            0x484
#define MSR_VMX_MISC                   0x485
#define MSR_VMX_CR0_FIXED0             0x486
#define MSR_VMX_CR0_FIXED1             0x487
#define MSR_VMX_CR4_FIXED0             0x488
#define MSR_VMX_CR4_FIXED1             0x489
#define MSR_VMX_VMCS_ENUM              0x48A
#define MSR_VMX_EPT_VPID_CAP           0x48C
#define MSR_VMX_TRUE_PINBASED_CTRLS    0x48D
#define MSR_VMX_TRUE_PROCBASED_CTRLS   0x48E
#define MSR_VMX_TRUE_EXIT_CTRLS        0x48F
#define MSR_VMX_TRUE_ENTRY_CTRLS       0x490

/*------------------------------ interface functions----------------------- */
uint64_t get_basic_cap()
{
	return asm_rdmsr(MSR_VMX_BASIC);
}

uint32_t get_pinctl_cap(uint32_t* p_may0)
{
	uint32_t pin_may0;
	uint32_t pin_may1;
	basic_info_t basic_info;

	basic_info.uint64 = get_basic_cap();
	if (basic_info.bits.use_true_msr){
		pin_may0 = asm_rdmsr_hl(MSR_VMX_TRUE_PINBASED_CTRLS,&pin_may1);
	}else{
		pin_may0 = asm_rdmsr_hl(MSR_VMX_PINBASED_CTRLS,&pin_may1);
	}
	if (p_may0)
		*p_may0 = pin_may0;

	return pin_may1;
}

uint32_t get_proctl1_cap(uint32_t* p_may0)
{
	uint32_t proc_may0;
	uint32_t proc_may1;
	basic_info_t basic_info;

	basic_info.uint64 = get_basic_cap();
	if(basic_info.bits.use_true_msr){
		proc_may0 = asm_rdmsr_hl(MSR_VMX_TRUE_PROCBASED_CTRLS,&proc_may1);
	}else{
		proc_may0 = asm_rdmsr_hl(MSR_VMX_PROCBASED_CTRLS,&proc_may1);
	}
	if (p_may0)
		*p_may0 = proc_may0;

	return proc_may1;
}

uint32_t get_proctl2_cap(uint32_t* p_may0)
{
	uint32_t proc2_may0;
	uint32_t proc2_may1;

	proc2_may0 = asm_rdmsr_hl(MSR_VMX_PROCBASED_CTRLS2,&proc2_may1);
	if (p_may0)
		*p_may0 = proc2_may0;

	return proc2_may1;
}

uint32_t get_exitctl_cap(uint32_t* p_may0)
{
	uint32_t exit_may0;
	uint32_t exit_may1;
	basic_info_t basic_info;

	basic_info.uint64 = get_basic_cap();
	if (basic_info.bits.use_true_msr){
		exit_may0 = asm_rdmsr_hl(MSR_VMX_TRUE_EXIT_CTRLS,&exit_may1);
	}else{
		exit_may0 = asm_rdmsr_hl(MSR_VMX_EXIT_CTRLS,&exit_may1);
	}
	if (p_may0)
		*p_may0 = exit_may0;

	return exit_may1;
}

uint32_t get_entryctl_cap(uint32_t* p_may0)
{
	uint32_t entry_may0;
	uint32_t entry_may1;
	basic_info_t basic_info;

	basic_info.uint64 = get_basic_cap();
	if (basic_info.bits.use_true_msr){
		entry_may0 = asm_rdmsr_hl(MSR_VMX_TRUE_ENTRY_CTRLS,&entry_may1);
	}else{
		entry_may0 = asm_rdmsr_hl(MSR_VMX_ENTRY_CTRLS,&entry_may1);
	}
	if (p_may0)
		*p_may0 = entry_may0;

	return entry_may1;
}

uint64_t get_misc_data_cap()
{
	return asm_rdmsr(MSR_VMX_MISC);
}

uint64_t get_ept_vpid_cap()
{
	return asm_rdmsr(MSR_VMX_EPT_VPID_CAP);
}

uint64_t get_cr0_cap(uint64_t* p_may0)
{
	uint64_t cr0_may0;
	uint64_t cr0_may1;

	cr0_may0 = asm_rdmsr(MSR_VMX_CR0_FIXED0);
	cr0_may1 = asm_rdmsr(MSR_VMX_CR0_FIXED1);
	if (p_may0)
		*p_may0 = cr0_may0;

	return cr0_may1;
}

uint64_t get_cr4_cap(uint64_t* p_may0)
{
	uint64_t cr4_may0;
	uint64_t cr4_may1;

	cr4_may0 = asm_rdmsr(MSR_VMX_CR4_FIXED0);
	cr4_may1 = asm_rdmsr(MSR_VMX_CR4_FIXED1);
	if (p_may0)
		*p_may0 = cr4_may0;

	return cr4_may1;
}

void vmx_cap_init()
{
	uint32_t ctrl_may0;
	uint32_t ctrl_may1;
	uint32_t must_have;
	uint32_t nice_have;
	uint64_t cr_may0;
	uint64_t cr_may1;
	msr_misc_data_t misc_data;
	basic_info_t basic_info;

	basic_info.uint64 = get_basic_cap();
	VMM_ASSERT_EX(basic_info.bits.memory_type == CACHE_TYPE_WB,
		"unexpected memory_type(%llu) for vmcs\n", basic_info.bits.memory_type);

	get_pinctl_cap(&ctrl_may0);
	g_vmx_cap.init_pin = ctrl_may0;

	ctrl_may1 = get_proctl1_cap(&ctrl_may0);
	must_have = PROC_INT_WINDOW_EXIT
		| PROC_NMI_WINDOW_EXIT
		| PROC_MSR_BITMAPS
		| PROC_SECONDARY_CTRL;
	VMM_ASSERT_EX((must_have & ctrl_may1) == must_have,
		"proc1: may1 = %x, must_have = %x\n", ctrl_may1, must_have);
	g_vmx_cap.init_proc1 = ctrl_may0
		| PROC_MSR_BITMAPS
		| PROC_SECONDARY_CTRL;

	ctrl_may1 = get_proctl2_cap(&ctrl_may0);
	must_have = PRO2C_ENABLE_EPT;
	VMM_ASSERT_EX((must_have & ctrl_may1) == must_have,
		"proc2: may1 = %x, must_have = %x\n", ctrl_may1, must_have);
	nice_have = PROC2_ENABLE_RDTSCP
		| PROC2_ENABLEC_VPID
		| PROC2_ENABLE_INVPCID
		| PROC2_ENABLE_XSAVE;
	g_vmx_cap.init_proc2 = ctrl_may0;
	g_vmx_cap.init_proc2 |= nice_have & ctrl_may1;

	ctrl_may1 = get_exitctl_cap(&ctrl_may0);
	must_have = EXIT_HOST_ADDR_SPACE
		| EXIT_SAVE_IA32_PAT
		| EXIT_LOAD_IA32_PAT
		| EXIT_SAVE_IA32_EFER
		| EXIT_LOAD_IA32_EFER
		| EXIT_SAVE_DBUG_CTRL;// used in vmexit_task_switch()
	VMM_ASSERT_EX((must_have & ctrl_may1) == must_have,
		"exit_ctrl: may1 = %x, must_have = %x\n", ctrl_may1, must_have);
	g_vmx_cap.init_exit = ctrl_may0 | must_have;

	ctrl_may1 = get_entryctl_cap(&ctrl_may0);
	must_have = ENTRY_GUEST_IA32E_MODE
		| ENTRY_LOAD_IA32_PAT
		| ENTRY_LOAD_IA32_EFER
		| ENTRY_LOAD_DBUG_CTRL;// used in vmexit_task_switch()
	VMM_ASSERT_EX((must_have & ctrl_may1) == must_have,
		"entry_ctrl: may1 = %x, must_have = %x\n", ctrl_may1, must_have);
	g_vmx_cap.init_entry = ctrl_may0
		| ENTRY_LOAD_IA32_PAT
		| ENTRY_LOAD_IA32_EFER;

	cr_may1 = get_cr0_cap(&cr_may0);
	must_have = CR0_PE
		| CR0_ET
		| CR0_NE
		| CR0_PG;
	VMM_ASSERT_EX((must_have & cr_may1) == must_have,
		"cr0: may1 = 0x%llX, must_have = 0x%X\n", cr_may1, must_have);
	g_vmx_cap.init_cr0 = cr_may0 | must_have;

	cr_may1 = get_cr4_cap(&cr_may0);
	must_have = CR4_PAE
		| CR4_VMXE;
	VMM_ASSERT_EX((must_have & cr_may1) == must_have,
		"cr4: may1 = 0x%llX, must_have = 0x%X\n", cr_may1, must_have);
	nice_have = CR4_OSXSAVE;// used in vmexit_xsetbv()
	g_vmx_cap.init_cr4 = cr_may0 | must_have;
	g_vmx_cap.init_cr4 |= nice_have & cr_may1;

	misc_data.uint64 = get_misc_data_cap();
#if (MAX_CPU_NUM > 1)
	VMM_ASSERT_EX(misc_data.bits.sipi,
		"wait-for-SIPI is not supported\n");
#endif
	VMM_ASSERT_EX(misc_data.bits.save_guest_mode,
		"auto-save of IA-32e mode guest in vmentry control is not supported.\n");
}

uint32_t get_init_pin(void)
{
	D(VMM_ASSERT(g_vmx_cap.init_pin));
	return g_vmx_cap.init_pin;
}

uint32_t get_init_proc1(void)
{
	D(VMM_ASSERT(g_vmx_cap.init_proc1));
	return g_vmx_cap.init_proc1;
}

uint32_t get_init_proc2(void)
{
	D(VMM_ASSERT(g_vmx_cap.init_proc2));
	return g_vmx_cap.init_proc2;
}

uint32_t get_init_exit(void)
{
	D(VMM_ASSERT(g_vmx_cap.init_exit));
	return g_vmx_cap.init_exit;
}

uint32_t get_init_entry(void)
{
	D(VMM_ASSERT(g_vmx_cap.init_entry));
	return g_vmx_cap.init_entry;
}

uint64_t get_init_cr0(void)
{
	D(VMM_ASSERT(g_vmx_cap.init_cr0));
	return g_vmx_cap.init_cr0;
}

uint64_t get_init_cr4(void)
{
	D(VMM_ASSERT(g_vmx_cap.init_cr4));
	return g_vmx_cap.init_cr4;
}

uint64_t vmcs_alloc()
{
	uint64_t hpa;
	uint32_t *hva = NULL;
	basic_info_t basic_info;

	basic_info.uint64 = get_basic_cap();
	/* allocate the VMCS area */
	/* the area must be 4K page aligned and zeroed */
	hva = (uint32_t *)page_alloc(1);
	memset((void *)hva, 0, PAGE_4K_SIZE);
	VMM_ASSERT_EX(hmm_hva_to_hpa((uint64_t)hva, &hpa, NULL),
		"hva(%p) to hpa conversion failed in %s\n", hva, __FUNCTION__);

	*hva =  basic_info.bits.rev_id;
	/* unmap VMCS region from the host memory */
	D(hmm_unmap_hpa(hpa,PAGE_4K_SIZE));

	return hpa;
}

#if DEBUG
#define CAP_IDX_START   MSR_VMX_BASIC
#define CAP_IDX_END     MSR_VMX_TRUE_ENTRY_CTRLS
#define SAVE_COUNT      (CAP_IDX_END - CAP_IDX_START + 1)

static uint64_t bsp_vmx_cap[SAVE_COUNT];

static void vmx_bsp_save()
{
	uint32_t msr_id;

	for(msr_id = CAP_IDX_START; msr_id <= CAP_IDX_END; msr_id ++)
	{
		bsp_vmx_cap [msr_id-CAP_IDX_START] = asm_rdmsr(msr_id);
	}
}

static void vmx_ap_check()
{
	uint32_t msr_id;

	for(msr_id = CAP_IDX_START; msr_id <= CAP_IDX_END; msr_id ++)
	{
		VMM_ASSERT_EX((bsp_vmx_cap [msr_id-CAP_IDX_START] == asm_rdmsr(msr_id)),
			"cpu(%d) cap(0x%x) copmared failed with cpu0 in %s\n", host_cpu_id(), msr_id, __FUNCTION__);
	}
}

void vmx_cap_check()
{
	uint16_t cpu_id = host_cpu_id();

	if(cpu_id == 0)
	{
		vmx_bsp_save();
	}else{
		vmx_ap_check();
	}
}
#endif
