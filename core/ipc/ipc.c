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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(IPC_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(IPC_C, __condition)
#include "mon_defs.h"
#include "ipc_impl.h"
#include "scheduler.h"
#include "vmcs_actual.h"
#include "vmx_ctrl_msrs.h"
#include "list.h"
#include "heap.h"
#include "guest_cpu_vmenter_event.h"
#include "mon_dbg.h"
#include "guest.h"
#include "cli.h"
#include "vmx_nmi.h"
#include "hw_includes.h"


static uint16_t num_of_host_processors;
static guest_id_t nmi_owner_guest_id;
static char *ipc_state_memory;

/* per-CPU contexts for IPC bookkeeping */
static ipc_cpu_context_t *ipc_cpu_contexts;

/* Acknowledge array. */
static volatile uint32_t *ipc_ack_array;

/* Per CPU activity state -- active/not-active (Wait-for-SIPI) */
static volatile ipc_cpu_activity_state_t *cpu_activity_state;

/* IPC send lock in order to have only one send in progress. */
static mon_lock_t send_lock;

/* Forward declaration of message preprocessing function. */
static boolean_t ipc_preprocess_message(ipc_cpu_context_t *ipc,
					cpu_id_t dst,
					ipc_message_type_t msg_type);

/* Forward declaration of IPC cli registartion function. */
static void ipc_cli_register(void);

/* Debug variables. */
static int32_t debug_not_resend;

/* ***************************** Local Utilities ************************* */

static
uint32_t ipc_get_max_pending_messages(uint32_t number_of_host_processors)
{
	/* the max ipc message queue length for each processor. */
	return number_of_host_processors;
}

static
uint32_t ipc_get_message_array_list_size(uint32_t number_of_host_processors)
{
	return (uint32_t)ALIGN_FORWARD(
		array_list_memory_size(NULL,
			sizeof(ipc_message_t),
			ipc_get_max_pending_messages
				(number_of_host_processors),
			IPC_ALIGNMENT),
		IPC_ALIGNMENT);
}

static
boolean_t ipc_hw_signal_nmi(ipc_destination_t dst)
{
	return local_apic_send_ipi(dst.addr_shorthand,
		dst.addr,
		IPI_DESTINATION_MODE_PHYSICAL,
		IPI_DELIVERY_MODE_NMI, 0,
		IPI_DELIVERY_LEVEL_ASSERT /* must be 1 */,
		IPI_DELIVERY_TRIGGER_MODE_EDGE);
}

static
boolean_t ipc_hw_signal_sipi(ipc_destination_t dst)
{
	return local_apic_send_ipi(dst.addr_shorthand, dst.addr,
		IPI_DESTINATION_MODE_PHYSICAL,
		IPI_DELIVERY_MODE_START_UP, 0xFF,
		IPI_DELIVERY_LEVEL_ASSERT,
		IPI_DELIVERY_TRIGGER_MODE_EDGE);
}

static
boolean_t ipc_cpu_is_destination(ipc_destination_t dst, cpu_id_t this_cpu_id,
				 cpu_id_t dst_cpu_id)
{
	boolean_t ret_val = FALSE;

	switch (dst.addr_shorthand) {
	case IPI_DST_SELF:
		ret_val = (this_cpu_id == dst_cpu_id);
		break;

	case IPI_DST_ALL_INCLUDING_SELF:
		ret_val = TRUE;
		break;

	case IPI_DST_ALL_EXCLUDING_SELF:
		ret_val = (this_cpu_id != dst_cpu_id);
		break;

	case IPI_DST_NO_SHORTHAND:
		ret_val = ((cpu_id_t)dst.addr == dst_cpu_id);
		break;

	case IPI_DST_CORE_ID_BITMAP:
		ret_val =
			(BITMAP_ARRAY64_GET(dst.core_bitmap, dst_cpu_id) != 0);
		break;
	}

	return ret_val;
}

/* ********************** Message Queue Management ************************ */

static
void ipc_increment_ack(volatile uint32_t *ack)
{
	if (NULL != ack) {
		hw_interlocked_increment((int32_t *)ack);
	}
}

/* NOTE: Queue function are not multi-thread safe. Caller must first aquire the
 * lock !!! */

/* FUNCTION: ipc_enqueue_message
 * DESCRIPTION: Add message to the queue. Caller must acquire the lock before
 * calling.
 * RETURN VALUE: TRUE if message was queued, FALSE if message could not be
 * queued
 */
static
boolean_t ipc_enqueue_message(ipc_cpu_context_t *ipc, ipc_message_type_t type,
			      func_ipc_handler_t handler, void *arg,
			      volatile uint32_t *before_handler_ack,
			      volatile uint32_t *after_handler_ack)
{
	ipc_message_t msg;
	cpu_id_t cpu_id = IPC_CPU_ID();

	MON_ASSERT(ipc != NULL);
	MON_ASSERT(handler != NULL);

	msg.type = type;
	msg.from = cpu_id;
	msg.handler = handler;
	msg.arg = arg;
	msg.before_handler_ack = before_handler_ack;
	msg.after_handler_ack = after_handler_ack;

	return array_list_add(ipc->message_queue, &msg);
}

/* FUNCTION: ipc_dequeue_message
 * DESCRIPTION: Dequeue message for processing. Acknowledge the sender. Caller
 * must acquire the lock before calling.
 * RETURN VALUE: TRUE if message was dequeued, FALSE if queue is empty
 */
static
ipc_message_t *ipc_dequeue_message(ipc_cpu_context_t *ipc)
{
	ipc_message_t *msg = NULL;

	MON_ASSERT(ipc != NULL);

	msg = (ipc_message_t *)array_list_first(ipc->message_queue, NULL);
	if (msg != NULL) {
		array_list_remove(ipc->message_queue, msg);
		ipc_increment_ack(msg->before_handler_ack);
		/* Receive IPC message counting. */
		ipc->num_of_received_ipc_messages++;
	}

	return msg;
}

/* ********************** IPC Mechanism *************************** */

/* FUNCTION: ipc_execute_send */
/* DESCRIPTION: Send message to destination processors. */
/* RETURN VALUE: number of CPUs on which handler is about to execute */

uint32_t ipc_execute_send(ipc_destination_t dst,
			  ipc_message_type_t type,
			  func_ipc_handler_t handler,
			  void *arg, boolean_t wait_for_handler_finish)
{
	cpu_id_t i;
	cpu_id_t sender_cpu_id = IPC_CPU_ID();
	ipc_cpu_context_t *ipc = NULL;
	volatile uint32_t num_received_acks = 0;
	uint32_t num_required_acks = 0;
	volatile uint32_t *ack_array =
		&ipc_ack_array[sender_cpu_id * num_of_host_processors];
	boolean_t status;
	ipc_destination_t single_dst;
	uint32_t wait_count = 0;
	uint64_t nmi_accounted_flag[CPU_BITMAP_MAX] = { 0 };
	uint64_t enqueue_flag[CPU_BITMAP_MAX] = { 0 };
	uint64_t next_send_tsc;

	/* Initializ ack array. */
	mon_memset((void *)ack_array, 0, num_of_host_processors *
		sizeof(uint32_t));

	for (i = 0; i < num_of_host_processors; i++) {
		if (i != sender_cpu_id) {
			/* Exclude yourself. */
			if (ipc_cpu_is_destination(dst, sender_cpu_id, i)) {
				ipc = &ipc_cpu_contexts[i];

				/* Aquire lock to prevent mutual data access. */
				lock_acquire(&ipc->data_lock);

				/* Preprocess IPC and check if need to enqueue.  */
				if (ipc_preprocess_message(ipc, i, type)) {
					boolean_t empty_queue =
						(array_list_size(ipc->
							 message_queue) == 0);
					/* Mark CPU active. */
					BITMAP_ARRAY64_SET(enqueue_flag, i);

					num_required_acks++;
					/* Do not wait for handlers to finish. */
					if (!wait_for_handler_finish) {
						status =
							ipc_enqueue_message(ipc,
								type,
								handler,
								arg,
								&ack_array[i],
								NULL);
					} else {
						/* Wait for handlers to finish. */
						status =
							ipc_enqueue_message(ipc,
								type,
								handler,
								arg,
								NULL,
								&ack_array[i]);
					}

					/* IPC sent message counting. */
					ipc->num_of_sent_ipc_messages++;

					MON_ASSERT(status);

					/* Check if IPC signal should be sent. */
					if (empty_queue) {
						/* Send IPC signal (NMI or SIPI) */
						single_dst.addr_shorthand =
							IPI_DST_NO_SHORTHAND;
						single_dst.addr = (uint8_t)i;

						if (cpu_activity_state[i] ==
						    IPC_CPU_ACTIVE) {
							BITMAP_ARRAY64_SET(
								nmi_accounted_flag,
								i);

							ipc->
							num_of_sent_ipc_nmi_interrupts
							++;

							ipc_hw_signal_nmi(
								single_dst);
						} else {
							ipc_hw_signal_sipi(
								single_dst);
						}
					}
				}

				lock_release(&ipc->data_lock);
			}
		}
	}

	if (num_required_acks > 0) {
		MON_ASSERT(hw_get_tsc_ticks_per_second() != 0);

		/* Calculate next tsc tick to resend NMI. */
		next_send_tsc = hw_rdtsc() + hw_get_tsc_ticks_per_second();
		/* Should be one second. */

		/* signal and wait for acknowledge */
		while (num_received_acks != num_required_acks) {
			/* Check wait count and time. */
			if (wait_count++ > 1000 && hw_rdtsc() > next_send_tsc) {
				wait_count = 0;
				next_send_tsc = hw_rdtsc() +
						hw_get_tsc_ticks_per_second();

				for (i = 0, num_received_acks = 0;
				     i < num_of_host_processors;
				     i++) {
					/* Send additional IPC signal to stalled cores. */
					if (BITMAP_ARRAY64_GET(enqueue_flag,
						    i) && !ack_array[i]) {
						/* exclude yourself and non active CPUs.  */
						single_dst.addr_shorthand =
							IPI_DST_NO_SHORTHAND;
						single_dst.addr = (uint8_t)i;

						/* Check that CPU is still active. */
						MON_ASSERT(
							cpu_activity_state[i] !=
							IPC_CPU_NOT_ACTIVE);
						if (!debug_not_resend) {
							ipc =
								&
								ipc_cpu_contexts
								[i];

							lock_acquire(
								&ipc->data_lock);

							if (cpu_activity_state[i] ==
							    IPC_CPU_ACTIVE) {
								if (!BITMAP_ARRAY64_GET(
									    nmi_accounted_flag,
									    i)) {
									BITMAP_ARRAY64_SET(
										nmi_accounted_flag,
										i);
									ipc->
									num_of_sent_ipc_nmi_interrupts
									++;
								}

								ipc_hw_signal_nmi(
									single_dst);

								MON_LOG(
									mask_anonymous,
									level_trace,
									"[%d] send additional nmi to %d\n",
									(int)sender_cpu_id,
									(int)i);
							} else {
								ipc_hw_signal_sipi(
									single_dst);
								MON_LOG(
									mask_anonymous,
									level_trace,
									"[%d] send additional SIPI to %d\n",
									(int)sender_cpu_id,
									(int)i);
							}

							lock_release(
								&ipc->data_lock);
						}
					}
				}
			} else {
				/* Try to processs own received messages. */
				/* To prevent deadlock situation when 2 core send messages
				 * simultaneously. */
				if (!ipc_process_one_ipc()) {
					hw_pause();
				}

				/* count received acks. */
				for (i = 0, num_received_acks = 0;
				     i < num_of_host_processors;
				     i++)
					num_received_acks += ack_array[i];
			}
		}
	}

	return num_required_acks;
}

/* FUNCTION: ipc_process_all_ipc_messages
 * DESCRIPTION: Process all IPC from this CPU's message queue. */
void ipc_process_all_ipc_messages(ipc_cpu_context_t *ipc, boolean_t nmi_flag)
{
	ipc_message_t *msg = 0;
	func_ipc_handler_t handler = NULL;
	void *arg = NULL;
	volatile uint32_t *after_handler_ack = NULL;
	boolean_t last_msg = FALSE;

	if (array_list_size(ipc->message_queue) == 0) {
		return;
	}

	/* Process all IPC messages. */
	lock_acquire(&ipc->data_lock);

	do {
		/* Get an IPC message from the queue. */
		msg = ipc_dequeue_message(ipc);

		MON_ASSERT(msg != NULL);

		/* Check for last message. */
		if (array_list_size(ipc->message_queue) == 0) {
			last_msg = TRUE;

			/* Adjust processed interrupt counters. */
			if (nmi_flag) {
				ipc->num_processed_nmi_interrupts++;
				ipc->num_of_processed_ipc_nmi_interrupts++;
			}
		}

		/* Process message. */
		handler = msg->handler;
		arg = msg->arg;
		after_handler_ack = msg->after_handler_ack;

		lock_release(&ipc->data_lock);

		handler(IPC_CPU_ID(), arg);

		lock_acquire(&ipc->data_lock);

		/* Postprocessing. */
		ipc_increment_ack(after_handler_ack);
	} while (!last_msg);

	lock_release(&ipc->data_lock);
}

/* FUNCTION: ipc_dispatcher
 * DESCRIPTION: Dequeue message and call the handler. Caller must acquire the
 * lock before calling.
 * RETURN VALUE: TRUE if message was handled, FALSE if queue is empty */
static
boolean_t ipc_dispatcher(ipc_cpu_context_t *ipc, guest_cpu_handle_t gcpu UNUSED)
{
	boolean_t nmi_injected_to_guest = FALSE;

	/* Process all IPC messages. */
	ipc_process_all_ipc_messages(ipc, TRUE);

	/* Perform decision about MNI injection to guest. */
	lock_acquire(&ipc->data_lock);

	MON_DEBUG_CODE(
		/* Sanity check. */
		if (ipc->num_received_nmi_interrupts <
		    ipc->num_processed_nmi_interrupts
		    || ipc->num_of_sent_ipc_nmi_interrupts <
		    ipc->num_of_processed_ipc_nmi_interrupts) {
			MON_LOG(mask_anonymous, level_trace,
				"[%d] IPC Anomaly\n",
				IPC_CPU_ID()); MON_DEADLOOP();
		}
		)

	/* Check if we have blocked guest NMI's. */
	if (ipc->num_blocked_nmi_injections_to_guest > 0) {
		MON_LOG(mask_anonymous,
			level_trace,
			"[%d] - %s: Blocked Injection counter = %d\n",
			IPC_CPU_ID(),
			__FUNCTION__,
			ipc->num_blocked_nmi_injections_to_guest);
		/* Set injection flag. */
		nmi_injected_to_guest = TRUE;
		/* Adjust blocked NMI counter. */
		ipc->num_blocked_nmi_injections_to_guest--;
	} else if (ipc->num_of_sent_ipc_nmi_interrupts !=
		   ipc->num_received_nmi_interrupts
		   && NMIS_WAITING_FOR_PROCESSING(ipc) !=
		   IPC_NMIS_WAITING_FOR_PROCESSING(ipc)) {
		/* MON_LOG(mask_anonymous, level_trace,"[%d] - %s: NMI_RCVD = %d
		 * NMI_PROCESSED = %d, IPC_NMI_SENT = %d IPC_NMI_PROCESSED = %d\n",
		 * IPC_CPU_ID(), __FUNCTION__, ipc->num_received_nmi_interrupts,
		 * ipc->num_processed_nmi_interrupts,
		 * ipc->num_of_sent_ipc_nmi_interrupts,
		 * ipc->num_of_processed_ipc_nmi_interrupts); */
		/* Set injection flag. */
		nmi_injected_to_guest = TRUE;
		/* Adjust common NMI processed counter. */
		ipc->num_processed_nmi_interrupts++;

		nmi_raise_this();
	}

	lock_release(&ipc->data_lock);

	return nmi_injected_to_guest;
}

/* FUNCTION: ipc_nmi_interrupt_handler
 * DESCRIPTION: ISR to handle NMI exception while in MON (vector 2).
 * Enables NMI Window for all guests to defer handling to more
 * convinient conditions (e.g. stack, blocking etc.) */
static
void ipc_nmi_interrupt_handler(const isr_parameters_on_stack_t *
			       p_stack UNUSED)
{
	cpu_id_t cpu_id = IPC_CPU_ID();
	ipc_cpu_context_t *ipc = &ipc_cpu_contexts[cpu_id];
	guest_cpu_handle_t gcpu = NULL;

	hw_interlocked_increment64(
		(int64_t *)(&ipc->num_received_nmi_interrupts));

	/* inject nmi windows to right guest on this host cpu. */
	gcpu = mon_scheduler_current_gcpu();
	MON_ASSERT(gcpu);
	vmcs_nmi_handler(mon_gcpu_get_vmcs(gcpu));
}

/* FUNCTION: ipc_nmi_window_vmexit_handler
 * DESCRIPTION: Handle Vm-Exit due to NMI Window -- handle pending IPC if any.
 * Decide on injecting NMIs to guest if required. */
boolean_t ipc_nmi_window_vmexit_handler(guest_cpu_handle_t gcpu)
{
	cpu_id_t cpu_id = IPC_CPU_ID();
	ipc_cpu_context_t *ipc = &ipc_cpu_contexts[cpu_id];

	MON_ASSERT(gcpu != NULL);

	/* disable nmi window */
	gcpu_set_pending_nmi(gcpu, 0);

	/* handle queued IPC's */
	return !ipc_dispatcher(ipc, gcpu);
}

/* FUNCTION: ipc_nmi_vmexit_handler
 * DESCRIPTION: Handle Vm-Exit due to NMI while in guest. Handle IPC if NMI was
 * due to IPC.
 * Reflect NMI back to guest if it is hardware or guest initiated NMI. */
boolean_t ipc_nmi_vmexit_handler(guest_cpu_handle_t gcpu)
{
	cpu_id_t cpu_id = IPC_CPU_ID();
	ipc_cpu_context_t *ipc = &ipc_cpu_contexts[cpu_id];

	hw_interlocked_increment64((int64_t *)&ipc->num_received_nmi_interrupts);

	hw_perform_asm_iret();

	/* Handle queued IPC's */
	return !ipc_dispatcher(ipc, gcpu);
}

/* FUNCTION: ipc_sipi_vmexit_handler
 * DESCRIPTION: Handle IPC if SIPI was due to IPC.
 * RETURN VALUE: TRUE, if SIPI was due to IPC, FALSE otherwise. */
boolean_t ipc_sipi_vmexit_handler(guest_cpu_handle_t gcpu)
{
	cpu_id_t cpu_id = IPC_CPU_ID();
	ipc_cpu_context_t *ipc = &ipc_cpu_contexts[cpu_id];
	vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
	ia32_vmx_exit_qualification_t qualification;
	boolean_t ret_val = FALSE;

	qualification.uint64 =
		mon_vmcs_read(vmcs, VMCS_EXIT_INFO_QUALIFICATION);

	/* Check if this is IPC SIPI signal. */
	if (qualification.sipi.vector == 0xFF) {
		/* Process all IPC messages. */
		ipc_process_all_ipc_messages(ipc, FALSE);

		/* Clear all NMI counters. */
		lock_acquire(&ipc->data_lock);

		ipc->num_received_nmi_interrupts = 0;
		ipc->num_processed_nmi_interrupts = 0;
		ipc->num_of_sent_ipc_nmi_interrupts = 0;
		ipc->num_of_processed_ipc_nmi_interrupts = 0;
		ipc->num_blocked_nmi_injections_to_guest = 0;

		lock_release(&ipc->data_lock);

		ret_val = TRUE;
	}

	return ret_val;
}

/* ********************** IPC Send Preprocessing *************************** */

/* FUNCTION: ipc_preprocess_normal_message
 * DESCRIPTION: Preprocess normal message. Caller must acquire the lock before
 * calling.
 * RETURN VALUE: TRUE if message must be enqueued at destination CPU, FALSE if
 * message should not be queued */
boolean_t ipc_preprocess_normal_message(ipc_cpu_context_t *ipc UNUSED,
					cpu_id_t dst)
{
	boolean_t enqueue_to_dst;

	enqueue_to_dst = (cpu_activity_state[dst] != IPC_CPU_NOT_ACTIVE);

	return enqueue_to_dst;
}

/* FUNCTION: ipc_preprocess_start_message
 * DESCRIPTION: Preprocess ON message. Caller must acquire the lock before
 * calling.
 * RETURN VALUE: TRUE if message must be enqueued at destination CPU, FALSE if
 * message should not be queued */
boolean_t ipc_preprocess_start_message(ipc_cpu_context_t *ipc,
				       cpu_id_t dst UNUSED)
{
	ipc->num_start_messages++;

	/* never enqueue 'start' message */
	return FALSE;
}

/* FUNCTION: ipc_preprocess_stop_message
 * DESCRIPTION: Preprocess OFF message. Caller must acquire the lock before
 * calling.
 * RETURN VALUE: TRUE if message must be enqueued at destination CPU, FALSE if
 * message should not be queued */
boolean_t ipc_preprocess_stop_message(ipc_cpu_context_t *ipc, cpu_id_t dst)
{
	boolean_t enqueue_to_dst;

	enqueue_to_dst = (cpu_activity_state[dst] != IPC_CPU_NOT_ACTIVE);

	ipc->num_stop_messages++;

	return enqueue_to_dst;
}

/* FUNCTION: ipc_preprocess_message
 * DESCRIPTION: Preprocess message. Caller must acquire the lock before
 * calling.
 * RETURN VALUE: TRUE if message must be enqueued at destination CPU,
 * FALSE if message should not be queued */
boolean_t ipc_preprocess_message(ipc_cpu_context_t *ipc, cpu_id_t dst,
				 ipc_message_type_t msg_type)
{
	boolean_t enqueue_to_dst = FALSE;

	switch (msg_type) {
	case IPC_TYPE_NORMAL:
		enqueue_to_dst = ipc_preprocess_normal_message(ipc, dst);
		break;

	case IPC_TYPE_START:
		enqueue_to_dst = ipc_preprocess_start_message(ipc, dst);
		break;

	case IPC_TYPE_STOP:
		enqueue_to_dst = ipc_preprocess_stop_message(ipc, dst);
		break;

	case IPC_TYPE_SYNC:
	default:
		break;
	}

	return enqueue_to_dst;
}

/* ********************** IPC API Implementation *************************** */

/* FUNCTION: ipc_send_message
 * DESCRIPTION: Send IPC to destination CPUs. Returns just before handlers are
 * about to execute.
 * RETURN VALUE: number of CPUs on which handler is about to execute */
uint32_t ipc_send_message(ipc_destination_t dst, ipc_message_type_t type,
			  func_ipc_handler_t handler, void *arg)
{
	uint32_t num_of_receivers = 0;

	if ((int)type >= IPC_TYPE_NORMAL && (int)type < IPC_TYPE_LAST) {
		switch (dst.addr_shorthand) {
		/* case IPI_DST_SELF: */
		/* case IPI_DST_ALL_INCLUDING_SELF: */
		case IPI_DST_ALL_EXCLUDING_SELF:
		case IPI_DST_NO_SHORTHAND:
		case IPI_DST_CORE_ID_BITMAP:
			num_of_receivers = ipc_execute_send(dst,
				type,
				handler,
				arg,
				FALSE);
			break;

		default:
			MON_LOG(mask_anonymous, level_trace,
				"ipc_send_message: Bad message destination"
				" shorthand 0x%X\r\n",
				dst.addr_shorthand);
			break;
		}
	} else {
		MON_LOG(mask_anonymous, level_trace,
			"ipc_send_message: Bad message type %d\r\n", type);
	}

	return num_of_receivers;
}

/* FUNCTION: ipc_send_message_sync
 * DESCRIPTION: Send IPC to destination CPUs. Returns after handlers finished
 * their execution
 * RETURN VALUE: number of CPUs on which handler is about to execute */
uint32_t ipc_send_message_sync(ipc_destination_t dst, ipc_message_type_t type,
			       func_ipc_handler_t handler, void *arg)
{
	uint32_t num_of_receivers = 0;

	if ((int)type >= IPC_TYPE_NORMAL && (int)type < IPC_TYPE_LAST) {
		switch (dst.addr_shorthand) {
		/* case IPI_DST_SELF: */
		/* case IPI_DST_ALL_INCLUDING_SELF: */
		case IPI_DST_ALL_EXCLUDING_SELF:
		case IPI_DST_NO_SHORTHAND:
		case IPI_DST_CORE_ID_BITMAP:
			num_of_receivers = ipc_execute_send(dst,
				type,
				handler,
				arg,
				TRUE);
			break;

		default:
			MON_LOG(mask_anonymous, level_trace,
				"ipc_send_message_sync: Bad message"
				" destination shorthand 0x%X\r\n",
				dst.addr_shorthand);
			break;
		}
	} else {
		MON_LOG(mask_anonymous, level_trace,
			"ipc_send_message_sync: Bad message type %d\r\n", type);
	}
	return num_of_receivers;
}

/* FUNCTION: ipc_process_one_ipc
 * DESCRIPTION: Process one IPC from this CPU's message queue.
 * RETURN VALUE: TRUE if IPC was processed, FALSE if there were no pending
 * IPCs. */
boolean_t ipc_process_one_ipc(void)
{
	cpu_id_t cpu_id = IPC_CPU_ID();
	ipc_cpu_context_t *ipc = &ipc_cpu_contexts[cpu_id];
	ipc_message_t *msg = 0;
	func_ipc_handler_t handler = NULL;
	void *arg = NULL;
	volatile uint32_t *after_handler_ack = NULL;
	boolean_t process_ipc_msg = FALSE;

	if (array_list_size(ipc->message_queue) == 0) {
		return process_ipc_msg;
	}

	lock_acquire(&ipc->data_lock);

	msg = ipc_dequeue_message(ipc);
	process_ipc_msg = (msg != NULL);

	if (process_ipc_msg) {
		/* Check for last message. */
		if (array_list_size(ipc->message_queue) == 0
		    && cpu_activity_state[cpu_id] == IPC_CPU_ACTIVE) {
			/* Adjust processed interrupt counters. */
			ipc->num_processed_nmi_interrupts++;
			ipc->num_of_processed_ipc_nmi_interrupts++;
		}

		/* Process a message. */
		handler = msg->handler;
		arg = msg->arg;
		after_handler_ack = msg->after_handler_ack;

		lock_release(&ipc->data_lock);

		handler(IPC_CPU_ID(), arg);

		lock_acquire(&ipc->data_lock);

		/* Postprocessing. */
		ipc_increment_ack(after_handler_ack);
	}

	lock_release(&ipc->data_lock);

	return process_ipc_msg;
}

/* FUNCTION: ipc_change_state_to_active
 * DESCRIPTION: Mark CPU as ready for IPC. Called when CPU is no longer in
 * Wait-for-SIPI state.
 * Waits for all start/stop messages to arrive before changing CPU's state. */
void ipc_change_state_to_active(guest_cpu_handle_t gcpu UNUSED)
{
	cpu_id_t cpu_id = IPC_CPU_ID();
	ipc_cpu_context_t *ipc = &ipc_cpu_contexts[cpu_id];

	if (cpu_activity_state[cpu_id] == IPC_CPU_ACTIVE) {
		return;
	}

	lock_acquire(&ipc->data_lock);

	cpu_activity_state[cpu_id] = IPC_CPU_ACTIVE;

	lock_release(&ipc->data_lock);

	MON_LOG(mask_anonymous,
		level_trace,
		"CPU%d: IPC state changed to ACTIVE\n",
		cpu_id);
}

/* FUNCTION: ipc_change_state_to_sipi
 * DESCRIPTION: Mark CPU as NOT ready for IPC. Called when CPU is about to
 * enter Wait-for-SIPI state.
 * Acknowledge and discard all queued messages. */
void ipc_change_state_to_sipi(guest_cpu_handle_t gcpu)
{
	cpu_id_t cpu_id = IPC_CPU_ID();
	ipc_cpu_context_t *ipc = &ipc_cpu_contexts[cpu_id];

	if (cpu_activity_state[cpu_id] == IPC_CPU_SIPI) {
		return;
	}

	lock_acquire(&ipc->data_lock);

	cpu_activity_state[cpu_id] = IPC_CPU_SIPI;

	gcpu_set_pending_nmi(gcpu, 0);

	lock_release(&ipc->data_lock);

	MON_LOG(mask_anonymous,
		level_trace,
		"CPU%d: IPC state changed to SIPI\n",
		cpu_id);
}

/* FUNCTION: ipc_mni_injection_failed
 * DESCRIPTION: Called when NMI injection to gues failed and should be
 * performed once more later.
 * Adjust right ounters. */
void ipc_mni_injection_failed(void)
{
	cpu_id_t cpu_id = IPC_CPU_ID();
	ipc_cpu_context_t *ipc = &ipc_cpu_contexts[cpu_id];

	/* count blocked NMI injection. */
	hw_interlocked_increment64((int64_t *)
		(&ipc->num_blocked_nmi_injections_to_guest));
}

/* ********************* IPC Initialize/ Finalize *********************** */

boolean_t ipc_state_init(uint16_t number_of_host_processors)
{
	uint32_t i = 0,
		ipc_cpu_context_size = 0,
		ipc_msg_array_size = 0,
		cpu_state_size = 0,
		ipc_ack_array_size = 0, ipc_data_size = 0,
		message_queue_offset = 0;
	ipc_cpu_context_t *ipc = 0;

	MON_LOG(mask_anonymous,
		level_trace,
		"IPC state init: #host CPUs = %d\r\n",
		number_of_host_processors);
	num_of_host_processors = number_of_host_processors;
	nmi_owner_guest_id = INVALID_GUEST_ID;

	ipc_cpu_context_size =
		number_of_host_processors *
		ALIGN_FORWARD(sizeof(ipc_cpu_context_t),
			IPC_ALIGNMENT);

	ipc_msg_array_size =
		number_of_host_processors *
		ipc_get_message_array_list_size(number_of_host_processors);

	cpu_state_size =
		(uint32_t)ALIGN_FORWARD(num_of_host_processors *
			sizeof(ipc_cpu_activity_state_t), IPC_ALIGNMENT);

	ipc_ack_array_size =
		number_of_host_processors * sizeof(uint32_t) *
		number_of_host_processors;
	ipc_ack_array_size =
		(uint32_t)ALIGN_FORWARD(ipc_ack_array_size, IPC_ALIGNMENT);

	ipc_data_size =
		ipc_cpu_context_size + ipc_msg_array_size + cpu_state_size +
		ipc_ack_array_size;
	ipc_state_memory = (char *)mon_memory_alloc(ipc_data_size);

	if (ipc_state_memory == NULL) {
		return FALSE;
	}

	mon_memset(ipc_state_memory, 0, ipc_data_size);

	ipc_cpu_contexts = (ipc_cpu_context_t *)ipc_state_memory;

	for (i = 0; i < number_of_host_processors; i++) {
		ipc = &ipc_cpu_contexts[i];

		message_queue_offset =
			ipc_cpu_context_size +
			i * ipc_get_message_array_list_size(
				number_of_host_processors);

		ipc->message_queue =
			array_list_init(ipc_state_memory + message_queue_offset,
				ipc_get_message_array_list_size
					(number_of_host_processors),
				sizeof(ipc_message_t),
				ipc_get_max_pending_messages
				(
					number_of_host_processors),
				IPC_ALIGNMENT);

		lock_initialize(&ipc->data_lock);
	}

	cpu_activity_state =
		(ipc_cpu_activity_state_t *)(ipc_state_memory +
					     ipc_cpu_context_size +
					     ipc_msg_array_size);

	ipc_ack_array =
		(uint32_t *)((char *)cpu_activity_state + cpu_state_size);

	lock_initialize(&send_lock);

	isr_register_handler((func_mon_isr_handler_t)ipc_nmi_interrupt_handler,
		NMI_VECTOR);

	ipc_cli_register();

	return TRUE;
}

boolean_t ipc_guest_state_init(guest_id_t guest_id)
{
	if (guest_is_nmi_owner(mon_guest_handle(guest_id))) {
		nmi_owner_guest_id = guest_id;
	}

	return TRUE;
}

void ipc_set_no_resend_flag(boolean_t val)
{
	if (val) {
		hw_interlocked_increment(&debug_not_resend);
	} else {
		hw_interlocked_decrement(&debug_not_resend);
	}
}

void ipc_print_cpu_context(cpu_id_t cpu_id, boolean_t use_lock)
{
	ipc_cpu_context_t *ipc = &ipc_cpu_contexts[cpu_id];

	if (use_lock) {
		lock_acquire(&ipc->data_lock);

		MON_LOG(mask_anonymous,
			level_trace,
			"IPC context on CPU %d:\r\n",
			cpu_id);
		MON_LOG(mask_anonymous, level_trace,
			"    num_received_nmi_interrupts         = %d\r\n",
			ipc->num_received_nmi_interrupts);
		MON_LOG(mask_anonymous, level_trace,
			"    num_processed_nmi_interrupts        = %d\r\n",
			ipc->num_processed_nmi_interrupts);
		MON_LOG(mask_anonymous, level_trace,
			"    num_of_sent_ipc_nmi_interrupts      = %d\r\n",
			ipc->num_of_sent_ipc_nmi_interrupts);
		MON_LOG(mask_anonymous, level_trace,
			"    num_of_processed_ipc_nmi_interrupts = %d\r\n",
			ipc->num_of_processed_ipc_nmi_interrupts);
		MON_LOG(mask_anonymous, level_trace,
			"    num_of_sent_ipc_messages            = %d\r\n",
			ipc->num_of_sent_ipc_messages);
		MON_LOG(mask_anonymous, level_trace,
			"    num_of_received_ipc_messages        = %d\r\n",
			ipc->num_of_received_ipc_messages);
		MON_LOG(mask_anonymous, level_trace,
			"    num_start_messages                  = %d\r\n",
			ipc->num_start_messages);
		MON_LOG(mask_anonymous, level_trace,
			"    num_stop_messages                   = %d\r\n",
			ipc->num_stop_messages);
		MON_LOG(mask_anonymous, level_trace,
			"    num_blocked_nmi_injections_to_guest = %d\r\n",
			ipc->num_blocked_nmi_injections_to_guest);
		MON_LOG(mask_anonymous, level_trace,
			"    Num of queued IPC messages          = %d\r\n",
			array_list_size(ipc->message_queue));

		lock_release(&ipc->data_lock);
	} else {
		MON_LOG_NOLOCK("IPC context on CPU %d:\r\n", cpu_id);
		MON_LOG_NOLOCK(
			"    num_received_nmi_interrupts         = %d\r\n",
			ipc->num_received_nmi_interrupts);
		MON_LOG_NOLOCK(
			"    num_processed_nmi_interrupts        = %d\r\n",
			ipc->num_processed_nmi_interrupts);
		MON_LOG_NOLOCK(
			"    num_of_sent_ipc_nmi_interrupts      = %d\r\n",
			ipc->num_of_sent_ipc_nmi_interrupts);
		MON_LOG_NOLOCK(
			"    num_of_processed_ipc_nmi_interrupts = %d\r\n",
			ipc->num_of_processed_ipc_nmi_interrupts);
		MON_LOG_NOLOCK(
			"    num_of_sent_ipc_messages            = %d\r\n",
			ipc->num_of_sent_ipc_messages);
		MON_LOG_NOLOCK(
			"    num_of_received_ipc_messages        = %d\r\n",
			ipc->num_of_received_ipc_messages);
		MON_LOG_NOLOCK(
			"    num_start_messages                  = %d\r\n",
			ipc->num_start_messages);
		MON_LOG_NOLOCK(
			"    num_stop_messages                   = %d\r\n",
			ipc->num_stop_messages);
		MON_LOG_NOLOCK(
			"    num_blocked_nmi_injections_to_guest = %d\r\n",
			ipc->num_blocked_nmi_injections_to_guest);
		MON_LOG_NOLOCK(
			"    Num of queued IPC messages          = %d\r\n",
			array_list_size(ipc->message_queue));
	}
}

#ifdef CLI_INCLUDE
static
int cli_ipc_print(unsigned argc, char *argv[])
{
	cpu_id_t cpu_id;

	if (argc != 2) {
		return -1;
	}

	cpu_id = (cpu_id_t)CLI_ATOL(argv[1]);

	if (cpu_id < 0 || cpu_id >= num_of_host_processors) {
		CLI_PRINT("CpuId must be in [0..%d] range\n",
			(int)num_of_host_processors - 1);
		return -1;
	}

	ipc_print_cpu_context(cpu_id, FALSE);

	return 0;
}

MON_DEBUG_CODE(
	static int cli_ipc_resend(unsigned argc UNUSED, char *argv[] UNUSED)
	{
		boolean_t no_resend;
		if (!CLI_STRNCMP(argv[1], "start", sizeof("start"))) {
			no_resend = FALSE;
		} else if (!CLI_STRNCMP(argv[1], "stop", sizeof("stop"))) {
			no_resend = TRUE;
		} else if (!CLI_STRNCMP(argv[1], "state", sizeof("state"))) {
			CLI_PRINT("IPC resend disable state counter = %d\n",
				debug_not_resend);
			CLI_PRINT("IPC resend is %s\n",
				(debug_not_resend == 0) ? "ENABLED" : "DISABLED");
			return 0;
		} else {
			CLI_PRINT("Wrong command argument\n");
			return -1;
		}

		ipc_set_no_resend_flag(no_resend);
		CLI_PRINT("IPC resend disable state counter = %d\n",
			debug_not_resend);
		CLI_PRINT("IPC resend is %s\n",
			(debug_not_resend == 0) ? "ENABLED" : "DISABLED");
		return 0;
	}
	)

static
void ipc_cli_register(void)
{
	MON_DEBUG_CODE(cli_add_command(cli_ipc_print, "ipc print",
			"Print internal IPC state for given CPU.",
			"<cpu id>", CLI_ACCESS_LEVEL_SYSTEM));

	MON_DEBUG_CODE(cli_add_command(cli_ipc_resend, "ipc resend",
			"Stop/Start resend IPC signal.",
			"stop | start | state",
			CLI_ACCESS_LEVEL_SYSTEM));
}
#else

static
void ipc_cli_register(void)
{
}

#endif
