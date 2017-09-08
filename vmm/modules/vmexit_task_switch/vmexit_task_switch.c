/*******************************************************************************
* Copyright (c) 2017 Intel Corporation
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
#include "gcpu.h"
#include "isr.h"
#include "gcpu_inject_event.h"
#include "hmm.h"
#include "vmexit_cr_access.h"
#include "vmm_arch.h"
#include "vmx_cap.h"
#include "guest.h"

#include "lib/util.h"

#include "modules/vmexit_task_switch.h"

/* This is 32-bit TSS. */

typedef struct {
	uint32_t	prev_tr;        /* 0 */
	uint32_t	esp0;           /* 4 */
	uint32_t	ss0;            /* 8 */
	uint32_t	esp1;           /* 12 */
	uint32_t	ss1;            /* 16 */
	uint32_t	esp2;           /* 20 */
	uint32_t	ss2;            /* 24 */
	uint32_t	cr3;            /* 28 */
	uint32_t	eip;            /* 32 */
	uint32_t	eflags;         /* 36 */
	uint32_t	eax;            /* 40 */
	uint32_t	ecx;            /* 44 */
	uint32_t	edx;            /* 48 */
	uint32_t	ebx;            /* 52 */
	uint32_t	esp;            /* 56 */
	uint32_t	ebp;            /* 60 */
	uint32_t	esi;            /* 64 */
	uint32_t	edi;            /* 68 */
	uint32_t	es;             /* 72 */
	uint32_t	cs;             /* 76 */
	uint32_t	ss;             /* 80 */
	uint32_t	ds;             /* 84 */
	uint32_t	fs;             /* 88 */
	uint32_t	gs;             /* 92 */
	uint32_t	ldtr;           /* 96 */
	uint32_t	io_base_addr;   /* 100 */
} tss32_t;

#define DR7_L0   (1u << 0)
#define DR7_L1   (1u << 2)
#define DR7_L2   (1u << 4)
#define DR7_L3   (1u << 6)

#define IS_SOFTWARE_VECTOR(vec) \
	( \
		((vec.bits.interrupt_type == \
		  VECTOR_TYPE_SW_INT) || \
		 (vec.bits.interrupt_type == \
		  VECTOR_TYPE_PRI_SW_INT) || \
		 (vec.bits.interrupt_type == \
		  VECTOR_TYPE_SW_EXCEPTION) \
		) \
	)

/* Two-byte padding after selector is ok. */
typedef struct {
	uint16_t	selector;
	uint32_t	base;
	uint32_t	limit;
	seg_ar_t	ar;
} seg_reg_t;

typedef union {
	uint64_t value;
	struct {
		uint32_t limit_15_00:16;       /* bits 15:0 */
		uint32_t base_15_00:16;        /* bits 31:16 */
		uint32_t base_23_16:8;         /* bits 39:32 */
		uint32_t type:4;               /* bits 43:40 */
		uint32_t s_bit:1;              /* bit 44 */
		uint32_t dpl:2;                /* bit2 46:45 */
		uint32_t p_bit:1;              /* bit 47 */
		uint32_t limit_19_16:4;        /* bits 51:48 */
		uint32_t avl_bit:1;            /* bit 52 */
		uint32_t l_bit:1;              /* bit 53 */
		uint32_t db_bit:1;             /* bit 54 */
		uint32_t g_bit:1;              /* bit 55 */
		uint32_t base_31_24:8;         /* bits 63:56 */
	} bits;
} segment_desc_32_t;

typedef struct {
	seg_reg_t  segment[SEG_COUNT]; // segment_desc
	tss32_t    tss;
} task_info_t;

/* Types for (s_bit == 0). */
#define TSS32_AVAL            (0x9)
#define TSS32_BUSY            (0xb)
#define IS_LDT(type)          ((type) == 0x2)
#define IS_TSS32_AVAL(type)   ((type) == TSS32_AVAL)
#define IS_TSS32_BUSY(type)   ((type) == TSS32_BUSY)
#define IS_TSS32(type)        (IS_TSS32_AVAL(type) || IS_TSS32_BUSY(type))

/* Types for (s_bit == 1). */
#define SET_ACCESSED(type)    (type |= 0x1)
#define IS_ACCESSED(type)     (((type) & 0x1) != 0)
#define IS_DATA_RW(type)      (((type) & 0xa) == 0x2)
#define IS_CODE(type)         (((type) & 0x8) != 0)
#define IS_CODE_R(type)       (((type) & 0xa) == 0xa)
#define IS_CODE_CONFORM(type) (((type) & 0xc) == 0xc)

/* Selector fields. */
#define SELECTOR_IDX(sel)     ((sel) & 0xfff8)
#define SELECTOR_GDT(sel)     (((sel) & 0x0004) == 0)
#define SELECTOR_RPL(sel)     ((sel) & 0x0003)

/* the null segment selector with an index of 0 and
 * the TI flag set to 0*/
#define SELECTOR_NULL(sel)     (((sel) & 0xfffc) == 0)

/*T flag*/
#define TTS_DEBUG_TRAP_FLAG   0x00000001

/*tansfer desc to seg*/
static void desc_to_seg(segment_desc_32_t *dsc, seg_reg_t *seg)
{
	seg->base =
		(dsc->bits.base_15_00) |
		(dsc->bits.base_23_16 << 16) | (dsc->bits.base_31_24 << 24);

	seg->limit = (dsc->bits.limit_15_00) | (dsc->bits.limit_19_16 << 16);

	seg->ar.u32 = 0;
	seg->ar.u32 = (dsc->value >> 40) & 0xFFFF;

	/* If g_bit is set, the unit is 4 KB. */
	if (seg->ar.bits.g_bit == 1) {
		seg->limit = (seg->limit << 12) | 0xfff;
	}
}

