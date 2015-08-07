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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMDB_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMDB_C, __condition)
#include "mon_defs.h"
#include "hw_utils.h"
#include "mon_dbg.h"
#include "guest.h"
#include "guest_cpu.h"
#include "memory_allocator.h"
#include "vmcs_api.h"
#include "vmx_vmcs.h"
#include "vmx_ctrl_msrs.h"
#include "guest_cpu_vmenter_event.h"
#include "vmexit.h"
#include "cli.h"
#include "ipc.h"
#include "scheduler.h"
#include "event_mgr.h"
#include "vmdb.h"

#ifdef VMDB_INCLUDE

#define VMDB_LOG(__level, ...) MON_LOG(mask_gdb, __level, __VA_ARGS__)

#define NUMBER_OF_HW_BREAKPOINTS 4
#define BREAKPOINT_ID_IS_VALID(__i) (0 <= (__i) && (__i) < 4)
#define RFLAGS_RF_BIT   16
#define RFLAGS_TF_BIT   8

typedef struct {
	/* dr:0..3 */
	address_t	dr[NUMBER_OF_HW_BREAKPOINTS];
	address_t	dr7;
	uint16_t	skip_counter[NUMBER_OF_HW_BREAKPOINTS];
	/* single step */
	boolean_t	sstep;
	uint8_t		pad[4];
} vmdb_thread_context_t;

typedef enum {
	VMDB_IPC_ATTACH,
	VMDB_IPC_DETACH,
	VMDB_IPC_ADD_BP,
	VMDB_IPC_DEL_BP,
	VMDB_IPC_SINGLE_STEP
} vmdb_ipc_function_id_t;

typedef struct {
	vmdb_ipc_function_id_t	function_id;
	guest_id_t		guest_id;
	uint8_t			pad1[2];
	union {
		struct {
			address_t			linear_address;
			vmdb_breakpoint_type_t		bp_type;
			vmdb_break_length_type_t	bp_len;
			uint16_t			skip_counter;
			uint8_t				pad2[6];
		} add_bp;
		struct {
			address_t linear_address;
		} del_bp;
		struct {
			boolean_t enable;
		} sstep;
	} u;
} vmdb_remote_params_t;

/* Macros for access Debug Registers fields */

#define FIELD_GET(__r, __s, __m)    (((__r) >> (__s)) & (__m))
#define FIELD_CLR(__r, __t, __s, __m) (__r &= ~(((__t)(__m)) << (__s)))
#define FIELD_WRT(__r, __t, __s, __m, __v)                                     \
	{                                                                              \
		FIELD_CLR(__r, __t, __s, __m);                                             \
		__r |= ((__t)((__v) & (__m))) << (__s);                                   \
	}

#define DR7_LOCAL_BIT(__x)          ((__x) * 2)
#define DR7_LOCAL_GET(__r, __x)     BIT_GET64(__r, DR7_LOCAL_BIT(__x))
#define DR7_LOCAL_SET(__r, __x)     BIT_SET64(__r, DR7_LOCAL_BIT(__x))
#define DR7_LOCAL_CLR(__r, __x)     BIT_CLR64(__r, DR7_LOCAL_BIT(__x))

#define DR7_GLOBAL_BIT(__x)         ((__x) * 2 + 1)
#define DR7_GLOBAL_GET(__r, __x)    BIT_GET64(__r, DR7_GLOBAL_BIT(__x))
#define DR7_GLOBAL_SET(__r, __x)    BIT_SET64(__r, DR7_GLOBAL_BIT(__x))
#define DR7_GLOBAL_CLR(__r, __x)    BIT_CLR64(__r, DR7_GLOBAL_BIT(__x))

#define DR7_GD_BIT                  13
#define DR7_GD_GET(__r)             BIT_GET64(__r, DR7_GD_BIT)
#define DR7_GD_SET(__r)             BIT_SET64(__r, DR7_GD_BIT)
#define DR7_GD_CLR(__r)             BIT_CLR64(__r, DR7_GD_BIT)

#define DR7_RW_MASK                 3
#define DR7_RW_SHIFT(__x)           (16 + ((__x) & DR7_RW_MASK) * 4)
#define DR7_RW_GET(__r, __x)       \
	FIELD_GET(__r, DR7_RW_SHIFT(__x), DR7_RW_MASK)
#define DR7_RW_CLR(__r, __x)       \
	FIELD_CLR(__r, address_t, DR7_RW_SHIFT(__x), DR7_RW_MASK)
#define DR7_RW_WRT(__r, __x, __rw) \
	FIELD_WRT(__r, address_t, DR7_RW_SHIFT(__x), DR7_RW_MASK, __rw)
#define DR7_ONES                    0x700

#define DR7_LEN_MASK                3
#define DR7_LEN_SHIFT(__x)          (18 + ((__x) & DR7_LEN_MASK) * 4)
#define DR7_LEN_GET(__r, __x)       \
	FIELD_GET(__r, DR7_LEN_SHIFT(__x), DR7_LEN_MASK)
#define DR7_LEN_CLR(__r, __x)       \
	FIELD_CLR(__r, address_t, DR7_LEN_SHIFT(__x), DR7_LEN_MASK)
