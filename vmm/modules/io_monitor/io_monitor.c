/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "dbg.h"
#include "heap.h"
#include "guest.h"
#include "gcpu.h"
#include "vmexit.h"
#include "vmcs.h"
#include "hmm.h"
#include "gcpu_inject_event.h"
#include "vmx_cap.h"
#include "vmm_arch.h"
#include "event.h"

#include "lib/util.h"

#include "modules/io_monitor.h"

/*-----------------Local Types and Macros Definitions----------------*/
#define INS 1
#define OUTS 0
#define IO_BUFFER_SIZE 512

typedef struct io_monitor {
	uint16_t		io_port;
	uint16_t		pad[3];
	io_read_handler_t	io_read_handler;
	io_write_handler_t	io_write_handler;
	struct io_monitor	*next;
} io_monitor_t;

typedef struct io_monitor_guest {
	uint16_t		guest_id;
	uint16_t		pad[3];
	uint8_t			*io_bitmap;
	io_monitor_t		*io_list;
	struct io_monitor_guest	*next;
} io_monitor_guest_t;

/*-----------------Local Variables----------------*/

static io_monitor_guest_t *g_io_monitor;

/*-----------------Forward Declarations for Local Functions----------------*/
inline static io_monitor_guest_t *io_monitor_guest_lookup(uint16_t guest_id)
{
	io_monitor_guest_t *io_mon_guest = g_io_monitor;

	while (io_mon_guest) {
		if (io_mon_guest->guest_id == guest_id)
			break;
		io_mon_guest = io_mon_guest->next;
	}

	return io_mon_guest;
}

inline static io_monitor_t *io_list_lookup(io_monitor_t *io_list, uint16_t io_port)
{
	io_monitor_t *p_io_mon = io_list;

	while (p_io_mon) {
		if (p_io_mon->io_port == io_port)
			break;
		p_io_mon = p_io_mon->next;
	}

	return p_io_mon;
}

void io_monitor_register(uint16_t guest_id,
				uint16_t port_id,
				io_read_handler_t read_handler,
				io_write_handler_t write_handler)
{
	io_monitor_guest_t *p_io_mon_guest;
	io_monitor_t * p_io_mon;

	D(VMM_ASSERT((read_handler || write_handler)));

	print_trace("Guest(%d) register io handler for port(%d)\n", guest_id, port_id);

	p_io_mon_guest = io_monitor_guest_lookup(guest_id);

	if (p_io_mon_guest == NULL) {
		p_io_mon_guest = (io_monitor_guest_t *)mem_alloc(sizeof(io_monitor_guest_t));

		p_io_mon_guest->guest_id = guest_id;
		p_io_mon_guest->io_bitmap = page_alloc(2);
		memset((void *)p_io_mon_guest->io_bitmap, 0, 2 * PAGE_4K_SIZE);
		p_io_mon_guest->io_list = NULL;

		p_io_mon_guest->next = g_io_monitor;
		g_io_monitor = p_io_mon_guest;
	}

	p_io_mon = io_list_lookup(p_io_mon_guest->io_list, port_id);
	VMM_ASSERT_EX(p_io_mon == NULL, "io monitor should not be re-registered\n");

	p_io_mon = mem_alloc(sizeof(io_monitor_t));
	p_io_mon->io_port = port_id;
	p_io_mon->next = p_io_mon_guest->io_list;
	p_io_mon_guest->io_list = p_io_mon;

	BITARRAY_SET((uint64_t *)(void *)p_io_mon_guest->io_bitmap, port_id);
	p_io_mon->io_read_handler = read_handler;
	p_io_mon->io_write_handler = write_handler;
}

uint32_t io_transparent_read_handler(UNUSED guest_cpu_handle_t gcpu,
				uint16_t port_id,
				uint32_t port_size)
{
	uint32_t value = 0;

	switch (port_size) {
	case 1:
		value = (uint32_t)asm_in8(port_id);
		break;
	case 2:
		value = (uint32_t)asm_in16(port_id);
		break;
	case 4:
		value = asm_in32(port_id);
		break;
	default:
		print_trace("Invalid IO port size(%d)\n", port_size);
		VMM_DEADLOOP();
		break;
	}

	return value;
}

