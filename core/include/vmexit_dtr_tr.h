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

#ifndef _VMEXIT_DTR_TR_H_
#define _VMEXIT_DTR_TR_H_

vmexit_handling_status_t vmexit_dr_access(guest_cpu_handle_t gcpu);
vmexit_handling_status_t vmexit_gdtr_idtr_access(guest_cpu_handle_t gcpu);
vmexit_handling_status_t vmexit_ldtr_tr_access(guest_cpu_handle_t gcpu);

#endif                          /* _VMEXIT_DTR_TR_H_ */