#define DR7_LEN_WRT(__r, __x, __rw) \
	FIELD_WRT(__r, address_t, DR7_LEN_SHIFT(__x), DR7_LEN_MASK, __rw)

#define DR7_MUST_ONE_BITS           0x400

#define DR6_BP(__x)         (1 << (__x))
#define DR6_BD              (1 << 13)
#define DR6_BS              (1 << 14)
#define DR6_BT              (1 << 15)

/*-------------------------------Local Variables----------------------------*/

static char *bp_type_name[] = { "exe", "write", "io", "rw" };
static uint8_t bp_actual_length[] = { 1, 2, 8, 4 };

static mon_ia32_gp_registers_t lkup_operand[] = {
	IA32_REG_RAX,
	IA32_REG_RCX,
	IA32_REG_RDX,
	IA32_REG_RBX,
	IA32_REG_RSP,
	IA32_REG_RBP,
	IA32_REG_RSI,
	IA32_REG_RDI,
	IA32_REG_R8,
	IA32_REG_R9,
	IA32_REG_R10,
	IA32_REG_R11,
	IA32_REG_R12,
	IA32_REG_R13,
	IA32_REG_R14,
	IA32_REG_R15
};

/*--------------------------------------------------------------------------*
*                      Forward Declarations for Local Functions
*--------------------------------------------------------------------------*/
static vmdb_thread_context_t *vmdb_thread_context_create(void);
static void vmdb_thread_context_destroy(vmdb_thread_context_t *vmdb);
static int vmdb_breakpoint_lookup(vmdb_thread_context_t *vmdb_context,
				  address_t bp_address);
static int vmdb_free_breakpoint_lookup(vmdb_thread_context_t *vmdb_context);
static vmexit_handling_status_t vmdb_dr_access_vmexit_handler(guest_cpu_handle_t
							      gcpu);
static void vmdb_cli_init(void);
static int vmdb_cli_breakpoint_add(unsigned argc, char *args[]);
static int vmdb_cli_breakpoint_delete(unsigned argc, char *args[]);
static int vmdb_cli_breakpoint_show(unsigned argc, char *args[]);
static void vmdb_fill_vmexit_request(vmexit_control_t *vmexit_request,
				     boolean_t enable);
static int vmdb_cli_breakpoint_add(unsigned argc, char *args[]);
static int vmdb_cli_breakpoint_delete(unsigned argc, char *args[]);
static int vmdb_cli_breakpoint_show(unsigned argc, char *args[]);
static int vmdb_cli_debug_attach(unsigned argc, char *args[]);
static int vmdb_cli_debug_detach(unsigned argc, char *args[]);
static guest_cpu_handle_t vmdb_cli_locate_gcpu(char *string,
					       boolean_t *apply_to_all);
static void vmdb_thread_log(guest_cpu_handle_t gcpu,
			    const char *msg,
			    const char *function_name);
static void vmdb_remote_handler(cpu_id_t from UNUSED,
				vmdb_remote_params_t *params);
static void vmdb_remote_execute(guest_cpu_handle_t gcpu,
				vmdb_remote_params_t *params);
static void vmdb_remote_thread_attach(guest_cpu_handle_t gcpu);
static void vmdb_remote_thread_detach(guest_cpu_handle_t gcpu);
static void vmdb_remote_breakpoint_add(guest_cpu_handle_t gcpu,
				       address_t linear_address,
				       vmdb_breakpoint_type_t bp_type,
				       vmdb_break_length_type_t bp_len,
				       uint16_t skip_counter);
static void vmdb_remote_breakpoint_delete(guest_cpu_handle_t gcpu,
					  address_t linear_address);
static void vmdb_remote_single_step_enable(guest_cpu_handle_t gcpu,
					   boolean_t enable);

/*---------------------------------------------------------------------------*
*                          VMDB Helper functions
*---------------------------------------------------------------------------*/

void vmdb_thread_log(guest_cpu_handle_t gcpu, const char *msg,
		     const char *function_name)
{
#if defined DEBUG
	const virtual_cpu_id_t *vcpu = mon_guest_vcpu(gcpu);
	MON_ASSERT(vcpu);
#endif
	VMDB_LOG(level_error, "%s() %s (%d,%d)\n",
		function_name, msg, vcpu->guest_id, vcpu->guest_cpu_id);
}

/*--------------------------------------------------------------------------*
*                          VMDB Initialization functions
*--------------------------------------------------------------------------*/
void vmdb_initialize(void)
{
	vmdb_cli_init();
}

vmdb_thread_context_t *vmdb_thread_context_create(void)
{
	vmdb_thread_context_t *vmdb = mon_malloc(sizeof(*vmdb));

	if (NULL != vmdb) {
		vmdb->dr7 = DR7_MUST_ONE_BITS;
	} else {
		VMDB_LOG(level_error,
			"[vmdb] %s failed due to memory lack %d\n",
			__FUNCTION__);
	}

	return vmdb;
}

void vmdb_thread_context_destroy(vmdb_thread_context_t *vmdb)
{
	if (NULL != vmdb) {
		/* remove existing breakpoint. TBD */
		mon_mfree(vmdb);
	}
}

/*----------------------------------------------------------------------------*
 *  Function : vmdb_guest_initialize
 *  Purpose  : Initialize CLI interface for vmdb.
 *           : Creates VMDB control structure at the guest and
 *           : install VMDB VMEXIT handler on INT1.
 *  Arguments: guest_id_t
 *  Returns  : mon_status_t
 */
