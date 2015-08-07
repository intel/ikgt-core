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

#ifndef _VMX_TIMER_H_
#define _VMX_TIMER_H_

/*
 *  Function : vmx_timer_hw_setup
 *  Purpose  : Checks if VMX timer is supported by hardware and if so,
 *           : calculates its rate relative to TSC.
 *  Arguments: void
 *  Return   : TRUE id supported
 *  Note     : Must be call 1st on the given core.
 */
boolean_t vmx_timer_hw_setup(void);
boolean_t vmx_timer_create(guest_cpu_handle_t gcpu);
boolean_t vmx_timer_start(guest_cpu_handle_t gcpu);
boolean_t vmx_timer_stop(guest_cpu_handle_t gcpu);
boolean_t vmx_timer_set_period(guest_cpu_handle_t gcpu, uint64_t period);
boolean_t vmx_timer_launch(guest_cpu_handle_t gcpu,
			   uint64_t time_to_expiration,
			   boolean_t periodic);
boolean_t vmx_timer_set_mode(guest_cpu_handle_t gcpu,
			     boolean_t save_value_mode);

#endif                          /* _VMX_TIMER_H_ */
