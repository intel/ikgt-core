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

#ifndef _ADDRESS_H_
#define _ADDRESS_H_

void API_FUNCTION addr_setup_address_space(void);
address_t API_FUNCTION addr_canonize_address(address_t address);
uint8_t API_FUNCTION addr_get_physical_address_size(void);
uint8_t API_FUNCTION addr_get_virtual_address_size(void);

boolean_t API_FUNCTION addr_is_canonical(address_t address);
boolean_t API_FUNCTION addr_physical_is_valid(address_t address);

#endif                          /* _ADDRESS_H_ */
