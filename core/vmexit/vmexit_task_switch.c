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
#include "guest_cpu.h"
#include "isr.h"
#include "vmx_vmcs.h"
#include "guest_cpu_vmenter_event.h"
#include "host_memory_manager_api.h"

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

/*
 * Only three 32-bit registers are used during task switch.  They are not to
 * be shared with MON.  MON works with 64-bit values.
 */
typedef union {
	uint32_t value;
	struct {
		uint32_t carry:1;
		uint32_t rsvd_1:1;
		uint32_t parity:1;
		uint32_t rsvd_3:1;
		uint32_t adjust:1;
		uint32_t rsvd_5:1;
		uint32_t zero:1;
		uint32_t sign:1;
		uint32_t trap:1;
		uint32_t intr_enable:1;
		uint32_t direction:1;
		uint32_t overflow:1;
		uint32_t iopl:2;
		uint32_t nested_task:1;
		uint32_t rsvd_15:1;
		uint32_t resume:1;
		uint32_t v86_mode:1;
		uint32_t align_chk:1;
		uint32_t v_intr:1;
		uint32_t v_intr_pend:1;
		uint32_t ident:1;
		uint32_t rsvd_31_22:10;
	} bits;
} eflags_t;

typedef union {
	uint32_t value;
	struct {
		uint32_t pe:1;           /* bit 0 Protection Enable */
		uint32_t mp:1;           /* bit 1 Monitor Coprocessor */
		uint32_t em:1;           /* bit 2 Emulation */
		uint32_t ts:1;           /* bit 3 Task Switched */
		uint32_t et:1;           /* bit 4 Extension Type */
		uint32_t ne:1;           /* bit 5 Numeric Error */
		uint32_t rsvd_15_6:10;   /* bits 15:6 reserved */
		uint32_t wp:1;           /* bit 16 Write Protect */
		uint32_t rsvd_17:1;      /* bit 17 reserved */
		uint32_t am:1;           /* bit 18 Alignment Mask */
		uint32_t rsvd_28_19:10;  /* bits 28:19 reserved */
		uint32_t nw:1;           /* bit 29 Not Write-through */
		uint32_t cd:1;           /* bit 30 Cache Disable */
		uint32_t pg:1;           /* bit 31 Paging */
	} bits;
} cr0_t;

typedef union {
	uint32_t value;
	struct {
		uint32_t l0:1;         /* bit 0 local b.p. enable */
		uint32_t g0:1;         /* bit 1 global b.p. enable */
		uint32_t l1:1;         /* bit 2 local b.p. enable */
		uint32_t g1:1;         /* bit 3 global b.p. enable */
		uint32_t l2:1;         /* bit 4 local b.p. enable */
		uint32_t g2:1;         /* bit 5 global b.p. enable */
		uint32_t l3:1;         /* bit 6 local b.p. enable */
		uint32_t g3:1;         /* bit 7 global b.p. enable */
		uint32_t le:1;         /* bit 8 local exact b.p. enable */
		uint32_t ge:1;         /* bit 9 global exact b.p. enable */
		uint32_t rsvd_12_10:3; /* bits 12:10 reserved */
		uint32_t gd:1;         /* bit 13 general detect enable */
		uint32_t rsvd_15_14:2; /* bits 15:14 reserved */
		uint32_t rw0:2;        /* bits 17:16 */
		uint32_t len0:2;       /* bits 19:18 */
		uint32_t rw1:2;        /* bits 21:20 */
		uint32_t len1:2;       /* bits 23:22 */
		uint32_t rw2:2;        /* bits 25:24 */
		uint32_t len2:2;       /* bits 27:26 */
		uint32_t rw3:2;        /* bits 29:28 */
		uint32_t len3:2;       /* bits 31:30 */
	} bits;
} dr7_t;

/* */
/* This is 32-bit selector and descriptor. */

typedef union {
	uint32_t value;
	struct {
		uint32_t type:4;               /* bits 3:0 */
		uint32_t s_bit:1;              /* bit 4 */
		uint32_t dpl:2;                /* bit2 6:5 */
		uint32_t p_bit:1;              /* bit 7 */
		uint32_t rsvd_11_8:4;          /* bits 11:8 */
		uint32_t avl_bit:1;            /* bit 12 */
		uint32_t l_bit:1;              /* bit 13 */
		uint32_t db_bit:1;             /* bit 14 */
		uint32_t g_bit:1;              /* bit 15 */
		uint32_t null_bit:1;           /* bit 16 */
		uint32_t rsvd_31_17:15;        /* bits 31:17 */
	} bits;
} ar_t;

/* Two-byte padding after selector is ok. */
typedef struct {
	uint16_t	selector;
	uint32_t	base;
	uint32_t	limit;
	ar_t		ar;
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
} desc_t;

