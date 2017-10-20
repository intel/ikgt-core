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
#ifndef _DR_H_
#define _DR_H_

#ifndef MODULE_DR
#error "MODULE_DR is not defined"
#endif

/* this module only isolates DR0~DR3, DR6 between GUESTs.
 * for DR7 and DEBUG_CTRL_MSR, they are isolated by VMCS directly
 * for host DRs, DR7 and DEBUG_CTRL_MSR will be set to 0x400
 * and 0x0 in each VMExit, which disables all DRs.
 * So, host DR0~DR3, DR6 are NOT isolated from guests.
 */
void dr_isolation_init(void);
#endif
