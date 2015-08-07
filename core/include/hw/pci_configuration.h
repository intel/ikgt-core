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

#ifndef _PCI_CONFIGURATION_H
#define _PCI_CONFIGURATION_H

#include "mon_defs.h"

#define PCI_CONFIG_SPACE_SIZE                   0x100

/* PCI config header fileds */
#define PCI_CONFIG_VENDOR_ID_OFFSET             0x00
#define PCI_CONFIG_DEVICE_ID_OFFSET             0x02
#define PCI_CONFIG_COMMAND_OFFSET               0x04
#define PCI_CONFIG_REVISION_ID_OFFSET           0x08
#define PCI_CONFIG_CLASS_CODE_OFFSET            0x09
#define PCI_CONFIG_PROGRAMMING_INTERFACE_OFFSET PCI_CONFIG_CLASS_CODE_OFFSET
#define PCI_CONFIG_SUB_CLASS_CODE_OFFSET        0x0A
#define PCI_CONFIG_BASE_CLASS_CODE_OFFSET       0x0B
#define PCI_CONFIG_CACHE_LINE_SIZE_OFFSET       0x0C
#define PCI_CONFIG_LATENCY_TIMER_OFFSET         0x0D
#define PCI_CONFIG_HEADER_TYPE_OFFSET           0x0E
#define PCI_CONFIG_BIST_OFFSET                  0x0F
#define PCI_CONFIG_BAR_OFFSET                   0x10
#define PCI_CONFIG_BAR_LAST_OFFSET              0x24
#define PCI_CONFIG_CARD_BUS_CIS_PTR_OFFSET      0x28
#define PCI_CONFIG_SUBSYSTEM_VENDOR_ID_OFFSET   0x2C
#define PCI_CONFIG_SUBSYSTEM_ID_OFFSET          0x2E
#define PCI_CONFIG_EXPANSION_ROM_BASE_OFFSET    0x30
#define PCI_CONFIG_CAPABILITIES_PTR_OFFSET      0x34
#define PCI_CONFIG_INTERRUPT_LINE_OFFSET        0x3C
#define PCI_CONFIG_INTERRUPT_PIN_OFFSET         0x3D
#define PCI_CONFIG_MIN_GNT_OFFSET               0x3E
#define PCI_CONFIG_MAX_LAT_OFFSET               0x3F

/* for PCI config of type '1' (bridge) */
#define PCI_CONFIG_SECONDARY_BUS_OFFSET         0x19
#define PCI_CONFIG_BRIDGE_MEMORY_BASE           0x20
#define PCI_CONFIG_BRIDGE_MEMORY_LIMIT          0x22
#define PCI_CONFIG_BRIDGE_IO_BASE_LOW           0x1C
#define PCI_CONFIG_BRIDGE_IO_LIMIT_LOW          0x1D
#define PCI_CONFIG_BRIDGE_IO_BASE_HIGH          0x30
#define PCI_CONFIG_BRIDGE_IO_LIMIT_HIGH         0x32

#define PCI_BASE_CLASS_BRIDGE                   0x06

#define PCI_CONFIG_ADDRESS_REGISTER             0xCF8
#define PCI_CONFIG_DATA_REGISTER                0xCFC

#define PCI_INVALID_VENDOR_ID                   0xFFFF
#define PCI_INVALID_DEVICE_ID                   PCI_INVALID_VENDOR_ID

#define PCI_CONFIG_HEADER_TYPE_DEVICE           0x0
#define PCI_CONFIG_HEADER_TYPE_PCI2PCI_BRIDGE   0x1
#define PCI_CONFIG_HEADER_TYPE_CARDBUS_BRIDGE   0x2

#define PCI_MAX_NUM_BUSES                       (uint16_t)256
#define PCI_MAX_NUM_DEVICES_ON_BUS              (uint16_t)32
#define PCI_MAX_NUM_FUNCTIONS_ON_DEVICE         (uint16_t)8
#define PCI_MAX_NUM_FUNCTIONS (PCI_MAX_NUM_BUSES * \
			       PCI_MAX_NUM_DEVICES_ON_BUS * \
			       PCI_MAX_NUM_FUNCTIONS_ON_DEVICE)

#define PCI_MAX_NUM_SUPPORTED_DEVICES           0x100
#define PCI_MAX_PATH                            16