void io_transparent_write_handler(UNUSED guest_cpu_handle_t gcpu,
				uint16_t port_id,
				uint32_t port_size,
				uint32_t value)
{
	switch (port_size) {
	case 1:
		asm_out8(port_id, (uint8_t)value);
		break;
	case 2:
		asm_out16(port_id, (uint16_t)value);
		break;
	case 4:
		asm_out32(port_id, value);
		break;
	default:
		print_trace("Invalid IO port size(%d)\n", port_size);
		VMM_DEADLOOP();
		break;
	}
}

static seg_id_t get_ios_seg(guest_cpu_handle_t gcpu,
				const char **p_direction,
				const char **p_seg_name)
{
	seg_id_t seg_id;
	const char *direction;
	const char *seg_name;
	vmx_exit_qualification_t qualification;
	vmx_exit_instr_info_t ios_instr_info;

	qualification.uint64 = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);

	if (qualification.io_instruction.direction == INS) { //INS -- check ES. cannot override
		direction = "INS";
		seg_name = "ES";
		seg_id = SEG_ES;
	} else { //OUTS -- default is DS. may override
		direction = "OUTS";
		ios_instr_info.uint32 =
			(uint32_t)vmcs_read(gcpu->vmcs, VMCS_EXIT_INSTR_INFO);
		switch (ios_instr_info.ins_outs_instr.seg_reg) {
		case 0: /* ES */
			seg_name = "ES";
			seg_id = SEG_ES;
			break;
		case 1: /* CS */
			seg_name = "CS";
			seg_id = SEG_CS;
			break;
		case 2: /* SS */
			seg_name = "SS";
			seg_id = SEG_SS;
			break;
		case 3: /* DS */
			seg_name = "DS";
			seg_id = SEG_DS;
			break;
		case 4: /* FS */
			seg_name = "FS";
			seg_id = SEG_FS;
			break;
		case 5: /* GS */
			seg_name = "GS";
			seg_id = SEG_GS;
			break;
		default:
			seg_name = "UNKNOWN";
			seg_id = SEG_COUNT;
			print_panic("Undefined value %d for segment register"
				" in the VM-Exit Instruction-Information Field\n",
				ios_instr_info.ins_outs_instr.seg_reg);
			VMM_DEADLOOP();
		}
	}

	if (p_direction) {
		*p_direction = direction;
	}

	if (p_seg_name) {
		*p_seg_name = seg_name;
	}

	return seg_id;
}

static boolean_t check_unusable_segment(guest_cpu_handle_t gcpu)
{
	const char* seg_name;
	const char* direction;
	seg_ar_t seg_ar;

	gcpu_get_seg(gcpu, get_ios_seg(gcpu, &direction, &seg_name),
				NULL, NULL, NULL, &seg_ar.u32);

	if (seg_ar.bits.null_bit) {
		print_warn("%s - %s segment is unusable, inject #GP\n",
			direction, seg_name);
		gcpu_inject_gp0(gcpu);
		return FALSE;
	}

	if (seg_ar.bits.p_bit == 0) {
		print_warn("%s - %s segment is not present, inject #GP\n",
			direction, seg_name);
		gcpu_inject_gp0(gcpu);
		return FALSE;
	}

	return TRUE;
}

static boolean_t check_canonical_address(guest_cpu_handle_t gcpu)
{
	uint64_t gva;

	gva = vmcs_read(gcpu->vmcs, VMCS_GUEST_LINEAR_ADDR);

	if (addr_is_canonical(TRUE, gva) == FALSE) {
		print_warn("address 0x%llX is not canonical, inject #GP\n", gva);
		gcpu_inject_gp0(gcpu);
		return FALSE;
	}

	return TRUE;
}

static boolean_t check_alignment(guest_cpu_handle_t gcpu)
{
	uint32_t cpl;
	uint64_t rflags;
	uint64_t gva;
	uint64_t cr0;
	vmx_exit_qualification_t qualification;

	cr0 = gcpu_get_visible_cr0(gcpu);
	rflags = vmcs_read(gcpu->vmcs, VMCS_GUEST_RFLAGS);
	cpl = ((uint32_t)vmcs_read(gcpu->vmcs, VMCS_GUEST_CS_SEL)) & 0x3;

	if ((cpl == 3) && (cr0 & CR0_AM) && (rflags & RFLAGS_AC)) {
		qualification.uint64 = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);
		gva = vmcs_read(gcpu->vmcs, VMCS_GUEST_LINEAR_ADDR);

		//qualification.io_instruction.size is 0/1/3 for insb/insw/insl and outsb/outsw/outs
		if ((gva & qualification.io_instruction.size) != 0) {
			print_warn("address 0x%llX is an unaligned, inject #AC\n", gva);
			gcpu_inject_ac(gcpu);
			return FALSE;
		}
	}

	return TRUE;
}

