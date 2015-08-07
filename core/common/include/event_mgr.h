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
#ifndef _EVNET_MGR_H
#define _EVNET_MGR_H

/*
 * Event delivery mechanism Based on the 'Observer' pattern
 */

#include "mon_startup.h"

#define EVENT_MGR_ERROR         ((uint32_t)-1)

/*
 *  CALLBACK
 */
typedef boolean_t (*event_callback_t) (guest_cpu_handle_t gcpu, void *pv);

/*
 *  EVENTS
 *
 *  This enumeration specify the supported MON events.
 *  Note that for every event there should be an entry in event_characteristics_t
 *  characterizing the event in event_mgr.c.
 *
 *  failing to add entry in event_characteristics_t triggers assertion at the
 *  event_initialize_event_manger entry point
 */
typedef enum {
	/* emulator */
	EVENT_EMULATOR_BEFORE_MEM_WRITE = 0,
	EVENT_EMULATOR_AFTER_MEM_WRITE,
	EVENT_EMULATOR_AS_GUEST_ENTER,
	EVENT_EMULATOR_AS_GUEST_LEAVE,

	/* guest cpu CR writes */
	EVENT_GCPU_AFTER_GUEST_CR0_WRITE,
	EVENT_GCPU_AFTER_GUEST_CR3_WRITE,
	EVENT_GCPU_AFTER_GUEST_CR4_WRITE,

	/* guest cpu invalidate page */
	EVENT_GCPU_INVALIDATE_PAGE,
	EVENT_GCPU_PAGE_FAULT,

	/* guest cpu msr writes */
	EVENT_GCPU_AFTER_EFER_MSR_WRITE,
	EVENT_GCPU_AFTER_PAT_MSR_WRITE,
	EVENT_GCPU_AFTER_MTRR_MSR_WRITE,

	/* guest activity state */
	EVENT_GCPU_ACTIVITY_STATE_CHANGE,
	EVENT_GCPU_ENTERING_S3,
	EVENT_GCPU_RETURNED_FROM_S3,

	/* ept events */
	EVENT_GCPU_EPT_MISCONFIGURATION,
	EVENT_GCPU_EPT_VIOLATION,

	/* mtf events */
	EVENT_GCPU_MTF,

	/* GPM modification */
	EVENT_BEGIN_GPM_MODIFICATION_BEFORE_CPUS_STOPPED,
	EVENT_BEGIN_GPM_MODIFICATION_AFTER_CPUS_STOPPED,
	EVENT_END_GPM_MODIFICATION_BEFORE_CPUS_RESUMED,
	EVENT_END_GPM_MODIFICATION_AFTER_CPUS_RESUMED,

	/* guest memory modification */
	EVENT_BEGIN_GUEST_MEMORY_MODIFICATION,
	EVENT_END_GUEST_MEMORY_MODIFICATION,

	/* guest lifecycle */
	EVENT_GUEST_CREATE,
	EVENT_GUEST_DESTROY,

	/* gcpu lifecycle */
	EVENT_GCPU_ADD,
	EVENT_GCPU_REMOVE,

	EVENT_GUEST_LAUNCH,

	EVENT_GUEST_CPU_BREAKPOINT,
	EVENT_GUEST_CPU_SINGLE_STEP,

	EVENTS_COUNT
} mon_event_t;

typedef enum {
	EVENT_GLOBAL_SCOPE = 1,
	EVENT_GUEST_SCOPE = 2,
	EVENT_GCPU_SCOPE = 4,
	EVENT_ALL_SCOPE =
		(EVENT_GLOBAL_SCOPE | EVENT_GUEST_SCOPE | EVENT_GCPU_SCOPE)
} event_scope_t;

typedef struct {
	uint32_t	specific_observers_limits;
	event_scope_t	scope;
	char8_t		*event_str;
} event_characteristics_t;

/*
 *  Event Manager Interface
 */

uint32_t event_initialize_event_manger(
	const mon_startup_struct_t *startup_struct);

uint32_t event_manager_initialize(uint32_t num_of_host_cpus);
uint32_t event_manager_guest_initialize(guest_id_t guest_id);
uint32_t event_manager_gcpu_initialize(guest_cpu_handle_t gcpu);

void event_cleanup_event_manger(void);

boolean_t event_global_register(mon_event_t e,                  /* in: event */
				event_callback_t call);         /* in: callback to register on event e */

boolean_t event_gcpu_register(mon_event_t e,                    /* in: event */
			      guest_cpu_handle_t gcpu,          /* in: guest cpu */
			      event_callback_t call);           /* in: callback to register on event e */

boolean_t event_global_unregister(mon_event_t e,                /* in: event */
				  event_callback_t call);       /* in: callback to unregister from event e */

boolean_t event_guest_unregister(mon_event_t e,                 /* in: event */
				 guest_handle_t guest,          /* in: guest handle */
				 event_callback_t call);        /* in: callback to unregister from event e */

typedef enum {
	EVENT_NO_HANDLERS_REGISTERED,
	EVENT_HANDLED,
	EVENT_NOT_HANDLED,
} raise_event_retval_t;

/* returns counter of executed observers */
boolean_t event_raise(mon_event_t e,                    /* in: event */
		      guest_cpu_handle_t gcpu,          /* in: guest cpu */
		      void *p);                         /* in: pointer to event specific structure */

#endif /* _EVNET_MGR_H */
