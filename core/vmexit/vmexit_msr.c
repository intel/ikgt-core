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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMEXIT_MSR_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMEXIT_MSR_C, __condition)
#include "mon_defs.h"
#include "heap.h"
#include "memory_allocator.h"
#include "hw_utils.h"
#include "isr.h"
#include "guest.h"
#include "guest_cpu.h"
#include "guest_cpu_vmenter_event.h"
#include "vmx_ctrl_msrs.h"
#include "vmcs_api.h"
#include "vmexit.h"
#include "vmexit_msr.h"
#include "mon_dbg.h"
#include "mtrrs_abstraction.h"
#include "host_memory_manager_api.h"
#include "mon_events_data.h"
#include "pat_manager.h"
#include "local_apic.h"
#include "unrestricted_guest.h"
#include "mon_callback.h"
#include "memory_dump.h"

#define MSR_LOW_RANGE_IN_BITS   ((MSR_LOW_LAST - MSR_LOW_FIRST + 1) / 8)
#define MSR_HIGH_RANGE_IN_BITS  ((MSR_HIGH_LAST - MSR_HIGH_FIRST + 1) / 8)

#define MSR_READ_LOW_OFFSET     0
#define MSR_READ_HIGH_OFFSET    (MSR_READ_LOW_OFFSET + MSR_LOW_RANGE_IN_BITS)
#define MSR_WRITE_LOW_OFFSET    (MSR_READ_HIGH_OFFSET + MSR_LOW_RANGE_IN_BITS)
#define MSR_WRITE_HIGH_OFFSET   (MSR_WRITE_LOW_OFFSET + MSR_HIGH_RANGE_IN_BITS)

/*
 *    *** Hyper-V MSRs access workaround  ***
 * When we run our pnp launch driver with Alpha4_882 IBAgent,
 * we saw msr read at 0x40000081, since it isn't a real hardware MSR, we got
 * "RDMSR[0x40000081] failed. FaultVector=0x0000000D ErrCode=0x00000000" message
 * in serial port, then got BSOD. After injecting GP to guest in this MSR read,
 * Our PnP driver can work with IBAgent. The address range of Hyper-V
 * MSRs is from 0x40000000 to 0x400000F0). We need to investigate this
 * workaround and check whether it is necessary to extend this fix to any MSR
 * read/write outside 0x00000000 to 0x00001FFF and  0xC0000000 to 0xC0001FFF
 */

#define HYPER_V_MSR_MIN 0x40000000
#define HYPER_V_MSR_MAX 0x400000F0

#define LOW_BITS_32_MASK    ((uint64_t)UINT32_ALL_ONES)

typedef struct {
	msr_id_t		msr_id;
	uint8_t			pad[4];
	msr_access_handler_t	msr_read_handler;
	msr_access_handler_t	msr_write_handler;
	void			*msr_context;
	list_element_t		msr_list;
} msr_vmexit_descriptor_t;

/*---------------------------------Local Data---------------------------------*/
static struct {
	uint32_t	msr_id;
	vmcs_field_t	vmcs_field_id;
} vmcs_resident_guest_msrs[] = {
	{ IA32_MSR_SYSENTER_CS,
	  VMCS_GUEST_SYSENTER_CS		    },
	{ IA32_MSR_SYSENTER_ESP,
	  VMCS_GUEST_SYSENTER_ESP		    },
	{ IA32_MSR_SYSENTER_EIP,
	  VMCS_GUEST_SYSENTER_EIP		    },
	{ IA32_MSR_DEBUGCTL,
	  VMCS_GUEST_DEBUG_CONTROL		    },
	{ IA32_MSR_PERF_GLOBAL_CTRL,
	  VMCS_GUEST_IA32_PERF_GLOBAL_CTRL	    },
	{ IA32_MSR_FS_BASE,
	  VMCS_GUEST_FS_BASE			    },
	{ IA32_MSR_GS_BASE,	    VMCS_GUEST_GS_BASE			       }
};

/*------------------------------Forward Declarations------------------------*/

static msr_vmexit_descriptor_t *msr_descriptor_lookup(list_element_t *msr_list,
						      msr_id_t msr_id);
/* static */ mon_status_t msr_vmexit_bits_config(uint8_t *p_bitmap,
						 msr_id_t msr_id,
						 rw_access_t access,
						 boolean_t set);
static boolean_t msr_common_vmexit_handler(guest_cpu_handle_t gcpu,
					   rw_access_t access,
					   uint64_t *msr_value);
static boolean_t msr_unsupported_access_handler(guest_cpu_handle_t gcpu,
						msr_id_t msr_id,
						uint64_t *value,
						void *context);
static vmexit_handling_status_t vmexit_msr_read(guest_cpu_handle_t gcpu);
static vmexit_handling_status_t vmexit_msr_write(guest_cpu_handle_t gcpu);
static boolean_t msr_efer_write_handler(guest_cpu_handle_t gcpu,
					msr_id_t msr_id,
					uint64_t *msr_value,
					void *context);
static boolean_t msr_efer_read_handler(guest_cpu_handle_t gcpu,
				       msr_id_t msr_id,
				       uint64_t *msr_value,
				       void *context);
static boolean_t msr_pat_read_handler(guest_cpu_handle_t gcpu,
				      msr_id_t msr_id,
				      uint64_t *msr_value,
				      void *context);