static boolean_t check_seg_limit(guest_cpu_handle_t gcpu)
{
	uint64_t start, end, len;
	uint32_t seg_limit;
	uint32_t port_size;
	uint32_t rep_cnt;
	uint64_t df;
	seg_id_t seg_id;
	const char *seg_name;
	const char *direction;
	vmx_exit_qualification_t qualification;

	df = vmcs_read(gcpu->vmcs, VMCS_GUEST_RFLAGS) & RFLAGS_DF;
	seg_id = get_ios_seg(gcpu, &direction, &seg_name);
	qualification.uint64 = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);
	port_size = (uint32_t)qualification.io_instruction.size + 1;
	rep_cnt = (qualification.io_instruction.rep ?
		(uint32_t)gcpu_get_gp_reg(gcpu, REG_RCX) : 1);

	if ((gcpu_get_visible_cr0(gcpu) & CR0_PE) == 0) { //Real-address mode
		seg_limit = 0xFFFFULL;
	} else {
		gcpu_get_seg(gcpu, seg_id, NULL, NULL, &seg_limit, NULL);
	}

	if (qualification.io_instruction.direction == INS) {
		start = gcpu_get_gp_reg(gcpu, REG_RDI);
	} else {
		start = gcpu_get_gp_reg(gcpu, REG_RSI);
	}

	len = port_size * rep_cnt;
	if (df) {
		end = start - len;
	} else {
		end = start + len;
	}

	if ((start > seg_limit) || (len > seg_limit) || (end > seg_limit)){
		if (seg_id == SEG_SS) {
			print_warn("%s - address 0x%llX - 0x%llX is outside the limit 0x%X"
				" of the %s segment, inject #SS\n",
				direction, start, end, seg_limit, seg_name);
			gcpu_inject_ss(gcpu, (uint32_t)vmcs_read(gcpu->vmcs, VMCS_GUEST_SS_SEL));
		} else {
			print_warn("%s - address 0x%llX - 0x%llX is outside the limit 0x%X"
				" of the %s segment, inject #GP\n",
				direction, start, end, seg_limit, seg_name);
			gcpu_inject_gp0(gcpu);
		}
		return FALSE;
	}

	return TRUE;
}

static boolean_t check_none_writable_segment(guest_cpu_handle_t gcpu)
{
	seg_ar_t seg_ar;
	vmx_exit_qualification_t qualification;

	qualification.uint64 = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);
	if (qualification.io_instruction.direction == OUTS) //the check is not applied to OUTS
		return TRUE;

	seg_ar.u32= vmcs_read(gcpu->vmcs, VMCS_GUEST_ES_AR);
	if (seg_ar.bits.p_bit && //segment-present
		seg_ar.bits.s_bit && //a code or data segment
		((seg_ar.bits.type & 0xa) == 2)) {//writable data segment
		return TRUE;
	}

	print_warn("the destination is located in"
		" a non-writable segment, inject #GP\n");
	gcpu_inject_gp0(gcpu);
	return FALSE;
}

static boolean_t realaddress_check_io_fault(guest_cpu_handle_t gcpu)
{
	//check address is outside the segment limit
	return check_seg_limit(gcpu);
}

static boolean_t vr8086_check_io_fault(guest_cpu_handle_t gcpu)
{
	//check #AC
	return check_alignment(gcpu);
}

static boolean_t protected_check_io_fault(guest_cpu_handle_t gcpu)
{
	//check segment unusable
	if (!check_unusable_segment(gcpu))
		return FALSE;

	//check address is outside the segment limit
	if (!check_seg_limit(gcpu))
		return FALSE;

	//check the destination is located in a non-writable segment only for INS
	if (!check_none_writable_segment(gcpu))
		return FALSE;

	//check #AC
	if (!check_alignment(gcpu))
		return FALSE;

	return TRUE;
}

static boolean_t x64_check_io_fault(guest_cpu_handle_t gcpu)
{
	//check canonical address form
	if (!check_canonical_address(gcpu))
		return FALSE;

	//check #AC
	if (!check_alignment(gcpu))
		return FALSE;

	return TRUE;
}

