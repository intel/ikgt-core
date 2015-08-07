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

#include "host_pci_configuration.h"
#include "hw_utils.h"
#include "mon_dbg.h"
#include "libc.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(HOST_PCI_CONFIGURATION_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(HOST_PCI_CONFIGURATION_C, \
	__condition)

/* index 0 is not in use. used to specify "invalid" in lookup table */
#define PCI_DEV_INDEX_INVALID   0

/* bit 7: =0 single function, =1 multi-function */
#define PCI_IS_MULTIFUNCTION_DEVICE(header_type) (((header_type) & 0x80) != 0)
#define PCI_IS_PCI_2_PCI_BRIDGE(base_class, sub_class) \
	((base_class) == PCI_BASE_CLASS_BRIDGE && (sub_class) == 0x04)
#ifdef PCI_SCAN
static host_pci_device_t pci_devices[PCI_MAX_NUM_SUPPORTED_DEVICES + 1];
/* index 0 is not in use.  used to specify "invalid" in lookup table */
static pci_dev_index_t avail_pci_device_index = 1;
static pci_dev_index_t pci_devices_lookup_table[PCI_MAX_NUM_FUNCTIONS];
static uint32_t num_pci_devices;

uint8_t pci_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg)
{
	pci_config_address_t addr;

	addr.uint32 = 0;
	addr.bits.bus = bus;
	addr.bits.device = device;
	addr.bits.function = function;
	addr.bits.reg = reg;
	addr.bits.enable = 1;

	hw_write_port_32(PCI_CONFIG_ADDRESS_REGISTER, addr.uint32 & ~0x3);
	return hw_read_port_8(PCI_CONFIG_DATA_REGISTER | (addr.uint32 & 0x3));
}

void pci_write8(uint8_t bus,
		uint8_t device,
		uint8_t function,
		uint8_t reg,
		uint8_t value)
{
	pci_config_address_t addr;

	addr.uint32 = 0;
	addr.bits.bus = bus;
	addr.bits.device = device;
	addr.bits.function = function;
	addr.bits.reg = reg;
	addr.bits.enable = 1;

	hw_write_port_32(PCI_CONFIG_ADDRESS_REGISTER, addr.uint32 & ~0x3);
	hw_write_port_8(PCI_CONFIG_DATA_REGISTER | (addr.uint32 & 0x3), value);
}

uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg)
{
	pci_config_address_t addr;

	addr.uint32 = 0;
	addr.bits.bus = bus;
	addr.bits.device = device;
	addr.bits.function = function;
	addr.bits.reg = reg;
	addr.bits.enable = 1;

	hw_write_port_32(PCI_CONFIG_ADDRESS_REGISTER, addr.uint32 & ~0x3);
	return hw_read_port_16(PCI_CONFIG_DATA_REGISTER | (addr.uint32 & 0x3));
}

void pci_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg,
		 uint16_t value)
{
	pci_config_address_t addr;

	addr.uint32 = 0;
	addr.bits.bus = bus;
	addr.bits.device = device;
	addr.bits.function = function;
	addr.bits.reg = reg;
	addr.bits.enable = 1;

	hw_write_port_32(PCI_CONFIG_ADDRESS_REGISTER, addr.uint32 & ~0x3);
	hw_write_port_16(PCI_CONFIG_DATA_REGISTER | (addr.uint32 & 0x2), value);
}

uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg)
{
	pci_config_address_t addr;

	addr.uint32 = 0;
	addr.bits.bus = bus;
	addr.bits.device = device;
	addr.bits.function = function;
	addr.bits.reg = reg;
	addr.bits.enable = 1;

	hw_write_port_32(PCI_CONFIG_ADDRESS_REGISTER, addr.uint32 & ~0x3);
	return hw_read_port_32(PCI_CONFIG_DATA_REGISTER);
}

void pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg,
		 uint32_t value)
{
	pci_config_address_t addr;

	addr.uint32 = 0;
	addr.bits.bus = bus;
	addr.bits.device = device;
	addr.bits.function = function;
	addr.bits.reg = reg;
	addr.bits.enable = 1;

	hw_write_port_32(PCI_CONFIG_ADDRESS_REGISTER, addr.uint32 & ~0x3);
	hw_write_port_32(PCI_CONFIG_DATA_REGISTER, value);
}

host_pci_device_t *get_host_pci_device(uint8_t bus,
				       uint8_t device,
				       uint8_t function)
{
	host_pci_device_t *pci_dev;
	pci_device_address_t lookup_table_index = 0;
	pci_dev_index_t pci_dev_index = 0;

	if (FALSE == PCI_IS_ADDRESS_VALID(bus, device, function)) {
		return NULL;
	}
	SET_PCI_BUS(lookup_table_index, bus);
	SET_PCI_DEVICE(lookup_table_index, device);
	SET_PCI_FUNCTION(lookup_table_index, function);
	pci_dev_index = pci_devices_lookup_table[lookup_table_index];

	if (PCI_DEV_INDEX_INVALID == pci_dev_index) {
		pci_dev = NULL;
	} else {
		pci_dev = &pci_devices[pci_dev_index];
	}
	return pci_dev;
}

boolean_t pci_read_secondary_bus_reg(uint8_t bus, uint8_t device, uint8_t func,
				     OUT uint8_t *secondary_bus)
{
	host_pci_device_t *pci_bridge = get_host_pci_device(bus, device, func);

	*secondary_bus = 0;
	if (NULL == pci_bridge
	    || FALSE == pci_bridge->is_pci_2_pci_bridge
	    || PCI_CONFIG_HEADER_TYPE_PCI2PCI_BRIDGE !=
	    pci_bridge->header_type) {
		return FALSE;
	}

	*secondary_bus =
		pci_read8(bus, device, func, PCI_CONFIG_SECONDARY_BUS_OFFSET);
	return TRUE;
}

static uint8_t
host_pci_decode_bar(uint8_t bus,
		    uint8_t device,
		    uint8_t function,
		    uint8_t bar_offset, pci_base_address_register_t *bar)
{
	uint32_t bar_value_low = pci_read32(bus, device, function, bar_offset);
	uint32_t bar_value_high = 0;
	uint64_t bar_value = 0;
	uint32_t encoded_size_low = 0;
	uint32_t encoded_size_high = 0;
	uint64_t encoded_size = 0;
	uint64_t mask;
	uint32_t address_type = PCI_CONFIG_HEADER_BAR_ADDRESS_32;

	MON_LOG(mask_anonymous,
		level_trace,
		"%s %d:%d:%d:%d, bar_value_low=0x%x\r\n",
		__FUNCTION__,
		bus,
		device,
		function,
		bar_offset,
		bar_value_low);

	if (bar_value_low > 1) {
		/* 0: not used mmio space; 1: not used io space
		 * issue size determination command */
		pci_write32(bus, device, function, bar_offset,
			PCI_CONFIG_HEADER_BAR_SIZING_COMMAND);
		encoded_size_low =
			pci_read32(bus, device, function, bar_offset);

		bar->type = bar_value_low &
			    PCI_CONFIG_HEADER_BAR_MEMORY_TYPE_MASK;

		mask = (PCI_BAR_IO_REGION == bar->type) ?
		       PCI_CONFIG_HEADER_BAR_ADDRESS_MASK_TYPE_IO :
		       PCI_CONFIG_HEADER_BAR_ADDRESS_MASK_TYPE_MMIO;

		/* valid only for mmio */
		address_type = (uint32_t)(bar_value_low &
					  PCI_CONFIG_HEADER_BAR_ADDRESS_TYPE_MASK)
			       >> 1;

		if (bar->type == PCI_BAR_MMIO_REGION
		    && address_type == PCI_CONFIG_HEADER_BAR_ADDRESS_64) {
			/* issue size determination command */
			bar_value_high = pci_read32(bus,
				device,
				function,
				bar_offset + 4);
			pci_write32(bus, device, function, bar_offset + 4,
				PCI_CONFIG_HEADER_BAR_SIZING_COMMAND);
			encoded_size_high =
				pci_read32(bus, device, function, bar_offset +
					4);
			bar_value = (uint64_t)bar_value_high << 32 |
				    ((uint64_t)bar_value_low &
				     0x00000000FFFFFFFF);
			bar->addr = bar_value & mask;
			encoded_size = (uint64_t)encoded_size_high << 32 |
				       ((uint64_t)encoded_size_low &
					0x00000000FFFFFFFF);
			encoded_size &= mask;
			bar->length = (~encoded_size) + 1;
			/* restore original value */
			pci_write32(bus,
				device,
				function,
				bar_offset,
				bar_value_low);
			pci_write32(bus,
				device,
				function,
				bar_offset + 4,
				bar_value_high);
		} else {
			bar->addr =
				((uint64_t)bar_value_low &
				 0x00000000FFFFFFFF) & mask;
			encoded_size = 0xFFFFFFFF00000000 |
				       ((uint64_t)encoded_size_low &
					0x00000000FFFFFFFF);
			encoded_size &= mask;
			bar->length = (~encoded_size) + 1;
			/* restore original value */
			pci_write32(bus,
				device,
				function,
				bar_offset,
				bar_value_low);
		}

		if (PCI_BAR_IO_REGION == bar->type) {
			/* IO space in Intel arch can't exceed 64K bytes */
			bar->length &= 0xFFFF;
		}
	} else {
		bar->type = PCI_BAR_UNUSED;
	}
	return (address_type == PCI_CONFIG_HEADER_BAR_ADDRESS_64)
}

