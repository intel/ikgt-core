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
#include "mon_dbg.h"
#include "memory_allocator.h"
#include "mon_objects.h"
#include "vmcs_api.h"
#include "vmcs_sw_object.h"
#include "vmcs_actual.h"
#include "vmcs_hierarchy.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(VMCS_HIERARCHY_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(VMCS_HIERARCHY_C, __condition)

typedef struct {
	vmcs_object_t *vmcs;
	list_element_t	list[1];
} vmcs_1_descriptor_t;

static vmcs_1_descriptor_t *vmcs_hierarchy_vmcs1_lkup(vmcs_hierarchy_t *obj,
						      vmcs_object_t *vmcs);

mon_status_t vmcs_hierarchy_create(vmcs_hierarchy_t *obj,
				   guest_cpu_handle_t gcpu)
{
	mon_status_t status;

	MON_ASSERT(obj);

	obj->vmcs[VMCS_LEVEL_0] = obj->vmcs[VMCS_MERGED] =
					  vmcs_act_create(gcpu);

	if (NULL == obj->vmcs[VMCS_LEVEL_0]) {
		MON_LOG(mask_anonymous,
			level_trace,
			"Failed to create merged VMCS\n");
		status = MON_ERROR;
	} else {
		obj->vmcs[VMCS_LEVEL_1] = NULL;
		list_init(obj->vmcs_1_list);
		status = MON_OK;
	}

	return status;
}

vmcs_object_t *vmcs_hierarchy_get_vmcs(vmcs_hierarchy_t *obj,
				       vmcs_level_t level)
{
	vmcs_object_t *vmcs;

	MON_ASSERT(obj);

	if (level >= VMCS_LEVEL_0 && level < VMCS_LEVELS) {
		vmcs = obj->vmcs[level];
	} else {
		MON_LOG(mask_anonymous, level_trace, "Invalid VMCS level\n");
		MON_ASSERT(0);
		vmcs = NULL;
	}

	return vmcs;
}
