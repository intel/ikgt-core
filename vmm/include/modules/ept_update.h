
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

#ifndef _EPT_UPDATE_H_
#define _EPT_UPDATE_H_

#ifndef MODULE_EPT_UPDATE
#error "MODULE_EPT_UPDATE is not defined"
#endif

void ept_update_install(uint32_t guest_id);

#endif /* _EPT_UPDATE_H_ */
