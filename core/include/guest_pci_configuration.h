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

#ifndef _GUEST_PCI_CONFIG_H
#define _GUEST_PCI_CONFIG_H

#include "list.h"
#include "host_pci_configuration.h"
#include "mon_objects.h"

struct guest_pci_device_t;

typedef void (*guest_pci_read_handler_t) (guest_cpu_handle_t gcpu,
					  struct guest_pci_device_t *pci_device,
					  uint32_t port_id, uint32_t port_size,
					  void *value);

typedef void (*guest_pci_write_handler_t) (guest_cpu_handle_t gcpu,
					   struct guest_pci_device_t *pci_device,
					   uint32_t port_id, uint32_t port_size,
					   void *value);

typedef struct {
	guest_pci_read_handler_t	pci_read;
	guest_pci_write_handler_t	pci_write;
} gpci_guest_profile_t;

typedef enum {
	GUEST_DEVICE_VIRTUALIZATION_DIRECT_ASSIGNMENT,
	GUEST_DEVICE_VIRTUALIZATION_HIDDEN
} guest_device_virtualization_type_t;

typedef struct guest_pci_device_t {
	guest_id_t				guest_id;
	char					padding[2];
	guest_device_virtualization_type_t	type;
	host_pci_device_t			*host_device;
	guest_pci_read_handler_t		pci_read;
	guest_pci_write_handler_t		pci_write;
	uint8_t					*config_space;
} guest_pci_device_t;

typedef struct {
	guest_id_t		guest_id;
	char			padding[2];
	uint32_t		num_devices;
	guest_pci_device_t	devices[PCI_MAX_NUM_SUPPORTED_DEVICES + 1];
	/* index 0 is reserved to mark "not-present" device */
	pci_dev_index_t		device_lookup_table[PCI_MAX_NUM_FUNCTIONS];
	list_element_t		guests[1];
	pci_config_address_t	*gcpu_pci_access_address;
} guest_pci_devices_t;

boolean_t gpci_initialize(void);

boolean_t gpci_guest_initialize(guest_id_t guest_id);

boolean_t gpci_register_device(guest_id_t guest_id,
			       guest_device_virtualization_type_t type,
			       host_pci_device_t *host_pci_device,
			       uint8_t *config_space,
			       guest_pci_read_handler_t pci_read,
			       guest_pci_write_handler_t pci_write);

guest_id_t gpci_get_device_guest_id(uint16_t bus,
				    uint16_t device,
				    uint16_t function);

#endif