/* Types for (s_bit == 0). */
#define TSS32_AVAL            (0x9)
#define TSS32_BUSY            (0xb)
#define LS_LDT(type)          ((type) == 0x2)
#define IS_TSS32_AVAL(type)   ((type) == TSS32_AVAL)
#define IS_TSS32_BUSY(type)   ((type) == TSS32_BUSY)
#define IS_TSS32(type)        (IS_TSS32_AVAL(type) || IS_TSS32_BUSY(type))

/* Types for (s_bit == 1). */
#define SET_ASSESSED(type)    (type |= 0x1)
#define IS_ASSESSED(type)     (((type) & 0x1) != 0)
#define IS_DATA_RW(type)      (((type) & 0xa) == 0x2)
#define IS_CODE(type)         (((type) & 0x8) != 0)
#define IS_CODE_R(type)       (((type) & 0xa) == 0xa)
#define IS_CODE_CONFORM(type) (((type) & 0xc) == 0xc)

/* Selector fields. */
#define SELECTOR_IDX(sel)     ((sel) & 0xfff8)
#define SELECTOR_GDT(sel)     (((sel) & 0x0004) == 0)
#define SELECTOR_RPL(sel)     ((sel) & 0x0003)

int copy_from_gva(guest_cpu_handle_t gcpu,
		  uint64_t gva,
		  uint32_t size,
		  uint64_t hva)
{
	uint64_t dst_hva = 0;
	uint64_t src_gva = gva;
	uint8_t *local_ptr = (uint8_t *)hva;
	uint32_t size_remaining = size;
	uint32_t size_copied = 0;

	while (size_remaining) {
		if (!gcpu_gva_to_hva(gcpu, (gva_t)src_gva, (hva_t *)&dst_hva)) {
			MON_LOG(mask_mon,
				level_error,
				"%s: Invalid Parameter Struct address %P\n",
				__FUNCTION__,
				src_gva);
			return -1;
		}
		/* Copy until end */
		if (src_gva > (UINT64_ALL_ONES - size_remaining)) {
			MON_LOG(mask_mon,
				level_error,
				"Error: size bounds exceeded\n");
			return -1;
		}
		if ((src_gva + size_remaining) <= (src_gva | PAGE_4KB_MASK)) {
			mon_memcpy((void *)local_ptr,
				(void *)dst_hva,
				size_remaining);
			return 0;
		} else {
			/* Copy until end of page */
			size_copied = (uint32_t)
				      (((src_gva +
					 PAGE_4KB_SIZE) &
					~PAGE_4KB_MASK) - src_gva);

			mon_memcpy((void *)local_ptr,
				(void *)dst_hva,
				size_copied);

			/* Adjust size and pointers for next copy */
			size_remaining -= size_copied;
			local_ptr += size_copied;
			src_gva += size_copied;
		}
	}

	return 0;
}

static
int copy_to_gva(guest_cpu_handle_t gcpu,
		uint64_t gva,
		uint32_t size,
		uint64_t hva)
{
	uint64_t dst_gva = gva;
	uint64_t src_hva = 0;
	uint8_t *local_ptr = (uint8_t *)hva;
	uint32_t size_remaining = size;
	uint32_t size_copied = 0;

	while (size_remaining) {
		if (!gcpu_gva_to_hva(gcpu, dst_gva, &src_hva)) {
			MON_LOG(mask_mon,
				level_error,
				"%s: Invalid guest pointer address %P\n",
				__FUNCTION__,
				gva);
			return -1;
		}
		/* Copy until end */
		if (dst_gva > (UINT64_ALL_ONES - size_remaining)) {
			MON_LOG(mask_mon,
				level_error,
				"Error: size bounds exceeded\n");
			return -1;
		}
		if ((dst_gva + size_remaining) <= (dst_gva | PAGE_4KB_MASK)) {
			mon_memcpy((void *)src_hva,
				(void *)local_ptr,
				size_remaining);
			return 0;
		} else {
			/* Copy until end of page */
			size_copied = (uint32_t)
				      (((dst_gva +
					 PAGE_4KB_SIZE) &
					~PAGE_4KB_MASK) - dst_gva);

			mon_memcpy((void *)src_hva,
				(void *)local_ptr,
				size_copied);

			/* Adjust size and pointers for next copy */
			size_remaining -= size_copied;
			local_ptr += size_copied;
			dst_gva += size_copied;
		}
	}

	return 0;
}

static
void parse_desc(desc_t *dsc, seg_reg_t *seg)
{
	seg->base =
		(dsc->bits.base_15_00) |
		(dsc->bits.base_23_16 << 16) | (dsc->bits.base_31_24 << 24);

	seg->limit = (dsc->bits.limit_15_00) | (dsc->bits.limit_19_16 << 16);

	seg->ar.value = 0;
	seg->ar.bits.type = dsc->bits.type;
	seg->ar.bits.s_bit = dsc->bits.s_bit;
	seg->ar.bits.dpl = dsc->bits.dpl;
	seg->ar.bits.p_bit = dsc->bits.p_bit;
	seg->ar.bits.avl_bit = dsc->bits.avl_bit;
	seg->ar.bits.l_bit = dsc->bits.l_bit;
	seg->ar.bits.db_bit = dsc->bits.db_bit;
	seg->ar.bits.g_bit = dsc->bits.g_bit;
}

