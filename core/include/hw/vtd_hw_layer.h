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

#ifndef _VTD_HW_LAYER
#define _VTD_HW_LAYER

#include "vtd.h"
#include "hw_utils.h"


typedef union {
	struct {
		uint32_t present:1;
		uint32_t reserved:11;
		uint32_t context_entry_table_ptr_low:20;
		uint32_t context_entry_table_ptr_high;
	} bits;
	uint64_t uint64;
} dma_remapping_root_entry_low_t;

typedef union {
	struct {
		uint64_t reserved;
	} bits;
	uint64_t uint64;
} dma_remapping_root_entry_high_t;

typedef struct {
	dma_remapping_root_entry_low_t	low;
	dma_remapping_root_entry_high_t high;
} dma_remapping_root_entry_t;

typedef enum {
	TRANSLATION_TYPE_UNTRANSLATED_ADDRESS_ONLY = 0,
	TRANSLATION_TYPE_ALL,
	TRANSLATION_TYPE_PASSTHROUGH_UNTRANSLATED_ADDRESS
} dma_remapping_translation_type_t;

typedef enum {
	DMA_REMAPPING_GAW_30 = 0,
	DMA_REMAPPING_GAW_39,
	DMA_REMAPPING_GAW_48,
	DMA_REMAPPING_GAW_57,
	DMA_REMAPPING_GAW_64
} dma_remapping_guest_address_width_t;

typedef union {
	struct {
		uint32_t present:1;
		uint32_t fault_processing_disable:1;
		uint32_t translation_type:2;
		uint32_t eviction_hint:1; /* 0 = default, 1 = eager eviction */
		uint32_t adress_locality_hint:1; /* 0 = default, 1 = requests have spatial locality */
		uint32_t reserved:6;
		uint32_t address_space_root_low:20;
		uint32_t address_space_root_high;
	} bits;
	uint64_t uint64;
} dma_remapping_context_entry_low_t;

typedef union {
	struct {
		uint32_t address_width:3;
		uint32_t available:4;
		uint32_t reserved0:1;
		uint32_t domain_id:16;
		uint32_t reserved1:8;
		uint32_t reserved;
	} bits;
	uint64_t uint64;
} dma_remapping_context_entry_high_t;

typedef struct {
	dma_remapping_context_entry_low_t	low;
	dma_remapping_context_entry_high_t	high;
} dma_remapping_context_entry_t;

typedef union {
	struct {
		uint32_t read:1;
		uint32_t write:1;
		uint32_t available0:5;
		uint32_t super_page:1;
		uint32_t available:3;
		uint32_t snoop_behavior:1; /* 0 = default, 1 = treat as snooped */
		uint32_t address_low:20;
		uint32_t address_high:30;
		uint32_t transient_mapping:1;
		uint32_t available1:1;
	} bits;
	uint64_t uint64;
} dma_remapping_page_table_entry_t;

typedef union {
	struct {
		uint32_t reserved:12;
		uint32_t fault_information_low:20;
		uint32_t fault_information_high;
	} bits;
	uint64_t uint64;
} dma_remapping_fault_record_low_t;

typedef union {
	struct {
		uint32_t source_id:16;
		uint32_t reserved0:16;
		uint32_t fault_reason:8;
		uint32_t reserved1:20;
		uint32_t address_type:2;
		uint32_t access_type:1; /* 0 = write, 1 = read */
		uint32_t reserved2:1;
	} bits;
	uint64_t uint64;
} dma_remapping_fault_record_high_t;

typedef struct {
	dma_remapping_fault_record_low_t	low;
	dma_remapping_fault_record_high_t	high;
} dma_remapping_fault_record_t;

typedef enum {
	SOURCE_ID_QUALIFIER_ALL = 0,
	SOURCE_ID_QUALIFIER_IGNORE_FUNCTION_MSB,
	SOURCE_ID_QUALIFIER_IGNORE_2_FUNCTION_MSB,
	SOURCE_ID_QUALIFIER_IGNORE_FUNCTION
} interrupt_remapping_source_id_qualifier_t;

