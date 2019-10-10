/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "dbg.h"
#include "vmm_objects.h"
#include "gcpu.h"
#include "vmx_cap.h"
#include "scheduler.h"
#include "guest.h"
#include "vmm_arch.h"
#include "vmcs.h"
#include "gpm.h"
#include "hmm.h"
#include "event.h"

#include "modules/vmenter_check.h"

#define MSR_DEBUGCTL_LBR                   (1ull << 0)
#define MSR_DEBUGCTL_BTF                   (1ull << 1)
#define MSR_DEBUGCTL_TR                    (1ull << 6)
#define MSR_DEBUGCTL_BTS                   (1ull << 7)
#define MSR_DEBUGCTL_BTINT                 (1ull << 8)
#define MSR_DEBUGCTL_BTS_OFF_OS            (1ull << 9)
#define MSR_DEBUGCTL_BTS_OFF_USR           (1ull << 10)
#define MSR_DEBUGCTL_FREEZE_LBRS_ON_PMI    (1ull << 11)
#define MSR_DEBUGCTL_FREEZE_PERFMON_ON_PMI (1ull << 12)
#define MSR_DEBUGCTL_FREEZE_WHILE_SMM_EN   (1ull << 14)
#define MSR_DEBUGCTL_RESERVED                                           \
	~(MSR_DEBUGCTL_LBR                                                  \
	| MSR_DEBUGCTL_BTF                                                \
	| MSR_DEBUGCTL_TR                                                 \
	| MSR_DEBUGCTL_BTS                                                \
	| MSR_DEBUGCTL_BTINT                                              \
	| MSR_DEBUGCTL_BTS_OFF_OS                                         \
	| MSR_DEBUGCTL_BTS_OFF_USR                                        \
	| MSR_DEBUGCTL_FREEZE_LBRS_ON_PMI                                 \
	| MSR_DEBUGCTL_FREEZE_PERFMON_ON_PMI                              \
	| MSR_DEBUGCTL_FREEZE_WHILE_SMM_EN)

#define MSR_PERF_GLOBAL_CTRL_PMC0          (1ull << 0)
#define MSR_PERF_GLOBAL_CTRL_PMC1          (1ull << 1)
#define MSR_PERF_GLOBAL_CTRL_FIXED_CTR0    (1ull << 31)
#define MSR_PERF_GLOBAL_CTRL_FIXED_CTR1    (1ull << 32)
#define MSR_PERF_GLOBAL_CTRL_FIXED_CTR2    (1ull << 33)
#define MSR_PERF_GLOBAL_CTRL_RESERVED                                   \
	(MSR_PERF_GLOBAL_CTRL_PMC0                                          \
	 | MSR_PERF_GLOBAL_CTRL_PMC1                                        \
	 | MSR_PERF_GLOBAL_CTRL_FIXED_CTR0                                  \
	 | MSR_PERF_GLOBAL_CTRL_FIXED_CTR1                                  \
	 | MSR_PERF_GLOBAL_CTRL_FIXED_CTR2)

#define MSR_EFER_RESERVED                        \
	~(EFER_SCE                                   \
	 | EFER_LME                                  \
	 | EFER_LMA                                  \
	 | EFER_NXE)

#define INTR_RESERVED                        \
	~(INTR_BLK_BY_STI                        \
	 | INTR_BLK_BY_MOV_SS                    \
	 | INTR_BLK_BY_SMI                       \
	 | INTR_BLK_BY_NMI                       \
	 | INTR_ENCLAVE_MODE)

#define NRS_PENDING_DEBG_BS_BIT      (1ull << 14)
#define NRS_PENDING_DEBG_RTM_BIT     (1ull << 16)


#define PAE_PDPTE_PRESENT            (1ull << 0)
#define PAE_PDPTE_RESV_21_85_BITS    0x1E6      // bits 2:1 and bits 8:5 are reserved

#define MSR_SIGN_BBL_CR_D3           0X8B
#define MSR_SMM_MONITOR_CTL          0x9B

static const char *segments_name[] = {"CS","DS","SS","ES","FS","GS","LDTR","TR"};
#define SEGMENT_NAME(__id)           segments_name[__id]

static inline boolean_t  is_zero_upper(uint64_t addr)
{
	if((addr >> 32) == 0)
	{
		return TRUE;
	}
	return FALSE;
}

static boolean_t is_compliant(uint64_t value, uint64_t may0, uint64_t may1)
{
	if (((value & may0) == may0) && ((value | may1) == may1))
	{
		return TRUE;
	}
	return FALSE;
}

static void check_guest_cr0(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.1:
	 **  The CR0 field must not set any bit to a value not supported
	 **  in VMX operation.
	 **  Bit 0 and bit 31 are not checked if the ug is 1.
	 **  Bit 29 and bit 30 are never checked because the values of
	 **  these bits are not changed by VM entry.
	 **  If bit 31 in the CR0 field is 1, bit 0 in that field must also be 1.
	 */
	uint64_t cr0_mask;
	uint64_t cr0_may0;
	uint64_t cr0_may1;

	uint64_t cr0 = vmcs_read(vmcs, VMCS_GUEST_CR0);
	uint32_t proc_ctrl2 = (uint32_t) vmcs_read(vmcs, VMCS_PROC_CTRL2);
	cr0_may1 = get_cr0_cap(&cr0_may0);

	if(proc_ctrl2 & PROC2_UNRESTRICTED_GUEST)
	{
		cr0_mask = CR0_PG | CR0_PE | CR0_NW | CR0_CD;
	}
	else
	{
		cr0_mask = CR0_NW | CR0_CD;
	}

	if (is_compliant(cr0, (cr0_may0 & ~cr0_mask), (cr0_may1 | cr0_mask)) == FALSE)
	{
		print_info("%s is not compliant with VMX requirements.\n", "Guest CR0");
	}

	if (cr0 & CR0_PG)
	{
		if((cr0 & CR0_PE) == 0)
		{
			print_info("CR0_PE must be 1\n");
		}
	}
}

static void check_guest_cr4(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.1:
	 ** The CR4 field must not set any bit to a value
	 ** not supported in VMX operation
	 */
	uint64_t cr4_may0;
	uint64_t cr4_may1;
	uint64_t cr4 = vmcs_read(vmcs, VMCS_GUEST_CR4);
	cr4_may1 = get_cr4_cap(&cr4_may0);

	if (is_compliant(cr4, cr4_may0, cr4_may1) == FALSE)
	{
		print_info("%s is not compliant with VMX requirements.\n", "Guest CR4");
	}
}

static void check_guest_debugctl(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.1:
	 ** If the "load debug controls" VM-entry control is 1, bits reserved in the
	 ** IA32_DEBUGCTL MSR must be 0 in the field for that register.
	 */
	uint32_t vmentry_control;
	boolean_t is_valid = TRUE;

	vmentry_control = (uint32_t) vmcs_read(vmcs, VMCS_ENTRY_CTRL);
	if (vmentry_control & ENTRY_LOAD_DBUG_CTRL)
	{
		uint64_t debugctl_msr = vmcs_read(vmcs, VMCS_GUEST_DBGCTL);
		is_valid = (0 == (debugctl_msr & MSR_DEBUGCTL_RESERVED));
		if ( ! is_valid)
		{
			print_info("Guest DEBUGCTLS.Reserved must be %d\n", 0);
		}
	}
}

static void check_guest_64bits_settings(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.1:
	 **  The following checks are performed on processors that support Intel 64 architecture:
	 **   If the "IA-32e mode guest" VM-entry control is 1, bit 31 (CR0.PG) in CR0 field
	 **   and bit 5 (CR4.PAE) in the CR4 field must be 1.
	 **   If the "IA-32e mode guest" VM-entry control is 0, bit 17 (CR4.PCIDE) in CR4 field
	 **   must be 0.
	 **   The CR3 field must be such that bits 63:52 and bits in the range 51:32
	 **   beyond the processor's physical-address width are 0.
	 **   If the "load debug controls" VM-entry control is 1, bits 63:32 in the DR7 field
	 **   must be 0.
	 **   The IA32_SYSENTER_ESP field and the IA32_SYSENTER_EIP field must each
	 **   contain a canonical address.
	 */
	boolean_t is_64bit;
	boolean_t load_debg;

	uint64_t cr3 = vmcs_read(vmcs, VMCS_GUEST_CR3);
	uint64_t cr4 = vmcs_read(vmcs, VMCS_GUEST_CR4);
	uint64_t dr7 = vmcs_read(vmcs, VMCS_GUEST_DR7);
	uint64_t sysenter_eip = vmcs_read(vmcs, VMCS_GUEST_SYSENTER_EIP);
	uint64_t sysenter_esp = vmcs_read(vmcs, VMCS_GUEST_SYSENTER_ESP);
	is_64bit  = ((vmcs_read(vmcs, VMCS_ENTRY_CTRL) & ENTRY_GUEST_IA32E_MODE) != 0);
	load_debg = ((vmcs_read(vmcs, VMCS_ENTRY_CTRL) & ENTRY_LOAD_DBUG_CTRL) != 0);

	if (is_64bit)
	{
		uint64_t cr0 = vmcs_read(vmcs, VMCS_GUEST_CR0);

		if ((cr0 & CR0_PG) == 0)
		{
			print_info("Guest CR0.PG must be %d\n", 1);
		}

		if ((cr4 & CR4_PAE) == 0)
		{
			print_info("Guest CR4.PAE must be %d\n", 1);
		}

		if(cr3 >= (1ull << 48))
		{
			print_info("Guest CR3 high bits must be 0 in 64bits\n");
		}
	}else{
		if ((cr4 & CR4_PCIDE) == CR4_PCIDE)
		{
			print_info("Guest CR4.PCIDE must be %d in 32bit guest\n", 0);
		}

		if(is_zero_upper(cr3) == FALSE)
		{
			print_info("Guest CR3 high bits must be 0 in 32 bits\n");
		}
	}

	if (load_debg)
	{
		if(is_zero_upper(dr7) == FALSE)
		{
			print_info("Guest DR7[63..32] must be %d\n", 0);
		}
	}

	if(addr_is_canonical(is_64bit, sysenter_esp) == FALSE)
	{
		print_info("Guest %s must be canonical\n", "SYSENTER ESP");
	}

	if(addr_is_canonical(is_64bit, sysenter_eip) == FALSE)
	{
		print_info("Guest %s must be canonical\n", "SYSENTER EIP ");
	}
}

static void check_guest_perf_global_ctrl(vmcs_obj_t vmcs)
{
	/*
	** According to IA32 Manual: Volume 3, Chapter 26.3.1.1:
	**  If the "load IA32_PERF_GLOBAL_CTRL" VM-entry control is 1, bits reserved in the
	**  IA32_PERF_GLOBAL_CTRL MSR must be 0 in the field for that register.
	*/

	uint32_t vmentry_control;
	boolean_t is_valid = TRUE;

	vmentry_control = (uint32_t) vmcs_read(vmcs, VMCS_ENTRY_CTRL);
	if (vmentry_control & ENTRY_LOAD_IA32_PERF_CTRL)
	{
		uint64_t perf_global_ctl_msr = vmcs_read(vmcs, VMCS_GUEST_PERF_G_CTRL);
		is_valid = (0 == (perf_global_ctl_msr & MSR_PERF_GLOBAL_CTRL_RESERVED));
		if ( ! is_valid)
		{
			print_info("Guest PERF_GLOBSL_CTRL.Reserved must be %d\n", 0);
		}
	}
}

static void check_guest_pat(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.1:
	 **  If the "load IA32_PAT" VM-entry control is 1, each of
	 **  the 8 bytes in the field must have one of the values 0 (UC),
	 **  1 (WC), 4 (WT), 5 (WP), 6 (WB), or 7 (UC-).
	 */
	uint32_t vmentry_control;
	boolean_t is_valid = TRUE;

	vmentry_control = (uint32_t) vmcs_read(vmcs, VMCS_ENTRY_CTRL);
	if (vmentry_control & ENTRY_LOAD_IA32_PAT)
	{
		union {
			uint64_t u64;
			uint8_t  u8[8];
		} guest_pat;

		uint32_t i;

		guest_pat.u64 = vmcs_read(vmcs, VMCS_GUEST_PAT);

		for (i = 0; i < 8; ++i)
		{
			if (0 == guest_pat.u8[i]
				|| 1 == guest_pat.u8[i]
				|| 4 == guest_pat.u8[i]
				|| 5 == guest_pat.u8[i]
				|| 6 == guest_pat.u8[i]
				|| 7 == guest_pat.u8[i])
			{
				continue;
			}
			// if reached this point, then PAT is bad
			is_valid = FALSE;
		}

		if ( ! is_valid)
		{
			print_info("Guest PAT is invalid\n");
		}
	}
}

static void check_guest_efer(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.1:
	 **  If the "load IA32_EFER" VM-entry control is 1,the values of the lma and
	 **  lme bits in the field must each be that of the "IA-32e mode guest" VM-entry
	 **  control.
	 */
	uint32_t vmentry_control;
	uint64_t cr0 = vmcs_read(vmcs, VMCS_GUEST_CR0);
	uint64_t is_64bit;
	boolean_t is_valid = TRUE;

	vmentry_control = (uint32_t) vmcs_read(vmcs, VMCS_ENTRY_CTRL);
	if (vmentry_control & ENTRY_LOAD_IA32_EFER)
	{
		msr_efer_t guest_efer;

		guest_efer.uint64 = vmcs_read(vmcs, VMCS_GUEST_EFER);
		is_64bit = ((vmentry_control & ENTRY_GUEST_IA32E_MODE)!=0);
		is_valid = (0 == (guest_efer.uint64 & MSR_EFER_RESERVED));

		if ( ! is_valid){
			print_info("The efer reserved bits must be 0\n");
		}

		if (guest_efer.bits.lma != is_64bit)
		{
			print_info("The value of guest_efer.bits.lma must be ENTRY_GUEST_IA32E_MODE\n");
		}

		if(cr0 & CR0_PG)
		{
			if (guest_efer.bits.lme != is_64bit)
			{
				print_info("The value of guest_efer.bits.lme must be ENTRY_GUEST_IA32E_MODE\n");
			}
		}
	}
}

static void check_guest_ctl_dbug_regs_msrs(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.1:
	 **  The following checks are performed on fields in the guest-state area
	 **  corresponding to control register, debug register, and MSRs.
	 */
	check_guest_cr0(vmcs);
	check_guest_cr4(vmcs);
	check_guest_debugctl(vmcs);
	check_guest_64bits_settings(vmcs);
	check_guest_perf_global_ctrl(vmcs);
	check_guest_pat(vmcs);
	check_guest_efer(vmcs);

	// TODO: The "load IA32_BNDCFGS" will not be used, so we do not check it here.
}