/* transfer seg to desc*/
static void seg_to_desc(seg_reg_t *seg, segment_desc_32_t *dsc)
{
	dsc->value = 0;
	dsc->bits.base_15_00 = seg->base & 0xFFFF;
	dsc->bits.base_23_16 = (seg->base >> 16) & 0xFF;
	dsc->bits.base_31_24 = (seg->base >> 24) & 0xFF;

	dsc->bits.limit_15_00 = seg->limit & 0xFFFF;
	dsc->bits.limit_19_16 = (seg->limit >> 16) & 0xF;

	dsc->bits.type = seg->ar.bits.type;
	dsc->bits.s_bit = seg->ar.bits.s_bit;
	dsc->bits.dpl = seg->ar.bits.dpl;
	dsc->bits.p_bit = seg->ar.bits.p_bit;
	dsc->bits.avl_bit = seg->ar.bits.avl_bit;
	dsc->bits.l_bit = seg->ar.bits.l_bit;
	dsc->bits.db_bit = seg->ar.bits.db_bit;
	dsc->bits.g_bit = seg->ar.bits.g_bit;
}

static inline uint32_t get_task_switch_source(guest_cpu_handle_t gcpu)
{
	vmx_exit_qualification_t qual;
	vmx_exit_idt_info_t vect;
	uint32_t source;

	vect.uint32 =(uint32_t)vmcs_read(gcpu->vmcs, VMCS_IDT_VECTOR_INFO);
	qual.uint64 = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);
	source = (uint32_t)(qual.task_switch.source);

	if (source == TASK_SWITCH_TYPE_IDT) {
		VMM_ASSERT_EX(vect.bits.valid,
				"VMCS_IDT_INFO is not valid in task switch vmexit from IDT.\n");

		/* If task_switch.source is IDT and the interrupt type is SW,
		 * it means the task switch is triggered by instructions like INT n.
		 * in this case,we deal with the task switch as source CALL*/
		if (IS_SOFTWARE_VECTOR(vect)) {
			source = TASK_SWITCH_TYPE_CALL;
		}
	}

	return source;
}

static inline uint16_t get_new_tr_sel(guest_cpu_handle_t gcpu)
{
	uint16_t tr_sel;
	vmx_exit_qualification_t qual;

	/*get new tr selector*/
	qual.uint64 = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);
	tr_sel = (uint16_t)(qual.task_switch.tss_selector);

	return tr_sel;
}

/*
 * Copy guest status from VMCS to tss buffer.
 */
static void copy_vmcs_to_tss32(guest_cpu_handle_t gcpu, tss32_t *tss)
{
	vmcs_obj_t vmcs = gcpu->vmcs;

	tss->eip = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_RIP);
	tss->eflags = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_RFLAGS);
	tss->eax = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RAX);
	tss->ecx = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RCX);
	tss->edx = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RDX);
	tss->ebx = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RBX);
	tss->esp = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RSP);
	tss->ebp = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RBP);
	tss->esi = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RSI);
	tss->edi = (uint32_t)gcpu_get_gp_reg(gcpu, REG_RDI);

	tss->es = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_ES_SEL);
	tss->cs = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_CS_SEL);
	tss->ss = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_SS_SEL);
	tss->ds = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_DS_SEL);
	tss->fs = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_FS_SEL);
	tss->gs = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_GS_SEL);
}

static inline boolean_t check_sel_limit(guest_cpu_handle_t gcpu, uint16_t sel, uint32_t xdt_limit)
{
	uint32_t ts_src;

	if ((SELECTOR_IDX(sel) + 7) > xdt_limit) {
		ts_src = get_task_switch_source(gcpu);
		if (ts_src != TASK_SWITCH_TYPE_IRET) {
			gcpu_inject_gp0(gcpu);
		}else {
			gcpu_inject_ts(gcpu, sel);
		}
		return FALSE;
	}
	return TRUE;
}

static boolean_t implicity_read_gva(guest_cpu_handle_t gcpu, uint64_t src_gva, uint64_t dst_hva, uint64_t size, pf_info_t *pfinfo)
{
	uint16_t cs_sel;
	boolean_t ret;

	/* According to IA32 Manual: Volume 3, Chapter 4.6.1:
	 * Access the gdt or ldt to load a segment descriptor and
	 * access to TSS as a part of task switch need workaround
	 * implicit supervisor-mode access.*/
	cs_sel = (uint16_t)vmcs_read(gcpu->vmcs, VMCS_GUEST_CS_SEL);
	vmcs_write(gcpu->vmcs, VMCS_GUEST_CS_SEL, cs_sel & 0xFFFC);

	ret = gcpu_copy_from_gva(gcpu, src_gva, dst_hva, size, pfinfo);

	vmcs_write(gcpu->vmcs, VMCS_GUEST_CS_SEL, cs_sel);

	return ret;
}

