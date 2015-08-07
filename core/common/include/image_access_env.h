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

#ifndef _IMAGE_ACCESS_ENV_H_
#define _IMAGE_ACCESS_ENV_H_

/* defintions for Mon environment */
#include "mon_defs.h"
#define MALLOC(__x) mon_page_alloc(((__x) + PAGE_4KB_SIZE - 1) / PAGE_4KB_SIZE)
#define FREE(__x)

#endif                          /* _IMAGE_ACCESS_ENV_H_ */