static void check_segment_selector(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.2:
	 ** Selector field:
	 **  TR. the TI flag (bit 2) must be 0.
	 **  LDTR. If LDTR is usable, the TI flag (bit 2)must be 0.
	 **  SS. If the guest will not be virtual-8086 and the ug control is 0,
	 **  the RPL (bits 1:0) must equal the RPL of the selector field for CS.
	 */
	seg_ar_t ldtr_ar;
	boolean_t  is_ug;
	boolean_t  is_vr8086;
	uint16_t  cs_sel, ss_sel, tr_sel, ldtr_sel;

	cs_sel   = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_CS_SEL);
	ss_sel   = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_SS_SEL);
	tr_sel   = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_TR_SEL);
	ldtr_sel = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_LDTR_SEL);
	ldtr_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_LDTR_AR);
	is_ug = ((vmcs_read(vmcs, VMCS_PROC_CTRL2) & PROC2_UNRESTRICTED_GUEST) != 0);
	is_vr8086 = ((vmcs_read(vmcs, VMCS_GUEST_RFLAGS) & RFLAGS_VM) != 0);

	if (tr_sel & 4)
	{
		print_info("Guest TR.selector TI flag must be zero\n");
	}

	if(ldtr_ar.bits.null_bit == 0)
	{
		if (ldtr_sel & 4)
		{
			print_info("Guest LDTR.selector TI flag must be zero\n");
		}
	}

	if((!is_ug) && (!is_vr8086))
	{
		if ((ss_sel & 3) != (cs_sel & 3))
		{
			print_info("Guest SS.RPL must be equal to CS.RPL\n");
		}
	}
}

static void check_base_virtual_8068_mode(segment_t segment, seg_id_t seg)
{
	if (segment.base != ((uint64_t) segment.selector << 4))
	{
		print_info("For virtual-8086 mode guest %s.base must be equal to cs.selector x 16\n",
			SEGMENT_NAME(seg));
	}
}

static void check_segment_base_address(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.2:
	 ** Base address field:
	 **  CS, SS, DS, ES, FS, GS:
	 **   If the guest will be virtual-8086, the address must be
	 **   the selector field shifted left 4 bits (multiplied by 16).
	 **  The checks are performed on processors that support Intel 64 architecture:
	 **  TR, FS, GS:
	 **   The address must be canonical.
	 **  LDTR:
	 **   If LDTR is usable, the address must be canonical.
	 **  CS:
	 **   bits 63:32 of the address must be zero.
	 **  SS, DS, ES:
	 **   If the register is usable, bits 63:32 of the address must be zero.
	 */
	seg_ar_t ss_ar;
	seg_ar_t ds_ar;
	seg_ar_t es_ar;
	seg_ar_t ldtr_ar;
	boolean_t  is_64bit;
	boolean_t  is_vr8086;
	segment_t  cs;
	segment_t  ss;
	segment_t  ds;
	segment_t  es;
	segment_t  fs;
	segment_t  gs;
	segment_t  tr;
	segment_t  ldtr;

	cs.base = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_CS_BASE);
	ss.base = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_SS_BASE);
	ds.base = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_DS_BASE);
	es.base = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_ES_BASE);
	fs.base = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_FS_BASE);
	gs.base = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_GS_BASE);
	cs.selector = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_CS_SEL);
	ss.selector = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_SS_SEL);
	ds.selector = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_DS_SEL);
	es.selector = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_ES_SEL);
	fs.selector = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_FS_SEL);
	gs.selector = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_GS_SEL);
	ss_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_SS_AR);
	ds_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_DS_AR);
	es_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_ES_AR);
	ldtr_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_LDTR_AR);
	is_vr8086 = ((vmcs_read(vmcs, VMCS_GUEST_RFLAGS) & RFLAGS_VM) != 0);
	is_64bit = ((vmcs_read(vmcs, VMCS_ENTRY_CTRL) & ENTRY_GUEST_IA32E_MODE) != 0);

	if (is_vr8086)
	{
		check_base_virtual_8068_mode(cs, SEG_CS);
		check_base_virtual_8068_mode(ss, SEG_SS);
		check_base_virtual_8068_mode(ds, SEG_DS);
		check_base_virtual_8068_mode(es, SEG_ES);
		check_base_virtual_8068_mode(fs, SEG_FS);
		check_base_virtual_8068_mode(gs, SEG_GS);
	}

	if (FALSE == addr_is_canonical(is_64bit, tr.base))
	{
		print_info("Guest tr.base must be canonical\n");
	}

	if (FALSE == addr_is_canonical(is_64bit, fs.base))
	{
		print_info("Guest fs.base must be canonical\n");
	}

	if (FALSE == addr_is_canonical(is_64bit, gs.base))
	{
		print_info("Guest gs.base must be canonical\n");
	}

	if (ldtr_ar.bits.null_bit == 0)
	{
		if (FALSE == addr_is_canonical(is_64bit, ldtr.base))
		{
			print_info("Guest ldtr.base must be canonical\n");
		}
	}

	if (is_zero_upper(cs.base) == FALSE)
	{
		print_info("Guest cs.base bit 63:32 must be zero\n");
	}

	if (ss_ar.bits.null_bit == 0)
	{
		if (is_zero_upper(ss.base) == FALSE)
		{
			print_info("Guest ss.base bit 63:32 must be zero\n");
		}
	}

	if (ds_ar.bits.null_bit == 0)
	{
		if (is_zero_upper(ds.base) == FALSE)
		{
			print_info("Guest ds.base bit 63:32 must be zero\n");
		}
	}

	if (es_ar.bits.null_bit == 0)
	{
		if (is_zero_upper(es.base) == FALSE)
		{
			print_info("Guest es.base bit 63:32 must be zero\n");
		}
	}
}

static void check_limit_virtual_8068_mode(uint64_t limit, seg_id_t seg)
{
	if (limit != 0xFFFFULL)
	{
		print_info("For virtual-8086 mode guest %s.limit must be equal to 0xFFFF\n",
			SEGMENT_NAME(seg));
	}
}
static void check_segment_limit(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.2:
	 **  Limit filed:
	 **   CS, SS, DS, ES, FS, GS:
	 **   If the guest will be virtual-8086, the limit filed must be 0000FFFFH.
	 */
	uint64_t cs_limit;
	uint64_t ss_limit;
	uint64_t ds_limit;
	uint64_t es_limit;
	uint64_t fs_limit;
	uint64_t gs_limit;
	boolean_t   is_vr8086;

	cs_limit = vmcs_read(vmcs, VMCS_GUEST_CS_LIMIT);
	ss_limit = vmcs_read(vmcs, VMCS_GUEST_SS_LIMIT);
	ds_limit = vmcs_read(vmcs, VMCS_GUEST_DS_LIMIT);
	es_limit = vmcs_read(vmcs, VMCS_GUEST_ES_LIMIT);
	fs_limit = vmcs_read(vmcs, VMCS_GUEST_FS_LIMIT);
	gs_limit = vmcs_read(vmcs, VMCS_GUEST_GS_LIMIT);
	is_vr8086 = ((vmcs_read(vmcs, VMCS_GUEST_RFLAGS) & RFLAGS_VM) != 0);

	if(is_vr8086)
	{
		check_limit_virtual_8068_mode(cs_limit, SEG_CS);
		check_limit_virtual_8068_mode(ss_limit, SEG_SS);
		check_limit_virtual_8068_mode(ds_limit, SEG_DS);
		check_limit_virtual_8068_mode(es_limit, SEG_ES);
		check_limit_virtual_8068_mode(fs_limit, SEG_FS);
		check_limit_virtual_8068_mode(gs_limit, SEG_GS);
	}
}

static void check_ar_virtual_8068_mode(uint32_t ar, seg_id_t seg)
{
	switch (seg)
	{
		case SEG_CS:
		case SEG_SS:
		case SEG_DS:
		case SEG_ES:
		case SEG_FS:
		case SEG_GS:
			if (ar != 0xF3)
			{
				print_info("For virtual-8086 mode guest %s.ar must be equal to 0xF3\n", SEGMENT_NAME(seg));
			}
			break;

		default:
			print_info("ERRO: invalid segment id (%d) in %s().\n", seg, __FUNCTION__);
			break;
	}
}

static void check_ar_type_bit(boolean_t is_ug, seg_ar_t ar, seg_id_t seg)
{
	switch (seg)
	{
		case SEG_CS:
			if(is_ug)
			{
				if (ar.bits.type != 3 &&
					ar.bits.type != 9  &&
					ar.bits.type != 11 &&
					ar.bits.type != 13 &&
					ar.bits.type != 15) {
						print_info("Guest segment cs type must be 3, 9,11,13,or 15 ");
						print_info("if unrestricted guest control is 1\n");
				}
			}else{
				if (ar.bits.type != 9 &&
					ar.bits.type != 11 &&
					ar.bits.type != 13 &&
					ar.bits.type != 15) {
						print_info("Guest segment cs type must be 9,11,13,or 15 ");
						print_info("if unrestricted guest control is 0\n");
				}
			}
			break;

		case SEG_SS:
			if (ar.bits.null_bit == 0)
			{
				if (ar.bits.type != 3 && ar.bits.type != 7)
				{
					print_info("Guest ss.ar type must be 3 or 7\n");
				}
			}
			break;

		case SEG_DS:
		case SEG_ES:
		case SEG_FS:
		case SEG_GS:
		if(ar.bits.null_bit == 0)
		{
			if (0 == (ar.bits.type & 1))
			{
				print_info("Guest %s.ar bit 0 of the Type must be 1 (accessed)\n", SEGMENT_NAME(seg));
			}

			/*type bit 3*/
			if (ar.bits.type & 8)
			{
				if (0 == (ar.bits.type & 2))
				{
					print_info("Guest %s.ar bit 1 of the Type must be 1 (readable)\n", SEGMENT_NAME(seg));
				}
			}
		}
		break;

	default:
		print_info("ERRO: invalid segment id (%d) in %s().\n", seg, __FUNCTION__);
		break;
	}
}

static void check_ar_s_bit(seg_ar_t ar, seg_id_t seg)
{
	switch (seg)
	{
		case SEG_CS:
			if (1 != ar.bits.s_bit)
			{
				print_info("Guest cs.ar system bit must be 1\n");
			}
			break;

		case SEG_SS:
		case SEG_DS:
		case SEG_ES:
		case SEG_FS:
		case SEG_GS:
			if (ar.bits.null_bit == 0)
			{
				if (1 != ar.bits.s_bit)
				{
					print_info("Guest %s.ar system bit must be 1\n", SEGMENT_NAME(seg));
				}
			}
			break;

		default:
			print_info("ERRO: invalid segment id (%d) in %s().\n", seg, __FUNCTION__);
			break;
	}
}

static void check_ar_dpl_bit(vmcs_obj_t vmcs, seg_id_t seg)
{
	seg_ar_t cs_ar;
	seg_ar_t ss_ar;
	seg_ar_t ds_ar;
	seg_ar_t es_ar;
	seg_ar_t fs_ar;
	seg_ar_t gs_ar;
	uint16_t ss_sel;
	uint16_t ds_sel;
	uint16_t es_sel;
	uint16_t fs_sel;
	uint16_t gs_sel;
	boolean_t  is_ug;
	uint64_t cr0;

	is_ug  = ((vmcs_read(vmcs, VMCS_PROC_CTRL2) & PROC2_UNRESTRICTED_GUEST) != 0);
	switch (seg)
	{
		case SEG_CS:
			/* check DPL bits[6:5] */
			cs_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_CS_AR);
			ss_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_SS_AR);


			if (cs_ar.bits.type == 3)
			{
				if (cs_ar.bits.dpl != 0) {
					print_info("Guest segment cs.ar.dpl must be 0, if cs.ar.type is 3.\n");
				}

				if (!is_ug)
				{
					print_info("if the cs.ar.type is 3, the ug must be 1.\n");
				}
			}

			if (cs_ar.bits.type == 9 || cs_ar.bits.type == 11)
			{
				// non conforming mode
				if (cs_ar.bits.dpl != ss_ar.bits.dpl)
				{
					print_info("Guest cs.ar.dpl bit must be equal to ss.ar.dpl.\n");
				}
			}

			if (cs_ar.bits.type == 13 || cs_ar.bits.type == 15)
			{
				// conforming mode
				if (cs_ar.bits.dpl > ss_ar.bits.dpl)
				{
					print_info("Guest cs.ar.dpl bit cannot be greater than ss.ar.dpl.\n");
				}
			}
			break;

		case SEG_SS:
			cs_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_CS_AR);
			ss_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_SS_AR);
			ss_sel = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_SS_SEL);
			cr0 = vmcs_read(vmcs, VMCS_GUEST_CR0);

			if (!is_ug)
			{
				if (ss_ar.bits.dpl != (ss_sel & 3))
				{
					print_info("Guest ss.ar.dpl bit must be equla to ss.rpl if unrestriced guest control is 0.\n");
				}
			}

			/* Type in ar field for cs is 3, or bit 0 in the cr0 field is 0.*/
			if ((cs_ar.bits.type == 3) || ((cr0 & CR0_PE) == 0))
			{
				if(ss_ar.bits.dpl)
				{
					print_info("Guest ss.dpl must be 0.\n");
				}
			}
			break;

		case SEG_DS:
			ds_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_DS_AR);
			ds_sel = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_DS_SEL);

			if (is_ug)
			{
				if ((ds_ar.bits.null_bit == 0) && (ds_ar.bits.type <= 11))
				{
					// non conforming mode
					if (ds_ar.bits.dpl < (ds_sel & 3))
					{
						print_info("Guest ds.ar.dpl bit cannot be less than ds.rpl.\n");
					}
				}
			}
			break;

		case SEG_ES:
			es_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_ES_AR);
			es_sel = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_ES_SEL);

			if (is_ug)
			{
				if ((es_ar.bits.null_bit == 0) && (es_ar.bits.type <= 11))
				{
					// non conforming mode
					if (es_ar.bits.dpl < (es_sel & 3))
					{
						print_info("Guest es.ar.dpl bit cannot be less than es.rpl.\n");
					}
				}
			}
			break;

		case SEG_FS:
			fs_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_FS_AR);
			fs_sel = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_FS_SEL);

			if (is_ug)
			{
				if ((fs_ar.bits.null_bit == 0) && (fs_ar.bits.type <= 11))
				{
					// non conforming mode
					if (fs_ar.bits.dpl < (fs_sel & 3))
					{
						print_info("Guest fs.ar.dpl bit cannot be less than fs.rpl.\n");
					}
				}
			}
			break;

		case SEG_GS:
			gs_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_GS_AR);
			gs_sel = (uint16_t) vmcs_read(vmcs, VMCS_GUEST_GS_SEL);

			if (is_ug)
			{
				if ((gs_ar.bits.null_bit == 0) && (gs_ar.bits.type <= 11))
				{
					// non conforming mode
					if (gs_ar.bits.dpl < (gs_sel & 3))
					{
						print_info("Guest gs.ar.dpl bit cannot be less than gs.rpl.\n");
					}
				}
			}
			break;

		default:
			print_info("ERRO: invalid segment id (%d) in %s().\n", seg, __FUNCTION__);
			break;
	}
}

