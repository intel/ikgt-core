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

#include "file_codes.h"
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMEXIT_IO_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMEXIT_IO_C, __condition)
#include "mon_defs.h"
#include "mon_dbg.h"
#include "heap.h"
#include "lock.h"
#include "hw_utils.h"
#include "guest.h"
#include "guest_cpu.h"
#include "vmexit.h"
#include "vmcs_api.h"
#include "vmx_ctrl_msrs.h"
#include "host_memory_manager_api.h"
#include "gpm_api.h"
#include "vmexit_io.h"
#include "memory_allocator.h"
#include "address.h"
#include "guest_cpu_vmenter_event.h"

/*-----------------Local Types and Macros Definitions----------------*/

#define IO_VMEXIT_MAX_COUNT   64

typedef struct {
	/* in fact only 16 bits are meaningful */
	io_port_id_t		io_port;
	uint16_t		pad;
	rw_access_t		io_access;
	/* io_port_owner_t io_owner; TODO: resolve owner conflict issues. */
	io_access_handler_t	io_handler;
	void			*io_handler_context;
} io_vmexit_descriptor_t;

typedef struct {
	guest_id_t		guest_id;
	char			padding[6];
	uint8_t			*io_bitmap;
	io_vmexit_descriptor_t	io_descriptors[IO_VMEXIT_MAX_COUNT];
	list_element_t		list[1];
} guest_io_vmexit_control_t;

typedef struct {
	list_element_t guest_io_vmexit_controls[1];
} io_vmexit_global_state_t;

/*-----------------Local Variables----------------*/

static io_vmexit_global_state_t io_vmexit_global_state;

/*-----------------Forward Declarations for Local Functions----------------*/

static
vmexit_handling_status_t io_vmexit_handler(guest_cpu_handle_t gcpu);
static
io_vmexit_descriptor_t *io_port_lookup(guest_id_t guest_id,
				       io_port_id_t port_id);
static
io_vmexit_descriptor_t *io_free_port_lookup(guest_id_t guest_id);
static
void io_blocking_read_handler(guest_cpu_handle_t gcpu,
			      io_port_id_t port_id,
			      unsigned port_size,
			      void *p_value);
static
void io_blocking_write_handler(guest_cpu_handle_t gcpu,
			       io_port_id_t port_id,
			       unsigned port_size,
			       void *p_value);
static
boolean_t io_blocking_handler(guest_cpu_handle_t gcpu,
			      io_port_id_t port_id,
			      unsigned port_size,
			      rw_access_t access,
			      boolean_t string_intr, /* ins/outs */
			      boolean_t rep_prefix,
			      uint32_t rep_count,
			      void *p_value,
			      void *context UNUSED);
void io_transparent_read_handler(guest_cpu_handle_t gcpu,
				 io_port_id_t port_id,
				 unsigned port_size, /* 1, 2, 4 */
				 void *p_value);
void io_transparent_write_handler(guest_cpu_handle_t gcpu,
				  io_port_id_t port_id,
				  unsigned port_size, /* 1, 2, 4 */
				  void *p_value);
static
guest_io_vmexit_control_t *io_vmexit_find_guest_io_control(guest_id_t
							   guest_id);

/*----------------------------------------------------------------------------*
*  FUNCTION : io_vmexit_setup()
*  PURPOSE  : Allocate and initialize IO VMEXITs related data structures,
*           : common for all guests
*  ARGUMENTS: guest_id_t    num_of_guests
*  RETURNS  : void
*----------------------------------------------------------------------------*/
void io_vmexit_initialize(void)
{
	mon_memset(&io_vmexit_global_state, 0, sizeof(io_vmexit_global_state));

	list_init(io_vmexit_global_state.guest_io_vmexit_controls);
}