/* according to IA32 spec, volume 3, chapter 25.1.1, below exceptions have priority over VM exits:
 * 1. invalid-opcode exceptions, faults based on privilege level
 * 2. general-protection exceptions that are based on checking I/O permission bits in the TSS */

/* below is the table of the exceptions to be checked here
**********************************************************************************************
                                        realmode      v8086      32bits      64bits
#GP(non-SS limit)                          1                       1
#SS(SS limit)                              1                       1
#GP(unwritable)                                                    1
#GP(unused or non-present segment)                                 1
#AC(unaligned)                                          1          1            1
#GP(non-canonical)                                                              1
**********************************************************************************************/
static boolean_t io_monitor_check_fault(guest_cpu_handle_t gcpu)
{
	uint64_t cr0;
	uint64_t efer;
	uint64_t rflags;
	seg_ar_t cs_ar;
	vmx_exit_qualification_t qualification;

	qualification.uint64 = vmcs_read(gcpu->vmcs, VMCS_EXIT_QUAL);

	/* For IN/OUT, only #GP and #UD might happen.  Since they are
	 * checked by CPU before VMExit, no need to check again. */
	if (qualification.io_instruction.string == FALSE) //ordinary IN/OUT instruction
		return TRUE;

	cr0 = gcpu_get_visible_cr0(gcpu);
	if ((cr0 & CR0_PE) == 0) { //Real-address mode
		return realaddress_check_io_fault(gcpu);
	}

	//CR0.PE is set
	efer = vmcs_read(gcpu->vmcs, VMCS_GUEST_EFER);
	if (efer & EFER_LME) { //IA-32e mode
		cs_ar.u32 = vmcs_read(gcpu->vmcs, VMCS_GUEST_CS_AR);
		if (cs_ar.bits.l_bit) {
			return x64_check_io_fault(gcpu);
		} else { //Compatibility mode , same as protected mode
			return protected_check_io_fault(gcpu);
		}
	}

	//EFER.LME is clear
	rflags = vmcs_read(gcpu->vmcs, VMCS_GUEST_RFLAGS);
	if (rflags & RFLAGS_VM) { //Virtual-8086 mode
		return vr8086_check_io_fault(gcpu);
	} else { //Protected mode
		return protected_check_io_fault(gcpu);
	}

	return TRUE;
}

static void handle_in(guest_cpu_handle_t gcpu,
				uint16_t port_id,
				uint32_t port_size,
				io_read_handler_t read_handler)
{
	uint32_t io_value = 0;

	io_value = read_handler(gcpu, port_id, port_size);
	gcpu_set_gp_reg(gcpu, REG_RAX, io_value & MASK64_LOW(port_size * 8));
}

static void handle_out(guest_cpu_handle_t gcpu,
				uint16_t port_id,
				uint32_t port_size,
				io_write_handler_t write_handler)
{
	uint64_t io_value = 0;

	io_value = gcpu_get_gp_reg(gcpu, REG_RAX);
	write_handler(gcpu, port_id, port_size, io_value & MASK64_LOW(port_size * 8));
}

static void handle_ins(guest_cpu_handle_t gcpu,
				uint16_t port_id,
				uint32_t port_size,
				uint32_t rep_cnt,
				io_read_handler_t read_handler)
{
	vmcs_obj_t vmcs = gcpu->vmcs;
	pf_info_t pfinfo;
	uint8_t buf[IO_BUFFER_SIZE];
	uint32_t value = 0;
	uint32_t iter_cnt;
	uint32_t cpy_size;
	uint32_t i;
	uint64_t df;
	boolean_t ret;

	/* The linear address gva is the base address of
	* relevant segment plus (E)DI (for INS) or (E)SI
	* (for OUTS). It is valid only when the relevant
	* segment is usable. Otherwise, it is undefined. */
	uint64_t gva = vmcs_read(vmcs, VMCS_GUEST_LINEAR_ADDR);
	df = vmcs_read(gcpu->vmcs, VMCS_GUEST_RFLAGS) & RFLAGS_DF;

	while (rep_cnt > 0) {
		iter_cnt = MIN(rep_cnt, IO_BUFFER_SIZE / port_size);
		cpy_size = iter_cnt * port_size;
		for (i = 0; i < iter_cnt; i++) {
			value = read_handler(gcpu, port_id, port_size);
			if (df) {
				memcpy((void *)&buf[IO_BUFFER_SIZE - (i + 1) * port_size],
								(void *)&value, port_size);
			} else {
				memcpy((void *)&buf[i * port_size], (void *)&value, port_size);
			}
		}

		if (df) {
			ret = gcpu_copy_to_gva(gcpu, gva - cpy_size + port_size,
				(uint64_t)&buf[IO_BUFFER_SIZE - cpy_size], cpy_size, &pfinfo);
		} else {
			ret = gcpu_copy_to_gva(gcpu, gva, (uint64_t)buf, cpy_size, &pfinfo);
		}

		if (!ret) {
			VMM_ASSERT_EX(pfinfo.is_pf, "Guest(%d) fail to copy io to "
					"gva(0x%llX) with df(%d)\n", gcpu->guest->id, gva, df?1:0);
			gcpu_set_cr2(gcpu, pfinfo.cr2);
			gcpu_inject_pf(gcpu, pfinfo.ec);
			return;
		}

		if (df) {
			gva -= cpy_size;
		} else {
			gva += cpy_size;
		}
		rep_cnt -= iter_cnt;
	}
}