static void check_ar_p_bit(seg_ar_t ar, seg_id_t seg)
{
	switch (seg)
	{
		case SEG_CS:
			if (!ar.bits.p_bit)
			{
				print_info("Guest cs.ar.p bit must be 1\n");
			}
			break;

		case SEG_SS:
		case SEG_DS:
		case SEG_ES:
		case SEG_FS:
		case SEG_GS:
			if (ar.bits.null_bit == 0)
			{
				if (!ar.bits.p_bit)
				{
					print_info("Guest %s.ar.p bit must be 1\n", SEGMENT_NAME(seg));
				}
			}
			break;

		default:
			print_info("ERRO: invalid segment id (%d) in %s().\n", seg, __FUNCTION__);
			break;
	}
}

static void check_ar_reserved_bit(seg_ar_t ar, seg_id_t seg)
{
	switch (seg)
	{
		case SEG_CS:
			if (ar.bits.reserved_11_8)
			{
				print_info("Guest cs.ar reserved 11:8 bits must be 0\n");
			}

			if (ar.bits.reserved_31_17)
			{
				print_info("Guest cs.ar reserved 31:17 bits must be 0\n");
			}
			break;

		case SEG_SS:
		case SEG_DS:
		case SEG_ES:
		case SEG_FS:
		case SEG_GS:
			if (ar.bits.null_bit == 0)
			{
				if (ar.bits.reserved_11_8)
				{
					print_info("Guest %s.ar reserved 11:8 bits must be 0\n", SEGMENT_NAME(seg));
				}

				if (ar.bits.reserved_31_17)
				{
					print_info("Guest %s.ar reserved 31:17 bits must be 0\n", SEGMENT_NAME(seg));
				}
			}
			break;

		default:
			print_info("ERRO: invalid segment id (%d) in %s().\n", seg, __FUNCTION__);
			break;
	}
}

static void check_ar_db_bit(boolean_t is_64bit, seg_ar_t ar, seg_id_t seg)
{
	switch (seg)
	{
		case SEG_CS:
			if (is_64bit && ar.bits.l_bit)
			{
				if (ar.bits.db_bit)
				{
					print_info("Guest cs.ar D/B bits must be 0.\n");
				}
			}
			break;

		default:
			print_info("ERRO: invalid segment id (%d) in %s().\n", seg, __FUNCTION__);
			break;
	}
}

static void check_ar_g_bit(uint64_t limit, seg_ar_t ar, seg_id_t seg)
{
	switch (seg)
	{
		case SEG_CS:
			if ((limit & 0xFFFULL) != 0xFFFULL)
			{
				if (ar.bits.g_bit)
				{
					print_info("Guest cs.ar G bits must be 0 if any bit in limit field in the range 11:0 is 0.\n");
				}
			}

			if ((limit & 0xFFF00000ULL) != 0)
			{
				if (!ar.bits.g_bit)
				{
					print_info("Guest cs.ar G bits must be 1 if any bit in limit field in the range 31:20 is 1.\n");
				}
			}
			break;

		case SEG_SS:
		case SEG_DS:
		case SEG_ES:
		case SEG_FS:
		case SEG_GS:
			if (ar.bits.null_bit == 0)
			{
				if ((limit & 0xFFFULL) != 0xFFFULL)
				{
					if (ar.bits.g_bit)
					{
						print_info("Guest %s.ar G bits must be 0 if any bit in limit field in the range 11:0 is 0\n",
							SEGMENT_NAME(seg));
					}
				}

				if ((limit & 0xFFF00000ULL) != 0)
				{
					if (!ar.bits.g_bit)
					{
						print_info("Guest %s.ar G bits must be 1 if any bit in limit field in the range 31:20 is 1\n",
							SEGMENT_NAME(seg));
					}
				}
			}
			break;

		default:
			print_info("ERRO: invalid segment id (%d) in %s().\n", seg, __FUNCTION__);
			break;
	}
}

static void check_tr_ar(vmcs_obj_t vmcs)
{
	seg_ar_t tr_ar;
	uint64_t   tr_limit;
	boolean_t  is_64bit;

	tr_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_TR_AR);
	tr_limit = vmcs_read(vmcs, VMCS_GUEST_TR_LIMIT);
	is_64bit = ((vmcs_read(vmcs, VMCS_ENTRY_CTRL) & ENTRY_GUEST_IA32E_MODE) != 0);

	//check TR type:
	if (is_64bit)
	{
		if (tr_ar.bits.type != 11)
		{
			print_info("Guest will be IA-32e mode, tr.ar type must be 11.\n");
		}
	}else{
		if ((tr_ar.bits.type != 3) && (tr_ar.bits.type != 11))
		{
			print_info("Guest will not be IA-32e mode, tr.ar type must be 3 or 11.\n");
		}
	}
	//check TR'S bit[4]:
	if (tr_ar.bits.s_bit)
	{
		print_info("Guest tr.ar s bit must be 0.\n");
	}

	//check TR'P bit[7]:
	if (!tr_ar.bits.p_bit)
	{
		print_info("Guest tr.ar p bit must be 1.\n");
	}

	//check TR reserve bit[11:8]:
	if (tr_ar.bits.reserved_11_8)
	{
		print_info("Guest tr.ar reserve 11:8 bit must be 0.\n");
	}

	//check TR G bit:
	if ((tr_limit & 0xFFFULL) != 0xFFFULL)
	{
		if (tr_ar.bits.g_bit)
		{
			print_info("Guest tr.ar G bits must be 0 if any bit in limit field in the range 11:0 is 0.\n");
		}
	}

	if ((tr_limit & 0xFFF00000ULL) != 0)
	{
		if (!tr_ar.bits.g_bit)
		{
			print_info("Guest tr.ar G bits must be 1 if any bit in limit field in the range 31:20 is 1.\n");
		}
	}

	//check TR Unusable bit[16]:
	if (tr_ar.bits.null_bit)
	{
		print_info("Guest tr.ar Unusable bit must be 0.\n");
	}

	//check TR reserve bit[31:17]:
	if (tr_ar.bits.reserved_31_17)
	{
		print_info("Guest tr.ar reserve 31:17 bit must be 0.\n");
	}
}

static void check_ldtr_ar(vmcs_obj_t vmcs)
{
	seg_ar_t ldtr_ar;
	uint64_t   ldtr_limit;

	ldtr_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_LDTR_AR);
	ldtr_limit = vmcs_read(vmcs, VMCS_GUEST_LDTR_LIMIT);

	if (ldtr_ar.bits.null_bit == 0)
	{
		//check type:
		if (ldtr_ar.bits.type != 2)
		{
			print_info("Guest ldtr.ar type bit must be 2\n");
		}

		//check LDTR S bit[4]:
		if (ldtr_ar.bits.s_bit)
		{
			print_info("Guest ldtr.ar s bit must be 0\n");
		}

		//check LDTR P bit[7]:
		if (!ldtr_ar.bits.p_bit)
		{
			print_info("Guest ldtr.ar p bit must be 1\n");
		}

		//check LDTR reserve bit[11:8]:
		if (ldtr_ar.bits.reserved_11_8)
		{
			print_info("Guest ldtr.ar reserve 11:8 bit must be 0\n");
		}

		//check LDTR G bit:
		if ((ldtr_limit & 0xFFFULL) != 0xFFFULL)
		{
			if (ldtr_ar.bits.g_bit)
			{
				print_info("Guest ldtr.ar G bits must be 0 if any bit in limit field in the range 11:0 is 0\n");
			}
		}

		if ((ldtr_limit & 0xFFF00000ULL) != 0)
		{
			if (!ldtr_ar.bits.g_bit)
			{
				print_info("Guest ldtr.ar G bits must be 1 if any bit in limit field in the range 31:20 is 1\n");
			}
		}

		//check LDTR reserve bit[31:17]:
		if (ldtr_ar.bits.reserved_31_17)
		{
			print_info("Guest ldtr.ar reserve 31:17 bit must be 0\n");
		}
	}
}

static void check_segment_access_rights(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.2:
	 ** Access-rights fields:
	 **  CS, SS, DS, ES, FS, GS:
	 **   If the guest will be virtual-8086, the field must be 000000F3H.
	 **   If the guest will not be virtual-8086, the different sub-fields
	 **   are considered separately.
	 **  TR:
	 **   The different sub-fields are considered separately.
	 **  LDTR:
	 **   The following checks on the different sub-fields apply
	 **   only if LDTR is usable.
	 */
	seg_ar_t cs_ar;
	seg_ar_t ss_ar;
	seg_ar_t ds_ar;
	seg_ar_t es_ar;
	seg_ar_t fs_ar;
	seg_ar_t gs_ar;
	boolean_t  is_ug;
	boolean_t  is_64bit;
	boolean_t  is_vr8086;
	uint64_t   cs_limit;
	uint64_t   ss_limit;
	uint64_t   ds_limit;
	uint64_t   es_limit;
	uint64_t   fs_limit;
	uint64_t   gs_limit;

	cs_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_CS_AR);
	ss_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_SS_AR);
	ds_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_DS_AR);
	es_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_ES_AR);
	fs_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_FS_AR);
	gs_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_GS_AR);
	cs_limit = vmcs_read(vmcs, VMCS_GUEST_CS_LIMIT);
	ss_limit = vmcs_read(vmcs, VMCS_GUEST_SS_LIMIT);
	ds_limit = vmcs_read(vmcs, VMCS_GUEST_DS_LIMIT);
	es_limit = vmcs_read(vmcs, VMCS_GUEST_ES_LIMIT);
	fs_limit = vmcs_read(vmcs, VMCS_GUEST_FS_LIMIT);
	gs_limit = vmcs_read(vmcs, VMCS_GUEST_GS_LIMIT);
	is_ug  = ((vmcs_read(vmcs, VMCS_PROC_CTRL2) & PROC2_UNRESTRICTED_GUEST) != 0);
	is_64bit  = ((vmcs_read(vmcs, VMCS_ENTRY_CTRL) & ENTRY_GUEST_IA32E_MODE) != 0);
	is_vr8086 = ((vmcs_read(vmcs, VMCS_GUEST_RFLAGS) & RFLAGS_VM) != 0);

	// check CS, SS, DS, ES, FS, GS:
	if (is_vr8086)
	{
		check_ar_virtual_8068_mode(cs_ar.u32, SEG_CS);
		check_ar_virtual_8068_mode(ss_ar.u32, SEG_SS);
		check_ar_virtual_8068_mode(ds_ar.u32, SEG_DS);
		check_ar_virtual_8068_mode(es_ar.u32, SEG_ES);
		check_ar_virtual_8068_mode(fs_ar.u32, SEG_FS);
		check_ar_virtual_8068_mode(gs_ar.u32, SEG_GS);
	}else{
		//check Type bit[3:0]:
		check_ar_type_bit(is_ug, cs_ar, SEG_CS);
		check_ar_type_bit(is_ug, ss_ar, SEG_SS);
		check_ar_type_bit(is_ug, ds_ar, SEG_DS);
		check_ar_type_bit(is_ug, es_ar, SEG_ES);
		check_ar_type_bit(is_ug, fs_ar, SEG_FS);
		check_ar_type_bit(is_ug, gs_ar, SEG_GS);

		//check S bit[4]:
		check_ar_s_bit(cs_ar, SEG_CS);
		check_ar_s_bit(ss_ar, SEG_SS);
		check_ar_s_bit(ds_ar, SEG_DS);
		check_ar_s_bit(es_ar, SEG_ES);
		check_ar_s_bit(fs_ar, SEG_FS);
		check_ar_s_bit(gs_ar, SEG_GS);

		/* check DPL bits[6:5] */
		check_ar_dpl_bit(vmcs, SEG_CS);
		check_ar_dpl_bit(vmcs, SEG_SS);
		check_ar_dpl_bit(vmcs, SEG_DS);
		check_ar_dpl_bit(vmcs, SEG_ES);
		check_ar_dpl_bit(vmcs, SEG_FS);
		check_ar_dpl_bit(vmcs, SEG_GS);

		//check P bit[7]:
		check_ar_p_bit(cs_ar, SEG_CS);
		check_ar_p_bit(ss_ar, SEG_SS);
		check_ar_p_bit(ds_ar, SEG_DS);
		check_ar_p_bit(es_ar, SEG_ES);
		check_ar_p_bit(fs_ar, SEG_FS);
		check_ar_p_bit(gs_ar, SEG_GS);

		//check reserve bit[11:8] and bit[31:17]:
		check_ar_reserved_bit(cs_ar, SEG_CS);
		check_ar_reserved_bit(ss_ar, SEG_SS);
		check_ar_reserved_bit(ds_ar, SEG_DS);
		check_ar_reserved_bit(es_ar, SEG_ES);
		check_ar_reserved_bit(fs_ar, SEG_FS);
		check_ar_reserved_bit(gs_ar, SEG_GS);

		//check D/B bit[14]:
		check_ar_db_bit(is_64bit, cs_ar, SEG_CS);

		//check G bit[15]:
		check_ar_g_bit(cs_limit, cs_ar, SEG_CS);
		check_ar_g_bit(ss_limit, ss_ar, SEG_SS);
		check_ar_g_bit(ds_limit, ds_ar, SEG_DS);
		check_ar_g_bit(es_limit, es_ar, SEG_ES);
		check_ar_g_bit(fs_limit, fs_ar, SEG_FS);
		check_ar_g_bit(gs_limit, gs_ar, SEG_GS);
	}

	//check TR:
	check_tr_ar(vmcs);

	//check LDTR:
	check_ldtr_ar(vmcs);
}

static void check_guest_segments(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.2:
	 **  The following checks are performed on fields in the segment
	 **  registers.
	 */
	check_segment_selector(vmcs);
	check_segment_base_address(vmcs);
	check_segment_limit(vmcs);
	check_segment_access_rights(vmcs);
}

