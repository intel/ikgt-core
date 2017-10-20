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
#ifndef _CR_H_
#define _CR_H_

#ifndef MODULE_CR
#error "MODULE_CR is not defined"
#endif

/* this module only isolates CR2 and CR8 between GUESTs.
 * for CR0, CR3, CR4, they are isolated by VMCS directly
 * for host CR2 and CR8, since it will not impact host and
 * host will not use them, they are NOT isolated from guests.
 */
void cr_isolation_init(void);
#endif

