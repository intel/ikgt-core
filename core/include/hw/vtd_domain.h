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

#ifndef _VTD_DOMAIN_H
#define _VTD_DOMAIN_H

#include "list.h"
#include "memory_address_mapper_api.h"
#include "vtd_acpi_dmar.h"
#include "mon_startup.h"

typedef uint32_t vtd_domain_id_t;

#define INVALID_VTD_DOMAIN_ID   ((vtd_domain_id_t)-1)

typedef struct {
	list_element_t	list; /* list of devices that belong to the same domain */
	source_id_t	source_id;
	char		padding[6];
} vtd_pci_device_t;

typedef struct {
	vtd_domain_id_t				domain_id;
	uint32_t				sagaw_bit_index;
	guest_id_t				guest_id;
	char					padding0[6];
	mam_handle_t				address_space;
	uint64_t				address_space_root;
	list_element_t				devices;
	struct vtd_dma_remapping_hw_uint_t	*dmar;
	list_element_t				list;           /* list of all existing domains */
	list_element_t				dmar_list;      /* list of domains in the same dmar */
} vtd_domain_t;

vtd_domain_t *vtd_domain_create(mam_handle_t address_space,
				uint32_t sagaw_bit_index);
vtd_domain_t *vtd_domain_create_guest_domain(struct vtd_dma_remapping_hw_uint_t
					     *dmar,
					     guest_id_t gid,
					     uint32_t sagaw_bit_index,
					     const mon_memory_layout_t *
					     mon_memory_layout,
					     const mon_application_params_struct_t *
					     application_params);

vtd_domain_t *vtd_get_domain(struct vtd_dma_remapping_hw_uint_t *dmar,
			     vtd_domain_id_t domain_id);
list_element_t *vtd_get_domain_list(void);

boolean_t vtd_domain_add_device(vtd_domain_t *domain,
				uint8_t bus,
				uint8_t device,
				uint8_t function);
void vtd_domain_remove_device(vtd_domain_t *domain,
			      uint8_t bus,
			      uint8_t device,
			      uint8_t function);

boolean_t vtd_domain_add_to_dmar(vtd_domain_t *domain,
				 struct vtd_dma_remapping_hw_uint_t *dmar);

uint64_t vtd_domain_get_address_space_root(vtd_domain_t *domain,
					   mam_vtdpt_snoop_behavior_t common_snpb,
					   mam_vtdpt_trans_mapping_t common_tm);

#endif