static void check_guest_xdtr(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.3:
	 **  The following checks are performed on the fields for GDTR and IDTR:
	 **   The base-address fields must contain canonical addresses.
	 **   Bits 31:16 of each limit field must be 0.
	 */
	uint64_t gdt_base;
	uint32_t gdt_limit;
	uint64_t idt_base;
	uint32_t idt_limit;
	boolean_t is_64bit;

	gdt_base = vmcs_read(vmcs, VMCS_GUEST_GDTR_BASE);
	idt_base = vmcs_read(vmcs, VMCS_GUEST_IDTR_BASE);
	gdt_limit = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_GDTR_LIMIT);
	idt_limit = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_IDTR_LIMIT);
	is_64bit = ((vmcs_read(vmcs, VMCS_ENTRY_CTRL) & ENTRY_GUEST_IA32E_MODE) != 0);

	if (FALSE == addr_is_canonical(is_64bit, gdt_base))
	{
		print_info("Guest GDT base must be canonical\n");
	}
	if ((gdt_limit & 0xFFFF0000) != 0)
	{
		print_info("Guest GDT limit[31..16] must be 0\n");
	}

	if (FALSE == addr_is_canonical(is_64bit, idt_base))
	{
		print_info("Guest IDT base must be canonical\n");
	}
	if ((idt_limit & 0xFFFF0000) != 0)
	{
		print_info("Guest IDT limit[31..16] must be 0\n");
	}
}

static void check_guest_rip(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.4:
	 **  RIP:
	 **   Bits 63:32 must be 0 if the "IA32e mode guest" VM-entry control is 0 or if
	 **   the L bit (bit 13) in the access-rights field for CS is 0.
	 **   If the processor supports N < 64 linear-address bits, bits 63:N must be
	 **   identical if the "IA32e mode guest" VM-entry control is 1 and the L bit in the
	 **   access-rights field for CS is 1.
	 */
	uint64_t rip;
	uint32_t vmentry_control;
	boolean_t is_64bit;
	seg_ar_t cs_ar;

	vmentry_control = (uint32_t) vmcs_read(vmcs, VMCS_ENTRY_CTRL);
	is_64bit = ((vmentry_control & ENTRY_GUEST_IA32E_MODE) != 0);
	rip = vmcs_read(vmcs, VMCS_GUEST_RIP);
	cs_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_CS_AR);

	if (is_64bit && (1 == cs_ar.bits.l_bit))
	{
		if (FALSE == addr_is_canonical(is_64bit, rip))
		{
			print_info("The rip must be canonical.\n");
		}
	}
	else
	{
		if (is_zero_upper (rip) == FALSE)
		{
			print_info("The bits 63:32 of rip must be 0.\n");
		}
	}
}

static void check_guest_rflags(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.4:
	 ** RFLAGS:
	 **  Reserved bits 63:22, bit 15, bit 5 and bit 3 must be 0 in the field, and reserved bit 1
	 **  must be 1.
	 **  The VM flag (bit 17) must be 0 if the "IA32e mode guest" VM-entry control is 1.
	 **  The IF flag (RFLAGS[bit 9]) must be 1 if the valid bit (bit 31) in the VM-entry
	 **  interruption-information field is 1 and the interruption type (bits 10:8) is
	 **  external interrupt.
	 */
	uint64_t rflags;
	uint64_t cr0;
	uint32_t vmentry_control;
	boolean_t is_64bit;
	vmx_exit_idt_info_t   vmenter_intr_info;

	vmentry_control = (uint32_t) vmcs_read(vmcs, VMCS_ENTRY_CTRL);
	is_64bit = ((vmentry_control & ENTRY_GUEST_IA32E_MODE) != 0);
	rflags = vmcs_read(vmcs, VMCS_GUEST_RFLAGS);
	cr0 = vmcs_read(vmcs, VMCS_GUEST_CR0);
	if ((rflags & 0xFFFFFFFFFFC0802AULL) != 2)
	{
		print_info("Guest RFLAGS.reserved bits are invalid\n");
	}

	if (is_64bit || ((cr0 & CR0_PE) == 0))
	{
		if (rflags & RFLAGS_VM)
		{
			print_info("Guest RFLAGS.VM must be 0\n");
		}
	}

	vmenter_intr_info.uint32 = (uint32_t) vmcs_read(vmcs, VMCS_ENTRY_INTR_INFO);
	if ((1 == vmenter_intr_info.bits.valid) &&
		(vmenter_intr_info.bits.interrupt_type == VECTOR_TYPE_EXT_INT)){
		if ((rflags & RFLAGS_IF) == 0)
		{
			print_info("The IF flag (RFLAGS[bit 9]) must be 1 ");
			print_info("if the valid bit (bit 31) in the VM-entry\n");
			print_info("interruption-information field is 1 and ");
			print_info("the interruption type (bits 10:8) is external interrupt\n");
		}
	}
}

static void check_guest_rip_and_rflags(vmcs_obj_t vmcs)
{
	/*check guest rip*/
	check_guest_rip(vmcs);

	/*check guest rflag*/
	check_guest_rflags(vmcs);
}

static void check_guest_activity_state(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.5:
	 ** Activity state:
	 **  The activity-state field must contain a value in the range 0-3.
	 **  The activity-state field must not indicate the HLT state if the DPL (bits 6:5) in
	 **  the access-rights field for SS is not 0.
	 **  The activity-state field must indicate the active state if the interruptibility state
	 **  field indicates blocking by either MOV-SS or by STI.
	 **  If the valid bit (bit 31) in the VM-entry interruption-information field is 1, the
	 **  interruption to be delivered must not be one that would normally be blocked while
	 **  a logical processor is in the activity state corresponding to the contents of the
	 **  activity-state field. The following items enumerate the interruptions whose injection
	 **  is allowed for the different activity states:
	 **   Active:
	 **    Any interruption is allowed.
	 **   HLT:
	 **    The only events allowed are the following:
	 **     Those with interruption type external interrupt or non-maskable
	 **     interrupt (NMI).
	 **     Those with interruption type hardware exception and vector 1
	 **     or vector 18 (machine-check exception).
	 **     Those with interruption type other event and vector 0.
	 **   Shutdown:
	 **     Only NMIs and machine-check exceptions are allowed.
	 **   Wait-for-SIPI:
	 **     No interruptions are allowed.
	 **  The activity-state field must not indicate the wait-for-SIPI state if the "entry
	 **  to SMM" VM-entry control is 1.
	 */
	uint32_t                       activity;
	uint32_t                       interruptibility;
	vmx_exit_idt_info_t            vmenter_intr_info;
	uint32_t                       vmentry_control;
	seg_ar_t                       ss_ar;

	activity = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_ACTIVITY_STATE);
	interruptibility = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_INTERRUPTIBILITY);
	ss_ar.u32 = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_SS_AR);
	vmenter_intr_info.uint32 = (uint32_t) vmcs_read(vmcs, VMCS_ENTRY_INTR_INFO);
	vmentry_control = (uint32_t) vmcs_read(vmcs, VMCS_ENTRY_CTRL);

	if (activity >= ACTIVITY_STATE_COUNT)
	{
		print_info("The activity-state field must contain a value in the range 0-3.");
	}

	if (0 != ss_ar.bits.dpl)
	{
		if (ACTIVITY_STATE_HLT == activity)
		{
			print_info("The activity-state must not indicate the HLT state ");
			print_info("if the DPL (bits 6:5) in the access-rights field for SS is not 0\n");
		}
	}

	if (interruptibility & (INTR_BLK_BY_MOV_SS | INTR_BLK_BY_STI))
	{
		if (ACTIVITY_STATE_ACTIVE != activity)
		{
			print_info("The activity-state must indicate the active state ");
			print_info("if the interruptibility state indicates blocking by either MOV-SS or by STI\n");
		}
	}

	if (1 == vmenter_intr_info.bits.valid)
	{
		switch (activity)
		{
			case ACTIVITY_STATE_ACTIVE:
				// OK, all interrupts are allowed.
				break;
			case ACTIVITY_STATE_HLT:
				if (vmenter_intr_info.bits.interrupt_type == VECTOR_TYPE_EXT_INT
					|| vmenter_intr_info.bits.interrupt_type == VECTOR_TYPE_NMI
					|| (vmenter_intr_info.bits.interrupt_type == VECTOR_TYPE_HW_EXCEPTION &&
					(vmenter_intr_info.bits.vector == EXCEPTION_DB || vmenter_intr_info.bits.vector == EXCEPTION_MC))
					|| (vmenter_intr_info.bits.interrupt_type == VECTOR_TYPE_OTHER_EVE &&
					vmenter_intr_info.bits.vector == EXCEPTION_DE))
				{
					// OK
				}
				else
				{
					print_info("Activity state is HLT. The only events allowed are the following:\n");
					print_info("Those with interruption type external interrupt or ");
					print_info("non-maskable interrupt (NMI)\n");

					print_info("Those with interruption type hardware exception and ");
					print_info("vector 1 (debug exception) or vector 18 (machine-check exception).\n");

					print_info("Those with interruption type other event and ");
					print_info("vector 0 (pending MTF VM exit)\n");
				}
				break;
			case ACTIVITY_STATE_SHUTDOWN:
				if (VECTOR_TYPE_NMI == vmenter_intr_info.bits.interrupt_type
					|| VECTOR_TYPE_HW_EXCEPTION == vmenter_intr_info.bits.interrupt_type)
				{
					// OK
				}
				else
				{
					print_info("Activity state is Shutdown. Only NMIs and ");
					print_info("machine-check exceptions are allowed\n");
				}
				break;
			case ACTIVITY_STATE_WAIT_FOR_SIPI:
				print_info("Activity state is WaitForSIPI. No interruptions are allowed\n");
				break;
			default:
				print_info("Activity-state has invalid value(%d) in %s\n", activity, __FUNCTION__);
				break;
		}
	}


	if (vmentry_control & ENTRY_TO_SMM)
	{
		if (ACTIVITY_STATE_WAIT_FOR_SIPI == activity)
		{
			print_info("The activity-state must not indicate the Wait-for-SIPI state ");
			print_info("if the entry to SMM VM-entry control is 1\n");
		}
	}
}

static void check_guest_interruptibility_state(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.5:
	 ** Interruptibility state:
	 **  The reserved bits (bits 31:5) must be 0.
	 **  The field cannot indicate blocking by both STI and MOV SS.
	 **  Bit 0 (blocking by STI) must be 0 if the IF flag is 0 in the RFLAGS field.
	 **  Bit 0 (blocking by STI) and bit 1 (blocking by MOV-SS) must both be 0 if the
	 **  valid bit (bit 31) in the VM-entry interruption-information field is 1 and the
	 **  interruption type (bits 10:8) in that field has value 0, indicating external
	 **  interrupt.
	 **  Bit 1 (blocking by MOV-SS) must be 0 if the valid bit (bit 31) in the VM-entry
	 **  interruption-information field is 1 and the interruption type (bits 10:8) in that
	 **  field has value 2, indicating non-maskable interrupt (NMI).
	 **  Bit 2 (blocking by SMI) must be 0 if the processor is not in SMM.
	 **  Bit 2 (blocking by SMI) must be 1 if the "entry to SMM" VM-entry control is 1.
	 **  A processor may require bit 0 (blocking by STI) to be 0 if the valid bit (bit 31)
	 **  in the VM-entry interruption-information field is 1 and the interruption type
	 **  (bits 10:8) in that field has value 2, indicating NMI. Other processors may not
	 **  make this requirement.
	 **  Bit 3 (blocking by NMI) must be 0 if the "virtual NMIs" VM-execution control is 1,
	 **  the valid bit (bit 31) in the VM-entry interruption-information field is 1, and the
	 **  interruption type (bits 10:8) in that field has value 2 (indicating NMI).
	 */
	uint32_t                        interruptibility;
	vmx_exit_idt_info_t             vmenter_intr_info;
	uint64_t                        guest_rflags;
	uint32_t                        pin_control;
	uint32_t                        vmentry_control;
	cpuid_params_t                 cpuid_params = {0, 0, 0, 0};

	interruptibility = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_INTERRUPTIBILITY);
	vmenter_intr_info.uint32 = (uint32_t) vmcs_read(vmcs, VMCS_ENTRY_INTR_INFO);
	vmentry_control = (uint32_t) vmcs_read(vmcs, VMCS_ENTRY_CTRL);
	pin_control     = (uint32_t) vmcs_read(vmcs, VMCS_PIN_CTRL);
	guest_rflags = vmcs_read(vmcs, VMCS_GUEST_RFLAGS);

	if (0 != (interruptibility & INTR_RESERVED))
	{
		print_info("Interruptibility-state reserved bits (bits 31:4) must be 0\n");
	}

	if ((interruptibility & (INTR_BLK_BY_STI | INTR_BLK_BY_MOV_SS)) == (INTR_BLK_BY_STI | INTR_BLK_BY_MOV_SS))
	{
		print_info("Interruptibility-state cannot indicate blocking by both STI and MOV SS\n");
	}

	if ((guest_rflags & RFLAGS_IF) == 0)
	{
		if (interruptibility & INTR_BLK_BY_STI)
		{
			print_info("Interruptibility-state Blocking by STI must be 0 ");
			print_info("if the IF flag is 0 in the RFLAGS\n");
		}
	}

	if (vmenter_intr_info.bits.valid)
	{
		if (VECTOR_TYPE_EXT_INT == vmenter_intr_info.bits.interrupt_type)
		{
			if (interruptibility & (INTR_BLK_BY_STI |INTR_BLK_BY_MOV_SS))
			{
				print_info("Interruptibility blocking by STI and ");
				print_info("Blocking by MOV-SS must both be 0 if the\n");
				print_info("valid bit in the VM-entry interruption-information field is 1 and the\n");
				print_info("interruption type in that field has value 0, indicating external interrupt.\n");
			}
		}

		if (VECTOR_TYPE_NMI == vmenter_intr_info.bits.interrupt_type)
		{
			if (interruptibility & INTR_BLK_BY_MOV_SS)
			{
				print_info("Interruptibility blocking by MOV-SS must be 0 if the valid bit in the VM-entry\n");
				print_info("interruption-information field is 1 and the interruption type in that\n");
				print_info("field has value 2, indicating non-maskable interrupt (NMI)\n");
			}
		}
	}

	if ((vmentry_control & ENTRY_TO_SMM) == 0)
	{
		if (interruptibility & INTR_BLK_BY_SMI)
		{
			print_info("If the processor is not in SMM, interruptibility blocking by SMI must be 0 \n");
		}
	}

	if (vmentry_control & ENTRY_TO_SMM)
	{
		if ((interruptibility & INTR_BLK_BY_SMI) == 0)
		{
			print_info("If the processor is in SMM, interruptibility blocking by SMI must be 1 \n");
		}
	}

	if(vmenter_intr_info.bits.valid)
	{
		if (VECTOR_TYPE_NMI == vmenter_intr_info.bits.interrupt_type)
		{
			if (interruptibility & INTR_BLK_BY_STI)
			{
				print_info("Interruptibility blocking by STI be 0 if the valid bit in the VM-entry\n");
				print_info("interruption-information field is 1 and the interruption type in that\n");
				print_info("field has value 2, indicating NMI\n");
			}

			if(pin_control & PIN_VIRTUAL_NMI)
			{
				if (interruptibility & INTR_BLK_BY_NMI)
				{
					print_info("Interruptibility blocking by NMI must be 0 if the virtual nmi is 1 in the VM-execution ctrl\n");
				}
			}
		}
	}

	if (interruptibility & INTR_ENCLAVE_MODE)
	{
		cpuid_params.eax = 7;
		cpuid_params.ecx = 0;
		asm_cpuid(&cpuid_params);
		if ((interruptibility & INTR_BLK_BY_MOV_SS) || (!(cpuid_params.ebx & CPUID_EBX_SGX)))
		{
			print_info("blocking by MOV-SS must be 0 and \n");
			print_info(" the processor must support for SGX by enumerating CPUID.(EAX=07H,ECX=0):EBX.SGX[bit 2] as 1.");
		}
	}
}