static boolean_t msr_pat_write_handler(guest_cpu_handle_t gcpu,
				       msr_id_t msr_id,
				       uint64_t *msr_value,
				       void *context);
static boolean_t msr_lapic_base_write_handler(guest_cpu_handle_t gcpu,
					      msr_id_t msr_id,
					      uint64_t *msr_value,
					      void *context);
static boolean_t msr_feature_control_read_handler(guest_cpu_handle_t gcpu,
						  msr_id_t msr_id,
						  uint64_t *msr_value,
						  void *context);
static boolean_t msr_feature_control_write_handler(guest_cpu_handle_t gcpu,
						   msr_id_t msr_id,
						   uint64_t *msr_value,
						   void *context);
static boolean_t msr_mtrr_write_handler(guest_cpu_handle_t gcpu,
					msr_id_t msr_id,
					uint64_t *msr_value,
					void *context);
static boolean_t msr_vmcs_resident_default_handler(guest_cpu_handle_t gcpu,
						   msr_id_t msr_id,
						   rw_access_t access,
						   uint64_t *msr_value);
static boolean_t msr_misc_enable_write_handler(guest_cpu_handle_t gcpu,
					       msr_id_t msr_id,
					       uint64_t *msr_value,
					       void *context);

/*--------------------------------Code Starts Here--------------------------*/

/*--------------------------------------------------------------------------*
*  FUNCTION : msr_vmexit_on_all()
*  PURPOSE  : Turns VMEXIT on all ON/OFF
*  ARGUMENTS: guest_cpu_handle_t gcpu
*           : boolean_t enable
*  RETURNS  : none, must succeed.
*--------------------------------------------------------------------------*/
void msr_vmexit_on_all(guest_cpu_handle_t gcpu, boolean_t enable)
{
	processor_based_vm_execution_controls_t exec_controls_mask;
	vmexit_control_t vmexit_request;

	MON_ASSERT(gcpu);

	MON_LOG(mask_mon, level_trace, "[msr] VMEXIT on %s\n",
		enable ? "all" : "bitmap");

	exec_controls_mask.uint32 = 0;
	exec_controls_mask.bits.use_msr_bitmaps = 1;

	mon_memset(&vmexit_request, 0, sizeof(vmexit_request));
	vmexit_request.proc_ctrls.bit_request = enable ? 0 : UINT64_ALL_ONES;
	vmexit_request.proc_ctrls.bit_mask = exec_controls_mask.uint32;

	gcpu_control_setup(gcpu, &vmexit_request);
}

mon_status_t msr_vmexit_bits_config(uint8_t *p_bitmap,
				    msr_id_t msr_id,
				    rw_access_t access, /* read or write */
				    boolean_t set)
{
	uint8_t *p_bitarray;
	msr_id_t bitno;
	rw_access_t access_index;

	for (access_index = WRITE_ACCESS; access_index <= READ_ACCESS;
	     ++access_index) {
		if (access_index & access) {
			/* is access of iterest ? */
			if (msr_id <= MSR_LOW_LAST) {
				bitno = msr_id;
				p_bitarray = READ_ACCESS == access_index ?
					     &p_bitmap[MSR_READ_LOW_OFFSET] :
					     &p_bitmap[MSR_WRITE_LOW_OFFSET];
			} else if (MSR_HIGH_FIRST <= msr_id && msr_id <=
				   MSR_HIGH_LAST) {
				bitno = msr_id - MSR_HIGH_FIRST;
				p_bitarray = READ_ACCESS == access_index ?
					     &p_bitmap[MSR_READ_HIGH_OFFSET] :
					     &p_bitmap[MSR_WRITE_HIGH_OFFSET];
			} else {
				/* wrong MSR ID */
				MON_ASSERT(0);
				return MON_ERROR;
			}

			if (set) {
				BITARRAY_SET(p_bitarray, bitno);
			} else {
				BITARRAY_CLR(p_bitarray, bitno);
			}
		}
	}
	return MON_OK;
}


msr_vmexit_descriptor_t *msr_descriptor_lookup(list_element_t *msr_list,
					       msr_id_t msr_id)
{
	msr_vmexit_descriptor_t *p_msr_desc;
	list_element_t *list_iterator;

	LIST_FOR_EACH(msr_list, list_iterator) {
		p_msr_desc = LIST_ENTRY(list_iterator,
			msr_vmexit_descriptor_t,
			msr_list);
		if (p_msr_desc->msr_id == msr_id) {
			return p_msr_desc; /* found */
		}
	}
	return NULL;
}