mon_status_t vmdb_guest_initialize(guest_id_t guest_id)
{
	return vmexit_install_handler(guest_id,
		vmdb_dr_access_vmexit_handler,
		IA32_VMX_EXIT_BASIC_REASON_DR_ACCESS);
}

/*----------------------------------------------------------------------------*
 *  Function : vmdb_thread_attach
 *  Purpose  : Enables VMDB on all given GCPU
 *  Arguments: guest_cpu_handle_t
 *  Returns  : mon_status_t
 */
mon_status_t vmdb_thread_attach(guest_cpu_handle_t gcpu)
{
	mon_status_t status = MON_ERROR;
	vmdb_thread_context_t *vmdb = gcpu_get_vmdb(gcpu);
	vmexit_control_t vmexit_request;

	/* one-shot loop */
	do {
		if (NULL != vmdb) {
			vmdb_thread_log(gcpu, "VMDB already attached to thread",
				__FUNCTION__);
			break;
		}

		vmdb = vmdb_thread_context_create();
		if (NULL == vmdb) {
			vmdb_thread_log(gcpu,
				"VMDB failed to create context for thread",
				__FUNCTION__);
			break;
		}

		gcpu_set_vmdb(gcpu, vmdb);

		vmdb_fill_vmexit_request(&vmexit_request, TRUE);
		gcpu_control_setup(gcpu, &vmexit_request);

		status = MON_OK;
	} while (0);

	return status;
}

/*---------------------------------------------------------------------------*
 *  Function : vmdb_thread_detach
 *  Purpose  : Disables VMDB on all given GCPU
 *  Arguments: guest_cpu_handle_t
 *  Returns  : mon_status_t
 */
mon_status_t vmdb_thread_detach(guest_cpu_handle_t gcpu)
{
	mon_status_t status = MON_ERROR;
	vmdb_thread_context_t *vmdb = gcpu_get_vmdb(gcpu);
	vmexit_control_t vmexit_request;

	/* one-shot loop */
	do {
		if (NULL == vmdb) {
			vmdb_thread_log(gcpu,
				"VMDB already detached from thread",
				__FUNCTION__);
			break;
		}

		vmdb_fill_vmexit_request(&vmexit_request, FALSE);
		gcpu_control_setup(gcpu, &vmexit_request);

		vmdb_breakpoint_delete_all(gcpu);
		vmdb_single_step_enable(gcpu, FALSE);
		vmdb_settings_apply_to_hw(gcpu);

		vmdb_thread_context_destroy(vmdb);
		gcpu_set_vmdb(gcpu, NULL);
	} while (0);

	return status;
}

/*---------------------------------------------------------------------------*
 *  Function : vmdb_fill_vmexit_request
 *  Purpose  : Configures VMDB-related VTx controls, depending on enble value
 *           :   DR-access
 *           :   Save/Load DR
 *           :   Exception on INT1
 *  Arguments: vmexit_control_t *vmexit_request
 *           : boolean_t enable/disable
 *  Returns  : void
 */
void vmdb_fill_vmexit_request(OUT vmexit_control_t *vmexit_request,
			      boolean_t enable)
{
	ia32_vmcs_exception_bitmap_t exceptions_mask;
	processor_based_vm_execution_controls_t exec_controls_mask;
	vmexit_controls_t vmexit_controls;
	vmentry_controls_t vmenter_controls;
	uint32_t value = enable ? (uint32_t)-1 : 0;

	mon_memset(vmexit_request, 0, sizeof(vmexit_control_t));
	exceptions_mask.uint32 = 0;
	exceptions_mask.bits.db = 1;
	vmexit_request->exceptions.bit_mask = exceptions_mask.uint32;
	vmexit_request->exceptions.bit_request = value;

	exec_controls_mask.uint32 = 0;
	exec_controls_mask.bits.mov_dr = 1;
	vmexit_request->proc_ctrls.bit_mask = exec_controls_mask.uint32;
	vmexit_request->proc_ctrls.bit_request = value;

	vmexit_controls.uint32 = 0;
	vmexit_controls.bits.save_debug_controls = 1;
	vmexit_request->vm_exit_ctrls.bit_mask = vmexit_controls.uint32;
	vmexit_request->vm_exit_ctrls.bit_request = value;

	vmenter_controls.uint32 = 0;
	vmenter_controls.bits.load_debug_controls = 1;

	vmexit_request->vm_enter_ctrls.bit_mask = vmenter_controls.uint32;
	vmexit_request->vm_enter_ctrls.bit_request = value;
}

/*--------------------------------------------------------------------------*
*                          VMDB breakpoint functions
*--------------------------------------------------------------------------*/
mon_status_t vmdb_single_step_enable(guest_cpu_handle_t gcpu, boolean_t enable)
{
	vmdb_thread_context_t *vmdb;
	mon_status_t status;

	MON_ASSERT(gcpu);

	vmdb = gcpu_get_vmdb(gcpu);
	if (NULL != vmdb) {
		vmdb->sstep = enable;
		status = MON_OK;
	} else {
		vmdb_thread_log(gcpu,
			"gDB is not attached to thread",
			__FUNCTION__);
		status = MON_ERROR;
	}
	return status;
}

