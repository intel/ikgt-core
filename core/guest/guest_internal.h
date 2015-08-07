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

#ifndef _GUEST_INTERNAL_H_
#define _GUEST_INTERNAL_H_

#include "guest.h"
#include "list.h"
#include "vmexit_msr.h"
#include "vmx_ctrl_msrs.h"
#include "policy_manager.h"
#include "../memory/ept/fvs.h"

/* -------------------------- types ----------------------------------------- */
typedef struct guest_descriptor_t {
	guest_id_t			id;
	uint16_t			reserved_0;
	uint32_t			magic_number;
	uint32_t			physical_memory_size;   /* 0 for primary */

	uint16_t			flags;                  /* guest_flags_t */
	uint16_t			cpu_count;
	uint32_t			cpu_affinity;           /* 1 bit for each allocated host CPU */

	uint32_t			reserved_1;
	uint64_t			physical_memory_base; /* 0 for primary */

	mon_policy_t			guest_policy;

	/* saved image descriptor - 0 for primary guest
	 * or guest that does not support reloading */
	const uint8_t			*saved_image;
	uint32_t			saved_image_size;
	uint32_t			image_load_gpa;

	guest_cpu_handle_t		*cpus_array; /* size of the array is cpu_count */
	gpm_handle_t			startup_gpm;

	fvs_object_t			fvs_desc;

	list_element_t			cpuid_filter_list[1];
	msr_vmexit_control_t		msr_control[1];

	uint32_t			padding2;
	boolean_t			is_initialization_finished;
	struct guest_descriptor_t	*next_guest;
} guest_descriptor_t;

typedef enum {
	GUEST_IS_PRIMARY_FLAG = 0,
	GUEST_IS_NMI_OWNER_FLAG,
	GUEST_IS_ACPI_OWNER_FLAG,
	GUEST_IS_DEFAULT_DEVICE_OWNER_FLAG,
	GUEST_BIOS_ACCESS_ENABLED_FLAG,

	GUEST_SAVED_IMAGE_IS_COMPRESSED_FLAG
} guest_flags_t;

#define SET_GUEST_IS_PRIMARY_FLAG(guest)                \
	BIT_SET((guest)->flags, GUEST_IS_PRIMARY_FLAG)
#define CLR_GUEST_IS_PRIMARY_FLAG(guest)                \
	BIT_CLR((guest)->flags, GUEST_IS_PRIMARY_FLAG)
#define GET_GUEST_IS_PRIMARY_FLAG(guest)                \
	BIT_GET((guest)->flags, GUEST_IS_PRIMARY_FLAG)

#define SET_GUEST_IS_NMI_OWNER_FLAG(guest)              \
	BIT_SET((guest)->flags, GUEST_IS_NMI_OWNER_FLAG)
#define CLR_GUEST_IS_NMI_OWNER_FLAG(guest)              \
	BIT_CLR((guest)->flags, GUEST_IS_NMI_OWNER_FLAG)
#define GET_GUEST_IS_NMI_OWNER_FLAG(guest)              \
	BIT_GET((guest)->flags, GUEST_IS_NMI_OWNER_FLAG)

#define SET_GUEST_IS_ACPI_OWNER_FLAG(guest)             \
	BIT_SET((guest)->flags, GUEST_IS_ACPI_OWNER_FLAG)
#define CLR_GUEST_IS_ACPI_OWNER_FLAG(guest)             \
	BIT_CLR((guest)->flags, GUEST_IS_ACPI_OWNER_FLAG)
#define GET_GUEST_IS_ACPI_OWNER_FLAG(guest)             \
	BIT_GET((guest)->flags, GUEST_IS_ACPI_OWNER_FLAG)

#define SET_GUEST_IS_DEFAULT_DEVICE_OWNER_FLAG(guest)   \
	BIT_SET((guest)->flags, GUEST_IS_DEFAULT_DEVICE_OWNER_FLAG)
#define CLR_GUEST_IS_DEFAULT_DEVICE_OWNER_FLAG(guest)   \
	BIT_CLR((guest)->flags, GUEST_IS_DEFAULT_DEVICE_OWNER_FLAG)
#define GET_GUEST_IS_DEFAULT_DEVICE_OWNER_FLAG(guest)   \
	BIT_GET((guest)->flags, GUEST_IS_DEFAULT_DEVICE_OWNER_FLAG)

#define SET_GUEST_BIOS_ACCESS_ENABLED_FLAG(guest)       \
	BIT_SET((guest)->flags, GUEST_BIOS_ACCESS_ENABLED_FLAG)
#define CLR_GUEST_BIOS_ACCESS_ENABLED_FLAG(guest)       \
	BIT_CLR((guest)->flags, GUEST_BIOS_ACCESS_ENABLED_FLAG)
#define GET_GUEST_BIOS_ACCESS_ENABLED_FLAG(guest)       \
	BIT_GET((guest)->flags, GUEST_BIOS_ACCESS_ENABLED_FLAG)

#define SET_GUEST_SAVED_IMAGE_IS_COMPRESSED_FLAG(guest) \
	BIT_SET((guest)->flags, GUEST_SAVED_IMAGE_IS_COMPRESSED_FLAG)
#define CLR_GUEST_SAVED_IMAGE_IS_COMPRESSED_FLAG(guest) \
	BIT_CLR((guest)->flags, GUEST_SAVED_IMAGE_IS_COMPRESSED_FLAG)
#define GET_GUEST_SAVED_IMAGE_IS_COMPRESSED_FLAG(guest) \
	BIT_GET((guest)->flags, GUEST_SAVED_IMAGE_IS_COMPRESSED_FLAG)

#endif    /* _GUEST_INTERNAL_H_ */
