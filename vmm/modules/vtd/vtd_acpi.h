/*******************************************************************************
* Copyright (c) 2018 Intel Corporation
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
#ifndef _VTD_ACPI_H_
#define _VTD_ACPI_H_

typedef struct {
	uint64_t reg_base_hva;
	uint64_t reg_base_hpa;
} vtd_engine_t;

void vtd_dmar_parse(vtd_engine_t *engine_list);

#endif