static boolean_t implicity_write_gva(guest_cpu_handle_t gcpu, uint64_t dst_gva, uint64_t src_hva, uint64_t size, pf_info_t *pfinfo)
{
	uint16_t cs_sel;
	boolean_t ret;

	/* According to IA32 Manual: Volume 3, Chapter 4.6.1:
	 * Access the gdt or ldt to load a segment descriptor and
	 * access to TSS as a part of task switch need workaround
	 * implicit supervisor-mode access.*/
	cs_sel = (uint16_t)vmcs_read(gcpu->vmcs, VMCS_GUEST_CS_SEL);
	vmcs_write(gcpu->vmcs, VMCS_GUEST_CS_SEL, cs_sel & 0xFFFC);

	ret = gcpu_copy_to_gva(gcpu, dst_gva, src_hva, size, pfinfo);

	vmcs_write(gcpu->vmcs, VMCS_GUEST_CS_SEL, cs_sel);

	return ret;
}

static boolean_t xdt_set_seg_desc(guest_cpu_handle_t gcpu, seg_reg_t *ldt, uint16_t sel, segment_desc_32_t *seg_desc)
{
	boolean_t ret;
	uint32_t  xdt_base;
	uint32_t  gdt_base;
	pf_info_t pfinfo;

	if (SELECTOR_GDT(sel)) {
		xdt_base = vmcs_read(gcpu->vmcs, VMCS_GUEST_GDTR_BASE);
	}else{
		xdt_base = ldt->base;
	}

	ret = implicity_write_gva(gcpu, (uint64_t)(xdt_base + SELECTOR_IDX(sel)), (uint64_t)seg_desc, sizeof(segment_desc_32_t), &pfinfo);
	/* not sure whether to inject #TS of #PF.
	 * no such detail info in IA32 spec.*/
	if (!ret) {
		VMM_ASSERT_EX(pfinfo.is_pf,
			"Guest(%d) fail to write to seg desc, the selector = (%u).\n",
			gcpu->guest->id, sel);
		gcpu_inject_ts(gcpu, sel);
		return FALSE;
	}

	return TRUE;
}

static boolean_t get_new_tss(guest_cpu_handle_t gcpu, seg_reg_t *new_tr, tss32_t *new_tss)
{
	boolean_t ret;
	pf_info_t pfinfo;

	ret = implicity_read_gva(gcpu, new_tr->base, (uint64_t)new_tss, sizeof(tss32_t), &pfinfo);
	/* read new tss fail will inject #PF*/
	if (!ret) {
		VMM_ASSERT_EX(pfinfo.is_pf,
			"Guest(%d) fail to read new tss (gva:0x%x).\n",
			gcpu->guest->id, new_tr->base);
		gcpu_set_cr2(gcpu, pfinfo.cr2);
		gcpu_inject_pf(gcpu, pfinfo.ec);
		return FALSE;
	}

	return TRUE;
}

static boolean_t set_old_tss(guest_cpu_handle_t gcpu, seg_reg_t *old_tr, tss32_t *old_tss)
{
	boolean_t ret;
	pf_info_t pfinfo;

	ret = implicity_write_gva(gcpu, (uint64_t)(old_tr->base + 32), (uint64_t)(&(old_tss->eip)), 64, &pfinfo);
	/* write old tss fail will inject #PF*/
	if (!ret) {
		VMM_ASSERT_EX(pfinfo.is_pf,
			"Guest(%d) fail to write to old tss (gva:0x%x).\n",
			gcpu->guest->id, (old_tr->base + 32));
		gcpu_set_cr2(gcpu, pfinfo.cr2);
		gcpu_inject_pf(gcpu, pfinfo.ec);
		return FALSE;
	}

	return TRUE;
}

// new tss contains the segment selector for the TSS of the old task.
static void set_previous_task_link(guest_cpu_handle_t gcpu, seg_reg_t *new_tr, seg_reg_t *old_tr)
{
	boolean_t ret;
	pf_info_t pfinfo;

	ret = implicity_write_gva(gcpu, (uint64_t)(new_tr->base + 0), (uint64_t)(&(old_tr->selector)), sizeof(old_tr->selector), &pfinfo);
	/* new tss writeable has been checked in check_new_tss_writeable()
	 * so we do not check it any more.*/
	VMM_ASSERT_EX(ret, "%s:new tss write fail.\n", __FUNCTION__);
}

static boolean_t xdt_get_seg(guest_cpu_handle_t gcpu, seg_reg_t *ldt, uint16_t sel, seg_reg_t* seg)
{
	segment_desc_32_t seg_desc;
	uint32_t xdt_base;
	uint32_t xdt_limit;
	pf_info_t pfinfo;
	boolean_t ret;

	// fill seg with input sel
	if (SELECTOR_GDT(sel)) {
		xdt_base = vmcs_read(gcpu->vmcs, VMCS_GUEST_GDTR_BASE);
		xdt_limit = vmcs_read(gcpu->vmcs, VMCS_GUEST_GDTR_LIMIT);
	}else{
		if (ldt == NULL) {
			gcpu_inject_ts(gcpu, sel);
			return FALSE;
		}

		xdt_base = ldt->base;
		xdt_limit = ldt->limit;
	}

	if (check_sel_limit(gcpu, sel, xdt_limit) == FALSE) {
		return FALSE;
 	}

	/*check sel null*/
	if (SELECTOR_NULL(sel)) {
		gcpu_inject_ts(gcpu, sel);
		return FALSE;
	}

	/*read seg desc*/
	ret = implicity_read_gva(gcpu,
		(uint64_t)(xdt_base + SELECTOR_IDX(sel)),
		(uint64_t)&seg_desc,
		sizeof(segment_desc_32_t),
		&pfinfo);
	/* read segment desc fail will inject #PF*/
	if (!ret) {
		VMM_ASSERT_EX(pfinfo.is_pf,
			"Guest(%d) fail to read seg desc, the selector = (%u).\n",
			gcpu->guest->id, sel);
		gcpu_set_cr2(gcpu, pfinfo.cr2);
		gcpu_inject_pf(gcpu, pfinfo.ec);
		return FALSE;
	}

	desc_to_seg(&seg_desc, seg);

	seg->selector = sel;

	return TRUE;
}

