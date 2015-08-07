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

#ifndef _LOCAL_APIC_H
#define _LOCAL_APIC_H

#include "mon_defs.h"
#include "ia32_defs.h"

#define CPUID_X2APIC_SUPPORTED_BIT  21

#define LOCAL_APIC_REG_MSR_BASE              IA32_MSR_x2APIC_BASE

typedef enum {
	LOCAL_APIC_ID_REG = 2,
	LOCAL_APIC_VERSION_REG = 3,
	LOCAL_APIC_TASK_PRIORITY_REG = 8,               /* TPR */
	LOCAL_APIC_PROCESSOR_PRIORITY_REG = 0xA,        /* PPR */
	LOCAL_APIC_EOI_REG = 0xB,
	LOCAL_APIC_LOGICAL_DESTINATION_REG = 0xD,
	LOCAL_APIC_DESTINATION_FORMAT_REG = 0xE,        /* !!not supported in x2APIC mode!! */
	LOCAL_APIC_SPURIOUS_INTR_VECTOR_REG = 0xF,
	LOCAL_APIC_IN_SERVICE_REG = 0x10,               /* ISR 8 sequential registers */
	LOCAL_APIC_TRIGGER_MODE_REG = 0x18,             /* TMR 8 sequential registers */
	LOCAL_APIC_INTERRUPT_REQUEST_REG = 0x20,        /* IRR 8 sequential registers */
	LOCAL_APIC_ERROR_STATUS_REG = 0x28,
	LOCAL_APIC_INTERRUPT_COMMAND_REG = 0x30,        /* 64-bits */
	LOCAL_APIC_INTERRUPT_COMMAND_HI_REG = 0x31,
	LOCAL_APIC_LVT_TIMER_REG = 0x32,
	LOCAL_APIC_LVT_THERMAL_SENSOR_REG = 0x33,
	LOCAL_APIC_LVT_PERF_MONITORING_REG = 0x34,
	LOCAL_APIC_LVT_LINT0_REG = 0x35,
	LOCAL_APIC_LVT_LINT1_REG = 0x36,
	LOCAL_APIC_LVT_ERROR_REG = 0x37,
	LOCAL_APIC_INITIAL_COUNTER_REG = 0x38,
	LOCAL_APIC_CURRENT_COUNTER_REG = 0x39,
	LOCAL_APIC_DIVIDE_CONFIGURATION_REG = 0x3E,
	LOCAL_APIC_SELF_IPI_REG = 0x40       /* valid for x2APIC mode only!! */
} local_apic_reg_id_t;

typedef enum {
	LOCAL_APIC_DISABLED,
	LOCAL_APIC_ENABLED,
	LOCAL_APIC_X2_ENABLED
} local_apic_mode_t;

typedef enum {
	LOCAL_APIC_NOERROR = 0,
	LOCAL_APIC_ACCESS_WHILE_DISABLED_ERROR = 1,
	LOCAL_APIC_X2_NOT_SUPPORTED = 2,
	LOCAL_APIC_INVALID_REGISTER_ERROR = 3,
	LOCAL_APIC_RESERVED_REGISTER_ERROR = 4,
	LOCAL_APIC_INVALID_RW_ACCESS_ERROR = 5,
	LOCAL_APIC_REGISTER_MMIO_ACCESS_DISABLED_ERROR = 6,
	LOCAL_APIC_REGISTER_MSR_ACCESS_DISABLED_ERROR = 7,
	LOCAL_APIC_REGISTER_ACCESS_LENGTH_ERROR = 8,
} local_apic_errno_t;

/*----------------------------------------------------------------
 *
 * IPI-related
 *
 *---------------------------------------------------------------- */

typedef ia32_icr_low_t local_apic_interrupt_command_register_low_t;
typedef ia32_icr_high_t local_apic_interrupt_command_register_high_t;
typedef ia32_icr_t local_apic_interrupt_command_register_t;

typedef enum {
	IPI_DELIVERY_MODE_FIXED = LOCAL_APIC_DELIVERY_MODE_FIXED,
	IPI_DELIVERY_MODE_LOWEST_PRIORITY =
		LOCAL_APIC_DELIVERY_MODE_LOWEST_PRIORITY,
	IPI_DELIVERY_MODE_SMI = LOCAL_APIC_DELIVERY_MODE_SMI,
	IPI_DELIVERY_REMOTE_READ = LOCAL_APIC_DELIVERY_MODE_REMOTE_READ,
	IPI_DELIVERY_MODE_NMI = LOCAL_APIC_DELIVERY_MODE_NMI,
	IPI_DELIVERY_MODE_INIT = LOCAL_APIC_DELIVERY_MODE_INIT,
	IPI_DELIVERY_MODE_START_UP = LOCAL_APIC_DELIVERY_MODE_SIPI
} local_apic_ipi_delivery_mode_t;

