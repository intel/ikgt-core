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

#ifndef _TRUSTY_SETUP_H_
#define _TRUSTY_SETUP_H_
#include "evmm_desc.h"
#include "vmm_arch.h"

void fill_code32_seg(segment_t *ss, uint16_t sel);
void fill_code64_seg(segment_t *ss, uint16_t sel);
void fill_data_seg(segment_t *ss, uint16_t sel);
void fill_tss_seg(segment_t *ss, uint16_t sel);
boolean_t trusty_gcpu_setup(evmm_desc_t *xd);

#endif