static void handle_outs(guest_cpu_handle_t gcpu,
				uint16_t port_id,
				uint32_t port_size,
				uint32_t rep_cnt,
				io_write_handler_t write_handler)
{
	vmcs_obj_t vmcs = gcpu->vmcs;
	pf_info_t pfinfo;
	uint8_t buf[IO_BUFFER_SIZE];
	uint32_t value = 0;
	uint32_t iter_cnt;
	uint32_t cpy_size;
	uint32_t i;
	uint64_t df;
	boolean_t ret;

	/* The linear address gva is the base address of
	* relevant segment plus (E)DI (for INS) or (E)SI
	* (for OUTS). It is valid only when the relevant
	* segment is usable. Otherwise, it is undefined. */
	uint64_t gva = vmcs_read(vmcs, VMCS_GUEST_LINEAR_ADDR);
	df = vmcs_read(gcpu->vmcs, VMCS_GUEST_RFLAGS) & RFLAGS_DF;

	while (rep_cnt > 0) {
		iter_cnt = MIN(rep_cnt, IO_BUFFER_SIZE / port_size);
		cpy_size = iter_cnt * port_size;
		if (df) {
			ret = gcpu_copy_from_gva(gcpu, gva - cpy_size + port_size,
				(uint64_t)&buf[IO_BUFFER_SIZE - cpy_size], cpy_size, &pfinfo);
		} else {
			ret = gcpu_copy_from_gva(gcpu, gva, (uint64_t)buf, cpy_size, &pfinfo);
		}

		if (!ret) {
			VMM_ASSERT_EX(pfinfo.is_pf, "Guest(%d) fail to copy io from "
					"gva(0x%llX) with df(%d)\n", gcpu->guest->id, gva, df?1:0);
			gcpu_set_cr2(gcpu, pfinfo.cr2);
			gcpu_inject_pf(gcpu, pfinfo.ec);
			return;
		}

		for (i = 0; i < iter_cnt; i++) {
			if (df) {
				memcpy((void *)&value,
				(void *)&buf[IO_BUFFER_SIZE - (i + 1) * port_size], port_size);
			} else {
				memcpy((void *)&value, (void *)&buf[i * port_size], port_size);
			}
			write_handler(gcpu, port_id, port_size, value);
		}

		if (df) {
			gva -= cpy_size;
		} else {
			gva += cpy_size;
		}
		rep_cnt -= iter_cnt;
	}
}

