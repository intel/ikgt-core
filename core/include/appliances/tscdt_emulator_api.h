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

#ifndef TSCDT_EMULATOR_API_H
#define TSCDT_EMULATOR_API_H

typedef enum {
	TSCDTE_MODE_OFF,
	TSCDTE_MODE_LAPIC_VIRTUALIZATION_OFF,
	TSCDTE_MODE_LAPIC_VIRTUALIZATION_USE_MSRS,
	TSCDTE_MODE_LAPIC_VIRTUALIZATION_USE_VMCALL,
	TSCDTE_MODE_PARTIAL_LAPIC_VIRTUALIZATION,
	TSCDTE_MODE_FULL_LAPIC_VIRTUALIZATION
} tscdte_mode_t;

void tscdte_initialize(tscdte_mode_t mode);

#endif
