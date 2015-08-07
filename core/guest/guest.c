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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(GUEST_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(GUEST_C, __condition)
#include "guest.h"
#include "guest_internal.h"
#include "guest_cpu_internal.h"
#include "guest_cpu.h"
#include "vmcall.h"
#include "gpm_api.h"
#include "heap.h"
#include "vmexit.h"
#include "mon_dbg.h"
#include "memory_allocator.h"
#include "mon_events_data.h"
#include "guest_pci_configuration.h"
#include "ipc.h"
#include "host_memory_manager_api.h"
#include "memory_address_mapper_api.h"
#include "scheduler.h"
#include "host_cpu.h"
#include <pat_manager.h>
#include "ept.h"

#define MIN_ANONYMOUS_GUEST_ID  30000

extern void mon_acpi_pm_initialize(guest_id_t guest_id);

/***************************************************************************
*
* Guest Manager
*
***************************************************************************/

static
void raise_gcpu_add_event(cpu_id_t from, void *arg);

/* -------------------------- types ----------------------------------------- */
/* ---------------------------- globals ------------------------------------- */

static uint32_t guests_count;
static uint32_t max_gcpus_count_per_guest;
static uint32_t num_host_cpus;

static guest_descriptor_t *guests;

/* ---------------------------- internal funcs ----------------------------- */

/* ---------------------------- APIs --------------------------------------- */
void guest_manager_init(uint16_t max_cpus_per_guest, uint16_t host_cpu_count)
{
	MON_ASSERT(max_cpus_per_guest);

	max_gcpus_count_per_guest = max_cpus_per_guest;
	num_host_cpus = host_cpu_count;

	guests = NULL;

	/* init subcomponents */
	gcpu_manager_init(host_cpu_count);
}

/*--------------------------------------------------------------------------
 * Get Guest by guest ID
 *
 * Return NULL if no such guest
 *-------------------------------------------------------------------------- */
guest_handle_t mon_guest_handle(guest_id_t guest_id)
{
	guest_descriptor_t *guest;

	if (guest_id >= guests_count) {
		return NULL;
	}
	for (guest = guests; guest != NULL; guest = guest->next_guest) {
		if (guest->id == guest_id) {
			return guest;
		}
	}
	return NULL;
}

/*--------------------------------------------------------------------------
 * Get Guest ID by guest handle
 *-------------------------------------------------------------------------- */
guest_id_t guest_get_id(guest_handle_t guest)
{
	MON_ASSERT(guest);

	return guest->id;
}

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
			      const mon_policy_t *guest_policy)
{
	guest_descriptor_t *guest;

	guest = (guest_descriptor_t *)mon_malloc(sizeof(guest_descriptor_t));
	MON_ASSERT(guest);

	guest->id = (guest_id_t)guests_count;
	++guests_count;

	if (magic_number == ANONYMOUS_MAGIC_NUMBER) {
		guest->magic_number = MIN_ANONYMOUS_GUEST_ID + guest->id;
	} else {
		MON_ASSERT(magic_number < MIN_ANONYMOUS_GUEST_ID);
		guest->magic_number = magic_number;
	}
	guest->physical_memory_size = physical_memory_size;
	guest->cpu_affinity = cpu_affinity;

	guest->cpus_array =
		(guest_cpu_handle_t *)mon_malloc(sizeof(guest_cpu_handle_t) *
			max_gcpus_count_per_guest);
	guest->cpu_count = 0;

	guest->flags = 0;
	guest->saved_image = NULL;
	guest->saved_image_size = 0;

	guest->startup_gpm = mon_gpm_create_mapping();
	MON_ASSERT(guest->startup_gpm != GPM_INVALID_HANDLE);

	if (guest_policy == NULL) {
		get_global_policy(&guest->guest_policy);
	} else {
		copy_policy(&guest->guest_policy, guest_policy);
	}

	/* prepare list for CPUID filters */
	list_init(guest->cpuid_filter_list);
	/* prepare list for MSR handlers */
	list_init(guest->msr_control->msr_list);

	guest->next_guest = guests;
	guests = guest;

	return guest;
}

/*--------------------------------------------------------------------------
 * Get total number of guests
 *-------------------------------------------------------------------------- */
uint16_t guest_count(void)
{
	return (uint16_t)guests_count;
}

/*--------------------------------------------------------------------------
 * Get guest magic number
 *-------------------------------------------------------------------------- */
uint32_t guest_magic_number(const guest_handle_t guest)
{
	MON_ASSERT(guest);

	return guest->magic_number;
}