static boolean_t check_seg_p_bit(guest_cpu_handle_t gcpu, seg_reg_t *seg, seg_id_t id)
{
	switch(id)
	{
		case SEG_CS:
		case SEG_DS:
		case SEG_ES:
		case SEG_FS:
		case SEG_GS:
		case SEG_LDTR:
		case SEG_TR:
			if (seg->ar.bits.p_bit != 1) {
				/* must be present */
				gcpu_inject_np(gcpu, seg->selector);
				return FALSE;
			}
			break;
		case SEG_SS:
			if (seg->ar.bits.p_bit != 1) {
				/* Must be present. */
				gcpu_inject_ss(gcpu, seg->selector);
				return FALSE;
			}
			break;
		default:
			break;
	}

	return TRUE;
}

static boolean_t check_seg_limit(guest_cpu_handle_t gcpu, seg_reg_t *seg, seg_id_t id)
{
	switch(id)
	{
		case SEG_TR:
			/*check tr limit*/
			if (seg->limit < 0x67) {
				gcpu_inject_ts(gcpu, seg->selector);
				return FALSE;
			}
		default:
			break;
	}

	return TRUE;
}

static boolean_t check_seg_s_bit(guest_cpu_handle_t gcpu, seg_reg_t *seg, seg_id_t id)
{
	switch(id)
	{
		case SEG_CS:
		case SEG_SS:
		case SEG_DS:
		case SEG_ES:
		case SEG_FS:
		case SEG_GS:
			if (seg->ar.bits.s_bit == 0) {	/* must be non-sys desc */
				gcpu_inject_ts(gcpu, seg->selector);
				return FALSE;
			}
			break;
		case SEG_LDTR:
		case SEG_TR:
			if (seg->ar.bits.s_bit != 0) {	/* must be sys desc */
				gcpu_inject_ts(gcpu, seg->selector);
				return FALSE;
			}
			break;
		default:
			break;
	}

	return TRUE;
}

static boolean_t check_new_task_busy_flag(guest_cpu_handle_t gcpu, seg_reg_t *seg)
{
	uint32_t ts_src;

	ts_src = get_task_switch_source(gcpu);
	if (ts_src == TASK_SWITCH_TYPE_IRET) {
		if (seg->ar.bits.type != TSS32_BUSY) {
			gcpu_inject_ts(gcpu, seg->selector);
			return FALSE;
		}
	}else{
		if (seg->ar.bits.type == TSS32_BUSY) {
			gcpu_inject_gp0(gcpu);
			return FALSE;
		}
	}

	return TRUE;
}

static boolean_t check_seg_type(guest_cpu_handle_t gcpu, seg_reg_t *seg, seg_id_t id)
{
	switch(id)
	{
		case SEG_CS:
			if (!IS_CODE(seg->ar.bits.type)) {		/* must be code */
				gcpu_inject_ts(gcpu, seg->selector);
				return FALSE;
			}
			break;
		case SEG_SS:
			if (IS_CODE(seg->ar.bits.type) || 		/* must not be code */
				!IS_DATA_RW(seg->ar.bits.type)) { 	/* must be data with r/w */
				gcpu_inject_ts(gcpu, seg->selector);
				return FALSE;
			}
			break;
		case SEG_DS:
		case SEG_ES:
		case SEG_FS:
		case SEG_GS:
			if ((IS_CODE(seg->ar.bits.type) && !IS_CODE_R(seg->ar.bits.type))) {
				gcpu_inject_ts(gcpu, seg->selector);
				return FALSE;
			}
			break;
		case SEG_LDTR:
			if (!IS_LDT(seg->ar.bits.type)) { /* must be ldt */
				gcpu_inject_ts(gcpu, seg->selector);
				return FALSE;
			}
			break;
		case SEG_TR:
			if (!IS_TSS32(seg->ar.bits.type)) {
				gcpu_inject_ts(gcpu, seg->selector);
				return FALSE;
			}

			/*check busy flag*/
			if (check_new_task_busy_flag(gcpu, seg) == FALSE) {
				return FALSE;
			}
			break;
		default:
			break;
	}

	return TRUE;
}

