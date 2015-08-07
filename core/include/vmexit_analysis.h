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

#ifndef VMEXIT_ANALYSIS_H

boolean_t vmexit_analysis_was_control_requested(guest_cpu_handle_t gcpu,
						vmcs_object_t *vmexit_vmcs,
						vmcs_object_t *control_vmcs,
						ia32_vmx_exit_basic_reason_t
						exit_reason);

#endif                          /* VMEXIT_ANALYSIS_H */
