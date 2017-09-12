/*******************************************************************************
* Copyright (c) 2017 Intel Corporation
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

#ifndef _GCPU_STATE_H_
#define _GCPU_STATE_H_

void gcpu_set_host_state(guest_cpu_handle_t gcpu);
void gcpu_set_ctrl_state(guest_cpu_handle_t gcpu);
void gcpu_set_init_state(guest_cpu_handle_t gcpu, const gcpu_state_t *initial_state);
void gcpu_set_reset_state(const guest_cpu_handle_t gcpu);

void prepare_g0gcpu_init_state(const gcpu_state_t *gcpu_state);

#endif   /* _GCPU_STATE_H_ */