static
void get_task_info(guest_cpu_handle_t gcpu, uint32_t *type, uint16_t *sel,
		   ia32_vmx_vmcs_vmexit_info_idt_vectoring_t vect)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	ia32_vmx_exit_qualification_t qual;

	qual.uint64 = mon_vmcs_read(vmcs, VMCS_EXIT_INFO_QUALIFICATION);

	*type = (uint32_t)(qual.task_switch.source);
	*sel = (uint16_t)(qual.task_switch.tss_selector);

	if ((*type == TASK_SWITCH_TYPE_IDT) && IS_SOFTWARE_VECTOR(vect)) {
		*type = TASK_SWITCH_TYPE_CALL;
	}
}

static
void force_ring3_ss(guest_cpu_handle_t gcpu)
{
	seg_reg_t ss;
	cr0_t cr0;
	eflags_t flags;

	cr0.value =
		(uint32_t)gcpu_get_guest_visible_control_reg(gcpu,
			IA32_CTRL_CR0);

	flags.value = (uint32_t)gcpu_get_gp_reg(gcpu, IA32_REG_RFLAGS);

	if ((cr0.bits.pe == 0) || (flags.bits.v86_mode == 1)) {
		return;
	}

	gcpu_get_segment_reg(gcpu, IA32_SEG_TR,
		(uint16_t *)&(ss.selector),
		(uint64_t *)&(ss.base),
		(uint32_t *)&(ss.limit),
		(uint32_t *)&(ss.ar));

	ss.ar.bits.dpl = 3;

	gcpu_set_segment_reg(gcpu, IA32_SEG_SS, ss.selector, ss.base,
		ss.limit, ss.ar.value);
	return;
}

/*
 * Set guest LDTR according to new tss.
 */
static
int set_guest_ldtr(guest_cpu_handle_t gcpu, seg_reg_t *gdtr,
		   seg_reg_t *ldtr, tss32_t *tss)
{
	desc_t desc;
	int r;

	mon_memset(ldtr, 0, sizeof(seg_reg_t));
	ldtr->selector = (uint16_t)tss->ldtr;

	if (SELECTOR_IDX(ldtr->selector) == 0) {
		ldtr->ar.bits.null_bit = 1;
		return 0;
	}

	if (!SELECTOR_GDT(ldtr->selector)) {
		/* must be in gdt */
		force_ring3_ss(gcpu);
		gcpu_inject_ts(gcpu, ldtr->selector);
		return -1;
	}

	r = copy_from_gva(gcpu,
		(uint64_t)(gdtr->base + SELECTOR_IDX(ldtr->selector)),
		sizeof(desc), (uint64_t)(&desc));

	if (r != 0) {
		force_ring3_ss(gcpu);
		gcpu_inject_ts(gcpu, ldtr->selector);
		return -1;
	}

	parse_desc(&desc, ldtr);

	if ((ldtr->ar.bits.s_bit != 0) ||       /* must be sys desc */
	    !LS_LDT(ldtr->ar.bits.type) ||      /* must be ldt */
	    (ldtr->ar.bits.p_bit != 1)) {       /* must be present */
		force_ring3_ss(gcpu);
		gcpu_inject_ts(gcpu, ldtr->selector);
		return -1;
	}

	gcpu_set_segment_reg(gcpu, IA32_SEG_LDTR, ldtr->selector, ldtr->base,
		ldtr->limit, ldtr->ar.value);

	return 0;
}

/*
 * Set guest SS according to new tss.
 */
