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

#ifndef _GCPU_SWITCH_H_
#define _GCPU_SWITCH_H_

/* perform full state save before switching to another guest */
void gcpu_swap_out(guest_cpu_handle_t gcpu);

/* perform state restore after switching from another guest */
void gcpu_swap_in(const guest_cpu_handle_t gcpu);

/*
 * Resume execution.
 * should never returns.
 */
void gcpu_resume(guest_cpu_handle_t gcpu);

#endif   /* _GCPU_SWITCH_H_ */
