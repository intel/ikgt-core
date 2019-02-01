/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "gcpu.h"
#include "gcpu_switch.h"
#include "scheduler.h"
#include "dbg.h"
#include "gcpu_inject_event.h"
#include "vmcs.h"
#include "isr.h"
#include "vmm_arch.h"
#include "guest.h"
#include "host_cpu.h"
#include "lock.h"
#include "event.h"
#include "gpm.h"

#include "lib/string.h"

#define VMM_DUMP_VERSION 1
#define DEADLOOP_SIGNATURE      "XMONASST"

typedef struct dump_data {
	uint32_t 	length;
	uint8_t 	data[0];
} dump_data_t;

typedef struct dump_header {
	char		vmm_version[64]; /* version of the vmm */
	char		signature[16];  /* signature for the dump structure */
	char		error_info[32];    /* filename:linenum */
	uint16_t	cpuid;              /* cpu of the first deadloop/assert */
} dump_header_t;

typedef struct deadloop_dump {
	uint16_t 		size_of_this_struct;
	uint16_t 		version_of_this_struct;
	uint32_t		is_valid;
	dump_header_t 		header; /* dump header */
	dump_data_t 		data; /* dump data */
}PACKED deadloop_dump_t;

static deadloop_dump_t *g_dump_buf = NULL;

static void fill_dump_buf(UNUSED guest_cpu_handle_t gcpu, UNUSED void *pv)
{
#ifdef DEBUG
	event_deadloop_t *event_deadloop = (event_deadloop_t *)pv;
	uint16_t cpu_id;
#endif
	static uint32_t dump_started = 0;

	if (asm_lock_cmpxchg32(&dump_started, 1, 0) != 0) {
		/* inter lock failed, can't assure to dump the info by once */
		print_warn("there is another cpu held the lock.\n");
		return;
	}

#ifdef DEBUG
	/* skip dumping debug info if deadloop/assert happened before launch */
	if (g_dump_buf == NULL) {
		print_warn("dump buf has not been set yet.\n");
		return;
	}

	cpu_id = host_cpu_id();
	/* fill with the header info */
	vmm_sprintf_s(g_dump_buf->header.vmm_version,
			sizeof(g_dump_buf->header.vmm_version),
			"BUILT: %s -- %s", __TIME__, __DATE__);

	vmm_sprintf_s(g_dump_buf->header.signature,
			sizeof(g_dump_buf->header.signature),
			"%s",DEADLOOP_SIGNATURE);

	vmm_sprintf_s(g_dump_buf->header.error_info,
			sizeof(g_dump_buf->header.error_info),
			"%s:%d", event_deadloop->file_name, event_deadloop->line_num);

	g_dump_buf->header.cpuid = cpu_id;

	//g_dump_buf is allocated in trusty-irq driver with size= 4 Page.
	g_dump_buf->data.length = vmcs_dump_all(gcpu->vmcs, (char *)(g_dump_buf->data.data),
			4 * PAGE_4K_SIZE - sizeof(deadloop_dump_t));

	print_info("msg dumped successfully.\n");
#endif

	g_dump_buf->is_valid = TRUE;
	dump_started = 0;
	return;
}

boolean_t deadloop_setup(guest_cpu_handle_t gcpu, uint64_t dump_gva)
{
	uint64_t dump_hva;
	pf_ec_t pfec;

	D(VMM_ASSERT(gcpu));

	if(g_dump_buf != NULL)
		return FALSE;

	if(!gcpu_gva_to_hva(gcpu, dump_gva, GUEST_CAN_READ | GUEST_CAN_WRITE, &dump_hva, &pfec))
		return FALSE;

	g_dump_buf = (deadloop_dump_t *)dump_hva;
	if(g_dump_buf->version_of_this_struct != VMM_DUMP_VERSION ||
		g_dump_buf->size_of_this_struct != sizeof(deadloop_dump_t)) {
		print_warn( "%s: unknown structure version or size\n", __FUNCTION__);
		g_dump_buf = NULL;
		return FALSE;
	}

	event_register(EVENT_DEADLOOP, fill_dump_buf);

	return TRUE;
}