static boolean_t check_seg_dpl(guest_cpu_handle_t gcpu, task_info_t *new_task, seg_id_t id)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 7.3:
	 **  1, With a JMP or CALL instruction, The CPL of the current (old) task
	 **     and the RPL of the segment selector for the new task must be less than
	 **     or equal to the DPL of the TSS desc.
	 **  2, we need check as below:
	 **     CS DPL matches segment selector RPL.
	 **     SS DPL matches CPL and DPL matches RPL.
	 **     DS, ES, FS, and GS segment DPL greater than or equal to CPL.
	 **
	 **  As the IA32 Manual, we have to check seg in sequence as below.
	 **     TR -> LDTR -> CS -> other segments.
	 */
	uint32_t old_cpl;
	uint32_t rpl;
	uint32_t new_cpl;
	uint32_t source;
	seg_reg_t *seg = &new_task->segment[id];

	new_cpl = (uint32_t)SELECTOR_RPL(new_task->segment[SEG_CS].selector);
	rpl = (uint32_t)SELECTOR_RPL(seg->selector);

	switch(id)
	{
		case SEG_CS:
			if (IS_CODE_CONFORM(seg->ar.bits.type)) {
				if (seg->ar.bits.dpl > new_cpl) {
					gcpu_inject_ts(gcpu, seg->selector);
					return FALSE;
				}
			} else {
				if (seg->ar.bits.dpl != new_cpl) {
					gcpu_inject_gp0(gcpu);
					return FALSE;
				}
			}
			break;
		case SEG_SS:
			if ((seg->ar.bits.dpl != new_cpl) ||
				(rpl != new_cpl)) {
				gcpu_inject_ts(gcpu, seg->selector);
				return FALSE;
			}
			break;
		case SEG_DS:
		case SEG_ES:
		case SEG_FS:
		case SEG_GS:
			if (IS_CODE(seg->ar.bits.type) && !IS_CODE_CONFORM(seg->ar.bits.type)) {
				if ((seg->ar.bits.dpl < new_cpl) || (seg->ar.bits.dpl < rpl)) {
					gcpu_inject_ts(gcpu, seg->selector);
					return FALSE;
				}
			}
			break;
		case SEG_TR:
			source = get_task_switch_source(gcpu);
			if ((source == TASK_SWITCH_TYPE_CALL) || (source == TASK_SWITCH_TYPE_JMP)) {
				old_cpl = SELECTOR_RPL((uint16_t)vmcs_read(gcpu->vmcs, VMCS_GUEST_CS_SEL));//old task cpl

				if ((seg->ar.bits.dpl < old_cpl) || (seg->ar.bits.dpl < rpl)) {
					gcpu_inject_ts(gcpu, seg->selector);
					return FALSE;
				}
			}
			break;
		default:
			break;
	}

	return TRUE;
}

static boolean_t check_seg_writeable(guest_cpu_handle_t gcpu, task_info_t *new_task, seg_id_t id)
{
	segment_desc_32_t desc;
	seg_reg_t *seg = &new_task->segment[id];

	/* check if segment is writable. some bits might be updated later.
	 * no bits in LDT is required, so that there's no need to check it for LDT*/
	if (id != SEG_LDTR) {
		seg_to_desc(seg, &desc);
		if (xdt_set_seg_desc(gcpu, &new_task->segment[SEG_LDTR], seg->selector, &desc) == FALSE) {
			return FALSE;
		}
	}

	return TRUE;
}

static boolean_t check_old_tr_writeable(guest_cpu_handle_t gcpu, seg_reg_t *old_tr)
{
	segment_desc_32_t old_desc;

	seg_to_desc(old_tr, &old_desc);
	if (xdt_set_seg_desc(gcpu, NULL, old_tr->selector, &old_desc) == FALSE) {
		return FALSE;
	}

	return TRUE;
}

static boolean_t check_new_tss_writeable(guest_cpu_handle_t gcpu, task_info_t *new_task)
{
	boolean_t ret;
	pf_info_t pfinfo;

	ret = implicity_write_gva(gcpu, (uint64_t)new_task->segment[SEG_TR].base, (uint64_t)&(new_task->tss), sizeof(tss32_t), &pfinfo);
	/* write new tss fail will inject #TS*/
	if (!ret) {
		VMM_ASSERT_EX(pfinfo.is_pf,
			"Guest(%d) fail to write to new tss (gva:0x%x)\n",
			gcpu->guest->id, new_task->segment[SEG_TR].base);
		gcpu_inject_ts(gcpu, new_task->segment[SEG_TR].selector);
		return FALSE;
	}

	return TRUE;
}

static boolean_t check_debug_trap(guest_cpu_handle_t gcpu, tss32_t *tss)
{
	/* T flag in new tss */
	if ((tss->io_base_addr & TTS_DEBUG_TRAP_FLAG) != 0) {
		gcpu_inject_db(gcpu);
		return FALSE;
	}

	return TRUE;
}

static boolean_t check_seg(guest_cpu_handle_t gcpu, task_info_t *new_task, seg_id_t id)
{
	seg_reg_t *seg = &new_task->segment[id];

	/*check p bit*/
	if (check_seg_p_bit(gcpu, seg, id) == FALSE) {
		return FALSE;
	}
	/*check seg limit*/
	if (check_seg_limit(gcpu, seg, id) == FALSE) {
		return FALSE;
	}
	/*check s bit*/
	if (check_seg_s_bit(gcpu, seg, id) == FALSE) {
		return FALSE;
	}
	/*check type*/
	if (check_seg_type(gcpu, seg, id) == FALSE) {
		return FALSE;
	}
	/*check dpl*/
	if (check_seg_dpl(gcpu, new_task, id) == FALSE) {
		return FALSE;
	}
	/*check seg writeable*/
	if (check_seg_writeable(gcpu, new_task, id) == FALSE) {
		return FALSE;
	}

	return TRUE;
}

static boolean_t check_tss(guest_cpu_handle_t gcpu, task_info_t *new_task)
{
	/*check debug_trap*/
	if (check_debug_trap(gcpu, &new_task->tss) == FALSE) {
		return FALSE;
	}
	/*check new tss writeable*/
	if (check_new_tss_writeable(gcpu, new_task) == FALSE) {
		return FALSE;
	}
	return TRUE;
}

