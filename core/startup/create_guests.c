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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(CREATE_GUESTS_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(CREATE_GUESTS_C, __condition)
#include "mon_defs.h"
#include "mon_bootstrap_utils.h"
#include "gpm_api.h"
#include "guest.h"
#include "guest_cpu.h"
#include "host_cpu.h"
#include "hw_utils.h"
#include "scheduler.h"
#include "layout_host_memory.h"
#include "mon_dbg.h"
#include "vmexit_msr.h"
#include "device_drivers_manager.h"
#include "vmcall_api.h"
#include "vmcall.h"
#include "host_memory_manager_api.h"
#include "mon_startup.h"
#include "mon_events_data.h"
#include "guest_pci_configuration.h"
#include "ipc.h"
#include "pat_manager.h"

/**************************************************************************
 *
 * Read input data structure and create all guests
 *
 ************************************************************************** */

extern void fvs_initialize(guest_handle_t guest,
			   uint32_t number_of_host_processors);

/* -------------------------- types ----------------------------------------- */
/* ---------------------------- globals ------------------------------------- */
/* ---------------------------- internal functions -------------------------- */
static void raise_guest_create_event(guest_id_t guest_id);

/*
 * Add CPU to guest
 */
void add_cpu_to_guest(const mon_guest_startup_t *gstartup, guest_handle_t guest,
		      cpu_id_t host_cpu_to_allocate, boolean_t ready_to_run)
{
	guest_cpu_handle_t gcpu;
	const virtual_cpu_id_t *vcpu = NULL;
	const mon_guest_cpu_startup_state_t *cpus_arr = NULL;

	gcpu = guest_add_cpu(guest);

	MON_ASSERT(gcpu);

	/* find init data */
	vcpu = mon_guest_vcpu(gcpu);
	MON_ASSERT(vcpu);
	/* register with scheduler */
	scheduler_register_gcpu(gcpu, host_cpu_to_allocate, ready_to_run);

	if (vcpu->guest_cpu_id < gstartup->cpu_states_count) {
		cpus_arr =
			(const mon_guest_cpu_startup_state_t *)gstartup->
			cpu_states_array;

		MON_ASSERT(cpus_arr);

		MON_LOG(mask_anonymous,
			level_trace,
			"Setting up initial state for the newly created Guest CPU\n");
		gcpu_initialize(gcpu, &(cpus_arr[vcpu->guest_cpu_id]));
	} else {
		MON_LOG(mask_anonymous, level_trace,
			"Newly created Guest CPU was initialized with the"
			" Wait-For-SIPI state\n");
	}

	/* host part will be initialized later */
}

/*
 * Init guest except of guest memory
 * Return NULL on error
 */
static guest_handle_t init_single_guest(uint32_t number_of_host_processors,
					const mon_guest_startup_t *gstartup,
					const mon_policy_t *guest_policy)
{
	guest_handle_t guest;
	uint32_t cpu_affinity = 0;
	uint32_t bit_number;
	boolean_t ready_to_run = FALSE;

	if ((gstartup->size_of_this_struct != sizeof(mon_guest_startup_t)) ||
	    (gstartup->version_of_this_struct != MON_GUEST_STARTUP_VERSION)) {
		MON_LOG(mask_anonymous, level_trace,
			"ASSERT: unknown guest struct: size: %#x version %d\n",
			gstartup->size_of_this_struct,
			gstartup->version_of_this_struct);

		return NULL;
	}

	/* create guest */
	guest = guest_register(gstartup->guest_magic_number,
		gstartup->physical_memory_size,
		gstartup->cpu_affinity, guest_policy);

	if (!guest) {
		MON_LOG(mask_anonymous,
			level_trace,
			"Cannot create guest with the following params: \n"
			"\t\tguest_magic_number    = %#x\n"
			"\t\tphysical_memory_size  = %#x\n"
			"\t\tcpu_affinity          = %#x\n",
			gstartup->guest_magic_number,
			gstartup->physical_memory_size,
			gstartup->cpu_affinity);

		return NULL;
	}

	fvs_initialize(guest, number_of_host_processors);

	vmexit_guest_initialize(guest_get_id(guest));

	if (gstartup->devices_count != 0) {
		MON_LOG(mask_anonymous, level_trace,
			"ASSERT: devices virtualization is not supported yet\n"
			"\t\tguest_magic_number    = %#x\n"
			"\t\tdevices_count         = %d\n",
			gstartup->guest_magic_number, gstartup->devices_count);

		MON_DEADLOOP();
		return NULL;
	}

	if (gstartup->image_size) {
		guest_set_executable_image(guest,
			(const uint8_t *)gstartup->image_address,
			gstartup->image_size,
			gstartup->image_offset_in_guest_physical_memory,
			BITMAP_GET(gstartup->flags,
				MON_GUEST_FLAG_IMAGE_COMPRESSED) != 0);
	}

	if (BITMAP_GET(gstartup->flags,
		    MON_GUEST_FLAG_REAL_BIOS_ACCESS_ENABLE) !=
	    0) {
		guest_set_real_BIOS_access_enabled(guest);
	}

	msr_vmexit_guest_setup(guest); /* setup MSR-related control structure */

	/* init cpus. */
	/* first init CPUs that has initial state */
	cpu_affinity = gstartup->cpu_affinity;
	if (cpu_affinity == 0) {
		MON_LOG(mask_anonymous,
			level_trace,
			"ASSERT: guest without CPUs:\n"
			"\t\tguest_magic_number    = %#x\n"
			"\t\tcpu_affinity          = %#x\n",
			gstartup->guest_magic_number,
			gstartup->cpu_affinity);

		MON_DEADLOOP();
		return NULL;
	}

	ready_to_run =
		(BITMAP_GET(gstartup->flags,
			 MON_GUEST_FLAG_LAUNCH_IMMEDIATELY) != 0);

	if (cpu_affinity == (uint32_t)-1) {
		/* special case - run on all existing CPUs */
		for (bit_number = 0; bit_number < number_of_host_processors;
		     bit_number++) {
			add_cpu_to_guest(gstartup, guest, (cpu_id_t)bit_number,
				ready_to_run);
			MON_LOG(mask_anonymous,
				level_trace,
				"CPU #%d added successfully to the current guest\n",
				bit_number);
		}
	}
#ifdef DEBUG
	guest_register_vmcall_services(guest);
#endif

	return guest;
}

/* ---------------------------- APIs ------------------------------------- */

/*-------------------------------------------------------------------------
 *
 * Preform initialization of guests and guest CPUs
 *
 * Should be called on BSP only while all APs are stopped
 *
 * Return TRUE for success
 *
 *------------------------------------------------------------------------- */
boolean_t initialize_all_guests(uint32_t number_of_host_processors,
				const mon_memory_layout_t *mon_memory_layout,
				const mon_guest_startup_t *
				primary_guest_startup_state,
				uint32_t number_of_secondary_guests,
				const mon_guest_startup_t *
				secondary_guests_startup_state_array,
				const mon_application_params_struct_t *
				application_params)
{
	guest_handle_t primary_guest;
	gpm_handle_t primary_guest_startup_gpm;
	boolean_t ok = FALSE;
	/* guest_handle_t cur_guest; */
	guest_cpu_handle_t gcpu;
	guest_gcpu_econtext_t gcpu_context;

	MON_ASSERT(hw_cpu_id() == 0);
	MON_ASSERT(number_of_host_processors > 0);
	MON_ASSERT(mon_memory_layout);
	MON_ASSERT(primary_guest_startup_state);

	if (number_of_secondary_guests > 0) {
		MON_LOG(mask_anonymous, level_trace,
			"initialize_all_guests ASSERT: Secondary guests are"
			" yet not implemented\n");

		MON_ASSERT(secondary_guests_startup_state_array);

		/* init guests and allocate memory for them */

		/* shutdown temporary layout object */

		MON_DEADLOOP();
		return FALSE;
	}

	/* first init primary guest */
	MON_LOG(mask_anonymous, level_trace, "Init primary guest\n");

	/* BUGBUG: This is a workaround until loader will not do this!!! */
	BITMAP_SET(((mon_guest_startup_t *)primary_guest_startup_state)->flags,
		MON_GUEST_FLAG_REAL_BIOS_ACCESS_ENABLE |
		MON_GUEST_FLAG_LAUNCH_IMMEDIATELY);

	/* TODO: Uses global policym but should be part of mon_guest_startup_t
	 * structure.  */
	primary_guest = init_single_guest(number_of_host_processors,
		primary_guest_startup_state, NULL);
	if (!primary_guest) {
		MON_LOG(mask_anonymous, level_trace,
			"initialize_all_guests: Cannot init primary guest\n");
		MON_DEADLOOP();
		return FALSE;
	}

	guest_set_primary(primary_guest);
	primary_guest_startup_gpm = mon_guest_get_startup_gpm(primary_guest);

	/* init memory layout in the startup gpm */
	ok = init_memory_layout(mon_memory_layout,
		primary_guest_startup_gpm,
		number_of_secondary_guests > 0,
		application_params);

	/* Set active_gpm to startup gpm */
	for (gcpu = mon_guest_gcpu_first(primary_guest, &gcpu_context); gcpu;
	     gcpu = mon_guest_gcpu_next(&gcpu_context))
		mon_gcpu_set_current_gpm(gcpu, primary_guest_startup_gpm);

	MON_LOG(mask_anonymous, level_trace,
		"Primary guest initialized successfully\n");

	return TRUE;
}

/*-------------------------------------------------------------------------
 *
 * Preform initialization of host cpu parts of all guest CPUs that run on
 * specified host CPU.
 *
 * Should be called on the target host CPU
 *------------------------------------------------------------------------- */
void initialize_host_vmcs_regions(cpu_id_t current_cpu_id)
{
	guest_cpu_handle_t gcpu;
	scheduler_gcpu_iterator_t it;

	MON_ASSERT(current_cpu_id == hw_cpu_id());

	for (gcpu = scheduler_same_host_cpu_gcpu_first(&it, current_cpu_id);
	     gcpu != NULL; gcpu = scheduler_same_host_cpu_gcpu_next(&it))
		/* now init the host CPU part for vm-exits */
		host_cpu_vmcs_init(gcpu);
}