static void check_guest_pending_debug_exception(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.5:
	 ** Pending debug exception:
	 **  Bit 11:4, bit 13, bit 15, and bit 63:17 must be 1.
	 **  The following checks are performed if any of the following hold:
	 **  (1)the interruptibility-state filed indicates blocking by STI.
	 **  (2)the interruptibility-state field indicates blocking by MOV SS.
	 **  (3)the activity-state field indicates HLT.
	 **    Bit 14 (BS) must be 1 if the TF flag (bit 8) in the RFLAGS field
	 **    is 1 and the BTF flag (bit 1) in the IA32_DEBUGCTL field is 0.
	 **    Bit 14 (BS) must be 0 if the TF flag (bit 8) in the RFLAGS field
	 **    is 0 or the BTF flag (bit 1) in the IA32_DEBUGCTL field is 1.
	 **  The following checks are performed if bit 16 (RTM) is 1:
	 **    Bits 11:0, bits 15:13, and bits 63:17 must be 0; bit 12 must be 1.
	 **    The processor must support for RTM by enumerating CPUID.
	 **    (EAX=07H,ECX=0):EBX[bit 11] as 1.
	 **    The interruptibility-state field must not indicate blocking by MOV SS
	 **    (bit 1 in that field must be 0).
	 */
	uint64_t                       pending_debg;
	uint32_t                       activity;
	uint32_t                       interruptibility;
	uint64_t                       ctrl_debg;
	uint64_t                       guest_rflags;
	cpuid_params_t                 cpuid_params = {0, 0, 0, 0};

	activity = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_ACTIVITY_STATE);
	interruptibility = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_INTERRUPTIBILITY);
	guest_rflags = vmcs_read(vmcs, VMCS_GUEST_RFLAGS);
	pending_debg = vmcs_read(vmcs, VMCS_GUEST_PEND_DBG_EXCEPTION);
	ctrl_debg = vmcs_read(vmcs, VMCS_GUEST_DBGCTL);

	if ((pending_debg & 0xFFFFFFFFFFFEAFF0ULL) != 0)
	{
		print_info("Guest Pending deug exception reserved bits are invalid\n");
	}

	if ((interruptibility & (INTR_BLK_BY_STI | INTR_BLK_BY_MOV_SS))
		|| (activity == ACTIVITY_STATE_HLT)){
		if ((guest_rflags & RFLAGS_TP) && ((ctrl_debg & MSR_DEBUGCTL_BTF) == 0))
		{
			if((pending_debg & NRS_PENDING_DEBG_BS_BIT) == 0)
			{
				print_info("Guest Pending deug exception BS must be 1\n");
			}
		}else{
			if((pending_debg & NRS_PENDING_DEBG_BS_BIT))
			{
				print_info("Guest Pending deug exception BS must be 0\n");
			}
		}
	}

	if (pending_debg & NRS_PENDING_DEBG_RTM_BIT)
	{
		if ((pending_debg & 0xFFFFFFFFFFFEFFFFULL) != 0x1000)
		{
			print_info("Guest Pending deug exception Bits 11:0, bits 15:13, and\n");
			print_info("bits 63:17 must be 0; bit 12 must be 1.\n");
		}

		cpuid_params.eax = 7;
		cpuid_params.ecx = 0;
		asm_cpuid(&cpuid_params);
		if ((cpuid_params.ebx & CPUID_EBX_RTM) == 0) {
			print_info("The processor must support for RTM by enumerating CPUID \n");
		}

		if (interruptibility & INTR_BLK_BY_MOV_SS)
		{
			print_info("The interruptibility-state field must not indicate blocking by MOV SS\n");
		}
	}
}

static void check_guest_vmcs_link_pointer(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.5:
	 ** VMCS link pointer:
	 **  The following checks apply if the field contains a value
	 **  FFFFFFFF_FFFFFFFFH:
	 */
	uint64_t  link_ptr;

	link_ptr = vmcs_read(vmcs, VMCS_LINK_PTR);
	if (link_ptr != 0xFFFFFFFFFFFFFFFFULL)
	{
		print_info("VMCS link pointer must be 0xFFFFFFFFFFFFFFFF\n");
	}
}

static void check_guest_non_register_state(vmcs_obj_t vmcs)
{
	/*check activity state*/
	check_guest_activity_state(vmcs);

	/*check interruptibility state*/
	check_guest_interruptibility_state(vmcs);

	/*check pending_debug_exception*/
	check_guest_pending_debug_exception(vmcs);

	/*check vmcs_link_pointer*/
	check_guest_vmcs_link_pointer(vmcs);
}

static void check_pdpte(uint64_t pdpte)
{
	if (pdpte & PAE_PDPTE_PRESENT)
	{
		if ((pdpte & PAE_PDPTE_RESV_21_85_BITS)
			|| (pdpte >> get_max_phy_addr()))
		{
			print_info("the PDPTE reserved bits is invalid.\n");
		}
	}
}

static void check_guest_pdpte(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.3.1.6:
	 ** PDPTE:
	 **  A VM entry is to a guest that uses PAE paging if (1) bit 31 is set
	 **  in the CR0 field in the guest-state area; (2) bit 5  is set in the
	 **  CR4 field; and (3) the "IA-32e mode guest" VM-entry control is 0.
	 **  Such a VM entry checks the validity of the PDPTEs:
	 **   If the "enable EPT" VM-execution control is 0, VM entry checks
	 **   the validity of the PDPTEs referenced by the CR3 field in the
	 **   guest-state area.
	 **   If the "enable EPT" VM-execution control is 1, VM entry checks
	 **   the validity of the PDPTE fields in the guest-state.
	 */
	boolean_t is_64bit;
	uint32_t idx;
	uint64_t* pdpt_hva;
	guest_cpu_handle_t gcpu = get_current_gcpu();
	guest_handle_t guest = gcpu->guest;

	uint64_t cr0 = vmcs_read(vmcs, VMCS_GUEST_CR0);
	uint64_t cr3 = vmcs_read(vmcs, VMCS_GUEST_CR3);
	uint64_t cr4 = vmcs_read(vmcs, VMCS_GUEST_CR4);
	uint64_t proc_ctrl2 = vmcs_read(vmcs, VMCS_PROC_CTRL2);
	is_64bit = (((uint32_t) vmcs_read(vmcs, VMCS_ENTRY_CTRL) & ENTRY_GUEST_IA32E_MODE) != 0);

	if ((cr0 & CR0_PG) && (cr4 & CR4_PAE) && (is_64bit == 0))
	{
		if (proc_ctrl2 & PROC2_ENABLE_EPT)
		{
			for (idx = VMCS_GUEST_PDPTR0; idx <= VMCS_GUEST_PDPTR3; idx++)
			{
				check_pdpte(vmcs_read(vmcs, idx));
			}
		}else{
			if (!gpm_gpa_to_hva(guest, (cr3 & MASK64_MID(31, 5)), GUEST_CAN_READ | GUEST_CAN_WRITE, (uint64_t *)(&pdpt_hva)))
			{
				print_info("ERRO: Failed to convert gpa=0x%llX to hva in %s()\n", cr3, __FUNCTION__);
				return;
			}

			for (idx = 0; idx <= 3; idx++)
			{
				check_pdpte(pdpt_hva[idx]);
			}
		}
	}
}

static void check_guest_state(vmcs_obj_t vmcs)
{
	/* According to IA32 Manual: Volume 3, Chapter 26.3.1.*/
	check_guest_ctl_dbug_regs_msrs(vmcs);
	check_guest_segments(vmcs);
	check_guest_xdtr(vmcs);
	check_guest_rip_and_rflags(vmcs);
	check_guest_non_register_state(vmcs);
	check_guest_pdpte(vmcs);
}

static void check_vmx_pin_ctrl(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  Reserved bits in the pin-based VM-execution controls
	 **  must be set properly. Software may consult the VMX
	 **  capability MSRs to determine the proper settings.
	 */
	uint32_t pin_ctrl;
	uint32_t may0,may1;

	pin_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PIN_CTRL);
	may1 = get_pinctl_cap(&may0);
	if (is_compliant(pin_ctrl, may0, may1) == FALSE)
	{
		print_info("incorrect value in pin-based VM-execution control.\n");
	}
}

static void check_vmx_proc_ctrl(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  Reserved bits in the primary processor-based VM-execution
	 **  controls must be set properly. Software may consult the VMX
	 **  capability MSRs to determine the proper settings.
	 */
	uint32_t proc_ctrl;
	uint32_t may0,may1;

	proc_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL1);
	may1 = get_proctl1_cap(&may0);
	if (is_compliant(proc_ctrl, may0, may1) == FALSE)
	{
		print_info("incorrect value in primary processor-based VM-execution control.\n");
	}
}

static void check_vmx_proc2_ctrl(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "activate secondary controls" primary processor-based
	 **  VM-execution control is 1, reserved bits in the secondary
	 **  processor-based VM-execution controls must be cleared. Software
	 **  may consult the VMX capability MSRs to determine which bits are reserved.
	 **  If the "activate secondary controls" primary processor-based VM-execution
	 **  control is 0, no checks are performed on the secondary processor-based
	 **  VM-execution controls.
	 */
	uint32_t proc2_ctrl;
	uint32_t may0,may1;

	proc2_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	may1 = get_proctl2_cap(&may0);
	if ((uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL1) & PROC_SECONDARY_CTRL)
	{
		if (is_compliant(proc2_ctrl, may0, may1) == FALSE)
		{
			print_info("incorrect value in secondary processor-based VM-execution control.\n");
		}
	}
}

static void check_vmx_cr3_count(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  The CR3-target count must not be greater than 4.
	 **  Future processors may support a different number of
	 **  CR3-target values. Software should read the VMX capability
	 **  MSR IA32_VMX_MISC to determine the number of values supported.
	 */
	uint32_t cr3_count;

	cr3_count = (uint32_t)vmcs_read(vmcs, VMCS_CR3_TARGET_COUNT);
	if (cr3_count > 4)
	{
		print_info("The CR3-target count must not be greater than 4.\n");
	}
}

static void check_vmx_io_bitmap(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "use I/O bitmaps" VM-execution control is 1,
	 **  bits 11:0 of each I/O-bitmap address must be 0. Neither
	 **  address should set any bits beyond the processor's
	 **  physical-address width.
	 */
	uint64_t io_bitmap_a;
	uint64_t io_bitmap_b;
	uint32_t proc_ctrl;

	proc_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL1);
	io_bitmap_a = vmcs_read(vmcs, VMCS_IO_BITMAP_A);
	io_bitmap_b = vmcs_read(vmcs, VMCS_IO_BITMAP_B);
	if (proc_ctrl & PROC_IO_BITMAPS)
	{
		if (((io_bitmap_a & 0xFFFULL) != 0) || ((io_bitmap_b & 0xFFFULL) != 0))
		{
			print_info("Bits 11:0 of each I/O-bitmap address must be 0.\n");
		}

		if ((io_bitmap_a >> get_max_phy_addr()) || (io_bitmap_b >> get_max_phy_addr()))
		{
			print_info("The address should not set any bits beyond the processor's physical-address width.\n");
		}
	}
}

static void check_vmx_msr_bitmap(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "use MSR bitmaps" VM-execution control is 1,
	 **  bits 11:0 of the MSR-bitmap address must be 0.
	 **  The address should not set any bits beyond the
	 **  processor's physical-address width.
	 */
	uint64_t msr_bitmap;
	uint32_t proc_ctrl;

	proc_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL1);
	msr_bitmap = vmcs_read(vmcs, VMCS_MSR_BITMAP);
	if (proc_ctrl & PROC_MSR_BITMAPS)
	{
		if ((msr_bitmap & 0xFFFULL) != 0)
		{
			print_info(" Bits 11:0 of the MSR-bitmap address must be 0.\n");
		}

		if ((msr_bitmap >> get_max_phy_addr()))
		{
			print_info("The address should not set any bits beyond the processor's physical-address width.\n");
		}
	}
}

static void check_vmx_tpr_shadow(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "use TPR shadow" VM-execution control is 1,
	 **  the virtual-APIC address must satisfy the following checks:
	 **  Bits 11:0 of the address must be 0.
	 **  The address should not set any bits beyond the processor's
	 **  physical-address width.
	 **  If the "use TPR shadow" VM-execution control is 1
	 **  and the "virtual-interrupt delivery" VM-execution
	 **  control is 0, bits 31:4 of the TPR threshold VM-execution
	 **  control field must be 0.
	 **  The following check is performed if the "use TPR shadow"
	 **  VM-execution control is 1 and the "virtualize APIC accesses"
	 **  and "virtual-interrupt delivery" VM-execution controls are both 0:
	 **  the value of bits 3:0 of the TPR threshold VM-execution control
	 **  field should not be greater than the value of bits 7:4 of VTPR.
	 */
	uint32_t proc_ctrl;
	uint32_t proc_ctrl2;

	proc_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL1);
	proc_ctrl2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	if (proc_ctrl & PROC_TPR_SHADOW)
	{
		// TODO: The virtual -APIC address will not be used, so we do not check it here.
	}

	if ((proc_ctrl & PROC_TPR_SHADOW) && ((proc_ctrl2 & PROC2_VINT_DELIVERY) == 0))
	{
		// TODO: The TPR threshold will not be used, so we do not check it here.
	}

	if ((proc_ctrl & PROC_TPR_SHADOW)
		&& ((proc_ctrl2 & PROC2_VAPIC_ACCESSES) == 0)
		&& ((proc_ctrl2 & PROC2_VINT_DELIVERY) == 0)){
		// TODO: The TPR threshold will not be used, so we do not check it here.
	}
}

static void check_vmx_nmi_exiting(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "NMI exiting" VM-execution control is 0,
	 **  the "virtual NMIs" VM-execution control must be 0.
	 */
	uint32_t pin_ctrl;

	pin_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PIN_CTRL);
	if ((pin_ctrl & PIN_NMI_EXIT) == 0)
	{
		if (pin_ctrl & PIN_VIRTUAL_NMI)
		{
			print_info("If the NMI exiting VM-execution control is 0,\n");
			print_info("The virtual NMIs VM-execution control must be 0.\n");
		}
	}
}