/*get seg sel from new tss*/
static inline uint16_t get_seg_sel(tss32_t *new_tss, seg_id_t id)
{
	uint16_t seg_sel = 0;

	/* TR sel has been gotten from get_new_tr_sel()*/
	switch(id)
	{
		case SEG_CS:
			seg_sel = (uint16_t)new_tss->cs;
			break;
		case SEG_SS:
			seg_sel = (uint16_t)new_tss->ss;
			break;
		case SEG_DS:
			seg_sel = (uint16_t)new_tss->ds;
			break;
		case SEG_ES:
			seg_sel = (uint16_t)new_tss->es;
			break;
		case SEG_FS:
			seg_sel = (uint16_t)new_tss->fs;
			break;
		case SEG_GS:
			seg_sel = (uint16_t)new_tss->gs;
			break;
		case SEG_LDTR:
			seg_sel = (uint16_t)new_tss->ldtr;
			break;
		default:
			print_panic("%s:invalid segment id(%d).\n", __FUNCTION__, id);
			VMM_DEADLOOP();
			break;
	}
	return seg_sel;
}

static boolean_t get_new_task(guest_cpu_handle_t gcpu, task_info_t *new_task)
{
	uint16_t tr_sel;
	uint32_t seg_id;
	uint32_t ts_src;

	/*get & check new tr sel*/
	tr_sel = get_new_tr_sel(gcpu);
	if (!SELECTOR_GDT(tr_sel)) {
		ts_src = get_task_switch_source(gcpu);
		if (ts_src != TASK_SWITCH_TYPE_IRET) {
			gcpu_inject_gp0(gcpu);
		}else{
			gcpu_inject_ts(gcpu, tr_sel);
		}
		return FALSE;
	}

	/*get tr seg*/
	if (xdt_get_seg(gcpu, NULL, tr_sel, &new_task->segment[SEG_TR]) == FALSE) {
		return FALSE;
	}
	/*check tr seg*/
	if (check_seg(gcpu, new_task, SEG_TR) == FALSE) {
		return FALSE;
	}

	/*get new tss*/
	if (get_new_tss(gcpu, &new_task->segment[SEG_TR], &new_task->tss) == FALSE) {
		return FALSE;
	}
	/*check tss*/
	if (check_tss(gcpu, new_task) == FALSE) {
		return FALSE;
	}

	/*check ldtr sel*/
	if (!SELECTOR_GDT(new_task->tss.ldtr)) {
		gcpu_inject_ts(gcpu, new_task->tss.ldtr);
		return FALSE;
	}

	/*get ldtr seg*/
	if (xdt_get_seg(gcpu, NULL, new_task->tss.ldtr, &new_task->segment[SEG_LDTR]) == FALSE) {
		return FALSE;
	}

	/*check ldtr seg*/
	if (check_seg(gcpu, new_task, SEG_LDTR) == FALSE) {
		return FALSE;
	}

	for (seg_id = SEG_CS; seg_id<= SEG_GS; seg_id++)
	{
		/*get other seg*/
		if (xdt_get_seg(gcpu, &new_task->segment[SEG_LDTR], get_seg_sel(&new_task->tss, seg_id), &new_task->segment[seg_id]) == FALSE) {
			return FALSE;
		}
		/*check other seg*/
		if (check_seg(gcpu, new_task, seg_id) == FALSE) {
			return FALSE;
		}
	}

	return TRUE;
}

static boolean_t get_old_task(guest_cpu_handle_t gcpu, seg_reg_t *old_tr)
{
	seg_reg_t gdt_tr;
	boolean_t consistency;
	uint32_t ts_src;
	boolean_t ret;

	/* get old tr. */
	gcpu_get_seg(gcpu, SEG_TR,
		(uint16_t *)&(old_tr->selector),
		(uint64_t *)&(old_tr->base),
		(uint32_t *)&(old_tr->limit),
		(uint32_t *)&(old_tr->ar));

	/*check old tr*/
	if (!SELECTOR_GDT(old_tr->selector)) {
		gcpu_inject_ts(gcpu, old_tr->selector);
		return FALSE;
	}

	if (xdt_get_seg(gcpu, NULL, old_tr->selector, &gdt_tr)) {
		return FALSE;
	}

	consistency = ((old_tr->limit == gdt_tr.limit)
		&& (old_tr->base == gdt_tr.base)
		&& (old_tr->ar.u32 == gdt_tr.ar.u32));

	VMM_ASSERT_EX(consistency,
		"tss in vmcs is not consistency with gdt.\n");

	VMM_ASSERT_EX(IS_TSS32_BUSY(old_tr->ar.bits.type),
		"old task do not set busy flag.\n");

	/* check old tr writeable*/
	ts_src = get_task_switch_source(gcpu);
	if ((ts_src == TASK_SWITCH_TYPE_JMP) ||
		(ts_src == TASK_SWITCH_TYPE_IRET)) {
		if (check_old_tr_writeable(gcpu, old_tr) == FALSE) {
			return FALSE;
		}
	}
	return TRUE;
}