static void host_pci_decode_pci_bridge(uint8_t bus,
				       uint8_t device,
				       uint8_t function,
				       pci_base_address_register_t *bar_mmio,
				       pci_base_address_register_t *bar_io)
{
	uint32_t memory_base =
		((uint32_t)pci_read16(bus, device, function,
			 PCI_CONFIG_BRIDGE_MEMORY_BASE) << 16) & 0xFFF00000;
	uint32_t memory_limit =
		((uint32_t)pci_read16(bus, device, function,
			 PCI_CONFIG_BRIDGE_MEMORY_LIMIT) << 16) | 0x000FFFFF;
	uint8_t io_base_low =
		pci_read8(bus, device, function, PCI_CONFIG_BRIDGE_IO_BASE_LOW);
	uint8_t io_limit_low =
		pci_read8(bus, device, function,
			PCI_CONFIG_BRIDGE_IO_LIMIT_LOW);
	uint16_t io_base_high = 0;
	uint16_t io_limit_high = 0;
	uint64_t io_base;
	uint64_t io_limit;

	/* mmio */
	if (memory_limit < memory_base) {
		bar_mmio->type = PCI_BAR_UNUSED;
	} else {
		bar_mmio->type = PCI_BAR_MMIO_REGION;
		bar_mmio->addr = (uint64_t)memory_base & 0x00000000FFFFFFFF;
		bar_mmio->length =
			(uint64_t)(memory_limit - memory_base +
				   1) & 0x00000000FFFFFFFF;
	}

	/* io */
	if (io_base_low == 0 || io_limit_low == 0 || io_limit_low <
	    io_base_low) {
		bar_io->type = PCI_BAR_UNUSED;
	} else if ((io_base_low & 0xF) > 1) {
		bar_io->type = PCI_BAR_UNUSED;
		MON_LOG(mask_anonymous, level_print_always,
			"%s Warning: reserved IO address capability in bridge"
			" (%d:%d:%d) detected, io_base_low=0x%x\r\n",
			__FUNCTION__, bus, device, function, io_base_low);
	} else {
		if ((io_base_low & 0xF) == 1) {
			/* 32 bit IO address */
			/* update the high 16 bits */
			io_base_high =
				pci_read16(bus, device, function,
					PCI_CONFIG_BRIDGE_IO_BASE_HIGH);
			io_limit_high =
				pci_read16(bus, device, function,
					PCI_CONFIG_BRIDGE_IO_LIMIT_HIGH);
		}
		io_base =
			(((uint64_t)io_base_high << 16) & 0x00000000FFFF0000) |
			(((uint64_t)io_base_low << 8) & 0x000000000000F000);
		io_limit =
			(((uint64_t)io_limit_high << 16) & 0x00000000FFFF0000) |
			(((uint64_t)io_limit_low <<
			8) & 0x000000000000F000) |
			0x0000000000000FFF;
		bar_io->type = PCI_BAR_IO_REGION;
		bar_io->addr = io_base;
		bar_io->length = io_limit - io_base + 1;
	}
}