static void check_vmx_virtual_nmi(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "virtual NMIs" VM-execution control is 0,
	 **  the "NMI-window exiting" VM-execution control must be 0.
	 */
	uint32_t pin_ctrl;
	uint32_t proc_ctrl;

	pin_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PIN_CTRL);
	proc_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL1);
	if ((pin_ctrl & PIN_VIRTUAL_NMI) == 0)
	{
		if (proc_ctrl & PROC_NMI_WINDOW_EXIT)
		{
			print_info("If the virtual NMIs VM-execution control is 0,\n");
			print_info("The NMI-window exiting VM-execution control must be 0.\n");
		}
	}
}

static void check_vmx_apic_access(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "virtualize APIC-accesses" VM-execution control is 1,
	 **  the APIC-access address must satisfy the following checks:
	 **  Bits 11:0 of the address must be 0.
	 **  The address should not set any bits beyond the processor's
	 **  physical-address width.
	 */
	uint32_t proc_ctrl2;

	proc_ctrl2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);

	if (proc_ctrl2 & PROC2_VAPIC_ACCESSES)
	{
		// TODO: The virtual -APIC address will not be used, so we do not check it here.
	}
}

static void check_vmx_2xpaic_mode(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "use TPR shadow" VM-execution control is 0,
	 **  the following VM-execution controls must also be 0:
	 **  "virtualize x2APIC mode", "APIC-register virtualization",
	 **  and "virtual-interrupt delivery".
	 **  If the "virtualize x2APIC mode" VM-execution control is 1,
	 **  the "virtualize APIC accesses" VM-execution control must be 0.
	 */
	uint32_t proc_ctrl;
	uint32_t proc_ctrl2;

	proc_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL1);
	proc_ctrl2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	if ((proc_ctrl & PROC_TPR_SHADOW) == 0)
	{
		if ((proc_ctrl2 & PROC2_VX2APIC_MODE)
			|| (proc_ctrl2 & PROC2_APIC_REG_VIRTUALIZE)
			|| (proc_ctrl2 & PROC2_VINT_DELIVERY)){
				print_info("If TPR_SHADOW is 0, X2APIC, APIC_REGISTER, INT_DELIVERY must also be 0\n");
			}
	}

	if (proc_ctrl2 & PROC2_VX2APIC_MODE)
	{
		if (proc_ctrl2 & PROC2_VAPIC_ACCESSES)
		{
			print_info(" If X2APIC_MODE is 1, the virtualize APIC accesses VM-execution control must be 0.\n");
		}
	}
}

static void check_vmx_virtual_intr_delivery(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "virtual-interrupt delivery" VM-execution control
	 **  is 1, the "external-interrupt exiting" VM-execution
	 **  control must be 1.
	 */
	uint32_t proc_ctrl2;
	uint32_t pin_ctrl;

	proc_ctrl2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	pin_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PIN_CTRL);
	if (proc_ctrl2 & PROC2_VINT_DELIVERY)
	{
		if ((pin_ctrl & PIN_EXINT_EXIT) == 0)
		{
			print_info(" If INT_DELIVERY is 1, the external-interrupt exiting VM-execution control must be 1.\n");
		}
	}
}

static void check_vmx_process_post_intr(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "process posted interrupts" VM-execution
	 **  control is 1, the following must be true:
	 **  The "virtual-interrupt delivery" VM-execution control is 1.
	 **  The "acknowledge interrupt on exit" VM-exit control is 1.
	 **  The posted-interrupt notification vector has a value in the range 0-255.
	 **  Bits 5:0 of the posted-interrupt descriptor address are all 0.
	 **  The posted-interrupt descriptor address does not set any bits beyond
	 **  the processor's physical-address width.
	 */
	uint32_t proc_ctrl2;
	uint32_t pin_ctrl;

	proc_ctrl2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	pin_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PIN_CTRL);
	if (pin_ctrl & PIN_PROC_POSTED_INT)
	{
		if ((proc_ctrl2 & PROC2_VINT_DELIVERY) == 0)
		{
			print_info("If the process posted interrupts VM-execution control is 1.\n");
			print_info("The virtual-interrupt delivery VM-execution control is 1.\n");
		}

		if ((proc_ctrl2 & EXIT_ACK_INT_EXIT) == 0)
		{
			print_info("If the process posted interrupts VM-execution control is 1.\n");
			print_info("The acknowledge interrupt on exit VM-exit control is 1.\n");
		}

		// TODO: The post-interrupt notifaction vector  will not be used, so we do not check it here.

		// TODO: The post-interrupt descriptor address  will not be used, so we do not check it here.
	}
}

static void check_vmx_enable_vpid(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "enable VPID" VM-execution control is 1,
	 **  the value of the VPID VM-execution control field
	 **  must not be 0000H.
	 */
	uint32_t vpid;
	uint32_t proc_ctrl2;

	vpid = (uint32_t)vmcs_read(vmcs, VMCS_VPID);
	proc_ctrl2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	if (proc_ctrl2 & PROC2_ENABLEC_VPID)
	{
		if (vpid == 0)
		{
			print_info("If enable VPID VM-execution controls is 1.\n");
			print_info("The value of the VPID VM-execution control field must not be .\n");
		}
	}
}

static void check_vmx_enable_ept(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "enable EPT" VM-execution control is 1,
	 **  the EPTP VM-execution control field must satisfy the following checks:
	 **  The EPT memory type must be a value supported by the processor as indicated
	 **  in the IA32_VMX_EPT_VPID_CAP MSR.
	 **  Bits 5:3 must be 3, indicating an EPT page-walk length of 4;
	 **  Bit 6 must be 0 if bit 21 of the IA32_VMX_EPT_VPID_CAP MSR
	 **  is read as 0, indicating that the processor does not support
	 **  accessed and dirty flags for EPT.
	 **  Reserved bits 11:7 and 63:N must all be 0.
	 */
	uint32_t proc_ctrl2;
	vmx_ept_vpid_cap_t ept_vpid_cap;
	eptp_t eptp;

	eptp.uint64 = vmcs_read(vmcs, VMCS_EPTP_ADDRESS);
	ept_vpid_cap.uint64 = get_ept_vpid_cap();
	proc_ctrl2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	if (proc_ctrl2 & PROC2_ENABLE_EPT)
	{
		if (eptp.bits.emt == CACHE_TYPE_WB)
		{
			if (ept_vpid_cap.bits.wb == 0)
			{
				print_info("The memory type wb is not supported by MSR.\n");
			}
		}

		if (eptp.bits.emt == CACHE_TYPE_UC)
		{
			if (ept_vpid_cap.bits.uc == 0)
			{
				print_info("The memory type uc is not supported by MSR.\n");
			}
		}

		if (eptp.bits.gaw != 3)
		{
			print_info("Bit[5:3], must be 3 since ept page-walk length is 4.\n");
		}

		if (ept_vpid_cap.bits.dirty_flag == 0)
		{
			if (eptp.bits.ad)
			{
				print_info("If bit 21 of the IA32_VMX_EPT_VPID_CAP_MSR is read as 0,\n");
				print_info("Bit 6 (ad for EPT) must be 0.\n");
			}
		}

		if(eptp.bits.rsvd || (eptp.uint64 >> get_max_phy_addr()))
		{
			print_info("Reserved bits 11:7 and 63:N must all be 0.\n");
		}
	}
}

static void check_vmx_enable_pml(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "enable PML" VM-execution control is 1,
	 **  the "enable EPT" VM-execution control must also be 1.
	 **  In addition, the PML address must satisfy the following checks:
	 **  Bits 11:0 of the address must be 0.
	 **  The address should not set any bits beyond the processor's physical-address width.
	 */
	uint32_t proc_ctrl2;

	proc_ctrl2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	if ((proc_ctrl2 & (PROC2_ENABLE_PML | PROC2_ENABLE_EPT)) == (PROC2_ENABLE_PML | PROC2_ENABLE_EPT))
	{
		// TODO: The PML address  will not be used, so we do not check it here.
	}
}

static void check_vmx_ug(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "unrestricted guest" VM-execution control is 1,
	 **  the "enable EPT" VM-execution control must also be 1.
	 */
	uint32_t proc_ctrl2;

	proc_ctrl2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	if (proc_ctrl2 & PROC2_UNRESTRICTED_GUEST)
	{
		if ((proc_ctrl2 & PROC2_ENABLE_EPT) == 0)
		{
			print_info("If ug is 1, the enable EPT must also be 1.\n");
		}
	}
}

static void check_vmx_enable_vm_function(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "enable VM functions" processor-based
	 **  VM-execution control is 1, reserved bits in the VM-function
	 **  controls must be clear.
	 **  In addition, the EPTP-list address must satisfy the following checks:
	 **   Bits 11:0 of the address must be 0.
	 **   The address must not set any bits beyond the processor's physical-address width.
	 */
	uint32_t proc_ctrl2;

	proc_ctrl2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);

	// TODO: The VM funtion control will not be used, so we do not check it here.

	if (proc_ctrl2 & PROC2_ENABLE_VM_FUNC)
	{
		// TODO: The  EPTP-list  will not be used, so we do not check it here.
	}
}

static void check_vmx_vmcs_shadow(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "VMCS shadowing" VM-execution control is 1,
	 **  the VMREAD-bitmap and VMWRITE-bitmap addresses must
	 **  each satisfy the following checks:
	 **   Bits 11:0 of the address must be 0.
	 **   The address must not set any bits beyond the processor's physical-address width.
	 */
	 uint32_t proc_ctrl2;

	proc_ctrl2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	if (proc_ctrl2 & PROC2_VMCS_SHADOW)
	{
		// TODO: The VMREAD-bitmap and VMWRITE-bitmap addresses  will not be used, so we do not check it here.
	}
}

static void check_vmx_ept_violation(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 **  If the "EPT-violation" VM-execution control is 1,
	 **  the virtualization-exception information address must
	 **  satisfy the following checks:
	 **   Bits 11:0 of the address must be 0.
	 **   The address must not set any bits beyond the processor's physical-address width.
	 */
	uint32_t proc_ctrl2;

	proc_ctrl2 = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	if (proc_ctrl2 & PROC2_EPT_VIOLATION)
	{
		// TODO: The virtualization-exception information address  will not be used, so we do not check it here.
	}
}

static void check_vm_execution_ctrl_field(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.1:
	 */
	check_vmx_pin_ctrl(vmcs);
	check_vmx_proc_ctrl(vmcs);
	check_vmx_proc2_ctrl(vmcs);
	check_vmx_cr3_count(vmcs);
	check_vmx_io_bitmap(vmcs);
	check_vmx_msr_bitmap(vmcs);
	check_vmx_tpr_shadow(vmcs);
	check_vmx_nmi_exiting(vmcs);
	check_vmx_virtual_nmi(vmcs);
	check_vmx_apic_access(vmcs);
	check_vmx_2xpaic_mode(vmcs);
	check_vmx_virtual_intr_delivery(vmcs);
	check_vmx_process_post_intr(vmcs);
	check_vmx_enable_vpid(vmcs);
	check_vmx_enable_ept(vmcs);
	check_vmx_enable_pml(vmcs);
	check_vmx_ug(vmcs);
	check_vmx_enable_vm_function(vmcs);
	check_vmx_vmcs_shadow(vmcs);
	check_vmx_ept_violation(vmcs);
}

static void check_vm_exit_ctrl(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.2:
	 **  Reserved bits in the VM-exit controls must be set properly.
	 **  Software may consult the VMX capability MSRs to determine
	 **  the proper settings.
	 */
	uint32_t exit_ctrl;
	uint32_t may0,may1;

	exit_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_EXIT_CTRL);
	may1 = get_exitctl_cap(&may0);
	if (is_compliant(exit_ctrl, may0, may1) == FALSE)
	{
		print_info("incorrect value in VM-exit control.\n");
	}
}

static void check_vm_active_preemption_timer(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.2:
	 **  If the "activate VMX-preemption timer" VM-execution control is 0,
	 **  the "save VMX-preemption timer value" VMexit control must also be 0.
	 */
	uint32_t pin_ctrl;
	uint32_t exit_ctrl;

	pin_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PIN_CTRL);
	exit_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_EXIT_CTRL);
	if ((pin_ctrl & PIN_PREEMPTION_TIMER) == 0)
	{
		if (exit_ctrl & EXIT_SAVE_PREE_TIME)
		{
			print_info("The save VMX-preemption timer value must be 0.\n");
		}
	}
}

static void check_vm_msr_addr(uint64_t addr, uint32_t count, const char* name)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.2:
	 **  The following checks are performed for the MSR store/load address
	 **  if the MSR store/load count field is non-zero:
	 **   The lower 4 bits of the MSR-store/load address must be 0.
	 **   The address should not set any bits beyond the processor¡¯s physical-address width.
	 **   The address of the last byte in the  MSR-store/load area should not set any bits
	 **   beyond the processor's physical-address width. The address of this last byte
	 **   is MSR-store/load address + (MSR count * 16) ¨C1.
	 **   If IA32_VMX_BASIC[48] is read as 1, neither address should set any bits in the range 63:32.
	 */
	uint64_t last_byte_addr;
	basic_info_t basic_info;

	basic_info.uint64 = get_basic_cap();
	if (count)
	{
		if (addr & 0xF)
		{
			print_info("The lower 4 bits of the VM %s addr must be 0.\n", name);
		}

		if(addr >> get_max_phy_addr())
		{
			print_info("The %s addr should not set any bits beyond the processor's physical-address width.\n", name);
		}

		last_byte_addr = addr + (count*16) - 1;
		if(last_byte_addr >> get_max_phy_addr())
		{
			print_info("The %s last byte addr should not set any bits beyond the processor¡¯s physical-address width..\n",
				name);
		}

		if (basic_info.bits.phy_addr_width)
		{
			if(is_zero_upper(addr) == FALSE)
			{
				print_info("If IA32_VMX_BASIC[48] is read as 1, The %s addr will not be set in range 63:32.\n", name);
			}
		}
	}
}

static void check_vm_exit_msr_store_addr(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.2:
	 */
	uint32_t store_count;
	uint64_t store_addr;

	store_count = (uint32_t)vmcs_read(vmcs, VMCS_EXIT_MSR_STORE_COUNT);
	store_addr = vmcs_read(vmcs, VMCS_EXIT_MSR_STORE_ADDR);
	check_vm_msr_addr(store_addr, store_count, "exit store");
}

