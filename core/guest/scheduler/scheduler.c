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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(SCHEDULER_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(SCHEDULER_C, __condition)
#include "scheduler.h"
#include "hw_utils.h"
#include "heap.h"
#include "guest.h"
#include "mon_dbg.h"
#include "list.h"
#include "memory_allocator.h"
#include "lock.h"

/*
 *
 * Guest Scheduler
 *
 * Principles:
 *
 * 1. Scheduler works independently on each host CPU
 * 2. Scheduler on different host CPUs may communicate to make common decision
 *
 */

/*---------------------- types and globals -------------------------------- */

/* scheduler vCPU object */
typedef struct scheduler_vcpu_object_t {
	guest_cpu_handle_t		gcpu;

	cpu_id_t			host_cpu;
	uint16_t			flags;

	uint32_t			reserved;
	struct scheduler_vcpu_object_t *next_same_host_cpu;
	struct scheduler_vcpu_object_t *next_all_cpus;
} scheduler_vcpu_object_t;

/* scheduler_vcpu_object_t flags */
typedef enum {
	VCPU_ALLOCATED_FLAG = 0,        /* vcpu is allocated for some guest */
	VCPU_READY_FLAG                 /* vcpu is ready for execution */
} vcpu_flags_enum_t;

#define SET_ALLOCATED_FLAG(obj)    BIT_SET((obj)->flags, VCPU_ALLOCATED_FLAG)
#define CLR_ALLOCATED_FLAG(obj)    BIT_CLR((obj)->flags, VCPU_ALLOCATED_FLAG)
#define GET_ALLOCATED_FLAG(obj)    BIT_GET((obj)->flags, VCPU_ALLOCATED_FLAG)

#define SET_READY_FLAG(obj)    BIT_SET((obj)->flags, VCPU_READY_FLAG)
#define CLR_READY_FLAG(obj)    BIT_CLR((obj)->flags, VCPU_READY_FLAG)
#define GET_READY_FLAG(obj)    BIT_GET((obj)->flags, VCPU_READY_FLAG)

typedef struct {
	scheduler_vcpu_object_t *vcpu_obj_list;
	scheduler_vcpu_object_t *current_vcpu_obj;
} scheduler_cpu_state_t;

static
scheduler_vcpu_object_t *scheduler_get_current_vcpu_for_guest(
	guest_id_t guest_id);

static uint16_t g_host_cpus_count;
static uint16_t g_registered_vcpus_count;

/* allocated space for internal objects */
static scheduler_vcpu_object_t *g_registered_vcpus;

/* scheduler state per host CPU */
static scheduler_cpu_state_t *g_scheduler_state;

/* lock to support guest addition while performing scheduling operations */
static mon_read_write_lock_t g_registration_lock[1];

/* --------------------------- internal functions ------------------------- */
static
scheduler_vcpu_object_t *gcpu_2_vcpu_obj(guest_cpu_handle_t gcpu)
{
	scheduler_vcpu_object_t *vcpu_obj = NULL;

	for (vcpu_obj = g_registered_vcpus; vcpu_obj != NULL;
	     vcpu_obj = vcpu_obj->next_all_cpus) {
		if (vcpu_obj->gcpu == gcpu) {
			return vcpu_obj;
		}
	}

	return NULL;
}

/* list funcs */
void add_to_per_cpu_list(scheduler_vcpu_object_t *vcpu_obj)
{
	cpu_id_t host_cpu = vcpu_obj->host_cpu;
	scheduler_cpu_state_t *state = &(g_scheduler_state[host_cpu]);

	vcpu_obj->next_same_host_cpu = state->vcpu_obj_list;
	state->vcpu_obj_list = vcpu_obj;
}

/* ----------------------- interface functions ---------------------------- */

/* init */
void scheduler_init(uint16_t number_of_host_cpus)
{
	uint32_t memory_for_state = 0;

	mon_memset(g_registration_lock, 0, sizeof(g_registration_lock));

	g_host_cpus_count = number_of_host_cpus;

	MON_ASSERT(number_of_host_cpus != 0);

	/* count needed memory amount */
	memory_for_state = sizeof(scheduler_cpu_state_t) * g_host_cpus_count;

	lock_initialize_read_write_lock(g_registration_lock);

	g_scheduler_state =
		(scheduler_cpu_state_t *)mon_malloc(memory_for_state);

	MON_ASSERT(g_scheduler_state != 0);
}

/* register guest cpu */
void scheduler_register_gcpu(guest_cpu_handle_t gcpu_handle,
			     cpu_id_t host_cpu_id,
			     boolean_t schedule_immediately)
{
	scheduler_vcpu_object_t *vcpu_obj = NULL;

	vcpu_obj =
		(scheduler_vcpu_object_t *)mon_malloc(sizeof(
				scheduler_vcpu_object_t));
	MON_ASSERT(vcpu_obj);

	interruptible_lock_acquire_writelock(g_registration_lock);

	vcpu_obj->next_all_cpus = g_registered_vcpus;
	g_registered_vcpus = vcpu_obj;

	hw_interlocked_increment((int32_t *)&g_registered_vcpus_count);

	vcpu_obj->gcpu = gcpu_handle;
	vcpu_obj->flags = 0;
	vcpu_obj->host_cpu = host_cpu_id;

	SET_ALLOCATED_FLAG(vcpu_obj);

	if (schedule_immediately) {
		SET_READY_FLAG(vcpu_obj);
	}

	/* add to the per-host-cpu list */
	add_to_per_cpu_list(vcpu_obj);

	lock_release_writelock(g_registration_lock);
}

/* Get current guest_cpu_handle_t */
guest_cpu_handle_t mon_scheduler_current_gcpu(void)
{
	scheduler_vcpu_object_t *vcpu_obj = 0;

	vcpu_obj = g_scheduler_state[hw_cpu_id()].current_vcpu_obj;

	MON_ASSERT(vcpu_obj != NULL);

	return vcpu_obj == NULL ? NULL : vcpu_obj->gcpu;
}

/* Get Host CPU Id for which given Guest CPU is assigned.
 * Function assumes gcpu as valid input.
 * Validate gcpu in caller. */
uint16_t scheduler_get_host_cpu_id(guest_cpu_handle_t gcpu)
{
	scheduler_vcpu_object_t *vcpu_obj = NULL;

	interruptible_lock_acquire_readlock(g_registration_lock);
	vcpu_obj = gcpu_2_vcpu_obj(gcpu);
	MON_ASSERT(vcpu_obj);
	lock_release_readlock(g_registration_lock);

	return vcpu_obj->host_cpu;
}

/* iterator */
guest_cpu_handle_t
scheduler_same_host_cpu_gcpu_next(scheduler_gcpu_iterator_t *ctx)
{
	scheduler_vcpu_object_t *vcpu_obj;

	MON_ASSERT(ctx);
	vcpu_obj = *ctx;

	if (vcpu_obj) {
		vcpu_obj = vcpu_obj->next_same_host_cpu;
		*ctx = vcpu_obj;
	}

	return vcpu_obj ? vcpu_obj->gcpu : NULL;
}

guest_cpu_handle_t
scheduler_same_host_cpu_gcpu_first(scheduler_gcpu_iterator_t *ctx,
				   cpu_id_t host_cpu_id)
{
	scheduler_vcpu_object_t *vcpu_obj;

	if (!(host_cpu_id < g_host_cpus_count)) {
		return NULL;
	}
	if (!ctx) {
		return NULL;
	}

	MON_ASSERT(g_scheduler_state);
	vcpu_obj = g_scheduler_state[host_cpu_id].vcpu_obj_list;
	*ctx = vcpu_obj;

	return vcpu_obj ? vcpu_obj->gcpu : NULL;
}

/*
 * scheduler
 */
guest_cpu_handle_t scheduler_select_initial_gcpu(void)
{
	cpu_id_t host_cpu = hw_cpu_id();
	scheduler_cpu_state_t *state = &(g_scheduler_state[host_cpu]);
	scheduler_vcpu_object_t *next_vcpu = state->vcpu_obj_list;

	/* very simple implementation */
	/* assume only one guest per host CPU */
	if (!(next_vcpu && GET_READY_FLAG(next_vcpu))) {
		return NULL;
	}

	state->current_vcpu_obj = next_vcpu;
	/* load full state of new guest from memory */
	gcpu_swap_in(state->current_vcpu_obj->gcpu);

	return next_vcpu->gcpu;
}

guest_cpu_handle_t scheduler_select_next_gcpu(void)
{
	cpu_id_t host_cpu = hw_cpu_id();
	scheduler_cpu_state_t *state = &(g_scheduler_state[host_cpu]);
	scheduler_vcpu_object_t *next_vcpu = NULL;

	if (state->current_vcpu_obj != NULL) {
		next_vcpu = state->current_vcpu_obj->next_same_host_cpu;
	}
	if (next_vcpu == NULL) {
		next_vcpu = state->vcpu_obj_list;
	}

	/* very simple implementation assume only one guest per host CPU */
	if (!(next_vcpu && GET_READY_FLAG(next_vcpu))) {
		return NULL;
	}

	if (state->current_vcpu_obj != next_vcpu) {
		if (state->current_vcpu_obj != NULL) {
			/* save full state of prev. guest in memory */
			gcpu_swap_out(state->current_vcpu_obj->gcpu);
		}
		state->current_vcpu_obj = next_vcpu;
		/* load full state of new guest from memory */
		gcpu_swap_in(state->current_vcpu_obj->gcpu);
	}

	return next_vcpu->gcpu;
}

/* Function assumes input parameter gcpu is valid.
 * Validate in caller function.  */
guest_cpu_handle_t scheduler_schedule_gcpu(guest_cpu_handle_t gcpu)
{
	cpu_id_t host_cpu = hw_cpu_id();
	scheduler_cpu_state_t *state = NULL;
	scheduler_vcpu_object_t *next_vcpu = gcpu_2_vcpu_obj(gcpu);

	if (!(next_vcpu && GET_READY_FLAG(next_vcpu))) {
		return NULL;
	}

	state = &(g_scheduler_state[host_cpu]);

	if (state->current_vcpu_obj != next_vcpu) {
		if (state->current_vcpu_obj != NULL) {
			/* save full state of prev. guest in memory */
			gcpu_swap_out(state->current_vcpu_obj->gcpu);
		}
		state->current_vcpu_obj = next_vcpu;
		/* load full state of new guest from memory */
		gcpu_swap_in(state->current_vcpu_obj->gcpu);
	}

	return state->current_vcpu_obj->gcpu;
}

guest_cpu_handle_t mon_scheduler_get_current_gcpu_for_guest(guest_id_t guest_id)
{
	scheduler_vcpu_object_t *vcpu_obj;
	const virtual_cpu_id_t *vcpu_id = NULL;

	MON_ASSERT(g_scheduler_state);
	for (vcpu_obj = g_scheduler_state[hw_cpu_id()].vcpu_obj_list;
	     NULL != vcpu_obj; vcpu_obj = vcpu_obj->next_same_host_cpu) {
		vcpu_id = mon_guest_vcpu(vcpu_obj->gcpu);
		/* paranoid check. If assertion fails, possible memory corruption. */
		MON_ASSERT(vcpu_id);
		if (vcpu_id->guest_id == guest_id) {
			/* found */
			return vcpu_obj->gcpu;
		}
	}
	return NULL;
}
