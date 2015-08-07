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

#ifndef EFER_MSR_ABSTRACTION_H
#define EFER_MSR_ABSTRACTION_H

/* Function: efer_msr_set_nxe
 * Description: This function sets NXE bit in hardware EFER MSR register
 */
void efer_msr_set_nxe(void);

/* Function: efer_msr_is_nxe_bit_set
 * Description: This function checks whether NXE bit is set in EFER MSR value
 * Input: efer_msr_value - 64 bit value of EFER MSR
 * Return Value: TRUE or FALSE
 */
boolean_t efer_msr_is_nxe_bit_set(IN uint64_t efer_msr_value);

/* Function: efer_msr_read_reg
 * Description: This function reads and returns the value of hardware EFER MSR
 * Return Value: 64 bit value of EFER MSR
 */
uint64_t efer_msr_read_reg(void);

#endif