typedef enum {
	SOURCE_ID_VALIDATION_NONE,
	SOURCE_ID_VALIDATION_AS_BDF,
	SOURCE_ID_VALIDATION_BUS_IN_RANGE
} interrupt_remapping_source_id_validation_type_t;

typedef union {
	struct {
		uint32_t present:1;
		uint32_t fault_processing_disable:1;
		uint32_t destination_mode:1; /* 0 = physical APIC ID, 1 = logical APIC ID */
		uint32_t redirection_hint:1; /* 0 = direct to CPU in destination id field, 1 = direct to one CPU from the group */
		uint32_t trigger_mode:1; /* 0 = edge, 1 = level */
		uint32_t delivery_mode:1;
		uint32_t available:4;
		uint32_t reserved:4;
		uint32_t destination_id;
	} bits;
	uint64_t uint64;
} interrupt_remapping_table_entry_low_t;

typedef union {
	struct {
		uint32_t source_id:16;
		uint32_t source_id_qualifier:2;
		uint32_t source_validation_type:2;
		uint32_t reserved0:12;
		uint32_t reserved1;
	} bits;
	uint64_t uint64;
} interrupt_remapping_table_entry_high_t;

typedef struct {
	interrupt_remapping_table_entry_low_t	low;
	interrupt_remapping_table_entry_high_t	high;
} interrupt_remapping_table_entry_t;

/* vtd registers */
#define VTD_VERSION_REGISTER_OFFSET                                    0x0000
#define VTD_CAPABILITY_REGISTER_OFFSET                                 0x0008
#define VTD_EXTENDED_CAPABILITY_REGISTER_OFFSET                        0x0010
#define VTD_GLOBAL_COMMAND_REGISTER_OFFSET                             0x0018
#define VTD_GLOBAL_STATUS_REGISTER_OFFSET                              0x001C
#define VTD_ROOT_ENTRY_TABLE_ADDRESS_REGISTER_OFFSET                   0x0020
#define VTD_CONTEXT_COMMAND_REGISTER_OFFSET                            0x0028
#define VTD_FAULT_STATUS_REGISTER_OFFSET                               0x0034
#define VTD_FAULT_EVENT_CONTROL_REGISTER_OFFSET                        0x0038
#define VTD_FAULT_EVENT_DATA_REGISTER_OFFSET                           0x003C
#define VTD_FAULT_EVENT_ADDRESS_REGISTER_OFFSET                        0x0040
#define VTD_FAULT_EVENT_ADDRESS_HIGH_REGISTER_OFFSET                   0x0044
#define VTD_ADVANCED_FAULT_LOG_REGISTER_OFFSET                         0x0058
#define VTD_PROTECTED_MEMORY_ENABLE_REGISTER_OFFSET                    0x0064
#define VTD_PROTECTED_LOW_MEMORY_BASE_REGISTER_OFFSET                  0x0068
#define VTD_PROTECTED_LOW_MEMORY_LIMIT_REGISTER_OFFSET                 0x006C
#define VTD_PROTECTED_HIGH_MEMORY_BASE_REGISTER_OFFSET                 0x0070
#define VTD_PROTECTED_HIGH_MEMORY_LIMIT_REGISTER_OFFSET                0x0078
#define VTD_INVALIDATION_QUEUE_HEAD_REGISTER_OFFSET                    0x0080
#define VTD_INVALIDATION_QUEUE_TAIL_REGISTER_OFFSET                    0x0088
#define VTD_INVALIDATION_QUEUE_ADDRESS_REGISTER_OFFSET                 0x0090
#define VTD_INVALIDATION_COMPLETION_STATUS_REGISTER_OFFSET             0x009C
#define VTD_INVALIDATION_COMPLETION_EVENT_CONTROL_REGISTER_OFFSET      0x00A0
#define VTD_INVALIDATION_COMPLETION_EVENT_DATA_REGISTER_OFFSET         0x00A4
#define VTD_INVALIDATION_COMPLETION_EVENT_ADDRESS_REGISTER_OFFSET      0x00A8
#define VTD_INVALIDATION_COMPLETION_EVENT_ADDRESS_HIGH_REGISTER_OFFSET 0x00AC
#define VTD_INTERRUPT_REMAPPING_TABLE_ADDRESS_REGISTER_OFFSET          0x00A0