/*----------------------------------------------------------------------------*
*  FUNCTION : io_vmexit_guest_setup()
*  PURPOSE  : Allocate and initialize IO VMEXITs related data structures for
*           : specific guest
*  ARGUMENTS: guest_id_t    guest_id
*  RETURNS  : void
*----------------------------------------------------------------------------*/
void io_vmexit_guest_initialize(guest_id_t guest_id)
{
	guest_io_vmexit_control_t *io_ctrl;

	MON_LOG(mask_anonymous, level_trace,
		"io_vmexit_guest_initialize start\r\n");

	io_ctrl =
		(guest_io_vmexit_control_t *)mon_malloc(sizeof(
				guest_io_vmexit_control_t));
	MON_ASSERT(io_ctrl);

	io_ctrl->guest_id = guest_id;
	io_ctrl->io_bitmap = mon_memory_alloc(2 * PAGE_4KB_SIZE);

	MON_ASSERT(io_ctrl->io_bitmap);

	list_add(io_vmexit_global_state.guest_io_vmexit_controls,
		io_ctrl->list);

	MON_LOG(mask_anonymous,
		level_trace,
		"io_vmexit_guest_initialize end\r\n");

	/* TTTTT mon_memset(io_ctrl->io_bitmap, 0xFF, 2 * PAGE_4KB_SIZE); */

	vmexit_install_handler(guest_id, io_vmexit_handler,
		IA32_VMX_EXIT_BASIC_REASON_IO_INSTRUCTION);
}

/*----------------------------------------------------------------------------*
*  FUNCTION : io_vmexit_activate()
*  PURPOSE  : enables in HW IO VMEXITs for specific guest on given CPU
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : void
*----------------------------------------------------------------------------*/
void io_vmexit_activate(guest_cpu_handle_t gcpu)
{
	processor_based_vm_execution_controls_t exec_controls;
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	guest_handle_t guest = mon_gcpu_guest_handle(gcpu);
	guest_id_t guest_id = guest_get_id(guest);
	guest_io_vmexit_control_t *io_ctrl = NULL;
	hpa_t hpa[2];
	int i;
	vmexit_control_t vmexit_request;

	MON_LOG(mask_anonymous, level_trace, "io_vmexit_activate start\r\n");
	io_ctrl = io_vmexit_find_guest_io_control(guest_id);

	MON_ASSERT(io_ctrl);

	mon_memset(&exec_controls, 0, sizeof(exec_controls));
	mon_memset(&vmexit_request, 0, sizeof(vmexit_request));

	if (NULL == io_ctrl->io_bitmap) {
		MON_LOG(mask_anonymous, level_trace,
			"IO bitmap for guest %d is not allocated\n", guest_id);
		MON_DEADLOOP();
		return;
	}

	/* first load bitmap addresses, and if OK, enable bitmap based IO VMEXITs */
	for (i = 0; i < 2; ++i) {
		if (FALSE ==
		    mon_hmm_hva_to_hpa((hva_t)&io_ctrl->io_bitmap[PAGE_4KB_SIZE
								  * i],
			    &hpa[i])) {
			MON_LOG(mask_anonymous,
				level_trace,
				"IO bitmap page for guest %d is invalid\n",
				guest_id);
			MON_DEADLOOP();
			return;
		}
		mon_vmcs_write(vmcs,
			(vmcs_field_t)(i + VMCS_IO_BITMAP_ADDRESS_A),
			hpa[i]);
		MON_LOG(mask_anonymous, level_trace,
			"IO bitmap page %c : VA=%P  PA=%P\n", 'A' + i,
			&io_ctrl->io_bitmap[PAGE_4KB_SIZE * i], hpa[i]);
	}

	exec_controls.bits.activate_io_bitmaps = 1;

	vmexit_request.proc_ctrls.bit_request = UINT64_ALL_ONES;
	vmexit_request.proc_ctrls.bit_mask = exec_controls.uint32;
	gcpu_control_setup(gcpu, &vmexit_request);
}

/*----------------------------------------------------------------------------*
*  FUNCTION : io_port_lookup()
*  PURPOSE  : Look for descriptor for specified port
*  ARGUMENTS: guest_id_t    guest_id
*           : uint16_t      port_id
*  RETURNS  : Pointer to the descriptor, NULL if not found
*----------------------------------------------------------------------------*/
io_vmexit_descriptor_t *io_port_lookup(guest_id_t guest_id,
				       io_port_id_t port_id)
{
	guest_io_vmexit_control_t *io_ctrl = NULL;
	unsigned i;

	io_ctrl = io_vmexit_find_guest_io_control(guest_id);

	if (NULL == io_ctrl) {
		return NULL;
	}

	for (i = 0; i < NELEMENTS(io_ctrl->io_descriptors); ++i) {
		if (io_ctrl->io_descriptors[i].io_port == port_id
		    && io_ctrl->io_descriptors[i].io_handler != NULL) {
			return &io_ctrl->io_descriptors[i];
		}
	}
	return NULL;
}