void io_monitor_handler(guest_cpu_handle_t gcpu)
{
	guest_handle_t guest;
	vmcs_obj_t vmcs ;
	vmx_exit_qualification_t qualification;
	uint16_t port_id;
	uint32_t port_size;
	uint32_t rep_cnt;
	io_monitor_guest_t *p_io_mon_guest;
	io_monitor_t * p_io_mon;
	io_read_handler_t read_handler;
 	io_write_handler_t write_handler;

	guest = gcpu->guest;
	vmcs = gcpu->vmcs;
	qualification.uint64 = vmcs_read(vmcs, VMCS_EXIT_QUAL);

	port_id = (0 == qualification.io_instruction.op_encoding) ?
		(uint16_t)gcpu_get_gp_reg(gcpu, REG_RDX) :
		(uint16_t)qualification.io_instruction.port_number;
	VMM_ASSERT_EX((qualification.io_instruction.size < 4) &&
		(qualification.io_instruction.size != 2),
		"invalid size (%llu) in io vmexit qualification",
		qualification.io_instruction.size);
	port_size = (uint32_t)qualification.io_instruction.size + 1;
	rep_cnt = (qualification.io_instruction.rep ?
		(uint32_t)gcpu_get_gp_reg(gcpu, REG_RCX) : 1);

	p_io_mon_guest = io_monitor_guest_lookup(guest->id);
	D(VMM_ASSERT(p_io_mon_guest));
	p_io_mon = io_list_lookup(p_io_mon_guest->io_list, port_id);
	D(VMM_ASSERT(p_io_mon));

	if (!io_monitor_check_fault(gcpu))
		return;

	/* if guest io handler is null, use default handler*/
	read_handler = (p_io_mon->io_read_handler ?
		p_io_mon->io_read_handler : io_transparent_read_handler);
	write_handler = (p_io_mon->io_write_handler ?
		p_io_mon->io_write_handler : io_transparent_write_handler);

	if (FALSE == qualification.io_instruction.string) {
		/* ordinary IN/OUT instruction */
		if (qualification.io_instruction.direction == INS) {
			handle_in(gcpu, port_id, port_size, read_handler);
		} else {
			handle_out(gcpu, port_id, port_size, write_handler);
		}
	} else {
		/* string IN/OUT instruction */
		if (qualification.io_instruction.direction == INS) {
			handle_ins(gcpu, port_id, port_size, rep_cnt, read_handler);
		} else {
			handle_outs(gcpu, port_id, port_size, rep_cnt, write_handler);
		}
	}

	gcpu_skip_instruction(gcpu);
}

static void io_monitor_gcpu_init(guest_cpu_handle_t gcpu, UNUSED void *pv)
{
	uint32_t exec_controls;
	vmcs_obj_t vmcs;
	guest_handle_t guest;
	io_monitor_guest_t *io_mon_guest;
	uint64_t hpa[2];

	D(VMM_ASSERT(gcpu));
	vmcs = gcpu->vmcs;
	guest = gcpu->guest;

	io_mon_guest = io_monitor_guest_lookup(guest->id);
	if (io_mon_guest == NULL) {
		D(print_warn("IO bitmap for guest %d is not allocated\n", guest->id));
		return;
	}

	D(VMM_ASSERT(io_mon_guest->io_bitmap));
	/* first load bitmap addresses, and if OK, enable bitmap based IO VMEXITs */
	VMM_ASSERT_EX(hmm_hva_to_hpa((uint64_t)&io_mon_guest->io_bitmap[0], &hpa[0], NULL),
		"IO bitmap page for guest %d is invalid\n", guest->id);
	VMM_ASSERT_EX(hmm_hva_to_hpa((uint64_t)&io_mon_guest->io_bitmap[PAGE_4K_SIZE], &hpa[1], NULL),
		"IO bitmap page for guest %d is invalid\n", guest->id);

	vmcs_write(vmcs, VMCS_IO_BITMAP_A, hpa[0]);
	print_trace("IO bitmap page A : VA=0x%llX  PA=0x%llX\n",
		&io_mon_guest->io_bitmap[0], hpa[0]);
	vmcs_write(vmcs, VMCS_IO_BITMAP_B, hpa[1]);
	print_trace("IO bitmap page B : VA=0x%llX  PA=0x%llX\n",
		&io_mon_guest->io_bitmap[PAGE_4K_SIZE], hpa[1]);

	exec_controls = (uint32_t)vmcs_read(vmcs, VMCS_PROC_CTRL1);
	exec_controls |=(PROC_IO_BITMAPS);
	vmcs_write(vmcs, VMCS_PROC_CTRL1, exec_controls);
}

void io_monitor_init(void)
{
	basic_info_t basic_info;

	VMM_ASSERT_EX((get_proctl1_cap(NULL) & PROC_IO_BITMAPS),
		"io bitmaps isn't supported\n");

	basic_info.uint64 = get_basic_cap();
	VMM_ASSERT_EX(basic_info.bits.instr_info_valid, "VMCS_EXIT_INSTR_INFO is unavailable\n");

	vmexit_install_handler(io_monitor_handler, REASON_30_IO_INSTR);
	event_register(EVENT_GCPU_MODULE_INIT, io_monitor_gcpu_init);
}