static void check_vm_exit_msr_load_addr(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.2:
	 */
	uint32_t load_count;
	uint64_t load_addr;

	load_count = (uint32_t)vmcs_read(vmcs, VMCS_EXIT_MSR_STORE_COUNT);
	load_addr = vmcs_read(vmcs, VMCS_EXIT_MSR_STORE_ADDR);
	check_vm_msr_addr(load_addr, load_count, "exit load");
}

static void check_vm_exit_ctrl_field(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.2:
	 */
	check_vm_exit_ctrl(vmcs);
	check_vm_active_preemption_timer(vmcs);
	check_vm_exit_msr_store_addr(vmcs);
	check_vm_exit_msr_load_addr(vmcs);
}

static void check_vm_entry_ctrl(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.3:
	 **  Reserved bits in the VM-entry controls must be set properly.
	 **  Software may consult the VMX capability MSRs to determine
	 **  the proper settings.
	 */
	uint32_t entry_ctrl;
	uint32_t may0,may1;

	entry_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_ENTRY_CTRL);
	may1 = get_entryctl_cap(&may0);
	if (is_compliant(entry_ctrl, may0, may1) == FALSE)
	{
		print_info("incorrect value in VM-entry control.\n");
	}
}

static void check_vm_entry_intr_type(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.3:
	 **  The field¡¯s interruption type (bits 10:8) is not set to a reserved value.
	 **  Value 1 is reserved on all logical processors; value 7 (other event)
	 **  is reserved on logical processors that do not support the 1-setting of
	 **  the "monitor trap flag" VM-execution control.
	 **  The field¡¯s vector (bits 7:0) is consistent with the interruption type:
	 **  If the interruption type is non-maskable interrupt (NMI), the vector is 2.
	 **  If the interruption type is hardware exception, the vector is at most 31.
	 **  If the interruption type is other event, the vector is 0 (pending MTF VM exit).
	 */
	vmx_entry_info_t entry_info;

	entry_info.uint32 = vmcs_read(vmcs, VMCS_ENTRY_INTR_INFO);
	if (entry_info.bits.valid)
	{
		if ((entry_info.bits.interrupt_type == VECTOR_TYPE_OTHER_EVE) || (entry_info.bits.interrupt_type == VECTOR_TYPE_RES))
		{
			print_info("The field¡¯s interruption type (bits 10:8) is not set to a reserved value.\n");
		}

		if (entry_info.bits.interrupt_type == VECTOR_TYPE_NMI)
		{
			if (entry_info.bits.vector != EXCEPTION_NMI)
			{
				print_info("If the interruption type is non-maskable interrupt (NMI), the vector is 2.\n");
			}
		}

		if (entry_info.bits.interrupt_type == VECTOR_TYPE_HW_EXCEPTION)
		{
			if (entry_info.bits.vector > 31)
			{
				print_info("If the interruption type is hardware exception, the vector is at most 31.\n");
			}
		}

		if (entry_info.bits.interrupt_type == VECTOR_TYPE_OTHER_EVE)
		{
			if (entry_info.bits.vector != 0)
			{
				print_info("If the interruption type is other event, the vector is 0 (pending MTF VM exit).\n");
			}
		}
	}
}

static void check_vm_entry_deliver_erro_code(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.3:
	 **  The field's deliver-error-code bit (bit 11) is 1
	 **  if and only if (1) either
	 **   (a) the "unrestricted guest" VMexecution control is 0;
	 **   (b) bit 0 is set in the CR0 field in the guest-state area;
	 **  (2) the interruption type is hardware exception;
	 **  (3) the vector indicates an exception that would normally deliver
	 **  an error code (8 = #DF; 10 = TS; 11 = #NP; 12 = #SS; 13 = #GP; 14 = #PF; or 17 =#AC).
	 */
	vmx_entry_info_t entry_info;
	uint32_t proc2_ctrl;
	uint64_t cr0;
	boolean_t deliver_code;

	proc2_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL2);
	entry_info.uint32 = (uint32_t)vmcs_read(vmcs, VMCS_ENTRY_INTR_INFO);
	cr0 = vmcs_read(vmcs, VMCS_GUEST_CR0);
	deliver_code = (((proc2_ctrl & PROC2_UNRESTRICTED_GUEST) == 0) || (cr0 & CR0_PE))
		&& (entry_info.bits.interrupt_type == VECTOR_TYPE_HW_EXCEPTION)
		&& (entry_info.bits.vector == EXCEPTION_DF
			|| entry_info.bits.vector == EXCEPTION_TS
			|| entry_info.bits.vector == EXCEPTION_NP
			|| entry_info.bits.vector == EXCEPTION_SS
			|| entry_info.bits.vector == EXCEPTION_GP
			|| entry_info.bits.vector == EXCEPTION_PF
			|| entry_info.bits.vector == EXCEPTION_AC);

	if (entry_info.bits.deliver_code != deliver_code)
	{
		print_info("is %d while it is expected to be %d.\n", entry_info.bits.deliver_code, deliver_code);
	}
}

static void check_vm_entry_info_reserved(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.3:
	 **  Reserved bits in the field (30:12) are 0.
	 */
	vmx_entry_info_t entry_info;

	entry_info.uint32 = vmcs_read(vmcs, VMCS_ENTRY_INTR_INFO);
	if (entry_info.bits.valid)
	{
		if (entry_info.bits.resv_12_30)
		{
			print_info("The entry info's reserved bits in the field (30:12) must be 0.\n");
		}
	}
}

static void check_vm_entry_execption_erro_code(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.3:
	 **  If the deliver-error-code bit (bit 11) is 1,
	 **  bits 31:15 of the VM-entry exception error-code field are 0.
	 */
	vmx_entry_info_t entry_info;
	uint32_t err_code;

	entry_info.uint32 = vmcs_read(vmcs, VMCS_ENTRY_INTR_INFO);
	err_code = vmcs_read(vmcs, VMCS_ENTRY_ERR_CODE);
	if (entry_info.bits.valid)
	{
		if (entry_info.bits.deliver_code)
		{
			if (err_code & 0xFFFF8000)
			{
				print_info("If the deliver-error-code bit is 1, bits 31:15 of the VM-entry exception error-code field must be 0.\n");
			}
		}
	}
}

static void check_vm_entry_instr_length(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.3:
	 **  If the interruption type is software interrupt,
	 **  software exception, or privileged software exception,
	 **  the VM-entry instruction-length field is in the range 0¨C15.
	 **  A VM-entry instruction length of 0 is allowed only
	 **  if IA32_VMX_MISC[30] is read as 1.
	 */
	vmx_entry_info_t entry_info;
	uint32_t instr_len;
	msr_misc_data_t misc_data;

	entry_info.uint32 = vmcs_read(vmcs, VMCS_ENTRY_INTR_INFO);
	instr_len = vmcs_read(vmcs, VMCS_ENTRY_INSTR_LEN);
	misc_data.uint64 = get_misc_data_cap();
	if (entry_info.bits.valid)
	{
		if (entry_info.bits.interrupt_type == VECTOR_TYPE_SW_INT
			|| entry_info.bits.interrupt_type == VECTOR_TYPE_PRI_SW_INT
			|| entry_info.bits.interrupt_type == VECTOR_TYPE_SW_EXCEPTION){
			if(instr_len > 15)
			{
				print_info("the VM-entry instruction-length field must be in the range 0-15.\n");
			}
		}

		if (instr_len == 0)
		{
			if (misc_data.bits.instr_len_allow_zero == 0)
			{
				print_info("If VM-entry instruction length is 0, the IA32_VMX_MISC[30] must be 1.\n");
			}
		}
	}
}

static void check_vm_entry_event_injection(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.3:
	 **  Fields relevant to VM-entry event injection must be set properly.
	 **  These fields are the VM-entry interruptioninformation field,
	 **  the VM-entry exception error code, and the VM-entry instruction length.
	 **  If the valid bit (bit 31) in the VM-entry interruption-information field is 1,
	 **  the following must hold:
	 */
	check_vm_entry_intr_type(vmcs);
	check_vm_entry_deliver_erro_code(vmcs);
	check_vm_entry_info_reserved(vmcs);
	check_vm_entry_execption_erro_code(vmcs);
	check_vm_entry_instr_length(vmcs);
}

static void check_vm_entry_msr_load_addr(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.3:
	 */
	uint64_t load_addr;
	uint32_t load_count;

	load_count = (uint32_t)vmcs_read(vmcs, VMCS_ENTRY_MSR_LOAD_COUNT);
	load_addr = vmcs_read(vmcs, VMCS_ENTRY_MSR_LOAD_ADDR);
	check_vm_msr_addr(load_addr, load_count, "entry load");
}

static void check_vm_entry_to_smm(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.3:
	 **  If the processor is not in SMM, the "entry to SMM" and
	 **  "deactivate dual-monitor treatment" VM-entry controls must be 0.
	 **  The "entry to SMM" and "deactivate dual-monitor treatment" VM-entry
	 **  controls cannot both be 1.
	 */
	uint32_t entry_ctrl;

	entry_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_ENTRY_CTRL);

	/*in this evmm, both "entry to SMM" and "deactivate dual-monitor treatment" are must be 0*/
	if (entry_ctrl & (ENTRY_TO_SMM | ENTRY_DE_2MONITOR_TREATMENT))
	{
		print_info(" the entry to SMM and deactivate dual-monitor treatment VM-entry controls must both be 0.\n");
	}
}

static void check_vm_entry_ctrl_field(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.1.3:
	 */
	check_vm_entry_ctrl(vmcs);
	check_vm_entry_event_injection(vmcs);
	check_vm_entry_msr_load_addr(vmcs);
	check_vm_entry_to_smm(vmcs);
}

static void check_vmx_ctrl(vmcs_obj_t vmcs)
{
	/* According to IA32 Manual: Volume 3, Chapter 26.2.1*/
	check_vm_execution_ctrl_field(vmcs);
	check_vm_exit_ctrl_field(vmcs);
	check_vm_entry_ctrl_field(vmcs);
}

static void check_host_cr0(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.2:
	 **  The CR0 field must not set any bit to a value not
	 **  supported in VMX operation.
	 */
	uint64_t cr0;
	uint64_t may0,may1;

	cr0 = vmcs_read(vmcs, VMCS_HOST_CR0);
	may1 = get_cr0_cap(&may0);
	if (is_compliant(cr0, may0, may1) == FALSE)
	{
		print_info("Host CR0 is not compliant with VMX requirements.\n");
	}
}

static void check_host_cr4(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.2:
	 **  The CR4 field must not set any bit to a value not
	 **  supported in VMX operation.
	 */
	uint64_t cr4;
	uint64_t may0,may1;

	cr4 = vmcs_read(vmcs, VMCS_HOST_CR4);
	may1 = get_cr4_cap(&may0);
	if (is_compliant(cr4, may0, may1) == FALSE)
	{
		print_info("Host CR4 is not compliant with VMX requirements.\n");
	}
}

static void check_host_64bits_settings(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.2:
	 **  On processors that support Intel 64 architecture,
	 **  the CR3 field must be such that bits 63:52 and bits
	 **  in the range 51:32 beyond the processor¡¯s
	 **  physical-address width must be 0.
	 **  On processors that support Intel 64 architecture,
	 **  the IA32_SYSENTER_ESP field and the IA32_SYSENTER_EIP
	 **  field must each contain a canonical address.
	 */
	uint64_t cr3;
	boolean_t is_64bit;
	uint64_t sysenter_eip;
	uint64_t sysenter_esp;

	cr3 = vmcs_read(vmcs, VMCS_HOST_CR3);
	is_64bit  = ((vmcs_read(vmcs, VMCS_ENTRY_CTRL) & ENTRY_GUEST_IA32E_MODE) != 0);
	sysenter_eip = vmcs_read(vmcs, VMCS_HOST_SYSENTER_EIP);
	sysenter_esp = vmcs_read(vmcs, VMCS_HOST_SYSENTER_ESP);

	if(cr3 >> get_max_phy_addr())
	{
		print_info("Host CR3 beyond the processor¡¯s physical-address width must be 0\n");
	}

	if(addr_is_canonical(is_64bit, sysenter_esp) == FALSE)
	{
		print_info("Host SYSENTER ESP must be canonical.\n");
	}

	if(addr_is_canonical(is_64bit, sysenter_eip) == FALSE)
	{
		print_info("Host SYSENTER EIP must be canonical.\n");
	}
}

static void check_host_perf_global_ctrl(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.2:
	 **  If the "load IA32_PERF_GLOBAL_CTRL" VM-exit control is 1,
	 **  bits reserved in the IA32_PERF_GLOBAL_CTRL MSR must be 0
	 **  in the field for that register.
	 */
	uint32_t exit_ctrl;
	uint64_t host_perf;

	exit_ctrl = vmcs_read(vmcs, VMCS_EXIT_CTRL);
	host_perf = vmcs_read(vmcs, VMCS_HOST_PERF_G_CTRL);
	if (exit_ctrl & EXIT_LOAD_IA32_PERF_CTRL)
	{
		if (host_perf & MSR_PERF_GLOBAL_CTRL_RESERVED)
		{
			print_info("Host PERF_GLOBSL_CTRL.Reserved must be 0.\n");
		}
	}
}

static void check_host_pat(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.2:
	 **  If the "load IA32_PA" VM-exit control is 1,
	 **  the value of the field for the IA32_PAT MSR must
	 **  be one that could be written by WRMSR without fault
	 **  at CPL 0. Specifically, each of the 8 bytes
	 **  in the field must have one of the values 0
	 **  (UC), 1 (WC), 4 (WT), 5 (WP), 6 (WB), or 7 (UC-).
	 */
	uint32_t exit_ctrl;
	boolean_t is_valid = TRUE;

	exit_ctrl = vmcs_read(vmcs, VMCS_EXIT_CTRL);
	if (exit_ctrl & EXIT_LOAD_IA32_PAT)
	{
		union {
			uint64_t u64;
			uint8_t  u8[8];
		} host_pat;

		uint32_t i;

		host_pat.u64 = vmcs_read(vmcs, VMCS_HOST_PAT);

		for (i = 0; i < 8; ++i)
		{
			if (0 == host_pat.u8[i]
				|| 1 == host_pat.u8[i]
				|| 4 == host_pat.u8[i]
				|| 5 == host_pat.u8[i]
				|| 6 == host_pat.u8[i]
				|| 7 == host_pat.u8[i])
			{
				continue;
			}
			// if reached this point, then PAT is bad
			is_valid = FALSE;
		}

		if ( ! is_valid)
		{
			print_info("Host PAT is invalid\n");
		}
	}
}

