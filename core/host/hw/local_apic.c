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

#include "local_apic.h"
#include "em64t_defs.h"
#include "hw_utils.h"
#include "mon_dbg.h"
#include "host_memory_manager_api.h"
#include "memory_allocator.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(LOCAL_APIC_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(LOCAL_APIC_C, __condition)


#define STRINGIFY(x)     #x

/*****************************************************************************
*                       Local Macros and Types
*****************************************************************************/
typedef struct local_apic_per_cpu_data_t {
	address_t		lapic_base_address_hpa;
	address_t		lapic_base_address_hva;

	local_apic_mode_t	lapic_mode;
	cpu_id_t		lapic_cpu_id;
	uint8_t			pad[2];

	void (*lapic_read_reg)(const struct local_apic_per_cpu_data_t *data,
			       local_apic_reg_id_t reg_id, void *p_data,
			       unsigned bytes);
	void (*lapic_write_reg)(const struct local_apic_per_cpu_data_t *data,
				local_apic_reg_id_t reg_id, void *p_data,
				unsigned bytes);
} local_apic_per_cpu_data_t;

/* array per hw cpu */
static local_apic_per_cpu_data_t *lapic_cpu_data;

#define IA32_APIC_BASE_MSR_BSP              0x100
#define IA32_APIC_BASE_MSR_X2APIC_ENABLE    0x400
#define IA32_APIC_BASE_MSR_GLOBAL_ENABLE    0x800
#define IA32_APIC_BASE_MSR_PHY_ADDRESS      0xFFFFFF000

/* SW enable/disable flag - bit 8 in the Spurious Vector Register */
#define IA32_APIC_SW_ENABLE_BIT_IDX         8

#define ACCESS_RO   READ_ACCESS
#define ACCESS_WO   WRITE_ACCESS
#define ACCESS_RW   READ_WRITE_ACCESS

#define MODE_NO   0
#define MODE_MMIO 1
#define MODE_MSR  2
#define MODE_BOTH (MODE_MMIO | MODE_MSR)

typedef struct {
	uint32_t	offset;
	uint8_t		access;
	uint8_t		modes;
	uint16_t	x2_size;
	char		*name;
} local_apic_register_t;

#define LOCAL_APIC_REG_MSR(__reg_id)        (LOCAL_APIC_REG_MSR_BASE + \
					     (__reg_id))
#define LOCAL_APIC_REG_ADDRESS(lapic_data, __reg_id)    \
	((lapic_data)->lapic_base_address_hva + ((__reg_id) << 4))

#define GET_OTHER_LAPIC(__cpu_id)  (lapic_cpu_data + (__cpu_id))
#define GET_CPU_LAPIC()         GET_OTHER_LAPIC(hw_cpu_id())

/*****************************************************************************
*               Forward Declarations for Local Functions
*****************************************************************************/
static void lapic_read_reg_msr(const local_apic_per_cpu_data_t *data,
			       local_apic_reg_id_t reg_id,
			       void *p_data,
			       unsigned bytes);
static void lapic_write_reg_msr(const local_apic_per_cpu_data_t *data,
				local_apic_reg_id_t reg_id,
				void *p_data,
				unsigned bytes);
static local_apic_mode_t local_apic_discover_mode(void);
static void lapic_read_reg_mmio(const local_apic_per_cpu_data_t *data,
				local_apic_reg_id_t reg_id,
				void *p_data,
				unsigned not_used);
static void lapic_write_reg_mmio(const local_apic_per_cpu_data_t *data,
				 local_apic_reg_id_t reg_id,
				 void *p_data,
				 unsigned not_used);
static void lapic_fill_current_mode(local_apic_per_cpu_data_t *data);
static boolean_t
local_apic_ipi_verify_params(
	local_apic_ipi_destination_shorthand_t dst_shorthand,
	local_apic_ipi_delivery_mode_t delivery_mode,
	uint8_t vector,
	local_apic_ipi_level_t level,
	local_apic_ipi_trigger_mode_t trigger_mode);