/*--------------------------------------------------------------------------
 * Get Guest by guest magic number
 *
 * Return NULL if no such guest
 *-------------------------------------------------------------------------- */
guest_handle_t guest_handle_by_magic_number(uint32_t magic_number)
{
	guest_descriptor_t *guest;

	for (guest = guests; guest != NULL; guest = guest->next_guest) {
		if (guest->magic_number == magic_number) {
			return guest;
		}
	}
	return NULL;
}

/*--------------------------------------------------------------------------
 * Get guest cpu affinity.
 *-------------------------------------------------------------------------- */
uint32_t guest_cpu_affinity(const guest_handle_t guest)
{
	MON_ASSERT(guest);

	return guest->cpu_affinity;
}

/*--------------------------------------------------------------------------
 * Get guest POLICY
 *-------------------------------------------------------------------------- */
const mon_policy_t *guest_policy(const guest_handle_t guest)
{
	MON_ASSERT(guest);

	return &guest->guest_policy;
}

/*--------------------------------------------------------------------------
 * Guest properties.
 * Default for all properties - FALSE
 *-------------------------------------------------------------------------- */
void guest_set_primary(guest_handle_t guest)
{
	MON_ASSERT(guest);
	MON_ASSERT(guest->physical_memory_size == 0);
	MON_ASSERT(guest->physical_memory_base == 0);
	MON_ASSERT(guest->saved_image == NULL);
	MON_ASSERT(guest->saved_image_size == 0);

	SET_GUEST_IS_PRIMARY_FLAG(guest);
}

boolean_t guest_is_primary(const guest_handle_t guest)
{
	MON_ASSERT(guest);

	return GET_GUEST_IS_PRIMARY_FLAG(guest) != 0;
}

guest_id_t guest_get_primary_guest_id(void)
{
	guest_descriptor_t *guest;

	for (guest = guests; guest != NULL; guest = guest->next_guest) {
		if (0 != GET_GUEST_IS_PRIMARY_FLAG(guest)) {
			return guest->id;
		}
	}
	return INVALID_GUEST_ID;
}

void guest_set_real_BIOS_access_enabled(guest_handle_t guest)
{
	MON_ASSERT(guest);

	SET_GUEST_BIOS_ACCESS_ENABLED_FLAG(guest);
}

void guest_set_nmi_owner(guest_handle_t guest)
{
	MON_ASSERT(guest);

	SET_GUEST_IS_NMI_OWNER_FLAG(guest);
}

boolean_t guest_is_nmi_owner(const guest_handle_t guest)
{
	MON_ASSERT(guest);

	return GET_GUEST_IS_NMI_OWNER_FLAG(guest) != 0;
}

void guest_set_acpi_owner(guest_handle_t guest)
{
	MON_ASSERT(guest);
	SET_GUEST_IS_ACPI_OWNER_FLAG(guest);

	mon_acpi_pm_initialize(guest->id); /* for ACPI owner only */
}

void guest_set_default_device_owner(guest_handle_t guest)
{
	MON_ASSERT(guest);

	SET_GUEST_IS_DEFAULT_DEVICE_OWNER_FLAG(guest);
}

guest_id_t guest_get_default_device_owner_guest_id(void)
{
	guest_descriptor_t *guest;

	for (guest = guests; guest != NULL; guest = guest->next_guest) {
		if (0 != GET_GUEST_IS_DEFAULT_DEVICE_OWNER_FLAG(guest)) {
			return guest->id;
		}
	}
	return INVALID_GUEST_ID;
}

/*--------------------------------------------------------------------------
 * Get startup guest physical memory descriptor
 *-------------------------------------------------------------------------- */
gpm_handle_t mon_guest_get_startup_gpm(guest_handle_t guest)
{
	MON_ASSERT(guest);
	return guest->startup_gpm;
}

/*--------------------------------------------------------------------------
 * Get guest physical memory descriptor
 *-------------------------------------------------------------------------- */
gpm_handle_t gcpu_get_current_gpm(guest_handle_t guest)
{
	guest_cpu_handle_t gcpu;

	MON_ASSERT(guest);

	gcpu = mon_scheduler_get_current_gcpu_for_guest(guest_get_id(guest));
	MON_ASSERT(gcpu);

	return gcpu->active_gpm;
}

void mon_gcpu_set_current_gpm(guest_cpu_handle_t gcpu, gpm_handle_t gpm)
{
	MON_ASSERT(gcpu);
	gcpu->active_gpm = gpm;
}

/*--------------------------------------------------------------------------
 * Guest executable image
 *
 * Should not be called for primary guest
 *-------------------------------------------------------------------------- */