static boolean_t update_old_tss(guest_cpu_handle_t gcpu, seg_reg_t *old_tr)
{
	segment_desc_32_t old_tss_desc;
	tss32_t old_tss;
	uint32_t ts_src;
	boolean_t ret;

	ts_src = get_task_switch_source(gcpu);

	/* Save guest status to old tss. */
	/* call, jmp or iret */
	if (ts_src != TASK_SWITCH_TYPE_IDT) {
		gcpu_skip_instruction(gcpu);
	}

	copy_vmcs_to_tss32(gcpu, &old_tss);

	if (ts_src == TASK_SWITCH_TYPE_IRET) {
		old_tss.eflags &= ~RFLAGS_NT;
	}

	if (set_old_tss(gcpu, old_tr, &old_tss) == FALSE) {
		return FALSE;
	}

	/* Clear busy bit in old tss descriptor. */

	if ((ts_src == TASK_SWITCH_TYPE_JMP) ||
		(ts_src == TASK_SWITCH_TYPE_IRET)) {

		/* transfer old tr to old tss decs*/
		seg_to_desc(old_tr, &old_tss_desc);

		/* Clear the B bit, and write it back. */
		old_tss_desc.bits.type = TSS32_AVAL;

		ret = xdt_set_seg_desc(gcpu, NULL, old_tr->selector, &old_tss_desc);
		/* write to segment is always successful.
		 * it was checked in check_old_tr_writeable()*/
		VMM_ASSERT_EX(ret, "old tss descriptor write fail.\n");
	}

	return TRUE;
}

static void update_new_tss(guest_cpu_handle_t gcpu, seg_reg_t *new_tr, seg_reg_t *old_tr)
{
	boolean_t ret;
	uint32_t ts_src;
	segment_desc_32_t new_tss_desc;

	ts_src = get_task_switch_source(gcpu);

	/* Save old tr sel in new tss. */
	if ((ts_src == TASK_SWITCH_TYPE_CALL) ||
		(ts_src == TASK_SWITCH_TYPE_IDT)) {
		/* we already check the new tss writeable
		 * in check_new_tss_writeable()*/
		set_previous_task_link(gcpu, new_tr, old_tr);
	}

	/* Set busy bit in new tss descriptor. */
	if (ts_src != TASK_SWITCH_TYPE_IRET) {
		/* transfer new tr to new tss decs*/
		seg_to_desc(new_tr, &new_tss_desc);
		/* set the B bit, and write it back. */
		new_tss_desc.bits.type = TSS32_BUSY;
		new_tr->ar.bits.type = TSS32_BUSY;

		/* write to segment is always successful.
		 * it was checked in check_seg()*/
		ret = xdt_set_seg_desc(gcpu, NULL, new_tr->selector, &new_tss_desc);
		VMM_ASSERT_EX(ret, "new tss descriptor write fail.\n");
	}
}

static void set_seg_access(guest_cpu_handle_t gcpu, task_info_t *new_task)
{
	uint32_t seg_id;
	boolean_t ret;
	segment_desc_32_t desc;

	for(seg_id = SEG_CS; seg_id <= SEG_GS; seg_id++)
	{
		if (!IS_ACCESSED(new_task->segment[seg_id].ar.bits.type)) {
			SET_ACCESSED(new_task->segment[seg_id].ar.bits.type);
			seg_to_desc(&new_task->segment[seg_id], &desc);
			ret = xdt_set_seg_desc(gcpu,
				&new_task->segment[SEG_LDTR],
				new_task->segment[seg_id].selector,
				&desc);
			/* write to segment is always successful.
			 * it was checked in check_seg()*/
			VMM_ASSERT_EX(ret, "write segment desc fail.\n");
		}
	}
}

static void load_cr(guest_cpu_handle_t gcpu, tss32_t *new_tss)
{
	uint64_t cr0;
	vmcs_obj_t vmcs = gcpu->vmcs;

	cr0 = gcpu_get_visible_cr0(gcpu);

	/* Load new cr3. */
	if (cr0 & CR0_PG) {
		vmcs_write(vmcs, VMCS_GUEST_CR3, new_tss->cr3);
	}

	/* Set the TS bit in CR0. */
	cr0 |= CR0_TS;
	vmcs_write(vmcs, VMCS_GUEST_CR0, cr0);
	VMM_ASSERT_EX((cr0_guest_write(gcpu, cr0) == FALSE),
			"task switch: #GP happened when writing new CR0\n");
}

static void load_general_regs(guest_cpu_handle_t gcpu, tss32_t *new_tss)
{
	vmcs_obj_t vmcs = gcpu->vmcs;
	uint32_t ts_src;

	/* Load new flags. */
	ts_src = get_task_switch_source(gcpu);
	if ((ts_src == TASK_SWITCH_TYPE_CALL) ||
	    (ts_src == TASK_SWITCH_TYPE_IDT)) {
		new_tss->eflags |= RFLAGS_NT;
	}

	new_tss->eflags |= RFLAGS_RSVD1;

	vmcs_write(vmcs, VMCS_GUEST_RIP, (uint64_t)new_tss->eip);
	vmcs_write(vmcs, VMCS_GUEST_RFLAGS, (uint64_t)new_tss->eflags);
	gcpu_set_gp_reg(gcpu, REG_RAX, (uint64_t)new_tss->eax);
	gcpu_set_gp_reg(gcpu, REG_RCX, (uint64_t)new_tss->ecx);
	gcpu_set_gp_reg(gcpu, REG_RDX, (uint64_t)new_tss->edx);
	gcpu_set_gp_reg(gcpu, REG_RBX, (uint64_t)new_tss->ebx);
	gcpu_set_gp_reg(gcpu, REG_RBP, (uint64_t)new_tss->ebp);
	gcpu_set_gp_reg(gcpu, REG_RSP, (uint64_t)new_tss->esp);
	gcpu_set_gp_reg(gcpu, REG_RSI, (uint64_t)new_tss->esi);
	gcpu_set_gp_reg(gcpu, REG_RDI, (uint64_t)new_tss->edi);
}