/* definition for "Snoop Behavior" and "Transient Mapping" filds
 * in VT-d page tables */
#define VTD_SNPB_SNOOPED                1
#define VTD_NON_SNPB_SNOOPED            0
#define VTD_TRANSIENT_MAPPING           1
#define VTD_NON_TRANSIENT_MAPPING       0

typedef union {
	struct {
		uint32_t minor:4;
		uint32_t major:4;
		uint32_t reserved:24;
	} bits;
	uint32_t uint32;
} vtd_version_register_t;

typedef enum {
	VTD_NUMBER_OF_SUPPORTED_DOMAINS_16 = 0,
	VTD_NUMBER_OF_SUPPORTED_DOMAINS_64,
	VTD_NUMBER_OF_SUPPORTED_DOMAINS_256,
	VTD_NUMBER_OF_SUPPORTED_DOMAINS_1024,
	VTD_NUMBER_OF_SUPPORTED_DOMAINS_4K,
	VTD_NUMBER_OF_SUPPORTED_DOMAINS_16K,
	VTD_NUMBER_OF_SUPPORTED_DOMAINS_64K
} vtd_number_of_supported_domains_t;

#define VTD_SUPER_PAGE_SUPPORT_21(sp_support)           ((sp_support) & 0x0001)
#define VTD_SUPER_PAGE_SUPPORT_30(sp_support)           ((sp_support) & 0x0010)
#define VTD_SUPER_PAGE_SUPPORT_39(sp_support)           ((sp_support) & 0x0100)
#define VTD_SUPER_PAGE_SUPPORT_48(sp_support)           ((sp_support) & 0x1000)

typedef union {
	struct {
		uint32_t number_of_domains:3;
		uint32_t advanced_fault_log:1;
		uint32_t required_write_buffer_flush:1;
		uint32_t protected_low_memory_region:1; /* 0 = not supported, 1 = supported */
		uint32_t protected_high_memory_region:1; /* 0 = not supported, 1 = supported */
		uint32_t caching_mode:1;
		uint32_t adjusted_guest_address_width:5;
		uint32_t reserved0:3;
		uint32_t max_guest_address_width:6;
		uint32_t zero_length_read:1;
		uint32_t isochrony:1;
		uint32_t fault_recording_register_offset_low:8;
		uint32_t fault_recording_register_offset_high:2;
		uint32_t super_page_support:4;
		uint32_t reserved1:1;
		uint32_t page_selective_invalidation:1; /* 0 = not supported (only global * and domain), 1 = supported */
		uint32_t number_of_fault_recording_registers:8;
		uint32_t max_address_mask_value:6;
		uint32_t dma_write_draining:1; /* 0 = not supported, 1 = supported */
		uint32_t dma_read_draining:1; /* 0 = not supported, 1 = supported */
		uint32_t reserved:8;
	} bits;
	uint64_t uint64;
} vtd_capability_register_t;

typedef union {
	struct {
		uint32_t coherency:1; /* 0 = not-snooped, 1 = snooped */
		uint32_t queued_invalidation:1; /* 0 = not-supported, 1 = supported */
		uint32_t device_iotlb:1;
		uint32_t interrupt_remapping:1;
		uint32_t extended_interrupt_mode:1; /* 0 = 8-bit APIC id, 1 = 16-bit (x2APIC) */
		uint32_t caching_hints:1;
		uint32_t pass_through:1;
		uint32_t snoop_control:1;
		uint32_t iotlb_register_offset:10;
		uint32_t reserved0:2;
		uint32_t max_handle_mask_value:4;
		uint32_t reserved1:8;
		uint32_t reserved2;
	} bits;
	uint64_t uint64;
} vtd_extended_capability_register_t;