void guest_set_executable_image(guest_handle_t guest,
				const uint8_t *image_address,
				uint32_t image_size,
				uint32_t image_load_gpa,
				boolean_t image_is_compressed)
{
	MON_ASSERT(guest);
	MON_ASSERT(GET_GUEST_IS_PRIMARY_FLAG(guest) == 0);

	guest->saved_image = image_address;
	guest->saved_image_size = image_size;
	guest->image_load_gpa = image_load_gpa;

	if (image_is_compressed) {
		SET_GUEST_SAVED_IMAGE_IS_COMPRESSED_FLAG(guest);
	}
}

/*--------------------------------------------------------------------------
 * Add new CPU to the guest
 *
 * Return the newly created CPU
 *-------------------------------------------------------------------------- */
guest_cpu_handle_t guest_add_cpu(guest_handle_t guest)
{
	virtual_cpu_id_t vcpu;
	guest_cpu_handle_t gcpu;

	MON_ASSERT(guest);

	MON_ASSERT(guest->cpu_count < max_gcpus_count_per_guest);

	vcpu.guest_id = guest->id;
	vcpu.guest_cpu_id = guest->cpu_count;
	++(guest->cpu_count);

	gcpu = gcpu_allocate(vcpu, guest);
	guest->cpus_array[vcpu.guest_cpu_id] = gcpu;

	return gcpu;
}

/*--------------------------------------------------------------------------
 * Get guest CPU count
 *-------------------------------------------------------------------------- */
uint16_t guest_gcpu_count(const guest_handle_t guest)
{
	MON_ASSERT(guest);

	return guest->cpu_count;
}

/*--------------------------------------------------------------------------
 * enumerate guest cpus
 *
 * Return NULL on enumeration end
 *-------------------------------------------------------------------------- */
guest_cpu_handle_t mon_guest_gcpu_first(const guest_handle_t guest,
					guest_gcpu_econtext_t *context)
{
	const guest_cpu_handle_t *p_gcpu;

	MON_ASSERT(guest);

	p_gcpu = ARRAY_ITERATOR_FIRST(guest_cpu_handle_t,
		guest->cpus_array, guest->cpu_count, context);

	return p_gcpu ? *p_gcpu : NULL;
}

guest_cpu_handle_t mon_guest_gcpu_next(guest_gcpu_econtext_t *context)
{
	guest_cpu_handle_t *p_gcpu;

	p_gcpu = ARRAY_ITERATOR_NEXT(guest_cpu_handle_t, context);

	return p_gcpu ? *p_gcpu : NULL;
}

/*--------------------------------------------------------------------------
 * enumerate guests
 *
 * Return NULL on enumeration end
 *-------------------------------------------------------------------------- */
guest_handle_t guest_first(guest_econtext_t *context)
{
	guest_descriptor_t *guest = NULL;

	MON_ASSERT(context);

	guest = guests;
	*context = guest;

	return guest;
}

guest_handle_t guest_next(guest_econtext_t *context)
{
	guest_descriptor_t *guest = NULL;

	MON_ASSERT(context);
	guest = (guest_descriptor_t *)*context;

	if (guest != NULL) {
		guest = guest->next_guest;
		*context = guest;
	}

	return guest;
}

list_element_t *guest_get_cpuid_list(guest_handle_t guest)
{
	return guest->cpuid_filter_list;
}

msr_vmexit_control_t *guest_get_msr_control(guest_handle_t guest)
{
	return guest->msr_control;
}

/* assumption - all CPUs are running */
void guest_begin_physical_memory_modifications(guest_handle_t guest)
{
	event_gpm_modification_data_t gpm_modification_data;
	guest_cpu_handle_t gcpu;

	MON_ASSERT(guest);

	gpm_modification_data.guest_id = guest->id;

	gcpu = mon_scheduler_get_current_gcpu_for_guest(guest_get_id(guest));
	MON_ASSERT(gcpu);

	event_raise(EVENT_BEGIN_GPM_MODIFICATION_BEFORE_CPUS_STOPPED, gcpu,
		&gpm_modification_data);

	stop_all_cpus();
}


static
void guest_notify_gcpu_about_gpm_change(cpu_id_t from UNUSED, void *arg)
{
	cpu_id_t guest_id = (cpu_id_t)(size_t)arg;
	guest_cpu_handle_t gcpu;

	gcpu = mon_scheduler_get_current_gcpu_for_guest(guest_id);

	if (!gcpu) {
		/* no gcpu for the current guest on the current host cpu */
		return;
	}

	mon_gcpu_physical_memory_modified(gcpu);
}