static void pci_init_device(pci_device_address_t device_addr,
			    pci_device_address_t parent_addr,
			    boolean_t parent_addr_valid, boolean_t is_bridge)
{
	host_pci_device_t *pci_dev;
	pci_dev_index_t pci_dev_index = 0;
	uint32_t i;
	uint8_t bus, device, function;
	uint8_t bar_offset;

	MON_ASSERT(avail_pci_device_index <= PCI_MAX_NUM_SUPPORTED_DEVICES);

	pci_dev_index = pci_devices_lookup_table[device_addr];

	if (PCI_DEV_INDEX_INVALID != pci_dev_index) {
		/* already initialized */
		return;
	}

	num_pci_devices++;
	pci_dev_index = avail_pci_device_index++;
	pci_devices_lookup_table[device_addr] = pci_dev_index;

	pci_dev = &pci_devices[pci_dev_index];
	pci_dev->address = device_addr;
	bus = GET_PCI_BUS(device_addr);
	device = GET_PCI_DEVICE(device_addr);
	function = GET_PCI_FUNCTION(device_addr);
	pci_dev->vendor_id =
		pci_read16(bus, device, function, PCI_CONFIG_VENDOR_ID_OFFSET);
	pci_dev->device_id =
		pci_read16(bus, device, function, PCI_CONFIG_DEVICE_ID_OFFSET);
	pci_dev->revision_id =
		pci_read8(bus, device, function, PCI_CONFIG_REVISION_ID_OFFSET);
	pci_dev->base_class =
		pci_read8(bus,
			device,
			function,
			PCI_CONFIG_BASE_CLASS_CODE_OFFSET);
	pci_dev->sub_class =
		pci_read8(bus,
			device,
			function,
			PCI_CONFIG_SUB_CLASS_CODE_OFFSET);
	pci_dev->programming_interface =
		pci_read8(bus, device, function,
			PCI_CONFIG_PROGRAMMING_INTERFACE_OFFSET);
	pci_dev->header_type =
		pci_read8(bus, device, 0, PCI_CONFIG_HEADER_TYPE_OFFSET);
	pci_dev->is_multifunction =
		PCI_IS_MULTIFUNCTION_DEVICE(pci_dev->header_type);
	/* clear multifunction bit */
	pci_dev->header_type = pci_dev->header_type & ~0x80;
	pci_dev->is_pci_2_pci_bridge =
		PCI_IS_PCI_2_PCI_BRIDGE(pci_dev->base_class,
			pci_dev->sub_class);
	pci_dev->interrupt_pin =
		pci_read8(bus, device, function,
			PCI_CONFIG_INTERRUPT_PIN_OFFSET);
	pci_dev->interrupt_line =
		pci_read8(bus,
			device,
			function,
			PCI_CONFIG_INTERRUPT_LINE_OFFSET);
	if (parent_addr_valid) {
		pci_dev->parent =
			get_host_pci_device(GET_PCI_BUS(parent_addr),
				GET_PCI_DEVICE(parent_addr),
				GET_PCI_FUNCTION(parent_addr));
	} else {
		pci_dev->parent = NULL;
	}

	if (pci_dev->parent == NULL) {
		pci_dev->depth = 1;
		pci_dev->path.start_bus = bus;
	} else {
		pci_dev->depth = pci_dev->parent->depth + 1;
		pci_dev->path.start_bus = pci_dev->parent->path.start_bus;
		for (i = 0; i < pci_dev->parent->depth; i++)
			pci_dev->path.path[i] = pci_dev->parent->path.path[i];
	}
	MON_ASSERT(pci_dev->depth <= PCI_MAX_PATH);
	pci_dev->path.path[pci_dev->depth - 1].device = device;
	pci_dev->path.path[pci_dev->depth - 1].function = function;

	bar_offset = PCI_CONFIG_BAR_OFFSET;
	if (is_bridge) {
		for (i = 0; i < PCI_MAX_BAR_NUMBER_IN_BRIDGE; i++) {
			/* Assumption: according to PCI bridge spec 1.2,
			 * host_pci_decode_bar() will only return 4 (as 32 bit) for bridge
			 * 64 bit mapping is not supported in bridge */
			bar_offset =
				bar_offset + host_pci_decode_bar(bus,
					device,
					function,
					bar_offset,
					&pci_dev->bars[i]);
		}
		/* set io range and mmio range */
		host_pci_decode_pci_bridge(bus,
			device,
			function,
			&pci_dev->bars[i],
			&pci_dev->bars[i + 1]);
		/* for the io bar and mmio bar set by
		 * host_pci_decode_pci_bridge() above */
		i += 2;
		/* set rest bars as unused */
		for (; i < PCI_MAX_BAR_NUMBER; i++)
			pci_dev->bars[i].type = PCI_BAR_UNUSED;
	} else {
		for (i = 0; i < PCI_MAX_BAR_NUMBER; i++) {
			if (bar_offset > PCI_CONFIG_BAR_LAST_OFFSET) {
				/* total bar size is 0x10~0x24 */
				pci_dev->bars[i].type = PCI_BAR_UNUSED;
			} else {
				bar_offset =
					bar_offset + host_pci_decode_bar(bus,
						device,
						function,
						bar_offset,
						&pci_dev->bars[i]);
			}
		}
	}
}

