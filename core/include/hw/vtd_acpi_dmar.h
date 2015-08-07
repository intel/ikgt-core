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

#ifndef _VTD_ACPI_DMAR_H
#define _VTD_ACPI_DMAR_H

#include "mon_defs.h"
#include "pci_configuration.h"
#include "list.h"

typedef pci_device_address_t source_id_t;

typedef enum {
	DMAR_DEVICE_PCI_DEVICE,
	DMAR_DEVICE_IOAPIC,
	DMAR_DEVICE_HPET
} dmar_device_type_t;

typedef struct {
	dmar_device_type_t	type;
	source_id_t		source_id;      /* pci-device or HPET */
	uint8_t			ioapic_id;      /* valid if type == IOAPIC */
	char			padding0[1];
} dmar_device_t;

typedef struct {
	list_element_t	list;
	uint32_t	id;
	boolean_t	include_all;
	uint16_t	segment;
	char		padding0[6];
	uint64_t	register_base;
	uint16_t	num_devices;
	char		padding1[6];
	dmar_device_t	*devices;
} dmar_hw_unit_t;

typedef struct {
	list_element_t	list;
	uint16_t	segment;
	char		padding0[6];
	uint64_t	base;
	uint64_t	limit;
	uint16_t	num_devices;
	char		padding1[6];
	dmar_device_t	*devices;
} dmar_reserved_memory_t;

typedef struct {
	list_element_t	list;
	uint16_t	segment;
	char		padding0[2];
	boolean_t	supported_on_all_ports;
	uint16_t	num_devices;
	char		padding1[6];
	dmar_device_t	*devices;
} dmar_addr_translation_service_t;

int vtd_acpi_dmar_init(hva_t address);
void restore_dmar_table(void);

uint32_t dmar_num_dma_remapping_hw_units(void);

list_element_t *dmar_get_dmar_unit_definitions(void);
list_element_t *dmar_get_reserved_memory_regions(void);

boolean_t rmrr_contains_device(dmar_reserved_memory_t *rmrr,
			       uint8_t bus,
			       uint8_t device,
			       uint8_t function);

#endif