typedef enum {
	IPI_DESTINATION_MODE_PHYSICAL = LOCAL_APIC_DESTINATION_MODE_PHYSICAL,
	IPI_DESTINATION_MODE_LOGICAL = LOCAL_APIC_DESTINATION_MODE_LOGICAL
} local_apic_ipi_destination_mode_t;

typedef enum {
	IPI_DELIVERY_STATUS_IDLE = LOCAL_APIC_DELIVERY_STATUS_IDLE,
	IPI_DELIVERY_STATUS_SEND_PENDING =
		LOCAL_APIC_DELIVERY_STATUS_SEND_PENDING
} local_apic_ipi_delivery_status_t;

typedef enum {
	IPI_DELIVERY_LEVEL_DEASSERT = LOCAL_APIC_DELIVERY_LEVEL_DEASSERT,
	IPI_DELIVERY_LEVEL_ASSERT = LOCAL_APIC_DELIVERY_LEVEL_ASSERT
} local_apic_ipi_level_t;

typedef enum _LOCAL_APIC_IPI_TRIGGER_MODE {
	IPI_DELIVERY_TRIGGER_MODE_EDGE = LOCAL_APIC_TRIGGER_MODE_EDGE,
	IPI_DELIVERY_TRIGGER_MODE_LEVEL = LOCAL_APIC_TRIGGER_MODE_LEVEL
} local_apic_ipi_trigger_mode_t;

typedef enum {
	IPI_DST_NO_SHORTHAND = LOCAL_APIC_BROADCAST_MODE_SPECIFY_CPU,
	IPI_DST_SELF = LOCAL_APIC_BROADCAST_MODE_SELF,
	IPI_DST_ALL_INCLUDING_SELF =
		LOCAL_APIC_BROADCAST_MODE_ALL_INCLUDING_SELF,
	IPI_DST_ALL_EXCLUDING_SELF =
		LOCAL_APIC_BROADCAST_MODE_ALL_EXCLUDING_SELF,
	/* For mon, not for APIC purpose, it behaves like
	 * IPI_DST_ALL_EXCLUDING_SELF */
	IPI_DST_CORE_ID_BITMAP = 0xFFF
} local_apic_ipi_destination_shorthand_t;

/*------------------------------------------------------------------------
 *
 * API
 *
 *------------------------------------------------------------------------ */

/* must be called on BSP before any other function */
boolean_t local_apic_init(uint16_t num_of_cpus);

/* must be called on each host cpu (after local_apic_init()) */
boolean_t local_apic_cpu_init(void);

/* update lapic cpu id. (must be called after S3 or Local APIC host base was
 * changed per cpu) */
boolean_t update_lapic_cpu_id(void);

/* must be called when Local APIC host base was changed per cpu */
void local_apic_setup_changed(void);

boolean_t validate_APIC_BASE_change(uint64_t);

/* return value for current host cpu */
address_t lapic_base_address_hpa(void);
address_t lapic_base_address_hva(void);

/* control host Local APIC per cpu */
local_apic_mode_t local_apic_get_mode(void);
local_apic_errno_t local_apic_set_mode(local_apic_mode_t mode);

local_apic_errno_t local_apic_access(local_apic_reg_id_t reg_id,
				     rw_access_t rw_access,
				     void *data,
				     int32_t bytes_to_deliver,
				     int32_t *p_bytes_delivered);

boolean_t local_apic_send_ipi(
	local_apic_ipi_destination_shorthand_t dst_shorthand,
	uint8_t dst,
	local_apic_ipi_destination_mode_t dst_mode,
	local_apic_ipi_delivery_mode_t delivery_mode,
	uint8_t vector,
	local_apic_ipi_level_t level,
	local_apic_ipi_trigger_mode_t trigger_mode);

/* returns current Local APIC ID suitable for IPIs with FIXED destination
 * mode */
uint8_t local_apic_get_current_id(void);

/* this function should not return */
void local_apic_send_init_to_self(void);

void local_apic_send_init(cpu_id_t dst);

/* returns TRUE is any of Local APIC modes is SW-enabled
 * If Local APIC is either HW disabled or SW-disabled, return FALSE */
boolean_t local_apic_is_sw_enabled(void);

/* Test for ready-to-be-accepted fixed interrupts. */
boolean_t local_apic_is_ready_interrupt_exist(void);

#endif
