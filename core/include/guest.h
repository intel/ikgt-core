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

#ifndef _GUEST_H_
#define _GUEST_H_

#include "mon_defs.h"
#include "list.h"
#include "mon_objects.h"
#include "array_iterators.h"
#include "vmexit.h"
#include "vmexit_msr.h"
#include "mon_startup.h"
#include "policy_manager.h"

/*****************************************************************************
*
* Define guest-related global structures
*
*****************************************************************************/

#define INVALID_GUEST_ID    ((guest_id_t)-1)
#define ANONYMOUS_MAGIC_NUMBER  ((uint32_t)-1)

/*
 * guest descriptor
 */

/*--------------------------------------------------------------------------
 * initialization
 *-------------------------------------------------------------------------- */
void guest_manager_init(uint16_t max_cpus_per_guest, uint16_t host_cpu_count);

/*--------------------------------------------------------------------------
 * Get total number of guests
 *-------------------------------------------------------------------------- */
uint16_t guest_count(void);

/*--------------------------------------------------------------------------
 * Get Guest by guest ID
 *
 * Return NULL if no such guest
 *-------------------------------------------------------------------------- */
guest_handle_t mon_guest_handle(guest_id_t guest_id);

/*--------------------------------------------------------------------------
 * Get Guest ID by guest handle
 *-------------------------------------------------------------------------- */
guest_id_t guest_get_id(guest_handle_t guest);

/*--------------------------------------------------------------------------
 * Register new guest
 *
 * For primary guest physical_memory_size must be 0
 *
 * cpu_affinity - each 1 bit corresponds to host cpu_id_t that should run
 * guest_cpu_t
 * on behalf of this guest. Number of bits should correspond
 * to the number of registered guest CPUs for this guest
 * -1 means run on all available CPUs
 *
 * Return NULL on error
 *-------------------------------------------------------------------------- */
guest_handle_t guest_register(uint32_t magic_number,
			      uint32_t physical_memory_size,
			      uint32_t cpu_affinity,
			      const mon_policy_t *guest_policy);

/*--------------------------------------------------------------------------
 * Get guest magic number
 *-------------------------------------------------------------------------- */
uint32_t guest_magic_number(const guest_handle_t guest);

/*--------------------------------------------------------------------------
 * Get Guest by guest magic number
 *
 * Return NULL if no such guest
 *-------------------------------------------------------------------------- */
guest_handle_t guest_handle_by_magic_number(uint32_t magic_number);

/*--------------------------------------------------------------------------
 * Get guest cpu affinity.
 *-------------------------------------------------------------------------- */
uint32_t guest_cpu_affinity(const guest_handle_t guest);

/*--------------------------------------------------------------------------
 * Get guest POLICY
 *-------------------------------------------------------------------------- */
const mon_policy_t *guest_policy(const guest_handle_t guest);

/*--------------------------------------------------------------------------
 * Guest properties.
 * Default for all properties - FALSE
 *-------------------------------------------------------------------------- */
void guest_set_primary(guest_handle_t guest);
boolean_t guest_is_primary(const guest_handle_t guest);
guest_id_t guest_get_primary_guest_id(void);

void guest_set_real_BIOS_access_enabled(guest_handle_t guest);

void guest_set_nmi_owner(guest_handle_t guest);
boolean_t guest_is_nmi_owner(const guest_handle_t guest);

void guest_set_acpi_owner(guest_handle_t guest);

void guest_set_default_device_owner(guest_handle_t guest);

guest_id_t guest_get_default_device_owner_guest_id(void);

/*--------------------------------------------------------------------------
 * Get guest physical memory descriptor
 *-------------------------------------------------------------------------- */
gpm_handle_t mon_guest_get_startup_gpm(guest_handle_t guest);
gpm_handle_t gcpu_get_current_gpm(guest_handle_t guest);
void mon_gcpu_set_current_gpm(guest_cpu_handle_t gcpu, gpm_handle_t gpm);

