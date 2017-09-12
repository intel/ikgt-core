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

#ifndef _MSR_MONITOR_H_
#define _MSR_MONITOR_H_

#ifndef MODULE_MSR_MONITOR
#error "MODULE_MSR_MONITOR is not defined"
#endif

typedef void (*msr_handler_t) (guest_cpu_handle_t gcpu, uint32_t msr_id);

uint64_t get_val_for_wrmsr(guest_cpu_handle_t gcpu);
void set_val_for_rdmsr(guest_cpu_handle_t gcpu, uint64_t msr_value);

void block_msr_read(uint16_t guest_id, uint32_t msr_id);
void block_msr_write(uint16_t guest_id, uint32_t msr_id);
void block_msr_access(uint16_t guest_id, uint32_t msr_id);

void monitor_msr_read(uint16_t guest_id, uint32_t msr_id, msr_handler_t handler);
void monitor_msr_write(uint16_t guest_id, uint32_t msr_id, msr_handler_t handler);
void monitor_msr_access(uint16_t guest_id, uint32_t msr_id, msr_handler_t read_handler, msr_handler_t write_handler);

void msr_monitor_init(void);

#endif /* _MSR_MONITOR_H_ */