static
int set_guest_ss(guest_cpu_handle_t gcpu, seg_reg_t *gdtr,
		 seg_reg_t *ldtr, tss32_t *tss)
{
	desc_t desc;
	seg_reg_t ss;
	seg_reg_t *dtr;
	uint32_t cpl;
	int r;

	mon_memset(&ss, 0, sizeof(ss));
	ss.selector = (uint16_t)tss->ss;
	cpl = SELECTOR_RPL(tss->cs);

	if (SELECTOR_IDX(ss.selector) == 0) {
		/* must not be null */
		force_ring3_ss(gcpu);
		gcpu_inject_ts(gcpu, ss.selector);
		return -1;
	}

	dtr = SELECTOR_GDT(ss.selector) ? gdtr : ldtr;

	r = copy_from_gva(gcpu,
		(uint64_t)(dtr->base + SELECTOR_IDX(ss.selector)),
		sizeof(desc),
		(uint64_t)(&desc));

	if (r != 0) {
		force_ring3_ss(gcpu);
		gcpu_inject_ts(gcpu, ss.selector);
		return -1;
	}

	parse_desc(&desc, &ss);

	if (ss.ar.bits.p_bit == 0) {
		/* must be present */
		force_ring3_ss(gcpu);
		gcpu_inject_ss(gcpu, ss.selector);
		return -1;
	}

	if ((ss.ar.bits.s_bit == 0) ||          /* must be non-sys desc */
	    IS_CODE(ss.ar.bits.type) ||         /* must not be code */
	    !IS_DATA_RW(ss.ar.bits.type) ||     /* must be data with r/w */
	    (ss.ar.bits.dpl != cpl) ||
	    ((uint32_t)SELECTOR_RPL(ss.selector) != cpl)) {
		force_ring3_ss(gcpu);
		gcpu_inject_ts(gcpu, ss.selector);
		return -1;
	}

	/* If g_bit is set, the unit is 4 KB. */
	if (ss.ar.bits.g_bit == 1) {
		ss.limit = (ss.limit << 12) | 0xfff;
	}

	if (!IS_ASSESSED(ss.ar.bits.type)) {
		SET_ASSESSED(ss.ar.bits.type);
		SET_ASSESSED(desc.bits.type);

		r = copy_to_gva(gcpu,
			(uint64_t)(dtr->base + SELECTOR_IDX(ss.selector)),
			sizeof(desc),
			(uint64_t)(&desc));

		if (r != 0) {
			force_ring3_ss(gcpu);
			gcpu_inject_ts(gcpu, ss.selector);
			return -1;
		}
	}

	gcpu_set_segment_reg(gcpu, IA32_SEG_SS, ss.selector, ss.base,
		ss.limit, ss.ar.value);

	return 0;
}

/*
 * Set guest CS according to new tss.
 */
static
int set_guest_cs(guest_cpu_handle_t gcpu, seg_reg_t *gdtr,
		 seg_reg_t *ldtr, tss32_t *tss)
{
	desc_t desc;
	seg_reg_t cs;
	seg_reg_t *dtr;
	uint32_t cpl;
	int r;

	mon_memset(&cs, 0, sizeof(cs));
	cs.selector = (uint16_t)tss->cs;
	cpl = SELECTOR_RPL(tss->cs);

	if (SELECTOR_IDX(cs.selector) == 0) {
		/* must not be null */
		gcpu_inject_ts(gcpu, cs.selector);
		return -1;
	}

	dtr = SELECTOR_GDT(cs.selector) ? gdtr : ldtr;

	r = copy_from_gva(gcpu,
		(uint64_t)(dtr->base + SELECTOR_IDX(cs.selector)),
		sizeof(desc),
		(uint64_t)(&desc));

	if (r != 0) {
		gcpu_inject_ts(gcpu, cs.selector);
		return -1;
	}

	parse_desc(&desc, &cs);

	if (cs.ar.bits.p_bit != 1) {
		/* must be present */
		gcpu_inject_np(gcpu, cs.selector);
		return -1;
	}

	if ((cs.ar.bits.s_bit == 0) ||          /* must be non-sys desc */
	    !IS_CODE(cs.ar.bits.type)) {        /* must be code */
		gcpu_inject_ts(gcpu, cs.selector);
		return -1;
	}

	/* Priv checks */
	if (IS_CODE_CONFORM(cs.ar.bits.type)) {
		if (cs.ar.bits.dpl > cpl) {
			gcpu_inject_ts(gcpu, cs.selector);
			return -1;
		}
	} else {
		if (cs.ar.bits.dpl != cpl) {
			gcpu_inject_ts(gcpu, cs.selector);
			return -1;
		}
	}

	/* If g_bit is set, the unit is 4 KB. */
	if (cs.ar.bits.g_bit == 1) {
		cs.limit = (cs.limit << 12) | 0xfff;
	}

	if (!IS_ASSESSED(cs.ar.bits.type)) {
		SET_ASSESSED(cs.ar.bits.type);
		SET_ASSESSED(desc.bits.type);

		r = copy_to_gva(gcpu,
			(uint64_t)(dtr->base + (cs.selector & 0xfff8)),
			sizeof(desc),
			(uint64_t)(&desc));

		if (r != 0) {
			gcpu_inject_ts(gcpu, cs.selector);
			return -1;
		}
	}

	cs.ar.bits.null_bit = 0;

	gcpu_set_segment_reg(gcpu, IA32_SEG_CS, cs.selector, cs.base,
		cs.limit, cs.ar.value);

	if (tss->eip > cs.limit) {
		gcpu_inject_ts(gcpu, cs.selector);
		return -1;
	}

	return 0;
}

/*
 * Set guest ES, DS, FS, or GS, based on register name and new tss.
 */