/*----------------------------------------------------------------------------*
*  FUNCTION : io_free_port_lookup()
*  PURPOSE  : Look for unallocated descriptor
*  ARGUMENTS: guest_id_t    guest_id
*  RETURNS  : Pointer to the descriptor, NULL if not found
*----------------------------------------------------------------------------*/
io_vmexit_descriptor_t *io_free_port_lookup(guest_id_t guest_id)
{
	guest_io_vmexit_control_t *io_ctrl = NULL;
	unsigned i;

	io_ctrl = io_vmexit_find_guest_io_control(guest_id);

	if (NULL == io_ctrl) {
		return NULL;
	}

	for (i = 0; i < NELEMENTS(io_ctrl->io_descriptors); ++i) {
		if (NULL == io_ctrl->io_descriptors[i].io_handler) {
			return &io_ctrl->io_descriptors[i];
		}
	}
	return NULL;
}


void io_blocking_read_handler(guest_cpu_handle_t gcpu UNUSED,
			      io_port_id_t port_id UNUSED,
			      unsigned port_size, /* 1, 2, 4 */
			      void *p_value)
{
	switch (port_size) {
	case 1:
	case 2:
	case 4:
		mon_memset(p_value, 0xFF, port_size);
		break;
	default:
		MON_LOG(mask_anonymous,
			level_trace,
			"Invalid IO port size(%d)\n",
			port_size);
		MON_DEADLOOP();
		break;
	}
}

void io_blocking_write_handler(guest_cpu_handle_t gcpu UNUSED,
			       io_port_id_t port_id UNUSED,
			       unsigned port_size UNUSED,       /* 1, 2, 4 */
			       void *p_value UNUSED)
{
}

/*----------------------------------------------------------------------------*
*  FUNCTION : io_blocking_handler()
*  PURPOSE  : Used as default handler when no IO handler is registered,
*           : but port configured as caused VMEXIT.
*  ARGUMENTS: guest_cpu_handle_t gcpu,
*           : io_port_id_t       port_id,
*           : unsigned         port_size,
*           : rw_access_t        access,
*           : void             *p_value
*  RETURNS  : void
*----------------------------------------------------------------------------*/
boolean_t io_blocking_handler(guest_cpu_handle_t gcpu, io_port_id_t port_id,
			      unsigned port_size, rw_access_t access,
			      boolean_t string_intr, /* ins/outs */
			      boolean_t rep_prefix, uint32_t rep_count,
			      void *p_value, void *context UNUSED)
{
	switch (access) {
	case WRITE_ACCESS:
		io_blocking_write_handler(gcpu, port_id, port_size, p_value);
		break;
	case READ_ACCESS:
		io_blocking_read_handler(gcpu, port_id, port_size, p_value);
		break;
	default:
		MON_LOG(mask_anonymous,
			level_trace,
			"Invalid IO access(%d)\n",
			access);
		MON_DEADLOOP();
		break;
	}

	return TRUE;
}

void io_transparent_read_handler(guest_cpu_handle_t gcpu UNUSED,
				 io_port_id_t port_id,
				 unsigned port_size,                       /* 1, 2, 4 */
				 void *p_value)
{
	switch (port_size) {
	case 1:
		*(uint8_t *)p_value = hw_read_port_8(port_id);
		break;

	case 2:
		*(uint16_t *)p_value = hw_read_port_16(port_id);
		break;
	case 4:
		*(uint32_t *)p_value = hw_read_port_32(port_id);
		break;
	default:
		MON_LOG(mask_anonymous,
			level_trace,
			"Invalid IO port size(%d)\n",
			port_size);
		MON_DEADLOOP();
		break;
	}
}