typedef union {
	struct {
		uint32_t reserved0:23;
		uint32_t compatibility_format_interrupt:1; /* 0 = block; 1 = pass-through */
		uint32_t set_interrupt_remap_table_ptr:1;
		uint32_t interrupt_remap_enable:1;
		uint32_t queued_invalidation_enable:1;
		uint32_t write_buffer_flush:1;
		uint32_t advanced_fault_log_enable:1;
		uint32_t set_advanced_fault_log_ptr:1;
		uint32_t set_root_table_ptr:1;
		uint32_t translation_enable:1;
	} bits;
	uint32_t uint32;
} vtd_global_command_register_t;

typedef union {
	struct {
		uint32_t reserved0:23;
		uint32_t compatibility_format_interrupt_status:1;
		uint32_t interrupt_remap_table_ptr_status:1;
		uint32_t interrupt_remap_enable_status:1;
		uint32_t queued_invalidation_enable_status:1;
		uint32_t write_buffer_flush_status:1;
		uint32_t advanced_fault_log_enable_status:1;
		uint32_t advanced_fault_log_ptr_status:1;
		uint32_t root_table_ptr_status:1;
		uint32_t translation_enable_status:1;
	} bits;
	uint32_t uint32;
} vtd_global_status_register_t;

typedef union {
	struct {
		uint32_t reserved:12;
		uint32_t address_low:20;
		uint32_t address_high;
	} bits;
	uint64_t uint64;
} vtd_root_entry_table_address_register_t;

typedef enum {
	VTD_CONTEXT_INV_GRANULARITY_GLOBAL = 0x1,
	VTD_CONTEXT_INV_GRANULARITY_DOMAIN = 0x2,
	VTD_CONTEXT_INV_GRANULARITY_DEVICE = 0x3
} vtd_context_inv_granularity_t;

typedef union {
	struct {
		uint32_t domain_id:16;
		uint32_t source_id:16;
		uint32_t function_mask:2;
		uint32_t reserved:25;
		uint32_t context_actual_invld_granularity:2;
		uint32_t context_invld_request_granularity:2;
		uint32_t invalidate_context_cache:1;
	} bits;
	uint64_t uint64;
} vtd_context_command_register_t;

typedef enum {
	VTD_IOTLB_INV_GRANULARITY_GLOBAL = 0x1,
	VTD_IOTLB_INV_GRANULARITY_DOMAIN = 0x2,
	VTD_IOTLB_INV_GRANULARITY_PAGE = 0x3
} vtd_iotlb_inv_granularity_t;

typedef union {
	struct {
		uint32_t reserved0;
		uint32_t domain_id:16;
		uint32_t drain_writes:1;
		uint32_t drain_reads:1;
		uint32_t reserved1:7;
		uint32_t iotlb_actual_invld_granularity:3;
		uint32_t iotlb_invld_request_granularity:3;
		uint32_t invalidate_iotlb:1;
	} bits;
	uint64_t uint64;
} vtd_iotlb_invalidate_register_t;

typedef union {
	struct {
		uint32_t address_mask:6;
		uint32_t invalidation_hint:1;
		uint32_t reserved:5;
		uint32_t address_low:20;
		uint32_t address_high;
	} bits;
	uint64_t uint64;
} vtd_invalidate_address_register_t;

typedef union {
	struct {
		uint32_t fault_overflow:1;
		uint32_t primary_pending_fault:1;
		uint32_t advanced_fault_overflow:1;
		uint32_t advanced_pending_fault:1;
		uint32_t invalidation_queue_error:1;
		uint32_t invalidation_completion_error:1;
		uint32_t invalidation_timeout_error:1;
		uint32_t reserved0:1;
		uint32_t fault_record_index:8;
		uint32_t reserved1:16;
	} bits;
	uint32_t uint32;
} vtd_fault_status_register_t;

typedef union {
	struct {
		uint32_t reserved:30;
		uint32_t interrupt_pending:1;
		uint32_t interrupt_mask:1;
	} bits;
	uint32_t uint32;
} vtd_fault_event_control_register_t;