static
int set_guest_seg(guest_cpu_handle_t gcpu, seg_reg_t *gdtr, seg_reg_t *ldtr,
		  tss32_t *tss, mon_ia32_segment_registers_t name)
{
	desc_t desc;
	seg_reg_t seg;
	seg_reg_t *dtr;
	uint32_t cpl;
	int r;

	mon_memset(&seg, 0, sizeof(seg));

	if (name == IA32_SEG_ES) {
		seg.selector = (uint16_t)tss->es;
	} else if (name == IA32_SEG_DS) {
		seg.selector = (uint16_t)tss->ds;
	} else if (name == IA32_SEG_FS) {
		seg.selector = (uint16_t)tss->fs;
	} else if (name == IA32_SEG_GS) {
		seg.selector = (uint16_t)tss->gs;
	} else {
		return -1;
	}

	cpl = SELECTOR_RPL(tss->cs);

	dtr = SELECTOR_GDT(seg.selector) ? gdtr : ldtr;

	if (SELECTOR_IDX(seg.selector) == 0) {
		seg.selector = 0;
		seg.ar.bits.null_bit = 1;
		goto set_seg_reg;
	}

	r = copy_from_gva(gcpu,
		(uint64_t)(dtr->base + SELECTOR_IDX(seg.selector)),
		sizeof(desc), (uint64_t)(&desc)
		);

	if (r != 0) {
		force_ring3_ss(gcpu);
		gcpu_inject_ts(gcpu, seg.selector);
		return -1;
	}

	parse_desc(&desc, &seg);

	if ((seg.ar.bits.s_bit == 0) || /* must be non-sys desc */
	    (IS_CODE(seg.ar.bits.type) && !IS_CODE_R(seg.ar.bits.type))) {
		force_ring3_ss(gcpu);
		gcpu_inject_ts(gcpu, seg.selector);
		return -1;
	}

	if (seg.ar.bits.p_bit != 1) {
		/* Must be present. */
		force_ring3_ss(gcpu);
		gcpu_inject_np(gcpu, seg.selector);
		return -1;
	}

	/* If g_bit is set, the unit is 4 KB. */
	if (seg.ar.bits.g_bit == 1) {
		seg.limit = (seg.limit << 12) | 0xfff;
	}

	/* Priv checks. */
	if (IS_CODE(seg.ar.bits.type) && !IS_CODE_CONFORM(seg.ar.bits.type)) {
		uint32_t rpl = (uint32_t)SELECTOR_RPL(seg.selector);

		if ((seg.ar.bits.dpl < cpl) || (seg.ar.bits.dpl < rpl)) {
			force_ring3_ss(gcpu);
			gcpu_inject_ts(gcpu, seg.selector);
			return -1;
		}
	}

set_seg_reg:

	gcpu_set_segment_reg(gcpu, name, seg.selector, seg.base,
		seg.limit, seg.ar.value);

	return 0;
}

/*
 * Copy guest status from VMCS to tss buffer.
 */
static
void copy_vmcs_to_tss32(guest_cpu_handle_t gcpu, tss32_t *tss)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	tss->eip = (uint32_t)gcpu_get_gp_reg(gcpu, IA32_REG_RIP);
	tss->eflags = (uint32_t)gcpu_get_gp_reg(gcpu, IA32_REG_RFLAGS);
	tss->eax = (uint32_t)gcpu_get_gp_reg(gcpu, IA32_REG_RAX);
	tss->ecx = (uint32_t)gcpu_get_gp_reg(gcpu, IA32_REG_RCX);
	tss->edx = (uint32_t)gcpu_get_gp_reg(gcpu, IA32_REG_RDX);
	tss->ebx = (uint32_t)gcpu_get_gp_reg(gcpu, IA32_REG_RBX);
	tss->esp = (uint32_t)gcpu_get_gp_reg(gcpu, IA32_REG_RSP);
	tss->ebp = (uint32_t)gcpu_get_gp_reg(gcpu, IA32_REG_RBP);
	tss->esi = (uint32_t)gcpu_get_gp_reg(gcpu, IA32_REG_RSI);
	tss->edi = (uint32_t)gcpu_get_gp_reg(gcpu, IA32_REG_RDI);

	tss->es = (uint32_t)mon_vmcs_read(vmcs, VMCS_GUEST_ES_SELECTOR);
	tss->cs = (uint32_t)mon_vmcs_read(vmcs, VMCS_GUEST_CS_SELECTOR);
	tss->ss = (uint32_t)mon_vmcs_read(vmcs, VMCS_GUEST_SS_SELECTOR);
	tss->ds = (uint32_t)mon_vmcs_read(vmcs, VMCS_GUEST_DS_SELECTOR);
	tss->fs = (uint32_t)mon_vmcs_read(vmcs, VMCS_GUEST_FS_SELECTOR);
	tss->gs = (uint32_t)mon_vmcs_read(vmcs, VMCS_GUEST_GS_SELECTOR);
}