void io_transparent_write_handler(guest_cpu_handle_t gcpu UNUSED,
				  io_port_id_t port_id,
				  unsigned port_size,                      /* 1, 2, 4 */
				  void *p_value)
{
	switch (port_size) {
	case 1:
		hw_write_port_8(port_id, *(uint8_t *)p_value);
		break;

	case 2:
		hw_write_port_16(port_id, *(uint16_t *)p_value);
		break;
	case 4:
		hw_write_port_32(port_id, *(uint32_t *)p_value);
		break;
	default:
		MON_LOG(mask_anonymous,
			level_trace,
			"Invalid IO port size(%d)\n",
			port_size);
		MON_DEADLOOP();
		break;
	}
}

void io_vmexit_transparent_handler(guest_cpu_handle_t gcpu, uint16_t port_id,
				   unsigned port_size,   /* 1, 2, 4 */
				   rw_access_t access, void *p_value,
				   void *context UNUSED)
{
	switch (access) {
	case WRITE_ACCESS:
		io_transparent_write_handler(gcpu, port_id, port_size, p_value);
		break;
	case READ_ACCESS:
		io_transparent_read_handler(gcpu, port_id, port_size, p_value);
		break;
	default:
		MON_LOG(mask_anonymous,
			level_trace,
			"Invalid IO access(%d)\n",
			access);
		MON_DEADLOOP();
		break;
	}
}


/*----------------------------------------------------------------------------*
*  FUNCTION : io_vmexit_handler_register()
*  PURPOSE  : Register/update IO handler for spec port/guest pair.
*  ARGUMENTS: guest_id_t            guest_id
*           : io_port_id_t          port_id
*           : io_access_handler_t   handler
*  RETURNS  : status
*----------------------------------------------------------------------------*/
mon_status_t mon_io_vmexit_handler_register(guest_id_t guest_id,
					    io_port_id_t port_id,
					    io_access_handler_t handler,
					    void *context)
{
	io_vmexit_descriptor_t *p_desc = io_port_lookup(guest_id, port_id);
	mon_status_t status;
	guest_io_vmexit_control_t *io_ctrl = NULL;

	io_ctrl = io_vmexit_find_guest_io_control(guest_id);

	MON_ASSERT(io_ctrl);
	MON_ASSERT(handler);

	if (NULL != p_desc) {
		MON_LOG(mask_anonymous,
			level_trace,
			"IO Handler for Guest(%d) port(%d) is already regitered. Update...\n",
			guest_id,
			port_id);
	} else {
		p_desc = io_free_port_lookup(guest_id);
	}

	if (NULL != p_desc) {
		BITARRAY_SET(io_ctrl->io_bitmap, port_id);
		p_desc->io_port = port_id;
		p_desc->io_handler = handler;
		p_desc->io_handler_context = context;
		status = MON_OK;
	} else {
		/* if reach the MAX number (IO_VMEXIT_MAX_COUNT) of ports, */
		/* return ERROR, but not deadloop. */
		status = MON_ERROR;
		MON_LOG(mask_anonymous, level_trace,
			"Not enough space to register IO handler\n");
	}

	return status;
}

/*----------------------------------------------------------------------------*
*  FUNCTION : io_vmexit_handler_unregister()
*  PURPOSE  : Unregister IO handler for spec port/guest pair.
*  ARGUMENTS: guest_id_t            guest_id
*           : io_port_id_t          port_id
*  RETURNS  : status
*----------------------------------------------------------------------------*/
mon_status_t mon_io_vmexit_handler_unregister(guest_id_t guest_id,
					      io_port_id_t port_id)
{
	io_vmexit_descriptor_t *p_desc = io_port_lookup(guest_id, port_id);
	mon_status_t status;
	guest_io_vmexit_control_t *io_ctrl = NULL;

	io_ctrl = io_vmexit_find_guest_io_control(guest_id);

	MON_ASSERT(io_ctrl);

	if (NULL != p_desc) {
		BITARRAY_CLR(io_ctrl->io_bitmap, port_id);
		p_desc->io_handler = NULL;
		p_desc->io_handler_context = NULL;
		status = MON_OK;
	} else {
		/* if not registered before, still return SUCCESS! */
		status = MON_OK;
		MON_LOG(mask_anonymous, level_trace,
			"IO Handler for Guest(%d) port(%d) is not regitered\n",
			guest_id, port_id);
	}

	return status;
}

