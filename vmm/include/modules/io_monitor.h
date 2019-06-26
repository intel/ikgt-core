/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _VMEXIT_IO_H_
#define _VMEXIT_IO_H_

#ifndef MODULE_IO_MONITOR
#error "MODULE_IO_MONITOR is not defined"
#endif

typedef uint32_t (*io_read_handler_t) (guest_cpu_handle_t gcpu,
					uint16_t port_id,
					uint32_t port_size);

typedef void (*io_write_handler_t) (guest_cpu_handle_t gcpu,
					uint16_t port_id,
					uint32_t port_size,
					uint32_t p_value);

void io_monitor_register(uint16_t guest_id,
				uint16_t port_id,
				io_read_handler_t read_handler,
				io_write_handler_t write_handler);

uint32_t io_transparent_read_handler(guest_cpu_handle_t gcpu,
				 uint16_t port_id,
				 uint32_t port_size);

void io_transparent_write_handler(guest_cpu_handle_t gcpu,
				  uint16_t port_id,
				  uint32_t port_size,
				  uint32_t value);
void io_monitor_init(void);

#endif /* _VMEXIT_IO_H_ */