typedef union {
	struct {
		uint32_t vector:8;
		uint32_t delivery_mode:3; /* 0 = fixed; 1=lowest priority */
		uint32_t reserved:3;
		uint32_t trigger_mode_level:1;
		uint32_t trigger_mode:1;
		uint32_t reserved1:18;
	} bits;
	uint32_t uint32;
} vtd_fault_event_data_register_t;

typedef union {
	struct {
		uint32_t reserved0:2;
		uint32_t destination_mode:1;
		uint32_t redirection_hint:1;
		uint32_t reserved1:8;
		uint32_t destination_id:8;
		uint32_t reserved2:12; /* reserved to 0xfee */
	} bits;
	uint32_t uint32;
} vtd_fault_event_address_register_t;

typedef struct {
	uint32_t reserved;
} vtd_fault_event_upper_address_register_t;

typedef union {
	struct {
		uint32_t reserved:12;
		uint32_t fault_info_low:20;
		uint32_t fault_info_high;
	} bits;
	uint64_t uint64;
} vtd_fault_recording_register_low_t;

typedef union {
	struct {
		uint32_t source_id:16;
		uint32_t reserved0:16;
		uint32_t fault_reason:8;
		uint32_t reserved1:20;
		uint32_t address_type:2;
		uint32_t request_type:1; /* 0 = write; 1 = read */
		uint32_t fault:1;
	} bits;
	uint64_t uint64;
} vtd_fault_recording_register_high_t;

typedef struct {
	vtd_fault_recording_register_low_t	low;
	vtd_fault_recording_register_high_t	high;
} vtd_fault_recording_register_t;

typedef union {
	struct {
		uint32_t reserved:9;
		uint32_t fault_log_size:3;
		uint32_t fault_log_address_low:20;
		uint32_t fault_log_address_high;
	} bits;
	uint64_t uint64;
} vtd_advanced_fault_log_register_t;

typedef union {
	struct {
		uint32_t protected_region_status:1;
		uint32_t reserved_p:30;
		uint32_t enable_protected_memory:1;
	} bits;
	uint32_t uint32;
} vtd_protected_memory_enable_register_t;

typedef union {
	struct {
		uint32_t reserved0:4;
		uint32_t queue_head:15; /* 128-bit aligned */
		uint32_t reserved1:13;
		uint32_t reserved2;
	} bits;
	uint64_t uint64;
} vtd_invalidation_queue_head_register_t;

typedef union {
	struct {
		uint32_t reserved0:4;
		uint32_t queue_tail:15; /* 128-bit aligned */
		uint32_t reserved1:13;
		uint32_t reserved2;
	} bits;
	uint64_t uint64;
} vtd_invalidation_queue_tail_register_t;

typedef union {
	struct {
		uint32_t queue_size:3;
		uint32_t reserved:9;
		uint32_t queue_base_low:20;
		uint32_t queue_base_high;
	} bits;
	uint64_t uint64;
} vtd_invalidation_queue_address_register_t;

typedef union {
	struct {
		uint32_t wait_descriptor_complete:1;
		uint32_t reserved:31;
	} bits;
	uint32_t uint32;
} vtd_invalidation_completion_status_register_t;

typedef union {
	struct {
		uint32_t reserved:30;
		uint32_t interrupt_pending:1;
		uint32_t interrupt_mask:1;
	} bits;
	uint32_t uint32;
} vtd_invalidation_event_control_register_t;

typedef union {
	struct {
		uint32_t interrupt_message_data:16;
		uint32_t extended_interrupt_message_data:16;
	} bits;
	uint32_t uint32;
} vtd_invalidation_event_data_register_t;

typedef union {
	struct {
		uint32_t reserved:2;
		uint32_t message_address:30;
	} bits;
	uint32_t uint32;
} vtd_invalidation_event_address_register_t;

typedef struct {
	uint32_t message_upper_address;
} vtd_invalidation_event_upper_address_register_t;

typedef union {
	struct {
		uint32_t size:4;
		uint32_t reserved:7;
		uint32_t extended_interrupt_mode_enable:1;
		uint32_t address_low:20;
		uint32_t address_high;
	} bits;
	uint64_t uint64;
} vtd_interrupt_remapping_table_address_register_t;


