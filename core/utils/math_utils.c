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

#include "mon_defs.h"
#include "hw_utils.h"

uint32_t align_forward_to_power_of_2(uint64_t number)
{
	uint32_t msb_index = 0;

	if (0 == number) {
		return 1;
	}

	hw_scan_bit_backward64((uint32_t *)&msb_index, number);
	if (!IS_POW_OF_2(number)) {
		msb_index++;
	}

	return 1 << msb_index;
}
