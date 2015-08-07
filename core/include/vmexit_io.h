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

#ifndef _VMEXIT_IO_H_
#define _VMEXIT_IO_H_

/* define this structure to resolve the conflict below:
 * an IO port is monitored by MON internally
 * status -- not enabled yet. */
typedef enum {
	NO_IO_OWNER = 0x00,
	IO_OWNED_BY_MON = 0x01,
	IO_OWNED_BY_USAGE = 0x02,
	IO_OWNED_BY_XMON_USAGE = IO_OWNED_BY_MON | IO_OWNED_BY_USAGE,
} io_port_owner_t;

typedef boolean_t (*io_access_handler_t) (guest_cpu_handle_t gcpu,
					  uint16_t port_id,
					  unsigned port_size, /* 1, 2, 4 */
					  rw_access_t access,
					  boolean_t string_intr, /* ins/outs */
					  boolean_t rep_prefix,
					  uint32_t rep_count,
					  /* gva for string I/O; otherwise hva. */
					  void *p_value, void *handler_context);

/*-----------------------------------------------------------------------*
*  FUNCTION : io_vmexit_setup()
*  PURPOSE  : Allocate and initialize IO VMEXITs related data structures,
*           : common for all guests
*  ARGUMENTS: guest_id_t    num_of_guests
*  RETURNS  : void
*-----------------------------------------------------------------------*/
void io_vmexit_initialize(void);

/*-----------------------------------------------------------------------*
*  FUNCTION : io_vmexit_guest_setup()
*  PURPOSE  : Allocate and initialize IO VMEXITs related data structures for
*           : specific guest
*  ARGUMENTS: guest_id_t    guest_id
*  RETURNS  : void
*-----------------------------------------------------------------------*/
void io_vmexit_guest_initialize(guest_id_t guest_id);

/*-----------------------------------------------------------------------*
*  FUNCTION : io_vmexit_activate()
*  PURPOSE  : enables in HW IO VMEXITs for specific guest on given CPU
*           : called during initialization
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : void
*-----------------------------------------------------------------------*/
void io_vmexit_activate(guest_cpu_handle_t gcpu);

/*-----------------------------------------------------------------------*
*  FUNCTION : mon_io_vmexit_handler_register()
*  PURPOSE  : Register/update IO handler for spec port/guest pair.
*  ARGUMENTS: guest_id_t            guest_id
*           : io_port_id_t          port_id
*           : io_access_handler_t   handler
*           : void*               handler_context - passed as it to the handler
*  RETURNS  : status
*-----------------------------------------------------------------------*/
mon_status_t mon_io_vmexit_handler_register(guest_id_t guest_id,
					    io_port_id_t port_id,
					    io_access_handler_t handler,
					    void *handler_context);

/*-----------------------------------------------------------------------*
*  FUNCTION : mon_io_vmexit_handler_unregister()
*  PURPOSE  : Unregister IO handler for spec port/guest pair.
*  ARGUMENTS: guest_id_t            guest_id
*           : io_port_id_t          port_id
*  RETURNS  : status
*-----------------------------------------------------------------------*/
mon_status_t mon_io_vmexit_handler_unregister(guest_id_t guest_id,
					      io_port_id_t port_id);

/*-----------------------------------------------------------------------*
*  FUNCTION : io_vmexit_block_port()
*  PURPOSE  : Enable VMEXIT on port without installing handler.
*           : Blocking_handler will be used for such cases.
*  ARGUMENTS: guest_id_t            guest_id
*           : io_port_id_t          port_from
*           : io_port_id_t          port_to
*  RETURNS  : void
*-----------------------------------------------------------------------*/
void io_vmexit_block_port(guest_id_t guest_id,
			  io_port_id_t port_from,
			  io_port_id_t port_to);

/*-----------------------------------------------------------------------*
*  FUNCTION : io_vmexit_transparent_handler()
*  PURPOSE  : Called to facilitate IO handlers to pass IO requests to HW, if needed
*  ARGUMENTS: guest_id_t            guest_id
*           : io_port_id_t          port_from
*           : io_port_id_t          port_to
*  RETURNS  : void
*-----------------------------------------------------------------------*/
void io_vmexit_transparent_handler(guest_cpu_handle_t gcpu,
				   uint16_t port_id,
				   unsigned port_size, /* 1, 2, 4 */
				   rw_access_t access,
				   void *p_value,
				   void *context); /* not used */

#endif /* _VMEXIT_IO_H_ */
