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

#ifndef _VMCS_HIERARCHY_H_
#define _VMCS_HIERARCHY_H_

#include "list.h"
#include "vmcs_api.h"

typedef struct {
	vmcs_object_t	*vmcs[VMCS_LEVELS];
	/* contains list of vmcs_1_descriptor_t, empty means no layering */
	list_element_t	vmcs_1_list[1];
} vmcs_hierarchy_t;

INLINE boolean_t vmcs_hierarchy_is_layered(vmcs_hierarchy_t *obj)
{
	return obj->vmcs[VMCS_LEVEL_0] != obj->vmcs[VMCS_MERGED];
}

mon_status_t vmcs_hierarchy_create(vmcs_hierarchy_t *obj,
				   guest_cpu_handle_t gcpu);
mon_status_t vmcs_hierarchy_add_vmcs(vmcs_hierarchy_t *obj,
				     guest_cpu_handle_t gcpu,
				     address_t gpa);
mon_status_t vmcs_hierarchy_remove_vmcs(vmcs_hierarchy_t *obj,
					vmcs_object_t *vmcs_1);
vmcs_object_t *vmcs_hierarchy_get_vmcs(vmcs_hierarchy_t *obj,
				       vmcs_level_t level);
vmcs_object_t *vmcs_hierarchy_get_next_vmcs_1(vmcs_hierarchy_t *obj);
vmcs_object_t *vmcs_hierarchy_select_vmcs_1(vmcs_hierarchy_t *obj,
					    vmcs_object_t *vmcs);

#endif                          /* _VMCS_HIERARCHY_H_ */