#define PCI_IS_ADDRESS_VALID(bus, device, function) \
	(bus < PCI_MAX_NUM_BUSES && device < PCI_MAX_NUM_DEVICES_ON_BUS && \
	 function < PCI_MAX_NUM_FUNCTIONS_ON_DEVICE)
#define PCI_GET_ADDRESS(bus, device, function) (bus << 8 | device << 3 | \
	function)

#define PCI_CONFIG_HEADER_BAR_MEMORY_TYPE_MASK            ((uint64_t)0x1)
#define PCI_CONFIG_HEADER_BAR_ADDRESS_TYPE_MASK           ((uint64_t)0x6)
#define PCI_CONFIG_HEADER_BAR_IO_ENCODING_MASK            ((uint64_t)0x3)
#define PCI_CONFIG_HEADER_BAR_MEM_ENCODING_MASK           ((uint64_t)0xf)
#define PCI_CONFIG_HEADER_COMMAND_IOSPACE_MASK            ((uint64_t)0x1)
#define PCI_CONFIG_HEADER_COMMAND_MEMORY_MASK             ((uint64_t)0x2)
#define PCI_CONFIG_HEADER_BAR_SIZING_COMMAND              0xFFFFFFFF
#define PCI_BAR_MMIO_REGION                               ((bar_type_t)0)
#define PCI_BAR_IO_REGION                                 ((bar_type_t)1)
#define PCI_BAR_UNUSED                                    ((bar_type_t)-1)
#define PCI_CONFIG_HEADER_BAR_ADDRESS_MASK_TYPE_MMIO      ((uint64_t)~(0xf))
#define PCI_CONFIG_HEADER_BAR_ADDRESS_MASK_TYPE_IO        ((uint64_t)~(0x3))
#define PCI_CONFIG_HEADER_BAR_ADDRESS_32                  0
#define PCI_CONFIG_HEADER_BAR_ADDRESS_64                  0x2

#if (PCI_MAX_NUM_SUPPORTED_DEVICES <= 0x100)
typedef uint8_t pci_dev_index_t;
#elif (PCI_MAX_NUM_SUPPORTED_DEVICES <= 0x10000)
typedef uint16_t pci_dev_index_t;
#else
typedef uint32_t pci_dev_index_t;
#endif



typedef union {
	struct {
		uint32_t reg:8;
		uint32_t function:3;
		uint32_t device:5;
		uint32_t bus:8;
		uint32_t reserved:7;
		uint32_t enable:1;
	} PACKED bits;
	uint32_t uint32;
} PACKED pci_config_address_t;

typedef uint16_t pci_device_address_t;

#define PCI_BUS_MASK                 0xff00
#define PCI_DEVICE_MASK              0x00f8
#define PCI_FUNCTION_MASK            0x0007

#define GET_PCI_BUS(addr)      (uint8_t)(BITMAP_GET((addr), PCI_BUS_MASK) >> 8)
#define GET_PCI_DEVICE(addr)   (uint8_t)(BITMAP_GET((addr), \
						 PCI_DEVICE_MASK) >> 3)
#define GET_PCI_FUNCTION(addr) (uint8_t)(BITMAP_GET((addr), PCI_FUNCTION_MASK))

#define SET_PCI_BUS(addr, bus)           \
	BITMAP_ASSIGN((addr), PCI_BUS_MASK, (bus) << 8)
#define SET_PCI_DEVICE(addr, device)     \
	BITMAP_ASSIGN((addr), PCI_DEVICE_MASK, (device) << 3)
#define SET_PCI_FUNCTION(addr, function) \
	BITMAP_ASSIGN((addr), PCI_FUNCTION_MASK, (function))



typedef struct {
	uint8_t device;
	uint8_t function;
} pci_path_element_t;

typedef struct {
	uint8_t			start_bus;
	pci_path_element_t	path[PCI_MAX_PATH];
} pci_path_t;

#define PCI_MAX_BAR_NUMBER      6

typedef enum {
	BAR_TYPE_IO,
	BAR_TYPE_MMIO
} bar_type_t;

#define PCI_MAX_BAR_NUMBER_IN_BRIDGE      2

typedef struct {
	bar_type_t	type;
	char		padding1[4];
	uint64_t	addr;
	uint64_t	length;
} pci_base_address_register_t;

#endif