/*
 * This function does task switch for 32-bit MON guest.
 */
int task_switch_for_guest(guest_cpu_handle_t gcpu,
			  ia32_vmx_vmcs_vmexit_info_idt_vectoring_t vec_info)
{
	int ret;
	uint32_t inst_type;
	tss32_t tss;

	cr0_t cr0;
	dr7_t dr7;

	seg_reg_t gdtr;
	seg_reg_t old_ldtr;
	seg_reg_t new_ldtr;

	seg_reg_t new_tr;
	seg_reg_t old_tr;
	desc_t new_tss_desc;
	desc_t old_tss_desc;

	gcpu_get_gdt_reg(gcpu, (uint64_t *)&(gdtr.base),
		(uint32_t *)&(gdtr.limit));
	gdtr.ar.value = 0x000080;

	cr0.value =
		(uint32_t)gcpu_get_guest_visible_control_reg(gcpu,
			IA32_CTRL_CR0);

	/* Find new tr & tss. */

	get_task_info(gcpu, &inst_type, &(new_tr.selector), vec_info);

	ret = copy_from_gva(gcpu,
		(uint64_t)(gdtr.base + SELECTOR_IDX(new_tr.selector)),
		sizeof(new_tss_desc),
		(uint64_t)(&new_tss_desc));

	if (ret != 0) {
		gcpu_inject_ts(gcpu, new_tr.selector);
		return -1;
	}

	parse_desc(&new_tss_desc, &new_tr);

	if (!IS_TSS32(new_tr.ar.bits.type)) {
		gcpu_inject_ts(gcpu, new_tr.selector);
		return -1;
	}

	/* Find old ldtr. */

	gcpu_get_segment_reg(gcpu, IA32_SEG_LDTR,
		(uint16_t *)&(old_ldtr.selector),
		(uint64_t *)&(old_ldtr.base),
		(uint32_t *)&(old_ldtr.limit),
		(uint32_t *)&(old_ldtr.ar));

	/* Find old tr. */

	gcpu_get_segment_reg(gcpu, IA32_SEG_TR,
		(uint16_t *)&(old_tr.selector),
		(uint64_t *)&(old_tr.base),
		(uint32_t *)&(old_tr.limit),
		(uint32_t *)&(old_tr.ar));

	if (!IS_TSS32_BUSY(old_tr.ar.bits.type)) {
		gcpu_inject_ts(gcpu, old_tr.selector);
		return -1;
	}

	/* Save guest status to old tss. */
	/* call, jmp or iret */
	if (inst_type != TASK_SWITCH_TYPE_IDT) {
		gcpu_skip_guest_instruction(gcpu);
	}

	mon_memset(&tss, 0, sizeof(tss));
	copy_vmcs_to_tss32(gcpu, &tss);

	if (inst_type == TASK_SWITCH_TYPE_IRET) {
		((eflags_t *)&(tss.eflags))->bits.nested_task = 0;
	}

	ret = copy_to_gva(gcpu,
		/* gva of old_tss.eip */
		(uint64_t)(old_tr.base + 32),
		/* from eip to gs: total 64 bytes */
		64,
		/* hva of old_tss.eip */
		(uint64_t)&(tss.eip));

	if (ret != 0) {
		gcpu_inject_ts(gcpu, old_tr.selector);
		return -1;
	}

	/* Read new tss from memory. */

	mon_memset(&tss, 0, sizeof(tss));

	ret = copy_from_gva(gcpu,
		(uint64_t)(new_tr.base),
		sizeof(tss),
		(uint64_t)&(tss));

	if (ret != 0) {
		gcpu_inject_ts(gcpu, new_tr.selector);
		return -1;
	}

	/* Clear busy bit in old tss descriptor. */

	if ((inst_type == TASK_SWITCH_TYPE_JMP) ||
	    (inst_type == TASK_SWITCH_TYPE_IRET)) {
		ret = copy_from_gva(gcpu,
			(uint64_t)(gdtr.base + SELECTOR_IDX(old_tr.selector)),
			sizeof(old_tss_desc),
			(uint64_t)(&old_tss_desc));

		if (ret != 0) {
			gcpu_inject_ts(gcpu, old_tr.selector);
			return -1;
		}

		/* Clear the B bit, and write it back. */
		old_tss_desc.bits.type = TSS32_AVAL;

		ret = copy_to_gva(gcpu,
			(uint64_t)(gdtr.base + SELECTOR_IDX(old_tr.selector)),
			sizeof(old_tss_desc),
			(uint64_t)(&old_tss_desc));

		if (ret != 0) {
			gcpu_inject_ts(gcpu, old_tr.selector);
			return -1;
		}
	}

	/* Set busy bit in new tss descriptor. */

	if (inst_type != TASK_SWITCH_TYPE_IRET) {
		new_tss_desc.bits.type = TSS32_BUSY;
		new_tr.ar.bits.type = TSS32_BUSY;

		ret = copy_to_gva(gcpu,
			(uint64_t)(gdtr.base + SELECTOR_IDX(new_tr.selector)),
			sizeof(new_tss_desc),
			(uint64_t)(&new_tss_desc));

		if (ret != 0) {
			gcpu_inject_ts(gcpu, new_tr.selector);
			return -1;
		}
	}

	/* Save old tr in new tss. */

	if ((inst_type == TASK_SWITCH_TYPE_CALL) ||
	    (inst_type == TASK_SWITCH_TYPE_IDT)) {
		/* gva of new_tss.prev_tr */
		ret = copy_to_gva(gcpu, (uint64_t)(new_tr.base + 0),
			/* two bytes */
			sizeof(old_tr.selector),
			/* hva */
			(uint64_t)(&(old_tr.selector)));

		if (ret != 0) {
			new_tss_desc.bits.type = TSS32_AVAL;

			copy_to_gva(gcpu,
				(uint64_t)(gdtr.base +
					   SELECTOR_IDX(new_tr.selector)),
				sizeof(new_tss_desc),
				(uint64_t)(&new_tss_desc));

			gcpu_inject_ts(gcpu, new_tr.selector);
			return -1;
		}
	}

	/* Load new tr. */

	gcpu_set_segment_reg(gcpu, IA32_SEG_TR, new_tr.selector,
		new_tr.base, new_tr.limit, new_tr.ar.value);

	/* Load new cr3. */

	if (cr0.bits.pg) {
		gcpu_set_guest_visible_control_reg(gcpu, IA32_CTRL_CR3,
			tss.cr3);
		gcpu_set_control_reg(gcpu, IA32_CTRL_CR3, tss.cr3);
	}

	/* Load new flags. */

	if ((inst_type == TASK_SWITCH_TYPE_CALL) ||
	    (inst_type == TASK_SWITCH_TYPE_IDT)) {
		((eflags_t *)&(tss.eflags))->bits.nested_task = 1;
	}

	((eflags_t *)&(tss.eflags))->bits.rsvd_1 = 1;

	/* Load general regs. */

	gcpu_set_gp_reg(gcpu, IA32_REG_RIP, (uint64_t)tss.eip);
	gcpu_set_gp_reg(gcpu, IA32_REG_RFLAGS, (uint64_t)tss.eflags);
	gcpu_set_gp_reg(gcpu, IA32_REG_RAX, (uint64_t)tss.eax);
	gcpu_set_gp_reg(gcpu, IA32_REG_RCX, (uint64_t)tss.ecx);
	gcpu_set_gp_reg(gcpu, IA32_REG_RDX, (uint64_t)tss.edx);
	gcpu_set_gp_reg(gcpu, IA32_REG_RBX, (uint64_t)tss.ebx);
	gcpu_set_gp_reg(gcpu, IA32_REG_RBP, (uint64_t)tss.ebp);
	gcpu_set_gp_reg(gcpu, IA32_REG_RSP, (uint64_t)tss.esp);
	gcpu_set_gp_reg(gcpu, IA32_REG_RSI, (uint64_t)tss.esi);
	gcpu_set_gp_reg(gcpu, IA32_REG_RDI, (uint64_t)tss.edi);

	/* Set the TS bit in CR0. */

	cr0.bits.ts = 1;
	gcpu_set_guest_visible_control_reg(gcpu, IA32_CTRL_CR0, cr0.value);
	gcpu_set_control_reg(gcpu, IA32_CTRL_CR0, cr0.value);

	/* Load new ldtr. */

	if (tss.ldtr != old_ldtr.selector) {
		if (set_guest_ldtr(gcpu, &gdtr, &new_ldtr, &tss) != 0) {
			return -1;
		}
	}

	/* Load new seg regs. */

	if (((eflags_t *)&(tss.eflags))->bits.v86_mode == 1) {
		uint16_t es = (uint16_t)tss.es;
		uint16_t cs = (uint16_t)tss.cs;
		uint16_t ss = (uint16_t)tss.ss;
		uint16_t ds = (uint16_t)tss.ds;
		uint16_t fs = (uint16_t)tss.fs;
		uint16_t gs = (uint16_t)tss.gs;

		/* Set v86 selector, base, limit, ar, in real-mode style. */
		gcpu_set_segment_reg(gcpu,
			IA32_SEG_ES,
			es,
			es << 4,
				0xffff,
				0xf3);
		gcpu_set_segment_reg(gcpu,
			IA32_SEG_CS,
			cs,
			cs << 4,
				0xffff,
				0xf3);
		gcpu_set_segment_reg(gcpu,
			IA32_SEG_SS,
			ss,
			ss << 4,
				0xffff,
				0xf3);
		gcpu_set_segment_reg(gcpu,
			IA32_SEG_DS,
			ds,
			ds << 4,
				0xffff,
				0xf3);
		gcpu_set_segment_reg(gcpu,
			IA32_SEG_FS,
			fs,
			fs << 4,
				0xffff,
				0xf3);
		gcpu_set_segment_reg(gcpu,
			IA32_SEG_GS,
			gs,
			gs << 4,
				0xffff,
				0xf3);

		goto all_done;
	}

	/* Load new ss. */

	if (set_guest_ss(gcpu, &gdtr, &new_ldtr, &tss) != 0) {
		return -1;
	}

	/* Load new es, ds, fs, gs. */

	if ((set_guest_seg(gcpu, &gdtr, &new_ldtr, &tss, IA32_SEG_ES) != 0) ||
	    (set_guest_seg(gcpu, &gdtr, &new_ldtr, &tss, IA32_SEG_DS) != 0) ||
	    (set_guest_seg(gcpu, &gdtr, &new_ldtr, &tss, IA32_SEG_FS) != 0) ||
	    (set_guest_seg(gcpu, &gdtr, &new_ldtr, &tss, IA32_SEG_GS) != 0)) {
		return -1;
	}

	/* Load new cs. */

	if (set_guest_cs(gcpu, &gdtr, &new_ldtr, &tss) != 0) {
		return -1;
	}

all_done:

	/* Clear the LE bits in dr7. */

	dr7.value = (uint32_t)gcpu_get_debug_reg(gcpu, IA32_REG_DR7);
	dr7.bits.l0 = 0;
	dr7.bits.l1 = 0;
	dr7.bits.l2 = 0;
	dr7.bits.l3 = 0;
	dr7.bits.le = 0;
	gcpu_set_debug_reg(gcpu, IA32_REG_DR7, (uint64_t)dr7.value);

	/* Debug trap in new task? */

	if ((tss.io_base_addr & 0x00000001) != 0) {
		gcpu_inject_db(gcpu);
		return -1;
	}

	return 0;
}