/*
 * VM exits caused by execution of the INS and OUTS instructions
 * have priority over the following faults:
 * --1. A #GP fault due to the relevant segment (ES for INS; DS for
 * OUTS unless overridden by an instruction prefix) being unusable;
 * --2. A #GP fault due to an offset (ESI, EDI) beyond the limit of
 * the relevant segment, for 64bit, check non-canonical form;
 * --3. An #AC exception (unaligned memory referenced when CR0.AM=1,
 * EFLAGS.AC=1, and CPL=3).
 * Hence, if those fault/exception above happens,inject back to guest.
 */
static
boolean_t io_access_native_fault(guest_cpu_handle_t gcpu,
				 ia32_vmx_exit_qualification_t *qualification)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	ia32_vmx_vmcs_vmexit_info_instruction_info_t ios_instr_info;
	boolean_t status = FALSE;
	mon_segment_attributes_t seg_ar = { 0 };
	vmentry_controls_t vmentry_control;
	boolean_t is_64bit = FALSE;
	uint64_t cs_selector = 0;
	em64t_cr0_t guest_cr0;
	em64t_rflags_t guest_rflags;

	MON_ASSERT(qualification);
	MON_ASSERT(vmcs);

	/* only handle ins/outs string io instructions. */
	MON_ASSERT(qualification->io_instruction.string == 1);

	ios_instr_info.uint32 = (uint32_t)mon_vmcs_read(vmcs,
		VMCS_EXIT_INFO_INSTRUCTION_INFO);
	vmentry_control.uint32 = (uint32_t)mon_vmcs_read(vmcs,
		VMCS_ENTER_CONTROL_VECTOR);

	if (1 == vmentry_control.bits.ia32e_mode_guest) {
		is_64bit = TRUE;
	}

	/*
	 * 1) check the 1st/2nd condidtion.-- #GP
	 */
	if (qualification->io_instruction.direction) {
		uint64_t rdi = gcpu_get_native_gp_reg(gcpu, IA32_REG_RDI);

		/* for INS -- check ES segement usable? */
		seg_ar.attr32 = (uint32_t)mon_vmcs_read(vmcs, VMCS_GUEST_ES_AR);
		if (seg_ar.bits.null_bit == 1) {
			/* ES unusable, inject #GP */
			MON_LOG(mask_anonymous, level_trace,
				"INS - ES segment is un-usable, inject #GP\n");

			mon_gcpu_inject_gp0(gcpu);
			return TRUE;
		}

		if (is_64bit) {
			/* must 64bit address size.
			 * MON_ASSERT(ios_instr_info.ins_outs_instruction.addr_size == 2); */

			if (FALSE == addr_is_canonical(rdi)) {
				/* address is not canonical , inject #GP */
				MON_LOG(mask_anonymous,
					level_trace,
					"INS - address %P is not canonical, inject #GP\n",
					rdi);

				mon_gcpu_inject_gp0(gcpu);
				return TRUE;
			}
		} else {
			/*
			 * TODO: OFFSET/rdi check against segment limit.
			 * Assume this case doesn't happen for 32bit Win7 OS, do nothing.
			 * Need to develop case to test it */
		}
	} else {
		uint64_t rsi = gcpu_get_native_gp_reg(gcpu, IA32_REG_RSI);

		/* for OUTS -- segment can be overridden, so check instr info */
		switch (ios_instr_info.ins_outs_instruction.seg_reg) {
		case 0:
			/* ES */
			seg_ar.attr32 = (uint32_t)mon_vmcs_read(vmcs,
				VMCS_GUEST_ES_AR);
			break;

		case 1:
			/* CS */
			seg_ar.attr32 = (uint32_t)mon_vmcs_read(vmcs,
				VMCS_GUEST_CS_AR);
			break;

		case 2:
			/* SS */
			seg_ar.attr32 = (uint32_t)mon_vmcs_read(vmcs,
				VMCS_GUEST_SS_AR);
			break;

		case 3:
			/* DS */
			seg_ar.attr32 = (uint32_t)mon_vmcs_read(vmcs,
				VMCS_GUEST_DS_AR);
			break;

		case 4:
			/* FS */
			seg_ar.attr32 = (uint32_t)mon_vmcs_read(vmcs,
				VMCS_GUEST_FS_AR);
			break;

		case 5:
			/* GS */
			seg_ar.attr32 = (uint32_t)mon_vmcs_read(vmcs,
				VMCS_GUEST_GS_AR);
			break;

		default:
			/* impossible */
			MON_ASSERT(0);
			break;
		}

		if (seg_ar.bits.null_bit == 1) {
			/* xS segment unusable, inject #GP */

			MON_LOG(mask_anonymous,
				level_trace,
				"OUTS - the relevant segment is un-usable, inject #GP\n");

			mon_gcpu_inject_gp0(gcpu);
			return TRUE;
		}

		if (is_64bit) {
			/* must 64bit address size. */

			if (FALSE == addr_is_canonical(rsi)) {
				/* address is not canonical , inject #GP */
				MON_LOG(mask_anonymous,
					level_trace,
					"INS - address %P is not canonical, inject #GP\n",
					rsi);

				mon_gcpu_inject_gp0(gcpu);
				return TRUE;
			}
		} else {
			/*
			 * TODO: OFFSET/rsi check against segment limit.
			 * Assume this case doesn't happen for 32bit OS, do nothing.
			 * Need to develop case to test it */
		}
	}

	/*
	 * 2) check the 3rd condidtion.-- #AC
	 */
	cs_selector = mon_vmcs_read(vmcs, VMCS_GUEST_CS_SELECTOR);
	if (BITMAP_GET(cs_selector, CS_SELECTOR_CPL_BIT) == 3) {
		/* ring3 level. */
		guest_cr0.uint64 =
			gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR0);
		if (guest_cr0.bits.am) {
			/* CR0.AM = 1 */
			guest_rflags.uint64 = mon_vmcs_read(vmcs,
				VMCS_GUEST_RFLAGS);
			if (guest_rflags.bits.ac) {
				/* rflag.ac = 1 */

				/* TODO:check address (rdi/rsi) alignment based on
				 * ios_instr_info.ins_outs_instruction.addr_size.
				 * if not word/dword/qword aligned, then inject #AC to guest
				 *
				 * Assume this case won't happen unless the IO port is allowed
				 * to access in ring3 level by setting I/O Permission Bit Map
				 * in TSS data structure.(actually, this is a hacking behavior)
				 */

				/* so, catch this case with deadloop. */
				MON_DEADLOOP();
			}
		}
	}

	return status;
}