mon_status_t vmdb_single_step_info(guest_cpu_handle_t gcpu, boolean_t *enable)
{
	vmdb_thread_context_t *vmdb;
	mon_status_t status;

	MON_ASSERT(gcpu);

	vmdb = gcpu_get_vmdb(gcpu);
	if (NULL != vmdb) {
		if (enable != NULL) {
			*enable = vmdb->sstep;
		}
		status = MON_OK;
	} else {
		vmdb_thread_log(gcpu,
			"gDB is not attached to thread",
			__FUNCTION__);
		status = MON_ERROR;
	}
	return status;
}

/*---------------------------------------------------------------------------*
 *  Function : vmdb_breakpoint_lookup
 *  Purpose  : Look for breakpoint equal to given address
 *  Arguments: vmdb_thread_context_t *vmdb_context - where to search
 *           : address_t            bp_address  - what to look for
 *  Returns  : Breakpoint ID if found, -1 if not
 */
int vmdb_breakpoint_lookup(vmdb_thread_context_t *vmdb_context,
			   address_t bp_address)
{
	int i;

	for (i = 0; i < NUMBER_OF_HW_BREAKPOINTS; ++i) {
		if (bp_address == vmdb_context->dr[i]) {
			/* found */
			return i;
		}
	}
	/* not found */
	return -1;
}

/*---------------------------------------------------------------------------*
 *  Function : vmdb_free_breakpoint_lookup
 *  Purpose  : Look for free (not used) breakpoint. Wrapper upon
 *             vmdb_breakpoint_lookup.
 *  Arguments: vmdb_thread_context_t *vmdb_context - where to search
 *  Returns  : Breakpoint ID if found, -1 if not
 */
int vmdb_free_breakpoint_lookup(vmdb_thread_context_t *vmdb_context)
{
	return vmdb_breakpoint_lookup(vmdb_context, 0);
}

/*---------------------------------------------------------------------------*
 *  Function : vmdb_settings_apply_to_hw
 *  Purpose  : Update GCPU DRs from its guest's VMDB context
 *  Arguments: guest_cpu_handle_t gcpu
 *  Returns  : void
 */
void vmdb_settings_apply_to_hw(guest_cpu_handle_t gcpu)
{
	vmdb_thread_context_t *vmdb = gcpu_get_vmdb(gcpu);

	if (NULL != vmdb) {
		uint64_t rflags;
		vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);

		gcpu_set_debug_reg(gcpu, IA32_REG_DR7, vmdb->dr7);
		gcpu_set_debug_reg(gcpu, IA32_REG_DR0, vmdb->dr[0]);
		gcpu_set_debug_reg(gcpu, IA32_REG_DR1, vmdb->dr[1]);
		gcpu_set_debug_reg(gcpu, IA32_REG_DR2, vmdb->dr[2]);
		gcpu_set_debug_reg(gcpu, IA32_REG_DR3, vmdb->dr[3]);

		rflags = mon_vmcs_read(vmcs, VMCS_GUEST_RFLAGS);
		if (vmdb->sstep) {
			BIT_SET64(rflags, RFLAGS_TF_BIT);
		} else {
			BIT_CLR64(rflags, RFLAGS_TF_BIT);
		}
		mon_vmcs_write(vmcs, VMCS_GUEST_RFLAGS, rflags);
	}
}

/*---------------------------------------------------------------------------*
 *  Function : vmdb_breakpoint_info
 *  Purpose  : Gets guests breakpont info.
 *  Arguments: self-descriptive
 *  Returns  : mon_status_t
 */
mon_status_t vmdb_breakpoint_info(guest_cpu_handle_t gcpu, uint32_t bp_id,
				  address_t *linear_address,
				  vmdb_breakpoint_type_t *bp_type,
				  vmdb_break_length_type_t *bp_len,
				  uint16_t *skip_counter)
{
	vmdb_thread_context_t *vmdb;
	mon_status_t status = MON_ERROR;

	/* one-shot loop */
	do {
		if (NULL == gcpu) {
			break;
		}

		vmdb = gcpu_get_vmdb(gcpu);
		if (NULL == vmdb) {
			vmdb_thread_log(gcpu, "gDB is not attached to thread",
				__FUNCTION__);
			break;
		}

		if (bp_id > NUMBER_OF_HW_BREAKPOINTS - 1) {
			break;
		}

		if (linear_address != NULL) {
			*linear_address = vmdb->dr[bp_id];
		}
		if (bp_type != NULL) {
			*bp_type = (vmdb_breakpoint_type_t)DR7_RW_GET(vmdb->dr7,
				bp_id);
		}
		if (bp_len != NULL) {
			*bp_len = (vmdb_break_length_type_t)DR7_LEN_GET(
				vmdb->dr7,
				bp_id);
		}
		if (skip_counter != NULL) {
			*skip_counter = vmdb->skip_counter[bp_id];
		}

		status = MON_OK;
	} while (0);

	return status;
}

/*---------------------------------------------------------------------------*
 *  Function : vmdb_breakpoint_add
 *  Purpose  : Add breakpont to guest, and propagate it to all guest's GCPUs
 *  Arguments: self-descriptive
 *  Returns  : mon_status_t
 */
