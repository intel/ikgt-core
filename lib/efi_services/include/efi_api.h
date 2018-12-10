/*
 * Copyright (c) 2018 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _EFI_API_H_
#define _EFI_API_H_

typedef struct _efi_table_header {
	uint64_t signature;
	uint32_t revision;
	uint32_t header_size;
	uint32_t crc32;
	uint32_t reserved;
} efi_table_header_t;

/*
 * Task priority level
 */
#define EFI_TPL_APPLICATION 4
#define EFI_TPL_CALLBACK    8
#define EFI_TPL_NOTIFY      16
#define EFI_TPL_HIGH_LEVEL  31

typedef void *efi_raise_tpl_t;
typedef void *efi_restore_tpl_t;

typedef void *efi_allocate_pages_t;
typedef void *efi_free_pages_t;
typedef void *efi_get_memory_map_t;
typedef void *efi_allocate_pool_t;
typedef void *efi_free_pool_t;

/*
 * EFI Event Types
 */
#define EFI_EVENT_TIMER                         0x80000000
#define EFI_EVENT_RUNTIME                       0x40000000
#define EFI_EVENT_RUNTIME_CONTEXT               0x20000000

#define EFI_EVENT_NOTIFY_WAIT                   0x00000100
#define EFI_EVENT_NOTIFY_SIGNAL                 0x00000200

#define EFI_EVENT_SIGNAL_EXIT_BOOT_SERVICES     0x00000201
#define EFI_EVENT_SIGNAL_VIRTUAL_ADDRESS_CHANGE 0x60000202

#define EFI_EVENT_EFI_SIGNAL_MASK               0x000000FF
#define EFI_EVENT_EFI_SIGNAL_MAX                4



typedef void (EFI_API *efi_event_notify_t) (
	efi_event_t event,
	void *context
);

typedef efi_status_t (EFI_API *efi_create_event_t) (
	uint32_t type,
	efi_tpl_t notify_tpl,
	efi_event_notify_t notify_func,
	void *notify_context,
	efi_event_t event
);

typedef void *efi_create_event_ex_t;
typedef void *efi_set_timer_t;
typedef void *efi_wait_for_event_t;
typedef void *efi_signal_event_t;
typedef void *efi_close_event_t;
typedef void *efi_check_event_t;

typedef void *efi_install_protocol_interface_t;
typedef void *efi_reinstall_protocol_interface_t;
typedef void *efi_uninstall_protocol_interface_t;
typedef void *efi_handle_protocol_t;
typedef void *efi_register_protocol_notify_t;
typedef void *efi_locate_handle_t;
typedef void *efi_locate_device_path_t;
typedef void *efi_install_configuration_table_t;

typedef void *efi_image_load_t;
typedef void *efi_image_start_t;
typedef void *efi_exit_t;
typedef void *efi_image_unload_t;
typedef void *efi_exit_boot_services_t;

typedef void *efi_get_next_monotonic_count_t;
typedef void *efi_stall_t;
typedef void *efi_set_watchdog_timer_t;

typedef void *efi_connect_controller_t;
typedef void *efi_disconnect_controller_t;

typedef void *efi_open_protocol_t;
typedef void *efi_close_protocol_t;
typedef void *efi_open_protocol_information_t;

typedef void *efi_protocols_per_handle_t;
typedef void *efi_locate_handle_buffer_t;

typedef void *efi_install_multiple_protocol_interfaces_t;
typedef void *efi_uninstall_multiple_protocol_interfaces_t;

typedef void *efi_calculate_crc32_t;

typedef void *efi_copy_mem_t;
typedef void *efi_set_mem_t;



typedef efi_status_t (EFI_API *efi_locate_protocol_t) (
	efi_guid_t *protocol,
	void *registration,
	void **interface
);

#define EFI_BOOT_SERVICES_SIGNATURE 0x56524553544f4f42ULL