vmexit_handling_status_t io_vmexit_handler(guest_cpu_handle_t gcpu)
{
	guest_handle_t guest_handle = mon_gcpu_guest_handle(gcpu);
	guest_id_t guest_id = guest_get_id(guest_handle);
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	uint64_t qualification = mon_vmcs_read(vmcs,
		VMCS_EXIT_INFO_QUALIFICATION);
	ia32_vmx_exit_qualification_t *p_qualification =
		(ia32_vmx_exit_qualification_t *)&qualification;
	io_port_id_t port_id =
		(0 == p_qualification->io_instruction.op_encoding) ?
		(uint16_t)gcpu_get_native_gp_reg(gcpu,
			IA32_REG_RDX)
		: (uint16_t)p_qualification->io_instruction.
		port_number;
	io_vmexit_descriptor_t *p_desc = io_port_lookup(guest_id, port_id);
	unsigned port_size = (unsigned)p_qualification->io_instruction.size + 1;
	rw_access_t access =
		p_qualification->io_instruction.direction ? READ_ACCESS :
		WRITE_ACCESS;
	io_access_handler_t handler =
		((NULL == p_desc) ? io_blocking_handler : p_desc->io_handler);
	void *context = ((NULL == p_desc) ? NULL : p_desc->io_handler_context);
	boolean_t string_io =
		(p_qualification->io_instruction.string ? TRUE : FALSE);
	boolean_t rep_prefix =
		(p_qualification->io_instruction.rep ? TRUE : FALSE);
	uint32_t rep_count =
		(rep_prefix ? (uint32_t)gcpu_get_native_gp_reg(gcpu,
			 IA32_REG_RCX) : 0);

	uint64_t io_value = 0;
	uint64_t rax;

	ia32_vmx_vmcs_vmexit_info_instruction_info_t ios_instr_info;

	if (FALSE == string_io) {
		/* ordinary IN/OUT instruction data is stored in guest RAX
		 * register, pass it to Handler here.
		 * It is the handler's responsibility to set the data
		 * either by I/O emulation/virtualization or MTF*/
		rax = gcpu_get_native_gp_reg(gcpu, IA32_REG_RAX);
		io_value = (uint64_t)&rax;
	} else {
		/* for string INS/OUTS instruction */

		/* The linear address gva is the base address of
		 * relevant segment plus (E)DI (for INS) or (E)SI
		 * (for OUTS). It is valid only when the relevant
		 * segment is usable. Otherwise, it is undefined. */
		uint64_t gva = mon_vmcs_read(vmcs,
			VMCS_EXIT_INFO_GUEST_LINEAR_ADDRESS);
		hva_t dumy_hva = 0;

		/* if a native fault/exception happens, then let OS handle them.
		 * and don't report invalid io-access event to Handler in order
		 * to avoid unexpected behaviors. */
		if (io_access_native_fault(gcpu, p_qualification) == TRUE) {
			return VMEXIT_HANDLED;
		}

		if (FALSE == gcpu_gva_to_hva(gcpu, gva, &dumy_hva)) {
			MON_LOG(mask_anonymous,
				level_trace,
				"Guest(%d) Virtual address %P Is Not Mapped\n",
				guest_id,
				gva);

			/* catch this failure to avoid further errors:
			 * for INS/OUTS instruction, if gva is invalid, which one will
			 * happen first?
			 * 1) native OS #PF; or 2) An IO VM exit
			 * if the testcase can reach here, then fix it. */
			MON_DEADLOOP();
		}

		ios_instr_info.uint32 = (uint32_t)mon_vmcs_read(vmcs,
			VMCS_EXIT_INFO_INSTRUCTION_INFO);

		switch (ios_instr_info.ins_outs_instruction.addr_size) {
		case 0:
			/* 16-bit */
			gva &= (uint64_t)0x0FFFF;
			break;

		case 1:
			/* 32-bit */
			gva &= (uint64_t)0x0FFFFFFFF;
			break;

		case 2:
			/* 64-bit */
			break;

		default:
			/* not h/w supported */
			MON_DEADLOOP();
		}

		/* GVA address */
		io_value = (gva_t)gva;
	}

	/* call to the handler */
	if (TRUE == handler(gcpu,
		    port_id,
		    port_size,
		    access,
		    string_io,
		    rep_prefix, rep_count, (void *)io_value, context)) {
		if ((FALSE == string_io) &&
		    (READ_ACCESS == access)) {
			gcpu_set_native_gp_reg(gcpu,
				IA32_REG_RAX,
				*(uint64_t *)io_value);
		}

		gcpu_skip_guest_instruction(gcpu);
	}

	return VMEXIT_HANDLED;
}

