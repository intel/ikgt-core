/*******************************************************************************
 * Copyright (c) 2018 Intel Corporation
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
#include "dbg.h"
#include "vmm_util.h"
#include "hmm.h"
#include "vtd_acpi.h"
#include "lib/util.h"
#include "modules/acpi.h"

#define DMAR_SIGNATURE 0x52414d44  //the ASCII vaule for "DMAR"

typedef struct {
	acpi_table_header_t header;
	uint8_t             width;
	uint8_t             flags;
	uint8_t             reserved[10];
	uint8_t             remapping_structures[0];
} acpi_dmar_table_t;

typedef struct {
	uint16_t            type;
	uint16_t            length;
	uint8_t             flags;
	uint8_t             reserved;
	uint16_t            segment;
	uint64_t            reg_base_hpa;
	uint8_t             device_scope[0];
} acpi_dma_hw_unit_t;

#ifdef SKIP_DMAR_GPU
// OAM-42091 work round -- start
typedef struct {
	uint8_t     dev;
	uint8_t     func;
} acpi_path_element_t;

typedef struct {
	uint8_t             type;
	uint8_t             length;
	uint16_t            reserved;
	uint8_t             enumeration_id;
	uint8_t             start_bus_num;
	acpi_path_element_t path[0];
} acpi_device_scope_t;

/*
 * Assumption:
 *	1. If DMAR engine takes charge of GPU, type of device scope should be
 *		0x01: PCI Endpoint Device, bridge is not expected, do not need to
 *		walk through bridge from start bus. And GPU should be the one and
 *		the only device in devoce scope. So device number should be 1.
 *	2. Bus:Dev:Func of GPU equals to 0:2:0.
 */
static boolean_t dmar_engine_takes_charge_of_gpu(acpi_dma_hw_unit_t *unit)
{
	uint32_t num_of_devices = 0;
	acpi_device_scope_t *device_scope;

	/*
	 * Flags.Bit0: INCLUDE_PCI_ALL. If clear, this remapping hardware unit has
	 * under its scope only devices in the specified Segment that are explicitly
	 * identified through the 'Device Scope' field.
	 *
	 * More details please reference VT Directed IO Specification
	 * Chapter 8.3: DMA Remapping Hardware Uint Definition Structure
	 */
	if (unit->flags & 1) {
		return FALSE;
	}

	device_scope = (acpi_device_scope_t *)unit->device_scope;
	num_of_devices = (device_scope->length - OFFSET_OF(acpi_device_scope_t, path))
		/ sizeof(acpi_path_element_t);

	/*
	 * GPU device is PCI Endpoint Device, walk through bridge is unexpected.
	 * So numbe of device should be 1.
	 * Device type should be 1(PCI Endpoint device)
	 * Bus:Dev:Func of GPU shoule be 0:2:0
	 */

	if ((1 == num_of_devices) &&
		(1 == device_scope->type) &&
		(0 == device_scope->start_bus_num) &&
		(2 == device_scope->path->dev) &&
		(0 == device_scope->path->func)) {
		return TRUE;
	}

	return FALSE;
}
// OAM-42091 work round -- end
#endif

void vtd_dmar_parse(vtd_engine_t *engine_list)
{
	acpi_dmar_table_t *acpi_dmar;
	acpi_dma_hw_unit_t *unit;
	uint32_t offset = 0, id = 0;
	uint64_t hva;

	D(VMM_ASSERT_EX(engine_list, "engine_list is NULL!\n"));

	acpi_dmar = (acpi_dmar_table_t *)acpi_locate_table(DMAR_SIGNATURE);
	VMM_ASSERT_EX(acpi_dmar, "acpi_dmar is NULL\n");
	print_info("VTD is detected.\n");

	while (offset < (acpi_dmar->header.length - sizeof(acpi_dmar_table_t))) {

		unit = (acpi_dma_hw_unit_t *)(acpi_dmar->remapping_structures + offset);

		switch(unit->type) {
			/* DMAR type hardware uint */
			case 0:
#ifdef SKIP_DMAR_GPU
// OAM-42091 work round -- start
				if (dmar_engine_takes_charge_of_gpu(unit)) {
					/*
					 * Add print info here to check VT-D GPU work round easy.
					 * DMAR engine 0 takes charge of GPU
					 */
					print_info("VT-D: SKIP_DMAR_GPU is on\n");
					print_info("\tSkip DMAR engine for GPU\n");
					if (id != 0) {
						print_info("*****************************************************\n");
						print_info("!!CAUTION!!:\t");
						print_info("\tDAMR ENGINE(%d) FOR GPU IS UNEXPECTED\n", id);
						print_info("*****************************************************\n");
					}
					break;
				}
// OAM-42091 work round -- end
#endif
				VMM_ASSERT_EX((id < DMAR_MAX_ENGINE),
						"too many dmar engines\n");
				VMM_ASSERT_EX(hmm_hpa_to_hva(unit->reg_base_hpa, &hva),
						"fail to convert hpa 0x%llX to hva", unit->reg_base_hpa);
				engine_list[id].reg_base_hpa = unit->reg_base_hpa;
				engine_list[id].reg_base_hva = hva;
				id ++;
				break;

			/* Just take care of DMAR hw uint */
			default:
				break;
		}

		offset += unit->length;
	}
	VMM_ASSERT_EX(id, "No DMAR HW unit found from ACPI table!");

	/* Hide VT-D ACPI table */
	memset((void *)&acpi_dmar->header.signature, 0, acpi_dmar->header.length);
}

