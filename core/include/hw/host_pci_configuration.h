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

#ifndef _HOST_PCI_CONFIG_H
#define _HOST_PCI_CONFIG_H

#include "pci_configuration.h"

typedef struct host_pci_device_t {
	pci_device_address_t		address;
	char				padding0[6];
	struct host_pci_device_t	*parent;
	uint8_t				depth;  /* number of bridges up to the device */
	pci_path_t			path;   /* path to the device */
	uint16_t			vendor_id;
	uint16_t			device_id;
	uint8_t				revision_id; /* device-specific revision id chosen by vendor */
	uint8_t				base_class;
	uint8_t				sub_class;
	uint8_t				programming_interface;
	uint8_t				header_type; /* =0x0 for devices, 0x1 for p2p bridge, 0x2 for cardbus bridge */
	char				padding1[1];
	boolean_t			is_multifunction;
	boolean_t			is_pci_2_pci_bridge;    /* baseclass and subclass specify pci2pci bridge */
	uint8_t				interrupt_pin;          /* interrupt pin (R/O) used by the device (INTA, INTB, INTC or INTD) */
	uint8_t				interrupt_line;         /* interrupt line that connects to interrupt controller (0xFF - not connected) */
	char				padding2[2];
	pci_base_address_register_t	bars[PCI_MAX_BAR_NUMBER];
} host_pci_device_t;

uint8_t pci_read8(uint8_t bus, uint8_t device, uint8_t function,
		  uint8_t reg_id);
void pci_write8(uint8_t bus,
		uint8_t device,
		uint8_t function,
		uint8_t reg_id,
		uint8_t value);

uint16_t pci_read16(uint8_t bus,
		    uint8_t device,
		    uint8_t function,
		    uint8_t reg_id);
void pci_write16(uint8_t bus,
		 uint8_t device,
		 uint8_t function,
		 uint8_t reg_id,
		 uint16_t value);

uint32_t pci_read32(uint8_t bus,
		    uint8_t device,
		    uint8_t function,
		    uint8_t reg_id);
void pci_write32(uint8_t bus,
		 uint8_t device,
		 uint8_t function,
		 uint8_t reg_id,
		 uint32_t value);

void host_pci_initialize(void);

host_pci_device_t *get_host_pci_device(uint8_t bus,
				       uint8_t device,
				       uint8_t function);

boolean_t pci_read_secondary_bus_reg(uint8_t bus,
				     uint8_t device,
				     uint8_t func,
				     OUT uint8_t *secondary_bus);

uint32_t host_pci_get_num_devices(void);

#endif