/*****************************************************************************
*                       Code
*****************************************************************************/
/*---------------------------------------------------------------------------*
*  FUNCTION : local_apic_is_x2apic_supported()
*  PURPOSE  : Checks if x2APIC mode is supported by CPU
*  ARGUMENTS: void
*  RETURNS  : TRUE if supported, FALSE otherwise
*---------------------------------------------------------------------------*/
boolean_t local_apic_is_x2apic_supported(void)
{
	cpuid_params_t cpuid_params;

	cpuid_params.m_rax = 1;
	hw_cpuid(&cpuid_params);
	return BIT_GET64(cpuid_params.m_rcx, CPUID_X2APIC_SUPPORTED_BIT) != 0;
}

/*---------------------------------------------------------------------------*
*  FUNCTION : local_apic_discover_mode()
*  PURPOSE  : Checks Local APIC current mode
*  ARGUMENTS: void
*  RETURNS  : local_apic_mode_t mode discovered
*---------------------------------------------------------------------------*/
local_apic_mode_t local_apic_discover_mode(void)
{
	uint64_t value = hw_read_msr(IA32_MSR_APIC_BASE);
	local_apic_mode_t mode;

	if (0 != BITMAP_GET(value, IA32_APIC_BASE_MSR_X2APIC_ENABLE)) {
		mode = LOCAL_APIC_X2_ENABLED;
	} else if (0 != BITMAP_GET(value, IA32_APIC_BASE_MSR_GLOBAL_ENABLE)) {
		mode = LOCAL_APIC_ENABLED;
	} else {
		mode = LOCAL_APIC_DISABLED;
	}
	return mode;
}

static void lapic_fill_current_mode(local_apic_per_cpu_data_t *lapic_data)
{
	lapic_data->lapic_mode = local_apic_discover_mode();

	switch (lapic_data->lapic_mode) {
	case LOCAL_APIC_X2_ENABLED:
		lapic_data->lapic_read_reg = lapic_read_reg_msr;
		lapic_data->lapic_write_reg = lapic_write_reg_msr;
		break;
	case LOCAL_APIC_ENABLED:
		/* SW-disabled is HW-enabled also */
		lapic_data->lapic_read_reg = lapic_read_reg_mmio;
		lapic_data->lapic_write_reg = lapic_write_reg_mmio;
		break;

	case LOCAL_APIC_DISABLED:
	default:
		MON_LOG(mask_anonymous, level_trace,
			"Setting Local APIC into HW-disabled state on CPU#%d\n",
			hw_cpu_id());
		MON_ASSERT(FALSE);
	}
}

void lapic_read_reg_mmio(const local_apic_per_cpu_data_t *data,
			 local_apic_reg_id_t reg_id, void *p_data,
			 unsigned not_used UNUSED)
{
	*(uint32_t *)p_data =
		*(volatile uint32_t *)LOCAL_APIC_REG_ADDRESS(data, reg_id);
}

void lapic_write_reg_mmio(const local_apic_per_cpu_data_t *data,
			  local_apic_reg_id_t reg_id, void *p_data,
			  unsigned not_used UNUSED)
{
	*(volatile uint32_t *)LOCAL_APIC_REG_ADDRESS(data, reg_id) =
		*(uint32_t *)p_data;
}

void lapic_read_reg_msr(const local_apic_per_cpu_data_t *data UNUSED,
			local_apic_reg_id_t reg_id, void *p_data,
			unsigned bytes)
{
	uint64_t value;

	value = hw_read_msr(LOCAL_APIC_REG_MSR(reg_id));
	if (4 == bytes) {
		*(uint32_t *)p_data = (uint32_t)value;
	} else {
		*(uint64_t *)p_data = value;
	}
}

void lapic_write_reg_msr(const local_apic_per_cpu_data_t *data UNUSED,
			 local_apic_reg_id_t reg_id,
			 void *p_data,
			 unsigned bytes)
{
	if (4 == bytes) {
		hw_write_msr(LOCAL_APIC_REG_MSR(reg_id), *(uint32_t *)p_data);
	} else {
		hw_write_msr(LOCAL_APIC_REG_MSR(reg_id), *(uint64_t *)p_data);
	}
}