/*--------------------------------------------------------------------------
 * Guest executable image
 *
 * Should not be called for primary guest
 *-------------------------------------------------------------------------- */
void guest_set_executable_image(guest_handle_t guest,
				const uint8_t *image_address,
				uint32_t image_size,
				uint32_t image_load_gpa,
				boolean_t image_is_compressed);

/*--------------------------------------------------------------------------
 * Add new CPU to the guest
 *
 * Return the newly created CPU
 *-------------------------------------------------------------------------- */
guest_cpu_handle_t guest_add_cpu(guest_handle_t guest);

/*--------------------------------------------------------------------------
 * Get guest CPU count
 *-------------------------------------------------------------------------- */
uint16_t guest_gcpu_count(const guest_handle_t guest);

/*--------------------------------------------------------------------------
 * enumerate guest cpus
 *
 * Return NULL on enumeration end
 *-------------------------------------------------------------------------- */
typedef generic_array_iterator_t guest_gcpu_econtext_t;

guest_cpu_handle_t mon_guest_gcpu_first(const guest_handle_t guest,
					guest_gcpu_econtext_t *context);

guest_cpu_handle_t mon_guest_gcpu_next(guest_gcpu_econtext_t *context);

/*--------------------------------------------------------------------------
 * Guest vmexits control
 *
 * request vmexits for given guest
 *
 * Receives 2 bitmasks:
 * For each 1bit in mask check the corresponding request bit. If request bit
 * is 1 - request the vmexit on this bit change, else - remove the
 * previous request for this bit.
 *-------------------------------------------------------------------------- */
void guest_control_setup(guest_handle_t guest, const vmexit_control_t *request);

list_element_t *guest_get_cpuid_list(guest_handle_t guest);

msr_vmexit_control_t *guest_get_msr_control(guest_handle_t guest);

/*--------------------------------------------------------------------------
 * enumerate guests
 *
 * Return NULL on enumeration end
 *-------------------------------------------------------------------------- */
typedef guest_handle_t guest_econtext_t;

guest_handle_t guest_first(guest_econtext_t *context);
guest_handle_t guest_next(guest_econtext_t *context);

/*
 * Dynamic Creation of Guests
 */

/*--------------------------------------------------------------------------
 * Dynamically create new Guest
 *
 * Return the newly created GUEST
 *-------------------------------------------------------------------------- */

guest_handle_t guest_dynamic_create(boolean_t stop_and_notify,
				    const mon_policy_t *guest_policy);

boolean_t guest_dynamic_assign_memory(guest_handle_t src_guest,
				      guest_handle_t dst_guest,
				      gpm_handle_t memory_map);

/*--------------------------------------------------------------------------
 * Dynamically add new CPU to the guest
 *
 * Return the newly created Guest CPU
 *
 * Note:
 * if do_not_stop_and_notify == FALSE
 * guest_before_dynamic_add_cpu() and guest_after_dynamic_add_cpu() are
 * called internally
 *
 * If do_not_stop_and_notify == TRUE the called must call
 * guest_before_dynamic_add_cpu() and guest_after_dynamic_add_cpu() around the
 * call
 *-------------------------------------------------------------------------- */
guest_cpu_handle_t guest_dynamic_add_cpu(guest_handle_t guest, /* assign to */
					 const mon_guest_cpu_startup_state_t *gcpu_startup, /* init */
					 cpu_id_t host_cpu, /* assign to */
					 boolean_t ready_to_run,
					 boolean_t stop_and_notify);

/* the following set of functions should be used only when you REALLY know what
 * you are doing - actually they are internal */
#ifdef DEBUG
void guest_register_vmcall_services(guest_handle_t guest);
#endif

/* called internally in guest_dynamic_add_cpu() if stop_and_notify == TRUE
 * and in guest_dynamic_add_cpu_default() */
void guest_before_dynamic_add_cpu(void);
/* if gcpu == NULL - creation failed */
void guest_after_dynamic_add_cpu(guest_cpu_handle_t gcpu);

#endif                          /* _GUEST_H_ */