static
void msr_vmexit_register_mtrr_accesses_handler(guest_handle_t guest)
{
	uint32_t i, msr_addr;

	mon_msr_vmexit_handler_register(guest, IA32_MTRRCAP_ADDR,
		msr_mtrr_write_handler, WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MTRR_DEF_TYPE_ADDR,
		msr_mtrr_write_handler, WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MTRR_FIX64K_00000_ADDR,
		msr_mtrr_write_handler, WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MTRR_FIX16K_80000_ADDR,
		msr_mtrr_write_handler, WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MTRR_FIX16K_A0000_ADDR,
		msr_mtrr_write_handler, WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MTRR_FIX4K_C0000_ADDR,
		msr_mtrr_write_handler, WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MTRR_FIX4K_C8000_ADDR,
		msr_mtrr_write_handler, WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MTRR_FIX4K_D0000_ADDR,
		msr_mtrr_write_handler, WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MTRR_FIX4K_D8000_ADDR,
		msr_mtrr_write_handler, WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MTRR_FIX4K_E0000_ADDR,
		msr_mtrr_write_handler, WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MTRR_FIX4K_E8000_ADDR,
		msr_mtrr_write_handler, WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MTRR_FIX4K_F0000_ADDR,
		msr_mtrr_write_handler, WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MTRR_FIX4K_F8000_ADDR,
		msr_mtrr_write_handler, WRITE_ACCESS, NULL);

	/* all other MTRR registers are sequential */
	for (msr_addr = IA32_MTRR_PHYSBASE0_ADDR, i = 0;
	     i < mtrrs_abstraction_get_num_of_variable_range_regs();
	     msr_addr += 2, i++) {
		if (msr_addr > IA32_MTRR_MAX_PHYSMASK_ADDR) {
			MON_LOG(mask_mon, level_error,
				"Error: No. of Variable MTRRs is incorrect\n");
			MON_DEADLOOP();
		}

		/* Register all MTRR PHYSBASE */
		mon_msr_vmexit_handler_register(guest, msr_addr,
			msr_mtrr_write_handler, WRITE_ACCESS, NULL);

		/* Register all MTRR PHYSMASK */
		mon_msr_vmexit_handler_register(guest, msr_addr + 1,
			msr_mtrr_write_handler, WRITE_ACCESS, NULL);
	}
}

/*--------------------------------------------------------------------------*
*  FUNCTION : msr_vmexit_guest_setup()
*  PURPOSE  : Allocates structures for MSR virtualization
*           : Must be called prior any other function from the package on
*             this gcpu,
*           : but after gcpu VMCS was loaded
*  ARGUMENTS: guest_handle_t guest
*  RETURNS  : none, must succeed.
*--------------------------------------------------------------------------*/
void msr_vmexit_guest_setup(guest_handle_t guest)
{
	msr_vmexit_control_t *p_msr_ctrl;
	msr_id_t msr_id;

	MON_ASSERT(guest);
	MON_LOG(mask_mon, level_trace, "[msr] Setup for Guest\n");

	p_msr_ctrl = guest_get_msr_control(guest);

	/* allocate zero-filled 4K-page to store MSR VMEXIT bitmap */
	p_msr_ctrl->msr_bitmap = mon_memory_alloc(PAGE_4KB_SIZE);
	MON_ASSERT(p_msr_ctrl->msr_bitmap);

	vmexit_install_handler(guest_get_id(guest), vmexit_msr_read,
		IA32_VMX_EXIT_BASIC_REASON_MSR_READ);
	vmexit_install_handler(guest_get_id(guest), vmexit_msr_write,
		IA32_VMX_EXIT_BASIC_REASON_MSR_WRITE);

	for (msr_id = IA32_MSR_VMX_FIRST; msr_id <= IA32_MSR_VMX_LAST; ++msr_id)
		msr_guest_access_inhibit(guest, msr_id);

	if (!mon_is_unrestricted_guest_supported()) {
		mon_msr_vmexit_handler_register(guest, IA32_MSR_EFER,
			msr_efer_write_handler, WRITE_ACCESS, NULL);

		mon_msr_vmexit_handler_register(guest, IA32_MSR_EFER,
			msr_efer_read_handler, READ_ACCESS, NULL);
	}

	mon_msr_vmexit_handler_register(guest, IA32_MSR_APIC_BASE,
		msr_lapic_base_write_handler,
		WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MSR_FEATURE_CONTROL,
		msr_feature_control_read_handler,
		READ_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MSR_FEATURE_CONTROL,
		msr_feature_control_write_handler,
		WRITE_ACCESS, NULL);

	mon_msr_vmexit_handler_register(guest, IA32_MSR_MISC_ENABLE,
		msr_misc_enable_write_handler,
		WRITE_ACCESS, NULL);

	msr_vmexit_register_mtrr_accesses_handler(guest);
}

/*--------------------------------------------------------------------------*
*  FUNCTION : msr_vmexit_activate()
*  PURPOSE  : Register MSR related structures with HW (VMCS)
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : none, must succeed.
*--------------------------------------------------------------------------*/
void msr_vmexit_activate(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	guest_handle_t guest;
	msr_vmexit_control_t *p_msr_ctrl;
	uint64_t msr_bitmap;

	MON_ASSERT(gcpu);

	MON_LOG(mask_mon, level_trace, "[msr] Activated on GCPU\n");

	guest = mon_gcpu_guest_handle(gcpu);
	MON_ASSERT(guest);
	p_msr_ctrl = guest_get_msr_control(guest);
	msr_bitmap = (uint64_t)p_msr_ctrl->msr_bitmap;

	msr_vmexit_on_all(gcpu, FALSE);

	if (NULL != p_msr_ctrl->msr_bitmap) {
		mon_hmm_hva_to_hpa(msr_bitmap, &msr_bitmap);
		mon_vmcs_write(vmcs, VMCS_MSR_BITMAP_ADDRESS, msr_bitmap);
	}
}