static void load_seg_regs(guest_cpu_handle_t gcpu, task_info_t *new_task)
{
	uint32_t seg_id;

	for(seg_id = SEG_CS; seg_id <= SEG_TR; seg_id++)
	{
		gcpu_set_seg(gcpu, seg_id, new_task->segment[seg_id].selector, new_task->segment[seg_id].base,
			new_task->segment[seg_id].limit, new_task->segment[seg_id].ar.u32);
	}
}

static boolean_t clear_ln_in_dr7(guest_cpu_handle_t gcpu)
{
	/*
	 ** According to IA32 Manual: Volume 3, Chapter 17.2.4:
	 ** When a breakpoint condition is detected and its associated
	 ** Ln flag is set, a debug exception is generated.The processor
	 ** automatically clears these flags on every task switch to avoid
	 ** unwanted breakpoint conditions in the new task.
	 */
	uint32_t dr7;
	vmcs_obj_t vmcs = gcpu->vmcs;

	dr7 = (uint32_t)vmcs_read(vmcs, VMCS_GUEST_DR7);
	dr7 &= ~(DR7_L0 | DR7_L1 | DR7_L2 | DR7_L3);

	vmcs_write(vmcs, VMCS_GUEST_DR7, (uint64_t)dr7);
}

static void load_new_task(guest_cpu_handle_t gcpu, task_info_t *new_task)
{
	/*set accessed bit*/
	set_seg_access(gcpu, new_task);

	/*load cr*/
	load_cr(gcpu, &new_task->tss);

	/* Load general regs. */
	load_general_regs(gcpu, &new_task->tss);

	/* Load new seg regs. */
	load_seg_regs(gcpu, new_task);

	/* Clear the ln bits in dr7 */
	clear_ln_in_dr7(gcpu);
}

#ifdef DEBUG
static void task_switch_debug(guest_cpu_handle_t gcpu)
{
	char *name;
	vmx_exit_qualification_t qualification;
	vmx_exit_idt_info_t idt_info;
	uint32_t vector = (uint32_t)-1;

	idt_info.uint32 =(uint32_t)vmcs_read(gcpu->vmcs, VMCS_IDT_VECTOR_INFO);
	if (idt_info.bits.valid) {
		vector = idt_info.bits.vector;
	}

	qualification.uint64 =vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);
	switch (qualification.task_switch.source)
	{
		case TASK_SWITCH_TYPE_CALL:
			name = "CALL";
			break;
		case TASK_SWITCH_TYPE_IRET:
			name = "IRET";
			break;
		case TASK_SWITCH_TYPE_JMP:
			name = "JMP";
			break;
		case TASK_SWITCH_TYPE_IDT:
			name = "IDT";
			break;
		default:
			name = "UNKNOWN";
			break;
	}

	print_info(
		"Task Switch on CPU#%d guest_id:%d src:%s Qual:0x%x Vec:0x%x \n",
		gcpu->id,
		gcpu->guest->id,
		name,
		qualification.uint64,
		vector);
}
#endif

static inline boolean_t check_64bit(guest_cpu_handle_t gcpu)
{
	/*
	 * According to IA32 Manual: Volume 3, Chapter 7.7:
	 *  The processor issues a general-protection exception (#GP)
	 *  if the following is attempted in 64-bit mode:
	 *  1. Control transfer to a TSS or a task gate using JMP, CALL, INTn, or interrupt.
	 *  2. An IRET with EFLAGS.NT (nested task) set to 1.
	 *
	 * Technically, #GP will happen before task switch if it is in 64-bit mode, but we
	 *  still keep this check to prevent hardware issues.
	 */
	uint64_t is_64bit;

	is_64bit = (((uint32_t) vmcs_read(gcpu->vmcs, VMCS_ENTRY_CTRL) & ENTRY_GUEST_IA32E_MODE) != 0);
	if (is_64bit) {
		print_panic("%s:task switch vmexit will never happen in 64 bit.\n", __FUNCTION__);
		gcpu_inject_gp0(gcpu);
		return FALSE;
	}

	return TRUE;
}

/*
 * This function does task switch for 32-bit VMM guest.
 */
static void vmexit_task_switch(guest_cpu_handle_t gcpu)
{
	seg_reg_t old_tr;
	task_info_t new_task;

	D(VMM_ASSERT(gcpu));

	if (check_64bit(gcpu) == FALSE) {
		return;
	}

#ifdef DEBUG
	task_switch_debug(gcpu);
#endif

	/*get and check old tr*/
	if (get_old_task(gcpu, &old_tr) == FALSE) {
		return;
	}

	/* get and check new task. */
	if (get_new_task(gcpu, &new_task) == FALSE) {
		return;
	}

	/*update old tss*/
	if (update_old_tss(gcpu, &old_tr) == FALSE) {
		return;
	}

	/*update new tss*/
	update_new_tss(gcpu, &new_task.segment[SEG_TR], &old_tr);

	/*load new task*/
	load_new_task(gcpu, &new_task);
}

void vmexit_task_switch_init(void)
{
	vmexit_install_handler(vmexit_task_switch, REASON_09_TASK_SWITCH);
}
/* End of file */