mon_status_t vmdb_breakpoint_add(guest_cpu_handle_t gcpu,
				 address_t linear_address,
				 vmdb_breakpoint_type_t bp_type,
				 vmdb_break_length_type_t bp_len,
				 uint16_t skip_counter)
{
	int bp_id;
	vmdb_thread_context_t *vmdb;
	mon_status_t status = MON_ERROR;

	/* one-shot loop */
	do {
		if (NULL == gcpu) {
			break;
		}

		vmdb = gcpu_get_vmdb(gcpu);
		if (NULL == vmdb) {
			vmdb_thread_log(gcpu, "gDB is not attached to thread",
				__FUNCTION__);
			break;
		}

		if (bp_type < VMDB_BREAK_TYPE_FIRST || bp_type >
		    VMDB_BREAK_TYPE_LAST) {
			VMDB_LOG(level_warning,
				"[vmdb] Invalid break type(%d)\n",
				bp_type);
			break;
		}

		if (VMDB_BREAK_ON_EXE == bp_type) {
			bp_len = VMDB_BREAK_LENGTH_1;
		}

		if (bp_len < VMDB_BREAK_LENGTH_FIRST || bp_len >
		    VMDB_BREAK_LENGTH_LAST) {
			VMDB_LOG(level_warning,
				"[vmdb] Invalid break length(%d)\n",
				bp_len);
			break;
		}

		bp_id = vmdb_free_breakpoint_lookup(vmdb);
		if (!BREAKPOINT_ID_IS_VALID(bp_id)) {
			VMDB_LOG(level_trace,
				"[vmdb] No room to set new breakpoint\n");
			break;
		}

		/* here free entry for breakpoint is found, so update VMDB context */
		vmdb->dr[bp_id] = linear_address;
		vmdb->skip_counter[bp_id] = skip_counter;
		DR7_GLOBAL_SET(vmdb->dr7, bp_id);
		DR7_RW_WRT(vmdb->dr7, bp_id, bp_type);
		DR7_LEN_WRT(vmdb->dr7, bp_id, bp_len);
		vmdb->dr7 |= DR7_MUST_ONE_BITS;

		status = MON_OK;
	} while (0);

	return status;
}

/*---------------------------------------------------------------------------*
 *  Function : vmdb_breakpoint_delete
 *  Purpose  : Delete breakpont at guest context and from all its GCPUs
 *  Arguments: guest_handle_t  guest
 *           : address_t linear_address - address of breakpoint to delete
 *           : if equal -1, then remove all breakpoints for this guest
 *  Returns  : mon_status_t
 */
mon_status_t vmdb_breakpoint_delete(guest_cpu_handle_t gcpu,
				    address_t linear_address)
{
	vmdb_thread_context_t *vmdb;
	int bp_id;
	int bp_from;
	int bp_to;
	mon_status_t status = MON_ERROR;

	/* one-shot loop */
	do {
		if (NULL == gcpu) {
			break;
		}

		vmdb = gcpu_get_vmdb(gcpu);
		if (NULL == vmdb) {
			vmdb_thread_log(gcpu, "VMDB is not attached to thread",
				__FUNCTION__);
			break;
		}

		if ((address_t)-1 == linear_address) {
			bp_from = 0;
			bp_to = NUMBER_OF_HW_BREAKPOINTS - 1;
		} else {
			bp_from = vmdb_breakpoint_lookup(vmdb, linear_address);
			bp_to = bp_from;
		}

		if (!BREAKPOINT_ID_IS_VALID(bp_from)) {
			vmdb_thread_log(gcpu,
				"VMDB did not find breakpoint on thread",
				__FUNCTION__);
			break;
		}

		for (bp_id = bp_from; bp_id <= bp_to; ++bp_id) {
			vmdb->dr[bp_id] = 0;
			DR7_LOCAL_CLR(vmdb->dr7, bp_id);
			DR7_GLOBAL_CLR(vmdb->dr7, bp_id);
		}

		status = MON_OK;
	} while (0);

	return status;
}

mon_status_t vmdb_breakpoint_delete_all(guest_cpu_handle_t gcpu)
{
	return vmdb_breakpoint_delete(gcpu, (address_t)-1);
}