boolean_t validate_APIC_BASE_change(uint64_t msr_value)
{
	local_apic_per_cpu_data_t *lapic_data = GET_CPU_LAPIC();
	uint64_t physical_address_size_mask =
		~((((uint64_t)1) << ((uint8_t)hw_read_address_size())) - 1);
	uint64_t bit_9_mask = (uint64_t)1 << 9;
	uint64_t last_byte_mask = 0xff;
	uint64_t reserved_bits_mask;

	if (local_apic_is_x2apic_supported()) {
		reserved_bits_mask =
			bit_9_mask + last_byte_mask +
			~((((uint64_t)1) << 36) - 1)
			+ IA32_APIC_BASE_MSR_BSP;
	} else {
		reserved_bits_mask =
			physical_address_size_mask + bit_9_mask +
			last_byte_mask +
			IA32_APIC_BASE_MSR_X2APIC_ENABLE +
			IA32_APIC_BASE_MSR_BSP;
	}

	/* if reserved bits are being changed, return FALSE, so,the caller will
	 * inject gp. */
	if ((hw_read_msr(IA32_MSR_APIC_BASE) & reserved_bits_mask) !=
	    (msr_value & reserved_bits_mask)) {
		return FALSE;
	}

	/* if the current mode is xAPIC, the legal target modes are xAPIC, x2APIC
	 * and disabled state.
	 * let's reject any change to disabled state since MON relies on xAPIC or
	 * x2APIC */

	/* if the current mode is x2APIC, the legal target modes are x2APIC and
	 * disabled state.
	 * let's reject any change to disabled state for the same reason */
	if (lapic_data->lapic_mode == LOCAL_APIC_X2_ENABLED) {
		if (!(BITMAP_GET(msr_value,
			      IA32_APIC_BASE_MSR_X2APIC_ENABLE)) ||
		    !(BITMAP_GET(msr_value,
			      IA32_APIC_BASE_MSR_GLOBAL_ENABLE))) {
			/* inject gp instead of deadloop--recommended by validation guys */
			return FALSE;
		}
	} else {
		if (lapic_data->lapic_mode != LOCAL_APIC_ENABLED) {
			/* inject gp instead of deadloop--recommended by validation guys */
			return FALSE;
		}

		if (!(BITMAP_GET(msr_value,
			      IA32_APIC_BASE_MSR_GLOBAL_ENABLE))) {
			/* inject gp instead of deadloop--recommended by validation guys */
			return FALSE;
		}
	}

	return TRUE;
}

void local_apic_setup_changed(void)
{
	local_apic_per_cpu_data_t *lapic_data = GET_CPU_LAPIC();
	boolean_t result;

	lapic_data->lapic_base_address_hpa = hw_read_msr(IA32_MSR_APIC_BASE);
	lapic_data->lapic_base_address_hpa =
		ALIGN_BACKWARD(lapic_data->lapic_base_address_hpa,
			PAGE_4KB_SIZE);

	lapic_fill_current_mode(lapic_data);

	if (lapic_data->lapic_mode != LOCAL_APIC_X2_ENABLED) {
		result = hmm_map_uc_physical_page(
			lapic_data->lapic_base_address_hpa,
			TRUE /* writable */,
			FALSE /* not_executable */,
			FALSE /* do synch with other CPUs to avoid loop back */,
			&(lapic_data->lapic_base_address_hva));
		MON_ASSERT(result);
	}

	MON_LOG(mask_anonymous, level_trace, "CPU#%d: local apic base = %p\r\n",
		hw_cpu_id(), lapic_data->lapic_base_address_hpa);

	/* We do not unmap previous mapping, so old pages will remain mapped
	 * uncachable */
}

/* update lapic cpu id. (must be called after S3 or Local APIC host base was
 * changed per cpu) */
boolean_t update_lapic_cpu_id(void)
{
	local_apic_per_cpu_data_t *lapic_data = GET_CPU_LAPIC();

	MON_ASSERT(lapic_data);

	lapic_data->lapic_cpu_id = local_apic_get_current_id();

	return TRUE;
}

boolean_t local_apic_cpu_init(void)
{
	local_apic_setup_changed();

	update_lapic_cpu_id();

	return TRUE;
}

boolean_t local_apic_init(uint16_t num_of_cpus)
{
	uint32_t chunk_size = num_of_cpus * sizeof(local_apic_per_cpu_data_t);

	if (lapic_cpu_data == 0) {
		lapic_cpu_data = mon_page_alloc(PAGE_ROUNDUP(chunk_size));

		MON_ASSERT(lapic_cpu_data != NULL);

		mon_memset(lapic_cpu_data, 0, chunk_size);
	}

	return TRUE;
}