typedef enum {
	VTD_POWER_ACTIVE,
	VTD_POWER_SUSPEND,
	VTD_POWER_RESUME
} vtd_power_state_t;

typedef struct vtd_dma_remapping_hw_uint_t {
	uint32_t				id;
	vtd_domain_id_t				avail_domain_id;
	list_element_t				domain_list;
	uint64_t				register_base;
	uint32_t				num_devices;
	mon_lock_t				hw_lock;
	vtd_power_state_t			power_state;
	vtd_capability_register_t		capability;
	vtd_extended_capability_register_t	extended_capability;
	dmar_device_t				*devices;
	dma_remapping_root_entry_t		*root_entry_table;
} vtd_dma_remapping_hw_uint_t;

boolean_t vtd_hw_set_root_entry_table(vtd_dma_remapping_hw_uint_t *dmar,
				      dma_remapping_root_entry_t *
				      root_entry_table);

boolean_t vtd_hw_enable_translation(vtd_dma_remapping_hw_uint_t *dmar);
void vtd_hw_disable_translation(vtd_dma_remapping_hw_uint_t *dmar);

boolean_t vtd_hw_enable_interrupt_remapping(vtd_dma_remapping_hw_uint_t *dmar);
void vtd_hw_disable_interrupt_remapping(vtd_dma_remapping_hw_uint_t *dmar);

void vtd_hw_inv_context_cache_global(vtd_dma_remapping_hw_uint_t *dmar);
void vtd_hw_flush_write_buffers(vtd_dma_remapping_hw_uint_t *dmar);
void vtd_hw_inv_iotlb_global(vtd_dma_remapping_hw_uint_t *dmar);
void vtd_hw_inv_iotlb_page(vtd_dma_remapping_hw_uint_t *dmar,
			   address_t addr,
			   size_t size,
			   vtd_domain_id_t domain_id);

uint32_t vtd_hw_get_protected_low_memory_base_alignment(vtd_dma_remapping_hw_uint_t
							*dmar);
uint32_t vtd_hw_get_protected_low_memory_limit_alignment(vtd_dma_remapping_hw_uint_t
							 *dmar);
uint64_t vtd_hw_get_protected_high_memory_base_alignment(vtd_dma_remapping_hw_uint_t
							 *dmar);
uint64_t
vtd_hw_get_protected_high_memory_limit_alignment(vtd_dma_remapping_hw_uint_t *
						 dmar);
boolean_t vtd_hw_setup_protected_low_memory(vtd_dma_remapping_hw_uint_t *dmar,
					    uint32_t base,
					    uint32_t limit);
boolean_t vtd_hw_setup_protected_high_memory(vtd_dma_remapping_hw_uint_t *dmar,
					     uint64_t base,
					     uint64_t limit);
boolean_t vtd_hw_enable_protected_memory(vtd_dma_remapping_hw_uint_t *dmar);
void vtd_hw_disable_protected_memory(vtd_dma_remapping_hw_uint_t *dmar);
boolean_t vtd_hw_is_protected_memory_enabled(vtd_dma_remapping_hw_uint_t *dmar);

/* hw read/write */
uint32_t vtd_hw_read_reg32(vtd_dma_remapping_hw_uint_t *dmar, uint64_t reg);
void vtd_hw_write_reg32(vtd_dma_remapping_hw_uint_t *dmar,
			uint64_t reg,
			uint32_t value);

uint64_t vtd_hw_read_reg64(vtd_dma_remapping_hw_uint_t *dmar, uint64_t reg);
void vtd_hw_write_reg64(vtd_dma_remapping_hw_uint_t *dmar,
			uint64_t reg,
			uint64_t value);

/* capabilities */
INLINE uint32_t vtd_hw_get_super_page_support(vtd_dma_remapping_hw_uint_t *dmar)
{
	return (uint32_t)dmar->capability.bits.super_page_support;
}

INLINE uint32_t
vtd_hw_get_supported_ajusted_guest_address_width(vtd_dma_remapping_hw_uint_t *
						 dmar)
{
	return (uint32_t)dmar->capability.bits.adjusted_guest_address_width;
}

