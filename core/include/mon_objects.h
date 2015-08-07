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

#ifndef _MON_OBJECTS_H_
#define _MON_OBJECTS_H_

/*
 * Typedefs of the mostly used objects
 */

typedef void *gpm_handle_t;
typedef struct guest_cpu_t *guest_cpu_handle_t;
typedef struct guest_descriptor_t *guest_handle_t;

/*
 * Support for call from NMI exception handler
 */
typedef enum {
	MON_CALL_NORMAL = 0,
	MON_CALL_FROM_NMI_HANDLER
} mon_calling_environment_t;

#endif                          /* _MON_OBJECTS_H_ */