static void pci_scan_bus(uint8_t bus, pci_device_address_t parent_addr,
			 boolean_t parent_addr_valid)
{
	uint8_t device = 0;
	uint8_t function = 0;
	uint8_t header_type = 0;
	uint8_t max_functions = 0;
	uint16_t vendor_id = 0;
	uint16_t device_id = 0;
	boolean_t is_multifunction = 0;
	uint8_t base_class = 0;
	uint8_t sub_class = 0;
	uint8_t secondary_bus = 0;
	pci_device_address_t this_device_address = 0;
	boolean_t is_bridge;

	for (device = 0; device < PCI_MAX_NUM_DEVICES_ON_BUS; device++) {
		header_type = pci_read8(bus,
			device,
			0,
			PCI_CONFIG_HEADER_TYPE_OFFSET);
		is_multifunction = PCI_IS_MULTIFUNCTION_DEVICE(header_type);
		/* bit 7: =0 single function, =1 multi-function */
		max_functions =
			is_multifunction ? PCI_MAX_NUM_FUNCTIONS_ON_DEVICE : 1;
		/* clear multifunction bit */
		header_type = header_type & ~0x80;

		for (function = 0; function < max_functions; function++) {
			vendor_id =
				pci_read16(bus,
					device,
					function,
					PCI_CONFIG_VENDOR_ID_OFFSET);
			device_id =
				pci_read16(bus,
					device,
					function,
					PCI_CONFIG_DEVICE_ID_OFFSET);

			if (PCI_INVALID_VENDOR_ID == vendor_id
			    || PCI_INVALID_DEVICE_ID == device_id) {
				continue;
			}

			SET_PCI_BUS(this_device_address, bus);
			SET_PCI_DEVICE(this_device_address, device);
			SET_PCI_FUNCTION(this_device_address, function);

			base_class =
				pci_read8(bus, device, function,
					PCI_CONFIG_BASE_CLASS_CODE_OFFSET);
			sub_class =
				pci_read8(bus, device, function,
					PCI_CONFIG_SUB_CLASS_CODE_OFFSET);

			is_bridge = PCI_IS_PCI_2_PCI_BRIDGE(base_class,
				sub_class);

			/* call device handler */
			pci_init_device(this_device_address,
				parent_addr,
				parent_addr_valid,
				is_bridge);

			/* check if it is needed to go downstream the bridge */
			if (is_bridge) {
				if (header_type == 1) {
					/* PCI Bridge header type. it should be
					 * 1. Skip misconfigured devices */
					secondary_bus =
						pci_read8(bus,
							device,
							function,
							PCI_CONFIG_SECONDARY_BUS_OFFSET);
					pci_scan_bus(secondary_bus,
						this_device_address,
						TRUE);
				}
			}
		}
	}
}