boolean_t vmdb_exception_handler(guest_cpu_handle_t gcpu)
{
	vmdb_thread_context_t *vmdb = gcpu_get_vmdb(gcpu);
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	ia32_vmx_exit_qualification_t qualification;
	address_t guest_rflags;
	int i;

#if defined DEBUG
	const virtual_cpu_id_t *vcpu = mon_guest_vcpu(gcpu);
	MON_ASSERT(vcpu);
#endif

	MON_ASSERT(vmdb);

	qualification.uint64 =
		mon_vmcs_read(vmcs, VMCS_EXIT_INFO_QUALIFICATION);

	if (qualification.dbg_exception.dbg_reg_access) {
		VMDB_LOG(level_print_always,
			"[vmdb] Debug Registers Access is NOT supported\n");
	}

	if (qualification.dbg_exception.single_step) {
		vmdb_thread_log(gcpu,
			"VMDB Single Step Break occurred on thread",
			__FUNCTION__);

		if (FALSE == event_raise(EVENT_GUEST_CPU_BREAKPOINT, gcpu, 0)) {
			MON_DEADLOOP();
		}
	} else {
		for (i = 0; i < NUMBER_OF_HW_BREAKPOINTS; ++i) {
			if (BIT_GET64(qualification.dbg_exception.break_points,
				    i)) {
#if defined DEBUG
				uint32_t db_type = (uint32_t)DR7_RW_GET(
					vmdb->dr7,
					i);
#endif

				if (0 != vmdb->skip_counter[i]) {
					vmdb->skip_counter[i];
					continue;
				}

				VMDB_LOG(level_print_always,
					"[vmdb] %s break occurred at address(%P) on"
					" thread(%d,%d)\n",
					bp_type_name[db_type],
					vmdb->dr[i],
					vcpu->guest_id,
					vcpu->guest_id);

				/* If it is breakpoint for the VMDB STUB, then propagate it. */
				if (FALSE ==
				    event_raise(EVENT_GUEST_CPU_SINGLE_STEP,
					    gcpu,
					    0)) {
					MON_DEADLOOP();
				}
			}
		}
	}

	/* Set Resume Flag to prevent breakpoint stucking */
	guest_rflags = gcpu_get_native_gp_reg(gcpu, IA32_REG_RFLAGS);
	BIT_SET64(guest_rflags, RFLAGS_RF_BIT);
	gcpu_set_native_gp_reg(gcpu, IA32_REG_RFLAGS, guest_rflags);

	gcpu_vmexit_exception_resolve(gcpu);

	return TRUE;
}

vmexit_handling_status_t vmdb_dr_access_vmexit_handler(guest_cpu_handle_t gcpu)
{
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	ia32_vmx_exit_qualification_t qualification;
	int dbreg_id;
	mon_ia32_gp_registers_t gpreg_id;

	qualification.uint64 =
		mon_vmcs_read(vmcs, VMCS_EXIT_INFO_QUALIFICATION);
	gpreg_id = lkup_operand[qualification.dr_access.move_gpr];
	dbreg_id = (int)qualification.dr_access.number;
	if (6 == dbreg_id) {
		dbreg_id = IA32_REG_DR6;
	}
	if (7 == dbreg_id) {
		dbreg_id = IA32_REG_DR7;
	}

	if (0 == qualification.dr_access.direction) {
		/* do nothing */
	} else {
		uint64_t reg_value =
			gcpu_get_debug_reg(gcpu,
				(mon_ia32_debug_registers_t)dbreg_id);
		gcpu_set_native_gp_reg(gcpu, gpreg_id, reg_value);
	}

	gcpu_skip_guest_instruction(gcpu);

	return VMEXIT_HANDLED;
}

/*--------------------------------------------------------------------------*
*                          VMDB Remote functions
*--------------------------------------------------------------------------*/
void vmdb_remote_handler(cpu_id_t from UNUSED, vmdb_remote_params_t *params)
{
	virtual_cpu_id_t vcpu;
	guest_cpu_handle_t gcpu;

	/* one-shot loop */
	do {
		if (NULL == params) {
			VMDB_LOG(level_error, "%s called wit NULL argument\n",
				__FUNCTION__);
			break;
		}

		vcpu.guest_id = params->guest_id;
		vcpu.guest_cpu_id = hw_cpu_id();
		gcpu = gcpu_state(&vcpu);
		if (NULL == gcpu) {
			VMDB_LOG(level_error, "%s GCPU(%d,%d) is not found\n",
				__FUNCTION__, vcpu.guest_id, vcpu.guest_cpu_id);
			break;
		}

		switch (params->function_id) {
		case VMDB_IPC_ATTACH:
			vmdb_thread_attach(gcpu);
			break;
		case VMDB_IPC_DETACH:
			vmdb_thread_detach(gcpu);
			break;
		case VMDB_IPC_ADD_BP:
			vmdb_breakpoint_add(gcpu,
				params->u.add_bp.linear_address,
				params->u.add_bp.bp_type,
				params->u.add_bp.bp_len,
				params->u.add_bp.skip_counter);
			break;
		case VMDB_IPC_DEL_BP:
			vmdb_breakpoint_delete(gcpu,
				params->u.del_bp.linear_address);
			break;
		case VMDB_IPC_SINGLE_STEP:
			vmdb_single_step_enable(gcpu, params->u.sstep.enable);
			break;
		default:
			VMDB_LOG(level_error,
				"%s GCPU(%d,%d) Unknown remote function ID(%d)\n",
				__FUNCTION__,
				vcpu.guest_id,
				vcpu.guest_cpu_id,
				params->function_id);
			break;
		}
	} while (0);
}


void vmdb_remote_execute(guest_cpu_handle_t gcpu, vmdb_remote_params_t *params)
{
	const virtual_cpu_id_t *vcpu = mon_guest_vcpu(gcpu);

	if (NULL != vcpu) {
		ipc_destination_t dst;

		dst.addr_shorthand = IPI_DST_ALL_EXCLUDING_SELF;
		params->guest_id = vcpu->guest_id;
		ipc_execute_handler(dst,
			(func_ipc_handler_t)vmdb_remote_handler,
			params);
	} else {
		VMDB_LOG(level_error, "%s Failed to locate VCPU\n",
			__FUNCTION__);
	}
}