typedef struct {
	efi_table_header_t hdr;

	/* Task Priority Services */
	efi_raise_tpl_t raise_tpl;                                                               // EFI 1.0+
	efi_restore_tpl_t restore_tpl;

	/* Memory Services */
	efi_allocate_pages_t allocate_pages;
	efi_free_pages_t free_pages;
	efi_get_memory_map_t get_memory_map;
	efi_allocate_pool_t allocate_pool;
	efi_free_pool_t free_pool;

	/* Event and timer Services */
	efi_create_event_t create_event;
	efi_set_timer_t set_timer;
	efi_wait_for_event_t wait_for_event;
	efi_signal_event_t signal_event;
	efi_close_event_t close_event;
	efi_check_event_t check_event;

	/* Protocol Handler Services */
	efi_install_protocol_interface_t install_protocol_interface;
	efi_reinstall_protocol_interface_t reinstall_protocol_interface;
	efi_uninstall_protocol_interface_t uninstall_protocol_interface;
	efi_handle_protocol_t handle_protocol;
	void *reserved;
	efi_register_protocol_notify_t register_protocol_notify;
	efi_locate_handle_t locate_handle;
	efi_locate_device_path_t locate_device_path;
	efi_install_configuration_table_t install_configuration_table;

	/* Image Services */
	efi_image_load_t load_image;
	efi_image_start_t start_image;
	efi_exit_t exit;
	efi_image_unload_t unload_image;
	efi_exit_boot_services_t exit_boot_services;

	/* Miscellaneous Services */
	efi_get_next_monotonic_count_t get_next_monotonic_count;
	efi_stall_t stall;
	efi_set_watchdog_timer_t set_watchdog_timer;

	/* Driver Support Services */
	efi_connect_controller_t connect_controller;                                             // EFI 1.1+
	efi_disconnect_controller_t disconnect_controller;

	/* Open and Close Protocol Services */
	efi_open_protocol_t open_protocol;
	efi_close_protocol_t close_protocol;
	efi_open_protocol_information_t open_protocol_information;

	/* Library services */
	efi_protocols_per_handle_t protocols_per_handle;
	efi_locate_handle_buffer_t locate_handle_buffer;
	efi_locate_protocol_t locate_protocol;

	efi_install_multiple_protocol_interfaces_t install_multiple_protocol_interfaces;
	efi_uninstall_multiple_protocol_interfaces_t uninstall_multiple_protocol_interfaces;

	/* 32-bit CRC Services */
	efi_calculate_crc32_t calculate_crc32;

	/* Miscellaneous Services */
	efi_copy_mem_t copy_mem;
	efi_set_mem_t set_mem;
	efi_create_event_ex_t create_event_ex;                                                   // UEFI 2.0+
} efi_boot_services_t;

typedef void *efi_simple_text_in_protocol_t;
typedef void *efi_simple_text_out_protocol_t;
typedef void *efi_runtime_services_t;

typedef struct _efi_configuration_table {
	efi_guid_t vendor_guid;
	void *vendor_table;
} efi_configuration_table_t;

#define EFI_SYSTEM_TABLE_SIGNATURE 0x5453595320494249ULL

/* UEFI Specification Version 2.6 */
typedef struct _efi_system_table {
	efi_table_header_t hdr;

	uint16_t *firmware_vendor;
	uint32_t firmware_revision;
	uint32_t padding; /* Not align with UEFI spec, but needed */

	efi_handle_t console_in_handle;
	efi_simple_text_in_protocol_t *con_in;

	efi_handle_t console_out_handle;
	efi_simple_text_out_protocol_t *con_out;

	efi_handle_t standard_error_handle;
	efi_simple_text_out_protocol_t *std_err;

	efi_runtime_services_t *runtime_services;
	efi_boot_services_t *boot_services;

	uintn_t num_of_table_entries;
	efi_configuration_table_t *configuration_table;

} efi_system_table_t;

#endif