uint32_t host_pci_get_num_devices(void)
{
	return num_pci_devices;
}

static void host_pci_print_bar(pci_base_address_register_t *bar)
{
	static char *bar_type_string;

	if (PCI_BAR_UNUSED == bar->type) {
		/* bar_type_string = "unused"; */
		return;
	} else if (PCI_BAR_IO_REGION == bar->type) {
		bar_type_string = "io";
	} else if (PCI_BAR_MMIO_REGION == bar->type) {
		bar_type_string = "mmio";
	}

	MON_LOG(mask_anonymous, level_trace, "%s addr=%p size=%p; ",
		bar_type_string, bar->addr, bar->length);
}

static void host_pci_print(void)
{
	uint32_t i = 0, j;
	pci_device_address_t device_addr;
	pci_dev_index_t pci_dev_index = 0;
	host_pci_device_t *pci_dev;

	MON_LOG(mask_anonymous, level_trace,
		"[Bus]    [Dev]    [Func]    [Vendor ID]    [Dev ID]"
		"   [PCI-PCI Bridge]\r\n");

	for (i = 0; i < PCI_MAX_NUM_FUNCTIONS; i++) {
		if (PCI_DEV_INDEX_INVALID == pci_devices_lookup_table[i]) {
			continue;
		}

		device_addr = (uint16_t)i;
		pci_dev_index = pci_devices_lookup_table[i];
		pci_dev = &pci_devices[pci_dev_index];

		MON_LOG(mask_anonymous, level_trace,
			"%5d    %5d    %6d    %#11x    %#8x    ",
			GET_PCI_BUS(device_addr), GET_PCI_DEVICE(device_addr),
			GET_PCI_FUNCTION(device_addr), pci_dev->vendor_id,
			pci_dev->device_id);

		if (pci_dev->is_pci_2_pci_bridge) {
			MON_LOG(mask_anonymous, level_trace, "%16c    ", 'X');
		}
		MON_LOG(mask_anonymous, level_trace, "\r\n BARs: ");
		for (j = 0; j < PCI_MAX_BAR_NUMBER; j++)
			host_pci_print_bar(&(pci_dev->bars[j]));
		MON_LOG(mask_anonymous, level_trace, "\r\n");
	}
}

void host_pci_initialize(void)
{
	/* use 16 bits instead of 8 to avoid wrap around on bus==256 */
	uint16_t bus;
	pci_device_address_t addr = { 0 };

	mon_zeromem(pci_devices, sizeof(pci_devices));
	mon_zeromem(pci_devices_lookup_table, sizeof(pci_devices_lookup_table));

	MON_LOG(mask_anonymous, level_trace, "\r\nSTART Host PCI scan\r\n");
	for (bus = 0; bus < PCI_MAX_NUM_BUSES; bus++)
		pci_scan_bus((uint8_t)bus, addr, FALSE);

	host_pci_print();
	MON_LOG(mask_anonymous, level_trace, "\r\nEND Host PCI scan\r\n");
}
#endif     /* PCI_SCAN */