void vmdb_remote_thread_attach(guest_cpu_handle_t gcpu)
{
	vmdb_remote_params_t params;

	params.function_id = VMDB_IPC_ATTACH;
	vmdb_remote_execute(gcpu, &params);
}

void vmdb_remote_thread_detach(guest_cpu_handle_t gcpu)
{
	vmdb_remote_params_t params;

	params.function_id = VMDB_IPC_DETACH;
	vmdb_remote_execute(gcpu, &params);
}

void vmdb_remote_breakpoint_add(guest_cpu_handle_t gcpu,
				address_t linear_address,
				vmdb_breakpoint_type_t bp_type,
				vmdb_break_length_type_t bp_len,
				uint16_t skip_counter)
{
	vmdb_remote_params_t params;

	params.function_id = VMDB_IPC_ADD_BP;
	params.u.add_bp.linear_address = linear_address;
	params.u.add_bp.bp_type = bp_type;
	params.u.add_bp.bp_len = bp_len;
	params.u.add_bp.skip_counter = skip_counter;
	vmdb_remote_execute(gcpu, &params);
}

void vmdb_remote_breakpoint_delete(guest_cpu_handle_t gcpu,
				   address_t linear_address)
{
	vmdb_remote_params_t params;

	params.function_id = VMDB_IPC_DEL_BP;
	params.u.del_bp.linear_address = linear_address;
	vmdb_remote_execute(gcpu, &params);
}

void vmdb_remote_single_step_enable(guest_cpu_handle_t gcpu, boolean_t enable)
{
	vmdb_remote_params_t params;

	params.function_id = VMDB_IPC_SINGLE_STEP;
	params.u.sstep.enable = enable;
	vmdb_remote_execute(gcpu, &params);
}

/*--------------------------------------------------------------------------*
*                          VMDB CLI functions
*--------------------------------------------------------------------------*/
guest_cpu_handle_t vmdb_cli_locate_gcpu(char *string, boolean_t *apply_to_all)
{
	virtual_cpu_id_t vcpu;

	if (NULL != apply_to_all) {
		if ('*' == string[0]) {
			*apply_to_all = TRUE;
			/* skip '*' symbol */
			string++;
		} else {
			apply_to_all = FALSE;
		}
	}

	vcpu.guest_id = (guest_id_t)CLI_ATOL(string);
	vcpu.guest_cpu_id = hw_cpu_id();

	return gcpu_state(&vcpu);
}

int vmdb_cli_breakpoint_add(unsigned argc, char *args[])
{
	address_t linear_address;
	vmdb_breakpoint_type_t bp_type;
	vmdb_break_length_type_t bp_len;
	uint32_t skip_counter = 0;
	boolean_t apply_to_all;
	guest_cpu_handle_t gcpu;

	if (argc < 5) {
		return -1;
	}

	gcpu = vmdb_cli_locate_gcpu(args[1], &apply_to_all);
	if (NULL == gcpu) {
		CLI_PRINT("Invalid Guest %s\n", args[1]);
		return -1;
	}

	linear_address = CLI_ATOL64(args[2]);

	switch (args[3][0]) {
	case 'e':
		bp_type = VMDB_BREAK_ON_EXE;
		break;
	case 'w':
		bp_type = VMDB_BREAK_ON_WO;
		break;
	case 'i':
		bp_type = VMDB_BREAK_ON_IO;
		break;
	case 'r':
		bp_type = VMDB_BREAK_ON_RW;
		break;
	default:
		return -1;
	}

	switch (args[4][0]) {
	case '1':
		bp_len = VMDB_BREAK_LENGTH_1;
		break;
	case '2':
		bp_len = VMDB_BREAK_LENGTH_2;
		break;
	case '4':
		bp_len = VMDB_BREAK_LENGTH_4;
		break;
	case '8':
		bp_len = VMDB_BREAK_LENGTH_8;
		break;
	default:
		return -1;
	}

	if (argc > 5) {
		skip_counter = CLI_ATOL(args[5]);
	}

	vmdb_breakpoint_add
		(gcpu, linear_address, bp_type, bp_len, (uint16_t)skip_counter);

	if (apply_to_all) {
		vmdb_remote_breakpoint_add
			(gcpu,
			linear_address,
			bp_type,
			bp_len,
			(uint16_t)skip_counter);
	}

	return 0;
}

int vmdb_cli_breakpoint_delete(unsigned argc, char *args[])
{
	guest_cpu_handle_t gcpu;
	address_t linear_address;
	boolean_t apply_to_all;

	if (argc < 3) {
		return -1;
	}

	gcpu = vmdb_cli_locate_gcpu(args[1], &apply_to_all);
	if (NULL == gcpu) {
		CLI_PRINT("Invalid Guest %s\n", args[1]);
		return -1;
	}

	if (0 == CLI_STRCMP("all", args[2])) {
		linear_address = (address_t)-1;
	} else {
		linear_address = CLI_ATOL64(args[2]);
	}

	vmdb_breakpoint_delete(gcpu, linear_address);

	if (apply_to_all) {
		vmdb_remote_breakpoint_delete(gcpu, linear_address);
	}

	return 0;
}