/*****************************************************************************
*                       Specific IPI support
*****************************************************************************/
static void local_apic_wait_for_ipi_delivery(local_apic_per_cpu_data_t *
					     lapic_data)
{
	local_apic_interrupt_command_register_low_t icr_low;

	/* delivery status bit does not exist for x2APIC mode */
	if (lapic_data->lapic_mode != LOCAL_APIC_X2_ENABLED) {
		while (TRUE) {
			lapic_data->lapic_read_reg(lapic_data,
				LOCAL_APIC_INTERRUPT_COMMAND_REG,
				&icr_low.uint32, sizeof(icr_low.uint32));

			if (IPI_DELIVERY_STATUS_IDLE ==
			    icr_low.bits.delivery_status) {
				break;
			}
		}
	}
}

boolean_t
local_apic_ipi_verify_params(
	local_apic_ipi_destination_shorthand_t dst_shorthand,
	local_apic_ipi_delivery_mode_t delivery_mode,
	uint8_t vector,
	local_apic_ipi_level_t level,
	local_apic_ipi_trigger_mode_t trigger_mode)
{
	boolean_t success = TRUE;

	if (dst_shorthand == IPI_DST_SELF &&
	    (delivery_mode == IPI_DELIVERY_MODE_LOWEST_PRIORITY
	     || delivery_mode == IPI_DELIVERY_MODE_NMI
	     || delivery_mode == IPI_DELIVERY_MODE_INIT
	     || delivery_mode == IPI_DELIVERY_MODE_SMI
	     || delivery_mode == IPI_DELIVERY_MODE_START_UP)) {
		success = FALSE;
		MON_LOG(mask_anonymous, level_trace,
			"IPI params verification failed: dst_shorthand =="
			" IPI_DST_SELF && delivery_mode=="
			STRINGIFY(delivery_mode) "\r\n");
	}

	if (dst_shorthand == IPI_DST_ALL_INCLUDING_SELF &&
	    (delivery_mode == IPI_DELIVERY_MODE_LOWEST_PRIORITY
	     || delivery_mode == IPI_DELIVERY_MODE_NMI
	     || delivery_mode == IPI_DELIVERY_MODE_INIT
	     || delivery_mode == IPI_DELIVERY_MODE_SMI
	     || delivery_mode == IPI_DELIVERY_MODE_START_UP)) {
		success = FALSE;
		MON_LOG(mask_anonymous, level_trace,
			"IPI params verification failed: dst_shorthand =="
			" IPI_DST_ALL_INCLUDING_SELF && delivery_mode=="
			STRINGIFY(delivery_mode) "\r\n");
	}

	if (trigger_mode == IPI_DELIVERY_TRIGGER_MODE_LEVEL &&
	    (delivery_mode == IPI_DELIVERY_MODE_SMI
	     || delivery_mode == IPI_DELIVERY_MODE_START_UP)) {
		success = FALSE;
		MON_LOG(mask_anonymous, level_trace,
			"IPI params verification failed: trigger_mode =="
			" IPI_DELIVERY_TRIGGER_MODE_LEVEL && delivery_mode=="
			STRINGIFY(delivery_mode) "\r\n");
	}

	if ((delivery_mode == IPI_DELIVERY_MODE_SMI
	     || delivery_mode == IPI_DELIVERY_MODE_INIT)
	    && vector != 0) {
		success = FALSE;
		MON_LOG(mask_anonymous, level_trace,
			"IPI params verification failed: delivery_mode == "
			STRINGIFY(delivery_mode) ", vector must be zero\r\n");
	}

	/* init level de-assert */
	if (delivery_mode == IPI_DELIVERY_MODE_INIT
	    && level == IPI_DELIVERY_LEVEL_DEASSERT
	    && trigger_mode == IPI_DELIVERY_TRIGGER_MODE_LEVEL
	    && dst_shorthand != IPI_DST_ALL_INCLUDING_SELF) {
		success = FALSE;
		MON_LOG(mask_anonymous,
			level_trace,
			"IPI params verification failed: init level deassert ipi"
			" - destination must be IPI_DST_ALL_INCLUDING_SELF\r\n");
	}

	/* level must be assert for ipis other than init level de-assert */
	if ((delivery_mode != IPI_DELIVERY_MODE_INIT
	     || trigger_mode != IPI_DELIVERY_TRIGGER_MODE_LEVEL)
	    && level == IPI_DELIVERY_LEVEL_DEASSERT) {
		success = FALSE;
		MON_LOG(mask_anonymous, level_trace,
			"IPI params verification failed: level must be ASSERT"
			" for all ipis except init level deassert ipi\r\n");
	}

	return success;
}

