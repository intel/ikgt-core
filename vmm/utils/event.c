/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/* ++ Module Name: event_mgr Abstract: Event delivery mechanism Based on the
 * 'Observer' pattern -- */

#include "event.h"
#include "dbg.h"
#include "heap.h"

#include "lib/util.h"

#define local_print(fmt, ...)
//#define local_print(fmt, ...) vmm_printf(fmt, ##__VA_ARGS__)

typedef struct _evnet_entry_t{
	event_callback_t callback;
	struct _evnet_entry_t *next;
} event_entry_t;

static event_entry_t* g_event_entry[EVENTS_COUNT];

/* TODO: using lock for racing. currently since event_register()
** is called in BSP only (before first resume), there's no need
** to use lock */
void event_register(vmm_event_t e, event_callback_t call)
{
	event_entry_t* entry = NULL;

	print_trace("%s(): e=%d, call=0x%llx\n", __FUNCTION__, e, call);

	D(VMM_ASSERT(e < EVENTS_COUNT));
	D(VMM_ASSERT(call));

#ifdef DEBUG
	for (entry = g_event_entry[e]; entry; entry=entry->next)
	{
		// not allow same caller for one event
		VMM_ASSERT(entry->callback != call);
	}
#endif

	entry = mem_alloc(sizeof(event_entry_t));
	entry->callback = call;
	entry->next = g_event_entry[e];
	g_event_entry[e] = entry;
}

void event_raise(guest_cpu_handle_t gcpu, vmm_event_t e, void *p)
{
	event_entry_t * entry;

	D(if(EVENT_PROCESS_NMI_BEFORE_RESUME != e) {
		local_print("%s(): e=%d\n", __FUNCTION__, e)});

	D(VMM_ASSERT(e < EVENTS_COUNT));

	for (entry = g_event_entry[e]; entry; entry=entry->next)
	{
		entry->callback(gcpu, p);
	}
}

