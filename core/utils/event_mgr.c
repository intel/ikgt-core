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

/* ++ Module Name: event_mgr Abstract: Event delivery mechanism Based on the
 * 'Observer' pattern -- */

#include "file_codes.h"
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(EVENT_MGR_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(EVENT_MGR_C, __condition)
#include "mon_objects.h"
#include "guest_cpu.h"
#include "lock.h"
#include "event_mgr.h"
#include "common_libc.h"
#include "mon_dbg.h"
#include "heap.h"
#include "hash64_api.h"
#include "memory_allocator.h"
#include "guest.h"
#include "list.h"

#define OBSERVERS_LIMIT         5
#define NO_EVENT_SPECIFIC_LIMIT ((uint32_t)-1)

typedef struct {
	mon_read_write_lock_t	lock;
	event_callback_t	call[OBSERVERS_LIMIT];
} event_entry_t;

typedef struct {
	event_entry_t event[EVENTS_COUNT];
} cpu_events_t;

typedef struct {
	event_entry_t	event[EVENTS_COUNT];
	list_element_t	link;
	guest_id_t	guest_id;
	uint8_t		pad[6];
} guest_events_t;

typedef struct {
	hash64_handle_t gcpu_events;
	list_element_t	guest_events;
	/* events not related to particular gcpu, e.g. guest create */
	event_entry_t	general_event[EVENTS_COUNT];
} event_manager_t;

uint32_t host_physical_cpus;
event_manager_t event_mgr;

/*
 *  event_characteristics_t:
 *
 *  Specify event specific characteristics, currently: name and observers limits.
 *  This list should be IDENTICAL(!) to mon_event_t enumration.
 */
event_characteristics_t events_characteristics[] = {
	/* { Event observers Limit , observ.registered, " Event Name "},
	 * emulator */
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_EMULATOR_BEFORE_MEM_WRITE" },
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_EMULATOR_AFTER_MEM_WRITE" },
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_EMULATOR_AS_GUEST_ENTER" },
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_EMULATOR_AS_GUEST_LEAVE" },

	/* guest cpu CR writes */
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GCPU_AFTER_GUEST_CR0_WRITE" },
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GCPU_AFTER_GUEST_CR3_WRITE" },
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GCPU_AFTER_GUEST_CR4_WRITE" },

	/* guest cpu invalidate page */
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GCPU_INVALIDATE_PAGE" },
	{ 1,			   EVENT_GCPU_SCOPE,
	  (char8_t *)"EVENT_GCPU_PAGE_FAULT"		},

	/* guest cpu msr writes */
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GCPU_AFTER_EFER_MSR_WRITE" },
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GCPU_AFTER_PAT_MSR_WRITE" },
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GCPU_AFTER_MTRR_MSR_WRITE" },

	/* guest activity state */
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GCPU_ACTIVITY_STATE_CHANGE" },
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GCPU_ENTERING_S3" },
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GCPU_RETRUNED_FROM_S3" },

	/* EPT events */
	{ 1,			   EVENT_GCPU_SCOPE,
	  (char8_t *)"EVENT_GCPU_EPT_MISCONFIGURATION"	},
	{ 1,			   EVENT_GCPU_SCOPE,
	  (char8_t *)"EVENT_GCPU_EPT_VIOLATION"		},

	/* MTF events */
	{ 1,			   EVENT_GCPU_SCOPE,
	  (char8_t *)"EVENT_GCPU_MTF"			},

	/* GPM modification */
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_BEGIN_GPM_MODIFICATION_BEFORE_CPUS_STOPPED" },
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_BEGIN_GPM_MODIFICATION_AFTER_CPUS_STOPPED" },
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_END_GPM_MODIFICATION_BEFORE_CPUS_RESUMED" },
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_END_GPM_MODIFICATION_AFTER_CPUS_RESUMED" },

	/* guest memory modification */
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_BEGIN_GUEST_MEMORY_MODIFICATION" },
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_END_GUEST_MEMORY_MODIFICATION" },

	/* guest lifecycle */
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GUEST_CREATE"		},
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GUEST_DESTROY"		},

	/* gcpu lifecycle */
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GCPU_ADD"			},
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GCPU_REMOVE"		},
	{ NO_EVENT_SPECIFIC_LIMIT, EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GUEST_LAUNCH"		},

	{ 1,			   EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GUEST_CPU_BREAKPOINT"	},
	{ 1,			   EVENT_ALL_SCOPE,
	  (char8_t *)"EVENT_GUEST_CPU_SINGLE_STEP"	},
};

static
boolean_t event_manager_add_gcpu(guest_cpu_handle_t gcpu, void *pv);
static
boolean_t event_register_internal(event_entry_t *p_event, mon_event_t e,        /* in: * event */
				  event_callback_t call);                       /* in: callback to register on event e */
static
boolean_t event_unregister_internal(event_entry_t *p_event, mon_event_t e,      /* in: * event */
				    event_callback_t call);                     /* in: callback to register on event e */
static
boolean_t event_raise_internal(event_entry_t *p_event, mon_event_t e,           /* in: event */
			       guest_cpu_handle_t gcpu,                         /* in: guest cpu */
			       void *p);                                        /* in: pointer to event specific structure */
static
boolean_t event_global_raise(mon_event_t e,                                     /* in: event */
			     guest_cpu_handle_t gcpu,                           /* in: guest cpu */
			     void *p);                                          /* in: pointer to event specific structure */
static
boolean_t event_guest_raise(mon_event_t e,                                      /* in: event */
			    guest_cpu_handle_t gcpu,                            /* in: guest cpu */
			    void *p);                                           /* in: pointer to event specific structure */
static
boolean_t event_gcpu_raise(mon_event_t e,                                       /* in: event */
			   guest_cpu_handle_t gcpu,                             /* in: guest cpu */
			   void *p);                                            /* in: pointer to event specific structure */

/*---------------------------------- Code ------------------------------------*/

static
event_entry_t *get_gcpu_observers(mon_event_t e, guest_cpu_handle_t gcpu)
{
	const virtual_cpu_id_t *p_vcpu;
	cpu_events_t *p_cpu_events = NULL;
	event_entry_t *p_event = NULL;
	boolean_t res;

	p_vcpu = mon_guest_vcpu(gcpu);
	MON_ASSERT(p_vcpu);
	res = hash64_lookup(event_mgr.gcpu_events,
		(uint64_t)(p_vcpu->guest_id << (8 *
						sizeof(guest_id_t)) |
			   p_vcpu->guest_cpu_id),
		(uint64_t *)&p_cpu_events);

	if (p_cpu_events != NULL) {
		p_event = &(p_cpu_events->event[e]);
	}
	return p_event;
}

static
event_entry_t *get_guest_observers(mon_event_t e, guest_handle_t guest)
{
	event_entry_t *p_event = NULL;
	guest_id_t guest_id = guest_get_id(guest);
	list_element_t *iter = NULL;

	LIST_FOR_EACH(&event_mgr.guest_events, iter) {
		guest_events_t *p_guest_events;

		p_guest_events = LIST_ENTRY(iter, guest_events_t, link);
		if (p_guest_events->guest_id == guest_id) {
			p_event = &p_guest_events->event[e];
			break;
		}
	}
	return p_event;
}

static
event_entry_t *get_global_observers(mon_event_t e)
{
	return &(event_mgr.general_event[e]);
}

static
uint32_t event_observers_limit(mon_event_t e)
{
	uint32_t observers_limits = 0;

	MON_ASSERT(e <= ARRAY_SIZE(events_characteristics));

	/*
	 * See if event has specific observers limits. (If none, we'll use the array
	 * boundry limits).
	 */
	if (events_characteristics[e].specific_observers_limits ==
	    NO_EVENT_SPECIFIC_LIMIT) {
		observers_limits = OBSERVERS_LIMIT;
	} else {
		observers_limits =
			(uint32_t)events_characteristics[e].
			specific_observers_limits;
		MON_ASSERT(observers_limits <= OBSERVERS_LIMIT);
	}
	MON_ASSERT(observers_limits > 0);

	return observers_limits;
}

uint32_t event_manager_initialize(uint32_t num_of_host_cpus)
{
	event_entry_t *general_event;
	int i;
	guest_handle_t guest = NULL;
	guest_id_t guest_id = INVALID_GUEST_ID;
	guest_econtext_t context;

	/*
	 *  Assert that all events are registed both in events_characteristics
	 *  and in the events enumeration mon_event_t
	 */
	MON_ASSERT(ARRAY_SIZE(events_characteristics) == EVENTS_COUNT);

	host_physical_cpus = num_of_host_cpus;

	mon_memset(&event_mgr, 0, sizeof(event_mgr));

	event_mgr.gcpu_events =
		hash64_create_default_hash(
			host_physical_cpus * host_physical_cpus);

	for (i = 0; i < EVENTS_COUNT; i++) {
		general_event = &(event_mgr.general_event[i]);
		lock_initialize_read_write_lock(&(general_event->lock));
	}

	list_init(&event_mgr.guest_events);

	for (guest = guest_first(&context); guest != NULL;
	     guest = guest_next(&context)) {
		guest_id = guest_get_id(guest);
		event_manager_guest_initialize(guest_id);
	}

	event_global_register(EVENT_GCPU_ADD, event_manager_add_gcpu);

	return 0;
}

uint32_t event_manager_guest_initialize(guest_id_t guest_id)
{
	guest_cpu_handle_t gcpu;
	guest_gcpu_econtext_t gcpu_context;
	guest_handle_t guest = mon_guest_handle(guest_id);
	guest_events_t *p_new_guest_events;
	event_entry_t *event;
	int i;

	p_new_guest_events = mon_malloc(sizeof(*p_new_guest_events));
	MON_ASSERT(p_new_guest_events);
	mon_memset(p_new_guest_events, 0, sizeof(*p_new_guest_events));

	/* init lock for each event */
	for (i = 0; i < EVENTS_COUNT; i++) {
		event = &(p_new_guest_events->event[i]);
		lock_initialize_read_write_lock(&(event->lock));
	}

	p_new_guest_events->guest_id = guest_id;

	/* for each guest/cpu we keep the event (callbacks) array */
	for (gcpu = mon_guest_gcpu_first(guest, &gcpu_context); gcpu;
	     gcpu = mon_guest_gcpu_next(&gcpu_context))
		event_manager_gcpu_initialize(gcpu);

	list_add(&event_mgr.guest_events, &p_new_guest_events->link);

	return 0;
}


static
boolean_t event_manager_add_gcpu(guest_cpu_handle_t gcpu, void *pv UNUSED)
{
	event_manager_gcpu_initialize(gcpu);
	return TRUE;
}


uint32_t event_manager_gcpu_initialize(guest_cpu_handle_t gcpu)
{
	const virtual_cpu_id_t *p_vcpu = NULL;
	cpu_events_t *gcpu_events = NULL;
	event_entry_t *event = NULL;
	int i;

	p_vcpu = mon_guest_vcpu(gcpu);
	MON_ASSERT(p_vcpu);
	gcpu_events = (cpu_events_t *)mon_malloc(sizeof(cpu_events_t));
	MON_ASSERT(gcpu_events);

	MON_LOG(mask_anonymous, level_trace,
		"event mgr add gcpu guest id=%d cpu id=%d with key %p\n",
		p_vcpu->guest_id, p_vcpu->guest_cpu_id,
		(uint64_t)(p_vcpu->guest_id << (8 *
						sizeof(guest_id_t)) |
			   p_vcpu->guest_cpu_id));
	hash64_insert(event_mgr.gcpu_events,
		(uint64_t)(p_vcpu->guest_id << (8 * sizeof(guest_id_t)) |
			   p_vcpu->guest_cpu_id), (uint64_t)gcpu_events);

	/* init lock for each event */
	for (i = 0; i < EVENTS_COUNT; i++) {
		event = &(gcpu_events->event[i]);
		lock_initialize_read_write_lock(&(event->lock));
	}

	return 0;
}

boolean_t event_register_internal(event_entry_t *p_event,
				  mon_event_t e,                /* in: event */
				  event_callback_t call)        /* in: callback to register on event e */
{
	uint32_t i = 0;
	uint32_t observers_limits;
	boolean_t registered = FALSE;

	observers_limits = event_observers_limit(e);

	lock_acquire_writelock(&p_event->lock);

	/*
	 *  Find free observer slot
	 */
	while (i < observers_limits && p_event->call[i])
		++i;

	if (i < observers_limits) {
		p_event->call[i] = call;
		registered = TRUE;
	} else {
		/*
		 *  Exceeding allowed observers count
		 */
		MON_DEADLOOP();
	}

	lock_release_writelock(&p_event->lock);
	return registered;
}

boolean_t event_global_register(mon_event_t e,                  /* in: event */
				event_callback_t call)          /* in: callback to register on event e */
{
	event_entry_t *list;

	if (call == 0) {
		return FALSE;
	}
	if (e >= EVENTS_COUNT) {
		return FALSE;
	}
	if (0 == (events_characteristics[e].scope & EVENT_GLOBAL_SCOPE)) {
		return FALSE;
	}
	list = get_global_observers(e);
	return event_register_internal(list, e, call);
}

boolean_t event_gcpu_register(mon_event_t e,                    /* in: event */
			      guest_cpu_handle_t gcpu,          /* in: guest cpu */
			      event_callback_t call)            /* in: callback to register on event e */
{
	event_entry_t *list;
	boolean_t registered = FALSE;

	if (call == 0) {
		return FALSE;
	}
	if (e >= EVENTS_COUNT) {
		return FALSE;
	}
	if (0 == (events_characteristics[e].scope & EVENT_GCPU_SCOPE)) {
		return FALSE;
	}

	list = get_gcpu_observers(e, gcpu);
	if (NULL != list) {
		registered = event_register_internal(list, e, call);
	}
	return registered;
}

boolean_t event_raise_internal(event_entry_t *p_event,
			       mon_event_t e,                   /* in: event */
			       guest_cpu_handle_t gcpu,         /* in: guest cpu */
			       void *p)                         /* in: pointer to event specific structure */
{
	uint32_t i = 0;
	uint32_t observers_limits;
	event_callback_t call[OBSERVERS_LIMIT];
	boolean_t event_is_handled = FALSE;

	observers_limits = event_observers_limit(e);

	lock_acquire_readlock(&p_event->lock);
	MON_ASSERT(observers_limits <= OBSERVERS_LIMIT);
	mon_memcpy(call, p_event->call, sizeof(call));
	lock_release_readlock(&p_event->lock);

	while (i < observers_limits && call[i]) {
		call[i] (gcpu, p);
		event_is_handled = TRUE;
		++i;
	}

	return event_is_handled;
}

boolean_t event_global_raise(mon_event_t e,             /* in: event */
			     guest_cpu_handle_t gcpu,   /* in: guest cpu */
			     void *p)                   /* in: pointer to event specific structure */
{
	event_entry_t *list;

	list = get_global_observers(e);
	return event_raise_internal(list, e, gcpu, p);
}

boolean_t event_guest_raise(mon_event_t e,              /* in: event */
			    guest_cpu_handle_t gcpu,    /* in: guest cpu */
			    void *p)                    /* in: pointer to event specific structure */
{
	guest_handle_t guest;
	event_entry_t *list;
	boolean_t event_handled = FALSE;

	MON_ASSERT(gcpu);

	guest = mon_gcpu_guest_handle(gcpu);
	MON_ASSERT(guest);
	list = get_guest_observers(e, guest);
	if (NULL != list) {
		event_handled = event_raise_internal(list, e, gcpu, p);
	}
	return event_handled;
}

boolean_t event_gcpu_raise(mon_event_t e,               /* in: event */
			   guest_cpu_handle_t gcpu,     /* in: guest cpu */
			   void *p)                     /* in: pointer to event specific structure */
{
	event_entry_t *list;
	boolean_t event_handled = FALSE;

	list = get_gcpu_observers(e, gcpu);
	if (NULL != list) {
		event_handled = event_raise_internal(list, e, gcpu, p);
	}

	return event_handled;
}

boolean_t event_raise(mon_event_t e,                    /* in: event */
		      guest_cpu_handle_t gcpu,          /* in: guest cpu */
		      void *p)                          /* in: pointer to event specific structure */
{
	boolean_t raised = FALSE;

	MON_ASSERT(e < EVENTS_COUNT);

	if (e < EVENTS_COUNT) {
		if (NULL != gcpu) {
			/* try to raise GCPU-scope event */
			raised = event_gcpu_raise(e, gcpu, p);
		}

		if (NULL != gcpu) {
			/* try to raise GUEST-scope event */
			raised = raised || event_guest_raise(e, gcpu, p);
		}

		/* try to raise global-scope event */
		raised = raised || event_global_raise(e, gcpu, p);
	}
	return raised;
}