/*----------------------------------------------------------------------------*
*  FUNCTION : io_vmexit_block_port()
*  PURPOSE  : Enable VMEXIT on port without installing handler.
*           : Blocking_handler will be used for such cases.
*  ARGUMENTS: guest_id_t            guest_id
*           : io_port_id_t          port_from
*           : io_port_id_t          port_to
*  RETURNS  : void
*----------------------------------------------------------------------------*/
void io_vmexit_block_port(guest_id_t guest_id,
			  io_port_id_t port_from, io_port_id_t port_to)
{
	unsigned i;
	guest_io_vmexit_control_t *io_ctrl = NULL;

	io_ctrl = io_vmexit_find_guest_io_control(guest_id);

	MON_ASSERT(io_ctrl);

	/* unregister handler in case it was installed before */
	for (i = port_from; i <= port_to; ++i) {
		mon_io_vmexit_handler_unregister(guest_id, (io_port_id_t)i);
		BITARRAY_SET(io_ctrl->io_bitmap, i);
	}
}

static
guest_io_vmexit_control_t *io_vmexit_find_guest_io_control(guest_id_t guest_id)
{
	list_element_t *iter = NULL;
	guest_io_vmexit_control_t *io_ctrl = NULL;

	LIST_FOR_EACH(io_vmexit_global_state.guest_io_vmexit_controls, iter) {
		io_ctrl = LIST_ENTRY(iter, guest_io_vmexit_control_t, list);
		if (io_ctrl->guest_id == guest_id) {
			return io_ctrl;
		}
	}

	return NULL;
}