/*--------------------------------------------------------------------------*
*  FUNCTION : msr_vmexit_handler_register()
*  PURPOSE  : Register specific MSR handler with VMEXIT
*  ARGUMENTS: guest_handle_t        guest
*           : msr_id_t              msr_id
*           : msr_access_handler_t  msr_handler,
*           : rw_access_t           access
*  RETURNS  : MON_OK if succeeded
*--------------------------------------------------------------------------*/
mon_status_t mon_msr_vmexit_handler_register(guest_handle_t guest,
					     msr_id_t msr_id,
					     msr_access_handler_t msr_handler,
					     rw_access_t access,
					     void *context)
{
	msr_vmexit_descriptor_t *p_desc;
	mon_status_t status = MON_OK;
	msr_vmexit_control_t *p_msr_ctrl = guest_get_msr_control(guest);

	/* check first if it already registered */
	p_desc = msr_descriptor_lookup(p_msr_ctrl->msr_list, msr_id);

	if (NULL == p_desc) {
		/* allocate new descriptor and chain it to the list */
		p_desc = mon_malloc(sizeof(*p_desc));
		if (NULL != p_desc) {
			mon_memset(p_desc, 0, sizeof(*p_desc));
			list_add(p_msr_ctrl->msr_list, &p_desc->msr_list);
		}
	} else {
		MON_LOG(mask_mon,
			level_trace,
			"MSR(%p) handler already registered. Update...\n",
			msr_id);
	}

	if (NULL != p_desc) {
		status =
			msr_vmexit_bits_config(p_msr_ctrl->msr_bitmap,
				msr_id,
				access,
				TRUE);
		if (MON_OK == status) {
			p_desc->msr_id = msr_id;
			if (access & WRITE_ACCESS) {
				p_desc->msr_write_handler = msr_handler;
			}
			if (access & READ_ACCESS) {
				p_desc->msr_read_handler = msr_handler;
			}
			p_desc->msr_context = context;
			/* MON_LOG(mask_mon, level_trace,"%s: [msr] Handler(%P)
			 * Registered\n", __FUNCTION__, msr_id); */
		} else {
			MON_LOG(mask_mon,
				level_trace,
				"MSR(%p) handler registration failed to bad ID\n",
				msr_id);
		}
	} else {
		status = MON_ERROR;
		MON_LOG(mask_mon,
			level_trace,
			"MSR(%p) handler registration failed due to lack of space\n",
			msr_id);
	}
	return status;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : msr_vmexit_handler_unregister()
*  PURPOSE  : Unregister specific MSR VMEXIT handler
*  ARGUMENTS: guest_handle_t  guest
*           : msr_id_t        msr_id
*  RETURNS  : MON_OK if succeeded, MON_ERROR if no descriptor for MSR
*--------------------------------------------------------------------------*/
mon_status_t msr_vmexit_handler_unregister(guest_handle_t guest,
					   msr_id_t msr_id,
					   rw_access_t access)
{
	msr_vmexit_descriptor_t *p_desc;
	mon_status_t status = MON_OK;
	msr_vmexit_control_t *p_msr_ctrl = guest_get_msr_control(guest);

	p_desc = msr_descriptor_lookup(p_msr_ctrl->msr_list, msr_id);

	if (NULL == p_desc) {
		status = MON_ERROR;
		MON_LOG(mask_mon,
			level_trace,
			"MSR(%p) handler is not registered\n",
			msr_id);
	} else {
		msr_vmexit_bits_config(p_msr_ctrl->msr_bitmap,
			msr_id,
			access,
			FALSE);

		if (access & WRITE_ACCESS) {
			p_desc->msr_write_handler = NULL;
		}
		if (access & READ_ACCESS) {
			p_desc->msr_read_handler = NULL;
		}

		if (NULL == p_desc->msr_write_handler &&
		    NULL == p_desc->msr_read_handler) {
			list_remove(&p_desc->msr_list);
			mon_mfree(p_desc);
		}
	}
	return status;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_msr_read()
*  PURPOSE  : Read handler which calls upon VMEXITs resulting from MSR read
*             access
*           : Read MSR value from HW and if OK, stores the result in EDX:EAX
*  ARGUMENTS: guest_cpu_handle_t gcp
*  RETURNS  :
*--------------------------------------------------------------------------*/
vmexit_handling_status_t vmexit_msr_read(guest_cpu_handle_t gcpu)
{
	uint64_t msr_value = 0;
	msr_id_t msr_id = (msr_id_t)gcpu_get_native_gp_reg(gcpu, IA32_REG_RCX);

	/* hypervisor synthenic MSR is not hardware MSR, inject GP to guest */
	if ((msr_id >= HYPER_V_MSR_MIN) && (msr_id <= HYPER_V_MSR_MAX)) {
		mon_gcpu_inject_gp0(gcpu);
		return VMEXIT_HANDLED;
	}

	if (TRUE == msr_common_vmexit_handler(gcpu, READ_ACCESS, &msr_value)) {
		/* write back to the guest. store MSR value in EDX:EAX */
		gcpu_set_native_gp_reg(gcpu, IA32_REG_RDX, msr_value >> 32);
		gcpu_set_native_gp_reg(gcpu, IA32_REG_RAX,
			msr_value & LOW_BITS_32_MASK);
	}
	return VMEXIT_HANDLED;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : vmexit_msr_write()
*  PURPOSE  : Write handler which calls upon VMEXITs resulting from MSR write
*             access
*           : Read MSR value from guest EDX:EAX and call registered write
*             handler
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  :
*--------------------------------------------------------------------------*/
vmexit_handling_status_t vmexit_msr_write(guest_cpu_handle_t gcpu)
{
	uint64_t msr_value;
	msr_id_t msr_id = (msr_id_t)gcpu_get_native_gp_reg(gcpu, IA32_REG_RCX);

	/* hypervisor synthenic MSR is not hardware MSR, inject GP to guest */
	if ((msr_id >= HYPER_V_MSR_MIN) && (msr_id <= HYPER_V_MSR_MAX)) {
		mon_gcpu_inject_gp0(gcpu);
		return VMEXIT_HANDLED;
	}

	msr_value = (gcpu_get_native_gp_reg(gcpu, IA32_REG_RDX) << 32);
	msr_value |=
		gcpu_get_native_gp_reg(gcpu, IA32_REG_RAX) & LOW_BITS_32_MASK;

	msr_common_vmexit_handler(gcpu, WRITE_ACCESS, &msr_value);
	return VMEXIT_HANDLED;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : msr_common_vmexit_handler()
*  PURPOSE  : If MSR handler is registered, call it, otherwise executes default
*           : MSR handler. If MSR R/W instruction was executed successfully
*           : from the Guest point of view, Guest IP is moved forward on
*           :  instruction
*           : length, otherwise exception is injected into Guest CPU.
*  ARGUMENTS: guest_cpu_handle_t    gcpu
*           : rw_access_t           access
*  RETURNS  : TRUE if instruction was executed, FALSE otherwise (fault occured)
*--------------------------------------------------------------------------*/
boolean_t msr_common_vmexit_handler(guest_cpu_handle_t gcpu, rw_access_t access,
				    uint64_t *msr_value)
{
	msr_id_t msr_id = (msr_id_t)gcpu_get_native_gp_reg(gcpu, IA32_REG_RCX);
	guest_handle_t guest = NULL;
	msr_vmexit_control_t *p_msr_ctrl = NULL;
	msr_vmexit_descriptor_t *msr_descriptor = NULL;
	boolean_t instruction_was_executed = FALSE;
	msr_access_handler_t msr_handler = NULL;

	guest = mon_gcpu_guest_handle(gcpu);
	MON_ASSERT(guest);
	p_msr_ctrl = guest_get_msr_control(guest);
	MON_ASSERT(p_msr_ctrl);

	msr_descriptor = msr_descriptor_lookup(p_msr_ctrl->msr_list, msr_id);

	if (NULL != msr_descriptor) {
		/* MON_LOG(mask_mon, level_trace,"%s: msr_descriptor is NOT NULL.\n",
		 * __FUNCTION__); */
		if (access & WRITE_ACCESS) {
			msr_handler = msr_descriptor->msr_write_handler;
		} else if (access & READ_ACCESS) {
			msr_handler = msr_descriptor->msr_read_handler;
		}
	}

	if (NULL == msr_handler) {
		/* MON_LOG(mask_mon, level_trace,"%s: msr_handler is NULL.\n",
		 * __FUNCTION__); */
		instruction_was_executed =
			msr_vmcs_resident_default_handler(gcpu,
				msr_id,
				access,
				msr_value)
			|| msr_trial_access(gcpu, msr_id, access, msr_value);
	} else {
		/* MON_LOG(mask_mon, level_trace,"%s: msr_handler is NOT NULL.\n",
		 * __FUNCTION__); */
		instruction_was_executed =
			msr_handler(gcpu,
				msr_id,
				msr_value,
				msr_descriptor->msr_context);
	}

	if (TRUE == instruction_was_executed) {
		gcpu_skip_guest_instruction(gcpu);
	}
	return instruction_was_executed;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : msr_trial_access()
*  PURPOSE  : Try to execute real MSR read/write
*           : If exception was generated, inject it into guest
*  ARGUMENTS: guest_cpu_handle_t    gcpu
*           : msr_id_t              msr_id
*           : rw_access_t           access
*  RETURNS  : TRUE if instruction was executed, FALSE otherwise (fault occured)
*--------------------------------------------------------------------------*/
boolean_t msr_trial_access(guest_cpu_handle_t gcpu, msr_id_t msr_id,
			   rw_access_t access, uint64_t *msr_value)
{
	boolean_t msr_implemented;
	/* just to shut up the warning */
	vector_id_t fault_vector = 0;
	/* just to shut up the warning */
	uint32_t error_code = 0;
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

	switch (access) {
	case READ_ACCESS:
		msr_implemented =
			hw_rdmsr_safe(msr_id,
				msr_value,
				&fault_vector,
				&error_code);
		break;
	case WRITE_ACCESS:
		msr_implemented =
			hw_wrmsr_safe(msr_id,
				*msr_value,
				&fault_vector,
				&error_code);
		break;
	default:
		/* should not be here */
		MON_ASSERT(0);
		return FALSE;
	}

	if (FALSE == msr_implemented) {
		/* inject GP into guest */
		vmenter_event_t exception;
		uint16_t inst_length =
			(uint16_t)mon_vmcs_read(vmcs,
				VMCS_EXIT_INFO_INSTRUCTION_LENGTH);

		mon_memset(&exception, 0, sizeof(exception));
		exception.interrupt_info.bits.valid = 1;
		exception.interrupt_info.bits.vector = fault_vector;
		exception.interrupt_info.bits.interrupt_type =
			VMENTER_INTERRUPT_TYPE_HARDWARE_EXCEPTION;
		exception.interrupt_info.bits.deliver_code = 1;
		exception.instruction_length = inst_length;
		exception.error_code = (address_t)error_code;

		gcpu_inject_event(gcpu, &exception);
	}

	return msr_implemented;
}

boolean_t msr_vmcs_resident_default_handler(guest_cpu_handle_t gcpu,
					    msr_id_t msr_id,
					    rw_access_t access,
					    uint64_t *msr_value)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	/* default to invalid id */
	vmcs_field_t vmcs_field_id = VMCS_FIELD_COUNT;
	boolean_t found = FALSE;
	unsigned int i;

	/* check if it is MSR which resides in Guest part of VMCS */
	for (i = 0; i < NELEMENTS(vmcs_resident_guest_msrs); ++i) {
		if (vmcs_resident_guest_msrs[i].msr_id == msr_id) {
			vmentry_controls_t vmenter_controls;

			if (IA32_MSR_DEBUGCTL == msr_id) {
				vmenter_controls.uint32 =
					(uint32_t)mon_vmcs_read(vmcs,
						VMCS_ENTER_CONTROL_VECTOR);
				if (vmenter_controls.bits.load_debug_controls) {
					found = TRUE;
				}
			} else if (IA32_MSR_PERF_GLOBAL_CTRL == msr_id) {
				vmenter_controls.uint32 =
					(uint32_t)mon_vmcs_read(vmcs,
						VMCS_ENTER_CONTROL_VECTOR);
				if (vmenter_controls.bits.
				    load_ia32_perf_global_ctrl
				    &&
				    vmcs_field_is_supported(
					    vmcs_resident_guest_msrs
					    [i].vmcs_field_id)) {
					found = TRUE;
				}
			} else {
				found = TRUE;
			}
			break;
		}
	}

	if (found) {
		vmcs_field_id = vmcs_resident_guest_msrs[i].vmcs_field_id;

		switch (access) {
		case READ_ACCESS:
			*msr_value = mon_vmcs_read(vmcs, vmcs_field_id);
			break;
		case WRITE_ACCESS:
			mon_vmcs_write(vmcs, vmcs_field_id, *msr_value);
			break;
		default:
			/* should not be here */
			MON_DEADLOOP();
			break;
		}
	}

	return found;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : msr_unsupported_access_handler()
*  PURPOSE  : Inject General Protection /fault event into the GCPU
*           : Used for both read and write accesses
*  ARGUMENTS: guest_cpu_handle_t  gcpu
*           : msr_id_t            msr_id - not used
*           : uint64_t           *value - not used
*  RETURNS  : FALSE, which means that instruction caused GP fault.
*--------------------------------------------------------------------------*/

boolean_t msr_unsupported_access_handler(guest_cpu_handle_t gcpu,
					 msr_id_t msr_id UNUSED,
					 uint64_t *value UNUSED,
					 void *context UNUSED)
{
	report_msr_write_access_data_t msr_write_access_data;

	msr_write_access_data.msr_id = msr_id;

	/* Using write access method for both read/write access here */
	if (!report_mon_event
		    (MON_EVENT_MSR_WRITE_ACCESS,
		    (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		    &msr_write_access_data)) {
		return FALSE;
	}

	/* inject GP Fault into guest */
	mon_gcpu_inject_gp0(gcpu);
	return FALSE;
}


/*--------------------------------------------------------------------------*
*  FUNCTION : msr_efer_update_is_gpf0()
*  PURPOSE  : Handle guest access to EFER. Update guest visible value.
*  ARGUMENTS: guest_cpu_handle_t  gcpu
*           : msr_id_t            msr_id
*           : uint64_t           *msr_value
*  RETURNS  : TRUE, which means that instruction was executed.
*--------------------------------------------------------------------------*/

static
boolean_t msr_efer_update_is_gpf0(guest_cpu_handle_t gcpu, uint64_t new_value)
{
	ia32_efer_t efer;

	efer.uint64 = new_value;

	if (efer.bits.lme) {
		em64t_cr4_t cr4;
		cr4.uint64 =
			gcpu_get_guest_visible_control_reg_layered(gcpu,
				IA32_CTRL_CR4,
				VMCS_MERGED);

		if (!cr4.bits.pae) {
			return TRUE;
		}
	}

	return FALSE;
}

boolean_t msr_efer_write_handler(guest_cpu_handle_t gcpu, msr_id_t msr_id,
				 uint64_t *msr_value, void *context UNUSED)
{
	event_gcpu_guest_msr_write_data_t data;
	raise_event_retval_t event_retval;
	report_msr_write_access_data_t msr_write_access_data;

	MON_ASSERT(IA32_MSR_EFER == msr_id);

	msr_write_access_data.msr_id = msr_id;
	if (!report_mon_event
		    (MON_EVENT_MSR_WRITE_ACCESS,
		    (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		    &msr_write_access_data)) {
		return FALSE;
	}

	if (msr_efer_update_is_gpf0(gcpu, *msr_value)) {
		MON_LOG(mask_mon,
			level_trace,
			"%s: EFER update should have caused GPF0 in native mode\n",
			__FUNCTION__);
		MON_LOG(mask_mon, level_trace,
			"%s: Changing vmexit to GP is not implemented yet\n",
			__FUNCTION__);
		MON_DEADLOOP();
	}

	gcpu_set_msr_reg(gcpu, IA32_MON_MSR_EFER, *msr_value);

	mon_memset(&data, 0, sizeof(data));
	data.new_guest_visible_value = *msr_value;
	data.msr_index = msr_id;
	event_retval =
		event_raise(EVENT_GCPU_AFTER_EFER_MSR_WRITE, gcpu, &data);
	MON_ASSERT(event_retval != EVENT_NOT_HANDLED);
	return TRUE;
}

boolean_t msr_efer_read_handler(guest_cpu_handle_t gcpu, msr_id_t msr_id UNUSED,
				uint64_t *msr_value, void *context UNUSED)
{
	report_mon_event(MON_EVENT_MSR_READ_ACCESS,
		(mon_identification_data_t)gcpu,
		(const guest_vcpu_t *)mon_guest_vcpu(gcpu), NULL);
	return FALSE;
}



boolean_t msr_pat_write_handler(guest_cpu_handle_t gcpu, msr_id_t msr_id,
				uint64_t *msr_value, void *context UNUSED)
{
	report_msr_write_access_data_t msr_write_access_data;

	MON_ASSERT(IA32_MSR_PAT == msr_id);
	msr_write_access_data.msr_id = msr_id;
	if (!report_mon_event
		    (MON_EVENT_MSR_WRITE_ACCESS,
		    (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		    &msr_write_access_data)) {
		return FALSE;
	}

	gcpu_set_msr_reg(gcpu, IA32_MON_MSR_PAT, *msr_value);
	return TRUE;
}

boolean_t msr_pat_read_handler(guest_cpu_handle_t gcpu, msr_id_t msr_id,
			       uint64_t *msr_value, void *context UNUSED)
{
	MON_ASSERT(IA32_MSR_PAT == msr_id);

	*msr_value = gcpu_get_msr_reg(gcpu, IA32_MON_MSR_PAT);
	return TRUE;
}

static
boolean_t msr_mtrr_write_handler(guest_cpu_handle_t gcpu, msr_id_t msr_id,
				 uint64_t *msr_value, void *context UNUSED)
{
	event_gcpu_guest_msr_write_data_t data;
	raise_event_retval_t event_retval;
	report_msr_write_access_data_t msr_write_access_data;

	/* IA32_MTRRCAP_ADDR is read only mtrr */
	MON_ASSERT(msr_id != IA32_MTRRCAP_ADDR);
	msr_write_access_data.msr_id = msr_id;
	if (!report_mon_event
		    (MON_EVENT_MSR_WRITE_ACCESS,
		    (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		    &msr_write_access_data)) {
		return FALSE;
	}

	hw_write_msr(msr_id, *msr_value);
	mtrrs_abstraction_track_mtrr_update(msr_id, *msr_value);

	mon_memset(&data, 0, sizeof(data));
	data.new_guest_visible_value = *msr_value;
	data.msr_index = msr_id;

	event_retval =
		event_raise(EVENT_GCPU_AFTER_MTRR_MSR_WRITE, gcpu, &data);
	MON_ASSERT(event_retval != EVENT_NOT_HANDLED);

	return TRUE;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : msr_lapic_base_write_handler()
*  PURPOSE  : Track Guest writes to Loacal APIC Base Register
*  ARGUMENTS: guest_cpu_handle_t  gcpu
*           : msr_id_t            msr_id
*           : uint64_t           *msr_value
*  RETURNS  : TRUE, which means that instruction was executed.
*--------------------------------------------------------------------------*/
boolean_t msr_lapic_base_write_handler(guest_cpu_handle_t gcpu, msr_id_t msr_id,
				       uint64_t *msr_value,
				       void *context UNUSED)
{
	report_msr_write_access_data_t msr_write_access_data;

	MON_ASSERT(IA32_MSR_APIC_BASE == msr_id);
	msr_write_access_data.msr_id = msr_id;
	if (!report_mon_event
		    (MON_EVENT_MSR_WRITE_ACCESS,
		    (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		    &msr_write_access_data)) {
		return FALSE;
	}

	if (!validate_APIC_BASE_change(*msr_value)) {
		mon_gcpu_inject_gp0(gcpu);
		return FALSE;
	}

	hw_write_msr(IA32_MSR_APIC_BASE, *msr_value);
	local_apic_setup_changed();
	return TRUE;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : msr_feature_control_read_handler()
*  PURPOSE  : Handles MSR reads on FEATURE_CONTROL MSR (0x3A).
*             Virtualizes VMX enable bit(bit 2).
*  ARGUMENTS: guest_cpu_handle_t  gcpu
*           : msr_id_t            msr_id
*           : uint64_t           *msr_value
*  RETURNS  : TRUE, which means that instruction was executed.
*--------------------------------------------------------------------------*/
boolean_t msr_feature_control_read_handler(guest_cpu_handle_t gcpu,
					   msr_id_t msr_id, uint64_t *msr_value,
					   void *context UNUSED)
{
	MON_ASSERT(IA32_MSR_FEATURE_CONTROL == msr_id);
	/* IA32 spec V2, 5.3, GETSEC[SENTER]
	 * IA32_FEATURE_CONTROL is only available on SMX or VMX enabled
	 * processors
	 * otherwise, it its treated as reserved. */
	MON_LOG(mask_mon,
		level_trace,
		"%s: IA32_FEATURE_CONTROL is only available on SMX or VMX enabled processors.\n",
		__FUNCTION__);
	mon_gcpu_inject_gp0(gcpu);
	return TRUE;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : msr_feature_control_write_handler()
*  PURPOSE  : Handles writes to FEATURE_CONTROL MSR (0x3A).
*             Induces GP(0) exception.
*  ARGUMENTS: guest_cpu_handle_t  gcpu
*           : msr_id_t            msr_id
*           : uint64_t           *msr_value
*  RETURNS  : TRUE, which means that instruction was executed.
*--------------------------------------------------------------------------*/
boolean_t msr_feature_control_write_handler(guest_cpu_handle_t gcpu,
					    msr_id_t msr_id,
					    uint64_t *msr_value,
					    void *context UNUSED)
{
	MON_ASSERT(IA32_MSR_FEATURE_CONTROL == msr_id);
	/* IA32 spec V2, 5.3, GETSEC[SENTER]
	 * IA32_FEATURE_CONTROL is only available on SMX or VMX enabled
	 * processors
	 * otherwise, it its treated as reserved. */
	MON_LOG(mask_mon, level_trace,
		"%s: IA32_FEATURE_CONTROL is only available on SMX or VMX"
		" enabled processors.\n",
		__FUNCTION__);
	mon_gcpu_inject_gp0(gcpu);
	return TRUE;
}

/*--------------------------------------------------------------------------*
*  FUNCTION : msr_misc_enable_write_handler()
*  PURPOSE  : Handles writes to MISC_ENABLE MSR (0x1A0).
*  ARGUMENTS: guest_cpu_handle_t  gcpu
*           : msr_id_t            msr_id
*           : uint64_t           *msr_value
*  RETURNS  : TRUE, which means that instruction was executed.
*--------------------------------------------------------------------------*/
boolean_t msr_misc_enable_write_handler(guest_cpu_handle_t gcpu,
					msr_id_t msr_id,
					uint64_t *msr_value,
					void *context UNUSED)
{
	report_msr_write_access_data_t msr_write_access_data;

	MON_ASSERT(IA32_MSR_MISC_ENABLE == msr_id);

	msr_write_access_data.msr_id = msr_id;
	if (!report_mon_event
		    (MON_EVENT_MSR_WRITE_ACCESS,
		    (mon_identification_data_t)gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(gcpu),
		    &msr_write_access_data)) {
		return FALSE;
	}

	/* Limit CPUID MAXVAL */
	BIT_CLR64(*msr_value, 22);

	hw_write_msr(IA32_MSR_MISC_ENABLE, *msr_value);

	return TRUE;
}


/*--------------------------------------------------------------------------*
*  FUNCTION : msr_guest_access_inhibit()
*  PURPOSE  : Install VMEXIT handler which prevents access to MSR from the
*             guest
*  ARGUMENTS: guest_handle_t    guest
*           : msr_id_t      msr_id
*  RETURNS  : MON_OK if succeeded
*--------------------------------------------------------------------------*/
mon_status_t msr_guest_access_inhibit(guest_handle_t guest, msr_id_t msr_id)
{
	return mon_msr_vmexit_handler_register(guest, msr_id,
		msr_unsupported_access_handler,
		READ_WRITE_ACCESS, NULL);
}

vmexit_handling_status_t msr_failed_vmenter_loading_handler(guest_cpu_handle_t gcpu
							    USED_IN_DEBUG_ONLY)
{
#ifndef DEBUG
	em64t_rflags_t rflags;
	ia32_vmx_vmcs_guest_interruptibility_t interruptibility;
#endif

	MON_LOG(mask_mon, level_trace, "%s: VMENTER failed\n", __FUNCTION__);

#ifdef DEBUG
	{
		vmcs_object_t *vmcs = vmcs_hierarchy_get_vmcs(
			gcpu_get_vmcs_hierarchy(gcpu), VMCS_MERGED);
		vmcs_print_vmenter_msr_load_list(vmcs);
	}
	MON_DEADLOOP();
#else
	mon_deadloop_internal(VMEXIT_MSR_C, __LINE__, gcpu);

	/* clear interrupt flag */
	rflags.uint64 = gcpu_get_gp_reg(gcpu, IA32_REG_RFLAGS);
	rflags.bits.ifl = 0;
	gcpu_set_gp_reg(gcpu, IA32_REG_RFLAGS, rflags.uint64);

	interruptibility.uint32 = gcpu_get_interruptibility_state(gcpu);
	interruptibility.bits.block_next_instruction = 0;
	gcpu_set_interruptibility_state(gcpu, interruptibility.uint32);

	mon_gcpu_inject_gp0(gcpu);
	gcpu_resume(gcpu);
#endif

	return VMEXIT_NOT_HANDLED;
}


boolean_t mon_vmexit_register_unregister_for_efer(guest_handle_t guest,
						  msr_id_t msr_id,
						  rw_access_t access,
						  boolean_t reg_dereg)
{
	if (!mon_is_unrestricted_guest_supported()) {
		return FALSE;
	}

	if ((msr_id == IA32_MSR_EFER) && reg_dereg) {
		if (access == WRITE_ACCESS) {
			mon_msr_vmexit_handler_register(guest,
				IA32_MSR_EFER,
				msr_efer_write_handler,
				WRITE_ACCESS, NULL);
			return TRUE;
		} else {
			mon_msr_vmexit_handler_register(guest,
				IA32_MSR_EFER,
				msr_efer_read_handler,
				READ_ACCESS, NULL);
			return TRUE;
		}
	}

	if ((msr_id == IA32_MSR_EFER) && !reg_dereg) {
		msr_vmexit_handler_unregister(guest, msr_id, access);
		return TRUE;
	}

	return FALSE;
}