int vmdb_cli_breakpoint_show(unsigned argc, char *args[])
{
	guest_cpu_handle_t gcpu;
	vmdb_thread_context_t *vmdb;
	address_t bp_address;
	int i;

	if (argc < 2) {
		return -1;
	}

	gcpu = vmdb_cli_locate_gcpu(args[1], NULL);

	if (NULL == gcpu) {
		CLI_PRINT("Invalid Guest %s\n", args[1]);
		return -1;
	}

	vmdb = gcpu_get_vmdb(gcpu);
	if (NULL == vmdb) {
		CLI_PRINT("VMDB is not attached to thread %s,%d\n", args[1],
			hw_cpu_id());
		return -1;
	}

	CLI_PRINT("======================================\n");
	CLI_PRINT("Single step:  %s\n", vmdb->sstep ? "enabled" : "disabled");
	CLI_PRINT("======================================\n");
	CLI_PRINT("bp  linear address  type  len  counter\n");
	CLI_PRINT("======================================\n");

	for (i = 0; i < NUMBER_OF_HW_BREAKPOINTS; ++i) {
		CLI_PRINT("%d: ", i);

		bp_address = vmdb->dr[i];

		if (0 != bp_address && DR7_GLOBAL_GET(vmdb->dr7, i)) {
#if defined DEBUG
			vmdb_breakpoint_type_t bp_type =
				(vmdb_breakpoint_type_t)DR7_RW_GET(vmdb->dr7,
					i);
			vmdb_break_length_type_t bp_len =
				(vmdb_break_length_type_t)DR7_LEN_GET(vmdb->dr7,
					i);
#endif
			CLI_PRINT
				("%16P %5s   %d   %d",
				bp_address,
				bp_type_name[bp_type],
				bp_actual_length[bp_len], vmdb->skip_counter[i]
				);
		}
		CLI_PRINT("\n");
	}
	return 0;
}

int vmdb_cli_single_step_enable(unsigned argc, char *args[])
{
	guest_cpu_handle_t gcpu;
	boolean_t enable;
	boolean_t apply_to_all;

	if (argc < 3) {
		return -1;
	}

	gcpu = vmdb_cli_locate_gcpu(args[1], &apply_to_all);
	if (NULL == gcpu) {
		CLI_PRINT("Invalid Guest %s\n", args[1]);
		return -1;
	}

	if (CLI_IS_SUBSTR("enable", args[2])) {
		enable = TRUE;
	} else if (CLI_IS_SUBSTR("disable", args[2])) {
		enable = FALSE;
	} else {
		return -1;
	}

	vmdb_single_step_enable(gcpu, enable);

	if (apply_to_all) {
		vmdb_remote_single_step_enable(gcpu, enable);
	}

	return 0;
}

int vmdb_cli_debug_attach(unsigned argc, char *args[])
{
	guest_cpu_handle_t gcpu;
	boolean_t apply_to_all;
	guest_id_t guest_id;

	if (argc < 2) {
		return -1;
	}

	gcpu = vmdb_cli_locate_gcpu(args[1], &apply_to_all);
	if (NULL == gcpu) {
		CLI_PRINT("Invalid Guest %s\n", args[1]);
		return -1;
	}

	guest_id = mon_guest_vcpu(gcpu)->guest_id;

	vmdb_guest_initialize(guest_id);

	vmdb_thread_attach(gcpu);
	if (apply_to_all) {
		vmdb_remote_thread_attach(gcpu);
	}

	return 0;
}

int vmdb_cli_debug_detach(unsigned argc, char *args[])
{
	guest_cpu_handle_t gcpu;
	boolean_t apply_to_all;

	if (argc < 2) {
		return -1;
	}

	gcpu = vmdb_cli_locate_gcpu(args[1], &apply_to_all);
	if (NULL == gcpu) {
		CLI_PRINT("Invalid Guest %s\n", args[1]);
		return -1;
	}

	vmdb_thread_detach(gcpu);
	if (apply_to_all) {
		vmdb_remote_thread_detach(gcpu);
	}

	return 0;
}

void vmdb_cli_init(void)
{
	cli_add_command
		(vmdb_cli_breakpoint_add,
		"dbg breakpoint add",
		"add breakpoint in guest",
		"<[*]guest> <lin addr> <e(xe)|w(rite)|i(o)|r(w)> <len> [skip count]",
		CLI_ACCESS_LEVEL_USER);
	cli_add_command
		(vmdb_cli_breakpoint_delete,
		"dbg breakpoint delete",
		"delete breakpoint in guest",
		"<[*]guest> <lin addr>", CLI_ACCESS_LEVEL_USER);
	cli_add_command
		(vmdb_cli_single_step_enable,
		"dbg singlestep",
		"guest single step on/off",
		"<[*]guest> <enable/disable>", CLI_ACCESS_LEVEL_USER);
	cli_add_command
		(vmdb_cli_breakpoint_show,
		"dbg show",
		"show breakpoints set for the guest",
		"<guest>", CLI_ACCESS_LEVEL_USER);
	cli_add_command
		(vmdb_cli_debug_attach,
		"dbg attach",
		"guest debuger attach", "<[*]guest>", CLI_ACCESS_LEVEL_USER);
	cli_add_command
		(vmdb_cli_debug_detach,
		"dbg detach",
		"guest debuger detach", "<[*]guest>", CLI_ACCESS_LEVEL_USER);
}

#endif                          /* DEBUG */
