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

#include <mon_defs.h>
#include <hw_utils.h>
#include <efer_msr_abstraction.h>
#include <em64t_defs.h>


void efer_msr_set_nxe(void)
{
	ia32_efer_t efer_reg;

	efer_reg.uint64 = hw_read_msr(IA32_MSR_EFER);
	if (efer_reg.bits.nxe == 0) {
		efer_reg.bits.nxe = 1;
		hw_write_msr(IA32_MSR_EFER, efer_reg.uint64);
	}
}

boolean_t efer_msr_is_nxe_bit_set(IN uint64_t efer_msr_value)
{
	ia32_efer_t efer_reg;

	efer_reg.uint64 = efer_msr_value;
	return efer_reg.bits.nxe != 0;
}

uint64_t efer_msr_read_reg(void)
{
	return hw_read_msr(IA32_MSR_EFER);
}
