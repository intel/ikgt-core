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

#ifndef _IPC_IMPL_H
#define _IPC_IMPL_H

#include "mon_defs.h"
#include "isr.h"
#include "list.h"
#include "ipc.h"
#include "lock.h"

#define IPC_ALIGNMENT                         ARCH_ADDRESS_WIDTH

#define IPC_CPU_ID()                          hw_cpu_id()

#define NMI_VECTOR                            2

#define NMIS_WAITING_FOR_PROCESSING(ipc)      \
	(ipc->num_received_nmi_interrupts - ipc->num_processed_nmi_interrupts)
#define IPC_NMIS_WAITING_FOR_PROCESSING(ipc)  \
	(ipc->num_of_sent_ipc_nmi_interrupts -    \
	 ipc->num_of_processed_ipc_nmi_interrupts)

typedef struct {
	ipc_message_type_t	type;
	cpu_id_t		from;
	char			padding[2];
	func_ipc_handler_t	handler;
	void			*arg;
	volatile uint32_t	*before_handler_ack;
	volatile uint32_t	*after_handler_ack;
} ipc_message_t;

typedef enum {
	IPC_CPU_NOT_ACTIVE = 0,
	IPC_CPU_ACTIVE,
	IPC_CPU_SIPI
} ipc_cpu_activity_state_t;

typedef struct {
	volatile uint64_t	num_received_nmi_interrupts;
	uint64_t		num_processed_nmi_interrupts;

	uint64_t		num_of_sent_ipc_nmi_interrupts;
	uint64_t		num_of_processed_ipc_nmi_interrupts;

	volatile uint64_t	num_blocked_nmi_injections_to_guest;
	volatile uint64_t	num_start_messages;
	volatile uint64_t	num_stop_messages;

	array_list_handle_t	message_queue;
	uint64_t		num_of_sent_ipc_messages;
	uint64_t		num_of_received_ipc_messages;

	mon_lock_t		data_lock;
} ipc_cpu_context_t;

boolean_t ipc_state_init(uint16_t number_of_host_processors);

boolean_t ipc_guest_state_init(guest_id_t guest_id);

#endif
