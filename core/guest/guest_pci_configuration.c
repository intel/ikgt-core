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

#include "file_codes.h"
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(GUEST_PCI_CONFIGURATION_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(GUEST_PCI_CONFIGURATION_C, \
	__condition)
#include "guest_pci_configuration.h"
#include "guest.h"
#include "hash64_api.h"
#include "memory_allocator.h"
#include "list.h"
#include "vmexit_io.h"
#include "guest_cpu.h"
#include "hw_utils.h"
#include "heap.h"
#ifdef PCI_SCAN

extern
void io_transparent_read_handler(guest_cpu_handle_t gcpu,
				 io_port_id_t port_id,
				 unsigned port_size, /* 1, 2, 4 */
				 void *p_value);

extern
void io_transparent_write_handler(guest_cpu_handle_t gcpu,
				  io_port_id_t port_id,
				  unsigned port_size, /* 1, 2, 4 */
				  void *p_value);

static void apply_default_device_assignment(guest_id_t guest_id);
static guest_pci_devices_t *find_guest_devices(guest_id_t guest_id);

static
void pci_read_hide(guest_cpu_handle_t gcpu,
		   guest_pci_device_t *pci_device,
		   uint32_t port_id,
		   uint32_t port_size,
		   void *value);
static
void pci_write_hide(guest_cpu_handle_t gcpu,
		    guest_pci_device_t *pci_device,
		    uint32_t port_id,
		    uint32_t port_size,
		    void *value);

static
void pci_read_passthrough(guest_cpu_handle_t gcpu,
			  guest_pci_device_t *pci_device,
			  uint32_t port_id,
			  uint32_t port_size,
			  void *value);

static
void pci_write_passthrough(guest_cpu_handle_t gcpu,
			   guest_pci_device_t *pci_device,
			   uint32_t port_id,
			   uint32_t port_size,
			   void *value);

static list_element_t guest_pci_devices[1];
static hash64_handle_t device_to_guest = HASH64_INVALID_HANDLE;

static gpci_guest_profile_t device_owner_guest_profile = {
	pci_read_passthrough, pci_write_passthrough
}; /* passthrough */
static gpci_guest_profile_t no_devices_guest_profile = {
	pci_read_hide, pci_write_hide };

boolean_t gpci_initialize(void)
{
	guest_handle_t guest;
	guest_econtext_t guest_ctx;

	mon_zeromem(guest_pci_devices, sizeof(guest_pci_devices));
	list_init(guest_pci_devices);
	device_to_guest = hash64_create_default_hash(256);

	for (guest = guest_first(&guest_ctx); guest;
	     guest = guest_next(&guest_ctx))
		gpci_guest_initialize(guest_get_id(guest));

	return TRUE;
}

boolean_t gpci_guest_initialize(guest_id_t guest_id)
{
	guest_pci_devices_t *gpci = NULL;

	/* uint32_t port; */

	gpci =
		(guest_pci_devices_t *)mon_memory_alloc(sizeof(
				guest_pci_devices_t));
	MON_ASSERT(gpci);

	if (gpci == NULL) {
		return FALSE;
	}

	gpci->guest_id = guest_id;

	list_add(guest_pci_devices, gpci->guests);

	gpci->gcpu_pci_access_address = (pci_config_address_t *)
					mon_malloc(guest_gcpu_count(guest_handle(
				guest_id)) *
		sizeof(pci_config_address_t));
	MON_ASSERT(gpci->gcpu_pci_access_address);

	apply_default_device_assignment(guest_id);


	return TRUE;
}

static void apply_default_device_assignment(guest_id_t guest_id)
{
	uint16_t bus, dev, func; /* 16-bit bus to avoid wrap around on bus==256 */
	host_pci_device_t *host_pci_device = NULL;
	gpci_guest_profile_t *guest_profile = NULL;
	guest_device_virtualization_type_t type;

	if (guest_id == guest_get_default_device_owner_guest_id()) {
		guest_profile = &device_owner_guest_profile;
		type = GUEST_DEVICE_VIRTUALIZATION_DIRECT_ASSIGNMENT;
	} else {
		guest_profile = &no_devices_guest_profile;
		type = GUEST_DEVICE_VIRTUALIZATION_HIDDEN;
	}
	for (bus = 0; bus < PCI_MAX_NUM_BUSES; bus++) {
		for (dev = 0; dev < PCI_MAX_NUM_DEVICES_ON_BUS; dev++) {
			for (func = 0; func < PCI_MAX_NUM_FUNCTIONS_ON_DEVICE;
			     func++) {
				host_pci_device =
					get_host_pci_device((uint8_t)bus,
						(uint8_t)dev, (uint8_t)func);
				if (NULL == host_pci_device) { /* device not found */
					continue;
				}
				gpci_register_device(guest_id,
					type,
					host_pci_device,
					NULL,
					guest_profile->pci_read,
					guest_profile->pci_write);
			}
		}
	}
}

boolean_t gpci_register_device(guest_id_t guest_id,
			       guest_device_virtualization_type_t type,
			       host_pci_device_t *host_pci_device,
			       uint8_t *config_space,
			       guest_pci_read_handler_t pci_read,
			       guest_pci_write_handler_t pci_write)
{
	guest_pci_device_t *guest_pci_device = NULL;
	guest_pci_devices_t *gpci = find_guest_devices(guest_id);
	pci_dev_index_t dev_index = 0;

	MON_ASSERT(NULL != gpci);
	MON_ASSERT(NULL != host_pci_device);

	dev_index = gpci->device_lookup_table[host_pci_device->address];

	if (dev_index != 0) {   /* already registered */
		MON_LOG(mask_anonymous, level_trace,
			"Warning: guest pci duplicate registration: guest #%d"
			" device(%d, %d, %d)\r\n",
			guest_id, GET_PCI_BUS(host_pci_device->address),
			GET_PCI_DEVICE(host_pci_device->address),
			GET_PCI_FUNCTION(host_pci_device->address));
		return FALSE;
	}

	dev_index = (pci_dev_index_t)gpci->num_devices++;
	MON_ASSERT(dev_index < PCI_MAX_NUM_SUPPORTED_DEVICES + 1);
	gpci->device_lookup_table[host_pci_device->address] = dev_index;
	guest_pci_device = &gpci->devices[dev_index];
	mon_zeromem(guest_pci_device, sizeof(guest_pci_device_t));

	guest_pci_device->guest_id = guest_id;
	guest_pci_device->host_device = host_pci_device;
	guest_pci_device->config_space = config_space;
	guest_pci_device->pci_read = pci_read;
	guest_pci_device->pci_write = pci_write;
	guest_pci_device->type = type;

	switch (type) {
	case GUEST_DEVICE_VIRTUALIZATION_DIRECT_ASSIGNMENT:
		hash64_insert(device_to_guest,
			(uint64_t)host_pci_device->address,
			guest_id);
		break;

	case GUEST_DEVICE_VIRTUALIZATION_HIDDEN:
		break;

	default:
		MON_ASSERT(0);
		break;
	}

	return TRUE;
}

static guest_pci_devices_t *find_guest_devices(guest_id_t guest_id)
{
	guest_pci_devices_t *guest_devices = NULL;
	list_element_t *guest_iter = NULL;
	boolean_t guest_found = FALSE;

	LIST_FOR_EACH(guest_pci_devices, guest_iter) {
		guest_devices = LIST_ENTRY(guest_iter,
			guest_pci_devices_t,
			guests);
		if (guest_devices->guest_id == guest_id) {
			guest_found = TRUE;
			break;
		}
	}
	if (guest_found) {
		return guest_devices;
	}
	return NULL;
}

guest_id_t gpci_get_device_guest_id(uint16_t bus,
				    uint16_t device,
				    uint16_t function)
{
	boolean_t status = FALSE;
	uint64_t owner_guest_id = 0;

	if (FALSE == PCI_IS_ADDRESS_VALID(bus, device, function)) {
		return INVALID_GUEST_ID;
	}

	status =
		hash64_lookup(device_to_guest,
			(uint64_t)PCI_GET_ADDRESS(bus, device, function),
			&owner_guest_id);

	return (guest_id_t)owner_guest_id;
}

static
void pci_read_hide(guest_cpu_handle_t gcpu UNUSED,
		   guest_pci_device_t *pci_device UNUSED,
		   uint32_t port_id UNUSED, uint32_t port_size, void *value)
{
	mon_memset(value, 0xff, port_size);
}

static
void pci_write_hide(guest_cpu_handle_t gcpu UNUSED,
		    guest_pci_device_t *pci_device UNUSED,
		    uint32_t port_id UNUSED,
		    uint32_t port_size UNUSED, void *value UNUSED)
{
}

static
void pci_read_passthrough(guest_cpu_handle_t gcpu,
			  guest_pci_device_t *pci_device UNUSED,
			  uint32_t port_id, uint32_t port_size, void *value)
{
	io_transparent_read_handler(gcpu,
		(io_port_id_t)port_id,
		port_size,
		value);
}

static
void pci_write_passthrough(guest_cpu_handle_t gcpu,
			   guest_pci_device_t *pci_device UNUSED,
			   uint32_t port_id, uint32_t port_size, void *value)
{
	io_transparent_write_handler(gcpu,
		(io_port_id_t)port_id,
		port_size,
		&value);
}

#endif     /* PCI_SCAN */