boolean_t
local_apic_send_ipi(local_apic_ipi_destination_shorthand_t dst_shorthand,
		    uint8_t dst,
		    local_apic_ipi_destination_mode_t dst_mode,
		    local_apic_ipi_delivery_mode_t delivery_mode,
		    uint8_t vector,
		    local_apic_ipi_level_t level,
		    local_apic_ipi_trigger_mode_t trigger_mode)
{
	local_apic_interrupt_command_register_t icr;
	uint32_t icr_high_save;
	local_apic_per_cpu_data_t *lapic_data = GET_CPU_LAPIC();
	boolean_t params_valid = FALSE;

	params_valid =
		local_apic_ipi_verify_params(dst_shorthand,
			delivery_mode,
			vector,
			level,
			trigger_mode);

	if (!params_valid) {
		return FALSE;
	}

	/* wait for IPI in progress to finish */
	local_apic_wait_for_ipi_delivery(lapic_data);

	icr.hi_dword.uint32 = 0;

	if (IPI_DST_NO_SHORTHAND == dst_shorthand) {
		local_apic_per_cpu_data_t *dst_lapic_data =
			GET_OTHER_LAPIC(dst);
		icr.hi_dword.bits.destination = dst_lapic_data->lapic_cpu_id;
	} else if (IPI_DST_SELF == dst_shorthand) {
		icr.hi_dword.bits.destination = lapic_data->lapic_cpu_id;
	} else {
		icr.hi_dword.bits.destination = dst;
	}

	if (lapic_data->lapic_mode == LOCAL_APIC_X2_ENABLED) {
		icr.hi_dword.uint32 = (uint32_t)icr.hi_dword.bits.destination;
	}

	icr.lo_dword.uint32 = 0;
	icr.lo_dword.bits.destination_shorthand = dst_shorthand;
	icr.lo_dword.bits.destination_mode = dst_mode;
	icr.lo_dword.bits.delivery_mode = delivery_mode;
	icr.lo_dword.bits.vector = vector;
	icr.lo_dword.bits.level = level;
	icr.lo_dword.bits.trigger_mode = trigger_mode;

	if (LOCAL_APIC_X2_ENABLED == lapic_data->lapic_mode) {
		lapic_data->lapic_write_reg(lapic_data,
			LOCAL_APIC_INTERRUPT_COMMAND_REG,
			&icr, sizeof(icr));

		/* wait for IPI in progress to finish */
		local_apic_wait_for_ipi_delivery(lapic_data);
	} else {
		/* save previous uint32: if guest is switched in the middle of IPI
		 * setup,
		 * need to restore the guest IPI destination uint32 */
		lapic_data->lapic_read_reg(lapic_data,
			LOCAL_APIC_INTERRUPT_COMMAND_HI_REG,
			&icr_high_save, sizeof(icr_high_save));

		/* write new destination */
		lapic_data->lapic_write_reg(lapic_data,
			LOCAL_APIC_INTERRUPT_COMMAND_HI_REG,
			&icr.hi_dword.uint32,
			sizeof(icr.hi_dword.uint32));

		/* send IPI */
		lapic_data->lapic_write_reg(lapic_data,
			LOCAL_APIC_INTERRUPT_COMMAND_REG,
			&icr.lo_dword.uint32,
			sizeof(icr.lo_dword.uint32));

		/* wait for IPI in progress to finish */
		local_apic_wait_for_ipi_delivery(lapic_data);

		/* restore guest IPI destination */
		lapic_data->lapic_write_reg(lapic_data,
			LOCAL_APIC_INTERRUPT_COMMAND_HI_REG,
			&icr_high_save, sizeof(icr_high_save));
	}

	return TRUE;
}

uint8_t local_apic_get_current_id(void)
{
	local_apic_per_cpu_data_t *lapic_data = GET_CPU_LAPIC();
	uint32_t local_apic_id = 0;

	lapic_data->lapic_read_reg(lapic_data,
		LOCAL_APIC_ID_REG,
		&local_apic_id, sizeof(local_apic_id));

	if (lapic_data->lapic_mode != LOCAL_APIC_X2_ENABLED) {
		return (uint8_t)(local_apic_id >>
				 LOCAL_APIC_ID_LOW_RESERVED_BITS_COUNT);
	} else {
		return (uint8_t)(local_apic_id);
	}
}