vmexit_handling_status_t vmexit_task_switch(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	ia32_vmx_vmcs_vmexit_info_idt_vectoring_t idt_vectoring_info;
	ia32_vmx_exit_qualification_t qualification;

	idt_vectoring_info.uint32 = (uint32_t)mon_vmcs_read(vmcs,
		VMCS_EXIT_INFO_IDT_VECTORING);
	qualification.uint64 =
		mon_vmcs_read(vmcs, VMCS_EXIT_INFO_QUALIFICATION);

#ifdef DEBUG
	{
#define TS_NMI_VECOTR           0x02
#define TS_DOUBLE_FAULT_VECTOR  0x08
#define TS_MC_VECOR             0x12

		char *task_switch_name[] = {
			"nmi", "Double Fault", "Machine Check", "Others" };
		char *task_switch_type[] = {
			"Call", "IRET", "Jmp", "Task Gate" };
		/* default Double fault */
		uint32_t name_id = 3;
		uint32_t vector = (uint32_t)-1;

		if (idt_vectoring_info.bits.valid) {
			vector = idt_vectoring_info.bits.vector;
		}

		if (qualification.task_switch.source == TASK_SWITCH_TYPE_IDT) {
			if (qualification.task_switch.tss_selector == 0x50) {
				name_id = 1;
			} else if (qualification.task_switch.tss_selector ==
				   0x58) {
				name_id = 0;
			} else if (qualification.task_switch.tss_selector ==
				   0xa0) {
				name_id = 2;
			} else {
				name_id = 3;
			}
		}

		MON_LOG(mask_anonymous,
			level_trace,
			"Task Switch on CPU#%d src:%s type:%s tss:0x%x Qual:0x%x Vec:0x%x \n",
			mon_guest_vcpu(gcpu)->guest_cpu_id,
			task_switch_name[name_id],
			task_switch_type[qualification.task_switch.source],
			(qualification.uint64 & 0xffff),
			qualification.uint64,
			vector);
	}
#endif    /* DEBUG */

	if (idt_vectoring_info.bits.valid &&
	    qualification.task_switch.source == TASK_SWITCH_TYPE_IDT) {
		/* clear IDT if valid, so that we can inject the event when needed
		 * (see event injections in task_switch_for_guest() below)
		 * or
		 * avoid re-inject unwanted exception, e.g NMI. */
		mon_vmcs_write(vmcs, VMCS_EXIT_INFO_IDT_VECTORING, 0);
	}

	/* pass idt_vectoring_info on to the following function calling since
	 * the value in VMCS_EXIT_INFO_IDT_VECTORING VMCS may be cleared above */
	task_switch_for_guest(gcpu, idt_vectoring_info);
	return VMEXIT_HANDLED;
}

/* End of file */