INLINE
uint32_t vtd_hw_get_max_guest_address_width(vtd_dma_remapping_hw_uint_t *dmar)
{
	return (uint32_t)dmar->capability.bits.max_guest_address_width;
}

INLINE uint32_t vtd_hw_get_number_of_domains(vtd_dma_remapping_hw_uint_t *dmar)
{
	return (uint32_t)dmar->capability.bits.number_of_domains;
}

INLINE uint32_t vtd_hw_get_caching_mode(vtd_dma_remapping_hw_uint_t *dmar)
{
	return (uint32_t)dmar->capability.bits.caching_mode;
}

INLINE
uint32_t vtd_hw_get_required_write_buffer_flush(vtd_dma_remapping_hw_uint_t *
						dmar)
{
	return (uint32_t)dmar->capability.bits.required_write_buffer_flush;
}

INLINE uint32_t vtd_hw_get_coherency(vtd_dma_remapping_hw_uint_t *dmar)
{
	return (uint32_t)dmar->extended_capability.bits.coherency;
}

INLINE
uint32_t vtd_hw_get_protected_low_memory_support(vtd_dma_remapping_hw_uint_t *
						 dmar)
{
	return (uint32_t)dmar->capability.bits.protected_low_memory_region;
}

INLINE
uint32_t vtd_hw_get_protected_high_memory_support(vtd_dma_remapping_hw_uint_t *
						  dmar)
{
	return (uint32_t)dmar->capability.bits.protected_high_memory_region;
}

/* fault handling */
INLINE
uint32_t vtd_hw_get_number_of_fault_recording_regs(vtd_dma_remapping_hw_uint_t *
						   dmar)
{
	return (uint32_t)dmar->capability.bits.
	       number_of_fault_recording_registers;
}

INLINE
uint64_t vtd_hw_get_fault_recording_reg_offset(vtd_dma_remapping_hw_uint_t *
					       dmar,
					       uint32_t fault_record_index)
{
	uint32_t fault_recording_register_offset =
		dmar->capability.bits.fault_recording_register_offset_high <<
		8 |
		dmar->capability.bits.fault_recording_register_offset_low;

	return (16 * fault_recording_register_offset)
	       + (sizeof(vtd_fault_recording_register_t) * fault_record_index);
}

void vtd_hw_mask_fault_interrupt(vtd_dma_remapping_hw_uint_t *dmar);
void vtd_hw_unmask_fault_interrupt(vtd_dma_remapping_hw_uint_t *dmar);

uint32_t vtd_hw_get_fault_overflow(vtd_dma_remapping_hw_uint_t *dmar);
uint32_t vtd_hw_get_primary_fault_pending(vtd_dma_remapping_hw_uint_t *dmar);
uint32_t vtd_hw_get_fault_record_index(vtd_dma_remapping_hw_uint_t *dmar);

void vtd_hw_set_fault_event_data(vtd_dma_remapping_hw_uint_t *dmar,
				 uint8_t vector,
				 uint8_t delivery_mode,
				 uint32_t trigger_mode_level,
				 uint32_t trigger_mode);

void vtd_hw_set_fault_event_addr(vtd_dma_remapping_hw_uint_t *dmar,
				 uint8_t dest_mode,
				 uint8_t dest_id);
void vtd_hw_clear_fault_overflow(vtd_dma_remapping_hw_uint_t *dmar);
uint64_t vtd_hw_read_fault_register(vtd_dma_remapping_hw_uint_t *dmar,
				    uint32_t fault_record_index);
uint64_t vtd_hw_read_fault_register_high(vtd_dma_remapping_hw_uint_t *dmar,
					 uint32_t fault_record_index);
void vtd_hw_clear_fault_register(vtd_dma_remapping_hw_uint_t *dmar,
				 uint32_t fault_record_index);

void vtd_hw_print_capabilities(vtd_dma_remapping_hw_uint_t *dmar);
void vtd_print_hw_status(vtd_dma_remapping_hw_uint_t *dmar);

#endif
