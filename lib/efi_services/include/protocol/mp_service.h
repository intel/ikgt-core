/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _MP_SERVICE_H_
#define _MP_SERVICE_H_

/*
 * PI(Platform Initialization) Specification Version 1.6
 *    Volume 2: Driver Execution Environment Core Interface
 *        Chapter 13.4: MP Services Protocol
 */

#define EFI_MP_SERVICES_PROTOCOL_GUID \
{\
	0x3fdda605, 0xa76e, 0x4f46, {0xad, 0x29, 0x12, 0xf4, 0x53, 0x1b, 0x3d, 0x08} \
}

typedef struct _efi_mp_services_protocol efi_mp_services_protocol_t;

typedef void (EFI_API *efi_ap_procedure_t) (
	void *procedure_argument
);

typedef efi_status_t (EFI_API *efi_mp_services_get_number_of_processors_t) (
	efi_mp_services_protocol_t *this,
	uintn_t *number_of_processors,
	uintn_t *number_of_enabled_processors
);

typedef efi_status_t (EFI_API *efi_mp_services_startup_all_aps_t) (
	efi_mp_services_protocol_t *this,
	efi_ap_procedure_t procedure,
	efi_bool_t single_thread,
	efi_event_t wait_event,
	uintn_t time_out_in_micro_seconds,
	void *procedure_argument,
	uintn_t **failed_cpu_list
);

typedef void *efi_mp_services_get_processor_info_t;
typedef void *efi_mp_services_startup_this_ap_t;
typedef void *efi_mp_services_switch_bsp_t;
typedef void *efi_mp_services_enable_disable_ap_t;
typedef void *efi_mp_services_who_am_i_t;

struct _efi_mp_services_protocol {
	efi_mp_services_get_number_of_processors_t get_number_of_processors;
	efi_mp_services_get_processor_info_t get_processor_info;
	efi_mp_services_startup_all_aps_t startup_all_aps;
	efi_mp_services_startup_this_ap_t startup_this_ap;
	efi_mp_services_switch_bsp_t switch_bsp;
	efi_mp_services_enable_disable_ap_t enable_disable_ap;
	efi_mp_services_who_am_i_t who_am_i;
};

#endif