void local_apic_send_init(cpu_id_t dst)
{
	local_apic_send_ipi(IPI_DST_NO_SHORTHAND, (uint8_t)dst,
		IPI_DESTINATION_MODE_PHYSICAL,
		IPI_DELIVERY_MODE_INIT,
		0,
		IPI_DELIVERY_LEVEL_ASSERT,
		IPI_DELIVERY_TRIGGER_MODE_EDGE);
}

#ifdef DEBUG

local_apic_mode_t local_apic_get_mode(void)
{
	return GET_CPU_LAPIC()->lapic_mode;
}

boolean_t local_apic_is_sw_enabled(void)
{
	local_apic_per_cpu_data_t *lapic_data = GET_CPU_LAPIC();
	uint32_t spurious_vector_reg_value = 0;

	if (LOCAL_APIC_DISABLED == lapic_data->lapic_mode) {
		return FALSE;
	}

	/* now read the spurios register */
	lapic_data->lapic_read_reg(lapic_data,
		LOCAL_APIC_SPURIOUS_INTR_VECTOR_REG,
		&spurious_vector_reg_value,
		sizeof(spurious_vector_reg_value));

	return BIT_GET(spurious_vector_reg_value,
		IA32_APIC_SW_ENABLE_BIT_IDX) ? TRUE : FALSE;
}

/* find highest set bit in 256bit reg (8 sequential regs 32bit each). Return
 * UINT32_ALL_ONES if no 1s found. */
static uint32_t find_highest_bit_in_reg(local_apic_per_cpu_data_t *lapic_data,
					local_apic_reg_id_t reg_id,
					uint32_t reg_size_32bit_units)
{
	uint32_t subreg_idx;
	uint32_t subreg_value;
	uint32_t bit_idx;

	for (subreg_idx = reg_size_32bit_units; subreg_idx > 0; --subreg_idx) {
		lapic_data->lapic_read_reg(lapic_data,
			reg_id + subreg_idx - 1,
			&subreg_value, sizeof(subreg_value));

		if (0 == subreg_value) {
			continue;
		}

		/* find highest set bit */
		hw_scan_bit_backward(&bit_idx, subreg_value);

		return (subreg_idx - 1) * sizeof(subreg_value) * 8 + bit_idx;
	}

	/* if we are here - not found */
	return UINT32_ALL_ONES;
}

/* Find maximum interrupt request register priority
 * IRR priority is a upper 4bit value of the highest interrupt bit set to 1 */
static uint32_t local_apic_get_irr_priority(
	local_apic_per_cpu_data_t *lapic_data)
{
	uint32_t irr_max_vector = find_highest_bit_in_reg(lapic_data,
		LOCAL_APIC_INTERRUPT_REQUEST_REG,
		8);

	return (irr_max_vector ==
		UINT32_ALL_ONES) ? 0 : ((irr_max_vector >> 4) & 0xF);
}

/* Processor Priority Register is a read-only register set to the highest
 * priority
 * class between ISR priority (priority of the highest ISR vector) and TPR
 * PPR = MAX( ISR, TPR ) */
static uint32_t local_apic_get_processor_priority(local_apic_per_cpu_data_t *
						  lapic_data)
{
	uint32_t ppr_value;

	lapic_data->lapic_read_reg(lapic_data,
		LOCAL_APIC_PROCESSOR_PRIORITY_REG,
		&ppr_value, sizeof(ppr_value));

	return (ppr_value >> 4) & 0xF;
}

/* Test for ready-to-be-accepted fixed interrupts.
 * Fixed interrupt is ready to be accepted if Local APIC will inject interrupt
 * when * SW will enable interrupts (assuming NMI is not in-service and no
 * other  execution-based interrupt blocking is active)
 * Fixed Interrupt is ready-to-be-accepted if
 * IRR_Priority > Processor_Priority */
boolean_t local_apic_is_ready_interrupt_exist(void)
{
	local_apic_per_cpu_data_t *lapic_data = GET_CPU_LAPIC();

	MON_ASSERT(local_apic_is_sw_enabled() == TRUE);

	return local_apic_get_irr_priority(lapic_data) >
	       local_apic_get_processor_priority(lapic_data);
}
#endif
