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

#include "hw_includes.h"
#include "address.h"

static address_t max_virtual_address;
static address_t virtual_address_copmplement;

static uint8_t physical_address_size;
static uint8_t virtual_address_size;

extern uint32_t pw_reserved_bits_high_mask;

void API_FUNCTION addr_setup_address_space(void)
{
	uint32_t value = hw_read_address_size();

	physical_address_size = (uint8_t)(value & 0xFF);
	virtual_address_size = (uint8_t)((value >> 8) & 0xFF);

	max_virtual_address = ((address_t)1 << virtual_address_size) - 1;;
	virtual_address_copmplement = ~(max_virtual_address >> 1);;

	/* bit mask to identify the reserved bits in paging structure high order
	 * address field */
	pw_reserved_bits_high_mask = ~((1 << (physical_address_size - 32)) - 1);
}

uint8_t API_FUNCTION addr_get_physical_address_size(void)
{
	return physical_address_size;
}

address_t API_FUNCTION addr_canonize_address(address_t address)
{
	if (address & virtual_address_copmplement) {
		address |= virtual_address_copmplement;
	}
	return address;
}

boolean_t addr_is_canonical(address_t address)
{
	return addr_canonize_address(address) == address;
}

boolean_t addr_physical_is_valid(address_t address)
{
	address_t phys_address_space =
		BIT_VALUE64((address_t)(physical_address_size));

	return address < phys_address_space;
}
