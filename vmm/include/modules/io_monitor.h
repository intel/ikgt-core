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