static void check_host_efer(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.2:
	 **  If the "load IA32_EFER" VM-exit control is 1,
	 **  bits reserved in the IA32_EFER MSR must be 0
	 **  in the field for that register. In addition,
	 **  the values of the LMA and LME bits in the field
	 **  must each be that of the "host addressspace size" VM-exit control.
	 */
	uint32_t exit_ctrl;
	msr_efer_t host_efer;
	boolean_t addr_space;

	exit_ctrl = vmcs_read(vmcs, VMCS_EXIT_CTRL);
	host_efer.uint64 = vmcs_read(vmcs, VMCS_HOST_EFER);
	addr_space = ((exit_ctrl & EXIT_HOST_ADDR_SPACE) != 0);
	if (exit_ctrl & EXIT_LOAD_IA32_EFER)
	{
		if (host_efer.uint64 & MSR_EFER_RESERVED)
		{
			print_info("The host efer reserved bits must be 0\n");
		}

		if ((host_efer.bits.lma != addr_space)
			|| (host_efer.bits.lme != addr_space)){
			print_info("The host efer lma and lme bits must be that of the host addressspace size VM-exit control.\n");
		}
	}
}

static void check_host_ctrl_msrs(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.2:
	 */
	check_host_cr0(vmcs);
	check_host_cr4(vmcs);
	check_host_64bits_settings(vmcs);
	check_host_perf_global_ctrl(vmcs);
	check_host_pat(vmcs);
	check_host_efer(vmcs);
}

static void check_host_segment_sel_base(seg_id_t seg_id, uint16_t seg_sel, uint64_t seg_base, boolean_t host_is_64bit)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.3:
	 **  In the selector field for each of CS, SS, DS, ES, FS, GS
	 **  and TR, the RPL (bits 1:0) and the TI flag (bit 2) must
	 **  be 0.
	 **  The selector fields for CS and TR cannot be 0000H.
	 **  The selector field for SS cannot be 0000H if the
	 **  "host address-space size" VM-exit control is 0.
	 **  On processors that support Intel 64 architecture,
	 **  the base-address fields for FS, GS, and TR must
	 **  contain canonical addresses.
	 */

	/*check segment selector*/
	switch(seg_id)
	{
		case SEG_CS:
		case SEG_SS:
		case SEG_DS:
		case SEG_ES:
		case SEG_FS:
		case SEG_GS:
		case SEG_TR:
			if (seg_sel & 3)
			{
				print_info("Host %s.selector RPL flag must be zero.\n", SEGMENT_NAME(seg_id));
			}

			if (seg_sel & 4)
			{
				print_info("Host %s.selector TI flag must be zero.\n", SEGMENT_NAME(seg_id));
			}
			break;

		default:
			print_info("ERRO: invalid segment id (%d) in %s().\n", seg_id, __FUNCTION__);
			break;
	}

	switch(seg_id)
	{
		case SEG_CS:
		case SEG_TR:
			if (seg_sel == 0)
			{
				print_info("Host %s.selector can not be zero.\n", SEGMENT_NAME(seg_id));
			}
			break;

		default:
			break;
	}

	switch(seg_id)
	{
		case SEG_SS:
			if (!host_is_64bit)
			{
				if (seg_sel == 0)
				{
					print_info("Host ss.selector can not be zero.\n");
				}
			}
		default:
			break;
	}

	/*check segment base*/
	switch (seg_id)
	{
		case SEG_FS:
		case SEG_GS:
		case SEG_TR:
			if (addr_is_canonical(host_is_64bit, seg_base) == FALSE)
			{
				print_info("The host %s.base must be canonical.\n", SEGMENT_NAME(seg_id));
			}
			break;

		default:
			break;
	}
}

static void check_host_xdtr_base(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.3:
	 **  On processors that support Intel 64 architecture,
	 **  the base-address fields for GDTR, and IDTR must
	 **  contain canonical addresses.
	 */
	uint64_t gdt_base;
	uint64_t idt_base;
	boolean_t is_64bit;

	gdt_base = vmcs_read(vmcs, VMCS_HOST_GDTR_BASE);
	idt_base = vmcs_read(vmcs, VMCS_HOST_IDTR_BASE);
	is_64bit = ((vmcs_read(vmcs, VMCS_EXIT_CTRL) & EXIT_HOST_ADDR_SPACE) != 0);

	if (addr_is_canonical(is_64bit, gdt_base) == FALSE)
	{
		print_info("The host gdt.base must be canonical.\n");
	}

	if (addr_is_canonical(is_64bit, idt_base) == FALSE)
	{
		print_info("The host idt.base must be canonical.\n");
	}
}

static void check_host_segment(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.3:
	 */
	uint16_t cs_sel;
	uint16_t ss_sel;
	uint16_t ds_sel;
	uint16_t es_sel;
	uint16_t fs_sel;
	uint16_t gs_sel;
	uint16_t tr_sel;
	uint64_t fs_base;
	uint64_t gs_base;
	uint64_t tr_base;
	boolean_t is_64bit;

	cs_sel = (uint16_t) vmcs_read(vmcs, VMCS_HOST_CS_SEL);
	ss_sel = (uint16_t) vmcs_read(vmcs, VMCS_HOST_CS_SEL);
	ds_sel = (uint16_t) vmcs_read(vmcs, VMCS_HOST_CS_SEL);
	es_sel = (uint16_t) vmcs_read(vmcs, VMCS_HOST_CS_SEL);
	fs_sel = (uint16_t) vmcs_read(vmcs, VMCS_HOST_CS_SEL);
	gs_sel = (uint16_t) vmcs_read(vmcs, VMCS_HOST_CS_SEL);
	tr_sel = (uint16_t) vmcs_read(vmcs, VMCS_HOST_CS_SEL);
	fs_base = vmcs_read(vmcs, VMCS_HOST_FS_BASE);
	gs_base = vmcs_read(vmcs, VMCS_HOST_GS_BASE);
	tr_base = vmcs_read(vmcs, VMCS_HOST_TR_BASE);
	is_64bit = ((vmcs_read(vmcs, VMCS_EXIT_CTRL) & EXIT_HOST_ADDR_SPACE) != 0);
	/*check host segment selector and base*/
	check_host_segment_sel_base(SEG_CS, cs_sel, 0, is_64bit);
	check_host_segment_sel_base(SEG_DS, ds_sel, 0, is_64bit);
	check_host_segment_sel_base(SEG_SS, ss_sel, 0, is_64bit);
	check_host_segment_sel_base(SEG_ES, es_sel, 0, is_64bit);
	check_host_segment_sel_base(SEG_FS, fs_sel, fs_base, is_64bit);
	check_host_segment_sel_base(SEG_GS, gs_sel, gs_base, is_64bit);
	check_host_segment_sel_base(SEG_TR, tr_sel, tr_base, is_64bit);
	/*check gdtr and idtr base*/
	check_host_xdtr_base(vmcs);
}

static void check_related_ia32_mode(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.4:
	 **  If the logical processor is outside IA-32e mode (if IA32_EFER.LMA = 0)
	 **  at the time of VM entry, the following must hold:
	 **   The "IA-32e mode guest" VM-entry control is 0.
	 **   The "host address-space size" VM-exit control is 0.
	 **  If the logical processor is in IA-32e mode (if IA32_EFER.LMA = 1)
	 **  at the time of VM entry, the "host addressspace size" VM-exit control must be 1.
	 */
	msr_efer_t host_efer;
	uint32_t exit_ctrl;

	exit_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_EXIT_CTRL);
	host_efer.uint64 = vmcs_read(vmcs, VMCS_HOST_EFER);
	/*This evmm always runs in 64 bit mode (IA-32e mode).*/
	if (host_efer.bits.lma == 0)
	{
		print_info("The IA32_EFER.LMA must be 1.\n");
	}

	if ((exit_ctrl & EXIT_HOST_ADDR_SPACE) == 0)
	{
		print_info("The host address-space size VM-exit control must always be 1.\n");
	}
}

static void check_related_host_addr_space_size(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.4:
	 **  This evmm always runs in 64 bit mode (IA-32e mode),
	 **  so the following must hold:
	 **   Bit 5 of the CR4 field (corresponding to CR4.PAE) is 1.
	 **   The RIP field contains a canonical address.
	 */
	uint64_t cr4;
	uint64_t rip;
	uint32_t exit_ctrl;

	exit_ctrl = (uint32_t)vmcs_read(vmcs, VMCS_EXIT_CTRL);
	rip = vmcs_read(vmcs, VMCS_HOST_RIP);
	cr4 = vmcs_read(vmcs, VMCS_HOST_CR4);
	/*This evmm always runs in 64 bit mode (IA-32e mode).*/
	if ((exit_ctrl & EXIT_HOST_ADDR_SPACE) == 1)
	{
		if ((cr4 & CR4_PAE) == 0)
		{
			print_info("The PAE of the host CR4 field must be 1.\n");
		}

		if (addr_is_canonical(TRUE, rip) == FALSE)
		{
			print_info("The host RIP must be canonical.\n");
		}
	}
}

static void check_related_address_space_size(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.2.4:
	 */
	check_related_ia32_mode(vmcs);
	check_related_host_addr_space_size(vmcs);
}

static void check_loading_msr(vmcs_obj_t vmcs)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 26.4:
	 ** The value of bits 31:0 is either C0000100H or C0000101.
	 ** The value of bits 31:8 is 000008H, meaning that the indexed
	 ** MSR is one that allows access to an APIC register when
	 ** the local APIC is in x2APIC mode.
	 ** The value of bits 31:0 indicates an MSR that can be written
	 ** only in system-management mode (SMM) and the VM entry did
	 ** not commence in SMM. (IA32_SMM_MONITOR_CTL is an MSR that
	 ** can be written only in SMM.)
	 ** The value of bits 31:0 indicates an MSR that cannot be loaded
	 ** on VM entries for model-specific reasons. A processor may prevent
	 ** loading of certain MSRs even if they can normally be written by WRMSR.
	 ** Such modelspecific behavior is documented in Chapter 35.
	 ** Bits 63:32 are not all 0.
	 ** An attempt to write bits 127:64 to the MSR indexed by bits 31:0 of
	 ** the entry would cause a general-protection exception if executed via
	 ** WRMSR with CPL = 0.(If VM entry has established CR0.PG = 1, the IA32_EFER
	 ** MSR should not be included in the VM-entry MSR-load area for the purpose of
	 ** modifying the LME bit.)
	 */
	uint32_t load_count;
	uint64_t load_addr;
	uint64_t entry_ctrl;
	msr_list_t * msr_list;
	uint32_t list_idx;

	load_addr = vmcs_read(vmcs, VMCS_ENTRY_MSR_LOAD_ADDR);
	load_count = (uint32_t)vmcs_read(vmcs, VMCS_ENTRY_MSR_LOAD_COUNT);
	entry_ctrl = vmcs_read(vmcs, VMCS_ENTRY_CTRL);

	if (load_count == 0)
	{
		return;
	}

	if (load_addr == 0)
	{
		print_info("%s: failed to get msr load addr.\n",__FUNCTION__);
		return;
	}

	if (!hmm_hpa_to_hva(load_addr, (uint64_t *)&msr_list))
	{
		print_info("%s: failed to get hva.\n",__FUNCTION__);
		return;
	}

	for (list_idx = 0; list_idx < load_count; list_idx++)
	{
		if ((msr_list[list_idx].msr_index == MSR_FS_BASE)
			|| (msr_list[list_idx].msr_index == MSR_GS_BASE))
		{
			print_info("The msr index(0x%x) in entry load msr list[id=%u] can not be MSR_FS_BASE or MSR_GS_BASE.\n",
				msr_list[list_idx].msr_index, list_idx);
		}

		if ((msr_list[list_idx].msr_index & 0xFFFFFF00) == 0x800)
		{
			print_info("The msr index(0x%x) in entry load msr list[id=%u] bits 31:8 can not be 0x8.\n", msr_list[list_idx].msr_index, list_idx);
		}

		if (msr_list[list_idx].msr_index == MSR_SMM_MONITOR_CTL)
		{
			if ((entry_ctrl & ENTRY_TO_SMM) == 0)
			{
				print_info("If the msr index(0x%x) in entry load msr list[id=%u] is MSR_SMM_MONITOR_CTL,\n", msr_list[list_idx].msr_index, list_idx);
				print_info("The ENTRY_TO_SMM must be 1.\n");
			}
		}

		if ((msr_list[list_idx].msr_index == MSR_BIOS_UPDT_TRIG)
			|| (msr_list[list_idx].msr_index == MSR_SIGN_BBL_CR_D3))
		{
			print_info("The msr index(0x%x) in entry load msr list[id=%u] can not be MSR_BIOS_UPDT_TRIG or MSR_SIGN_BBL_CR_D3.\n",
				msr_list[list_idx].msr_index, list_idx);
		}

		if (msr_list[list_idx].reserved != 0)
		{
			print_info("The reserved bits in entry load msr list[id=%u] must be 0.\n", list_idx);
		}

		if (msr_list[list_idx].msr_index == MSR_EFER)
		{
			print_info("The msr index(0x%x) in entry load msr list[id=%u] can not be MSR_EFER.\n", msr_list[list_idx].msr_index, list_idx);
		}
	}
}

static void check_vmx_ctrl_host_state(vmcs_obj_t vmcs)
{
	/* According to IA32 Manual: Volume 3, Chapter 26.2.*/
	check_vmx_ctrl(vmcs);
	check_host_ctrl_msrs(vmcs);
	check_host_segment(vmcs);
	check_related_address_space_size(vmcs);
}

static void vmcs_check(vmcs_obj_t vmcs)
{
	/*check guest_state*/
	vmcs_clear_all_cache(vmcs);
	check_guest_state(vmcs);
	/*check vmx and host state*/
	check_vmx_ctrl_host_state(vmcs);
	/*check loading msrs*/
	check_loading_msr(vmcs);
}

static void vmentry_failure_function_check(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	vmcs_obj_t vmcs = gcpu->vmcs;
	vmcs_check(vmcs);
}

static void vmenter_failure_check(guest_cpu_handle_t gcpu)
{
	/* According to IA32 Manual: Volume 3, Chapter 26.3.1.*/
	VMM_ASSERT_EX(gcpu, "gcpu is NULL in vmenter failure check\n");

	vmcs_check(gcpu->vmcs);

	VMM_DEADLOOP();
}

/* This module checks on the Guest State Area:
 *  Checks on Guest Control Registers, Debug Registers, and MSRs.
 *  Checks on Guest Segment Registers.
 *  Checks on Guest Descriptor-Table Registers.
 *  Checks on Guest RIP and RFLAGS.
 *  Checks on Guest Non-Register State.
 *  Checks on Guest Page-Directory-Pointer-Table Entries.
 */
void vmenter_check_init(void)
{
	event_register(EVENT_VMENTRY_FAIL, vmentry_failure_function_check);
	vmexit_install_handler(vmenter_failure_check, REASON_33_ENTRY_FAIL_GUEST);
}