/* assumption - all CPUs stopped */
void guest_end_physical_memory_perm_update(guest_handle_t guest)
{
	event_gpm_modification_data_t gpm_modification_data;
	guest_cpu_handle_t gcpu;

	MON_ASSERT(guest);

	/* prepare to raise events */
	gpm_modification_data.guest_id = guest->id;
	gpm_modification_data.operation = MON_MEM_OP_UPDATE;

	gcpu = mon_scheduler_get_current_gcpu_for_guest(guest_get_id(guest));
	MON_ASSERT(gcpu);

	event_raise(EVENT_END_GPM_MODIFICATION_BEFORE_CPUS_RESUMED, gcpu,
		&gpm_modification_data);
	start_all_cpus(NULL, NULL);
	event_raise(EVENT_END_GPM_MODIFICATION_AFTER_CPUS_RESUMED, gcpu,
		&gpm_modification_data);
}

/* assumption - all CPUs stopped */
void guest_end_physical_memory_modifications(guest_handle_t guest)
{
	event_gpm_modification_data_t gpm_modification_data;
	ipc_destination_t ipc_dest;
	guest_cpu_handle_t gcpu;

	MON_ASSERT(guest);

	/* notify gcpu of the guest running on the current host cpu */
	guest_notify_gcpu_about_gpm_change(guest->id,
		(void *)(size_t)guest->id);

	/* notify all other gcpu of the guest */
	ipc_dest.addr_shorthand = IPI_DST_ALL_EXCLUDING_SELF;
	ipc_dest.addr = 0;
	ipc_execute_handler(ipc_dest, guest_notify_gcpu_about_gpm_change,
		(void *)(size_t)guest->id);

	/* prepare to raise events */
	gpm_modification_data.guest_id = guest->id;
	gpm_modification_data.operation = MON_MEM_OP_RECREATE;

	gcpu = mon_scheduler_get_current_gcpu_for_guest(guest_get_id(guest));
	MON_ASSERT(gcpu);

	event_raise(EVENT_END_GPM_MODIFICATION_BEFORE_CPUS_RESUMED, gcpu,
		&gpm_modification_data);
	start_all_cpus(NULL, NULL);
	event_raise(EVENT_END_GPM_MODIFICATION_AFTER_CPUS_RESUMED, gcpu,
		&gpm_modification_data);
}

guest_handle_t guest_dynamic_create(boolean_t stop_and_notify,
				    const mon_policy_t *guest_policy)
{
	guest_handle_t guest = NULL;
	guest_id_t guest_id = INVALID_GUEST_ID;
	event_guest_create_data_t guest_create_event_data;

	if (TRUE == stop_and_notify) {
		stop_all_cpus();
	}

	/* create guest */
	guest = guest_register(ANONYMOUS_MAGIC_NUMBER, 0,
		(uint32_t)-1 /* cpu affinity */,
		guest_policy);

	if (!guest) {
		MON_LOG(mask_anonymous, level_trace,
			"Cannot create guest with the following params: \n"
			"\t\tguest_magic_number    = %#x\n"
			"\t\tphysical_memory_size  = %#x\n"
			"\t\tcpu_affinity          = %#x\n",
			guest_magic_number(guest),
			0, guest_cpu_affinity(guest));

		return NULL;
	}

	guest_id = guest_get_id(guest);

	vmexit_guest_initialize(guest_id);
	ipc_guest_initialize(guest_id);
	event_manager_guest_initialize(guest_id);

#ifdef DEBUG
	guest_register_vmcall_services(guest);
#endif

	MON_LOG(mask_anonymous,
		level_trace,
		"Created new guest #%d\r\n",
		guest_id);

	if (TRUE == stop_and_notify) {
		mon_zeromem(&guest_create_event_data,
			sizeof(guest_create_event_data));
		guest_create_event_data.guest_id = guest_id;

		event_raise(EVENT_GUEST_CREATE, NULL, &guest_create_event_data);

		start_all_cpus(NULL, NULL);
	}
	return guest;
}

boolean_t guest_dynamic_assign_memory(guest_handle_t src_guest,
				      guest_handle_t dst_guest,
				      gpm_handle_t memory_map)
{
	gpm_handle_t src_gpm = NULL, dst_gpm = NULL;
	gpm_ranges_iterator_t gpm_iter = GPM_INVALID_RANGES_ITERATOR;
	gpa_t gpa = 0, src_gpa = 0;
	uint64_t size = 0;
	boolean_t status = FALSE;
	hpa_t hpa = 0;
	uint64_t i;
	mam_attributes_t attrs;

	MON_ASSERT(dst_guest);
	MON_ASSERT(memory_map);

	dst_gpm = gcpu_get_current_gpm(dst_guest);
	gpm_iter = gpm_get_ranges_iterator(dst_gpm);

	/* check that target gpm is empty */
	MON_ASSERT(GPM_INVALID_RANGES_ITERATOR == gpm_iter);
	if (GPM_INVALID_RANGES_ITERATOR != gpm_iter) {
		return FALSE;
	}

	if (src_guest != NULL) {
		guest_begin_physical_memory_modifications(src_guest);

		gpm_iter = gpm_get_ranges_iterator(memory_map);

		while (GPM_INVALID_RANGES_ITERATOR != gpm_iter) {
			gpm_iter = gpm_get_range_details_from_iterator(
				memory_map,
				gpm_iter,
				&gpa,
				&size);

			status = mon_gpm_gpa_to_hpa(memory_map,
				gpa,
				&hpa,
				&attrs);
			MON_ASSERT(status);

			src_gpm = gcpu_get_current_gpm(src_guest);
			for (i = hpa; i < hpa + size; i += PAGE_4KB_SIZE) {
				status = mon_gpm_hpa_to_gpa(src_gpm,
					hpa,
					&src_gpa);
				MON_ASSERT(status);

				mon_gpm_remove_mapping(src_gpm,
					src_gpa,
					PAGE_4KB_SIZE);
			}
		}

		guest_end_physical_memory_modifications(src_guest);
	}

	status = mon_gpm_copy(src_gpm, dst_gpm, FALSE, mam_no_attributes);
	MON_ASSERT(status);

	return TRUE;
}

guest_cpu_handle_t guest_dynamic_add_cpu(guest_handle_t guest,
					 const mon_guest_cpu_startup_state_t *
					 gcpu_startup, cpu_id_t host_cpu,
					 boolean_t ready_to_run,
					 boolean_t stop_and_notify)
{
	guest_cpu_handle_t gcpu;

	if (TRUE == stop_and_notify) {
		guest_before_dynamic_add_cpu();
	}

	gcpu = guest_add_cpu(guest);

	MON_ASSERT(gcpu);

	/* register with scheduler */
	scheduler_register_gcpu(gcpu, host_cpu, ready_to_run);

	if (gcpu_startup != NULL) {
		MON_LOG(mask_anonymous,
			level_trace,
			"Setting up initial state for the newly created Guest CPU\n");
		gcpu_initialize(gcpu, gcpu_startup);
	} else {
		MON_LOG(mask_anonymous, level_trace,
			"Newly created Guest CPU was initialized with"
			" the Wait-For-SIPI state\n");
	}
	host_cpu_vmcs_init(gcpu);

	if (TRUE == stop_and_notify) {
		guest_after_dynamic_add_cpu(gcpu);
	}
	return gcpu;
}


static
void raise_gcpu_add_event(cpu_id_t from UNUSED, void *arg)
{
	cpu_id_t this_cpu_id = hw_cpu_id();
	guest_cpu_handle_t gcpu = (guest_cpu_handle_t)arg;

	MON_LOG(mask_anonymous, level_trace,
		"cpu#%d raise gcpu add event gcpu %p\r\n", this_cpu_id, gcpu);
	if (this_cpu_id == scheduler_get_host_cpu_id(gcpu)) {
		event_raise(EVENT_GCPU_ADD, gcpu, NULL);
	}
}

void guest_before_dynamic_add_cpu(void)
{
	stop_all_cpus();
}

void guest_after_dynamic_add_cpu(guest_cpu_handle_t gcpu)
{
	cpu_id_t cpu_id = hw_cpu_id();

	if (gcpu) {
		/* created ok */
		host_cpu_vmcs_init(gcpu);
		MON_LOG(mask_anonymous,
			level_trace,
			"CPU#%d: Notify all on added gcpu: %p host_cpu: %d\r\n",
			cpu_id,
			gcpu,
			scheduler_get_host_cpu_id(gcpu));
		start_all_cpus(raise_gcpu_add_event, gcpu);
		MON_LOG(mask_anonymous,
			level_trace,
			"CPU#%d: raise local gcpu add\r\n",
			cpu_id);
		raise_gcpu_add_event(cpu_id, gcpu);
	} else {
		/* creation failed */
		start_all_cpus(NULL, NULL);
	}
}


/* utils */

#ifdef DEBUG
extern void mon_io_emulator_register(guest_id_t guest_id);
void guest_register_vmcall_services(guest_handle_t guest)
{
	guest_id_t guest_id = guest_get_id(guest);

	mon_io_emulator_register(guest_id);
}
#endif
