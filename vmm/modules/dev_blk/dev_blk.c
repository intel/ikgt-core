/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
* * Licensed under the Apache License, Version 2.0 (the "License");
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
#include "vmm_asm.h"
#include "vmm_base.h"
#include "heap.h"
#include "gpm.h"
#include "guest.h"
#include "gcpu.h"
#include "vmm_objects.h"
#include "event.h"
#include "hmm.h"
#include "lib/pci.h"
#include "lib/util.h"
#include "modules/io_monitor.h"
#include "modules/acpi.h"
#include "modules/dev_blk.h"

#define PCI_CFG_VID				0x00
#define PCI_CFG_DID				0x02
#define PCI_CFG_SUB_CLASS			0x0A
#define PCI_CFG_BASE_CLASS			0x0B
#define PCI_CFG_BAR_FIRST			0x10
#define PCI_CFG_BAR_LAST			0x24

#define PCI_CFG_BRIDGE_IO_BASE_LOW		0x1C
#define PCI_CFG_BRIDGE_IO_LIMIT_LOW		0x1D
#define PCI_CFG_BRIDGE_MEM_BASE			0x20
#define PCI_CFG_BRIDGE_MEM_LIMIT		0x22
#define PCI_CFG_BRIDGE_IO_BASE_HIGH		0x30
#define PCI_CFG_BRIDGE_IO_LIMIT_HIGH		0x32


#define PCI_DEVICE_BAR_NUM			6
#define PCI_BRIDGE_BAR_NUM			2

/* PCI-to-PCI bridge: class code 0x06, sub class 0x04 */
#define PCI_IS_BRIDGE(base_class, sub_class) ((base_class) == 0x06 && (sub_class) == 0x04)

/* MCFG: PCI Express memory mapped configuration space base address description table */
#define ACPI_SIG_MCFG 				0x4746434d //"MCFG"

#define PCI_BLOCK_DEV_NUM 			10

#define PCI_BAR_MMIO				0
#define PCI_BAR_IO				1
#define PCI_BAR_UNUSED				-1
#define PCI_BAR_MMIO_32				0
#define PCI_BAR_MMIO_64				0x4

typedef struct {
	uint32_t	type; //PCI_BAR_MMIO, PCI_BAR_IO, PCI_BAR_UNUSED
	uint32_t	pad;
	uint64_t	addr;
	uint64_t	length;
} base_addr_reg_t;

typedef struct {
	uint16_t	pci_dev;
	uint8_t		pad[6];
	base_addr_reg_t	bars[PCI_DEVICE_BAR_NUM];
} pci_block_device_t;

typedef struct pci_block_guest {
	uint16_t		guest_id;
	uint16_t		pad;
	uint32_t		block_dev_num;
	pci_block_device_t	block_dev_list[PCI_BLOCK_DEV_NUM];
	struct pci_block_guest	*next;
} pci_block_guest_t;

typedef struct {
	acpi_table_header_t	header;
	uint64_t		reserved_1;
	uint64_t		mmcfg_base_addr;
	uint16_t		pci_segment_num;
	uint8_t			start_bus_num;
	uint8_t			end_bus_num;
	uint32_t		reserved_2;
} PACKED acpi_table_mcfg_t;

static pci_block_guest_t *g_pci_block_guest;
static uint64_t g_mmio_dummy_page_hpa;
static uint64_t pcix_mmcfg_addr;

void device_block_event(guest_cpu_handle_t gcpu, void *p)
{
	event_ept_violation_t *event_ept_violation = (event_ept_violation_t *)p;
	guest_handle_t guest = gcpu->guest;
	vmcs_obj_t vmcs = gcpu->vmcs;
	vmx_exit_qualification_t ept_qualification;
	uint64_t gpa;
	uint64_t hpa;
	ept_attr_t attrs;

	ept_qualification.uint64 = vmcs_read(vmcs, VMCS_EXIT_QUAL);
	gpa = vmcs_read(vmcs, VMCS_GUEST_PHY_ADDR);
	gpm_gpa_to_hpa(guest, gpa, &hpa, &attrs);

	if (ept_qualification.ept_violation.w == 1 &&
		(hpa & PAGE_4K_MASK) == g_mmio_dummy_page_hpa) {
		gcpu_skip_instruction(gcpu);
		event_ept_violation->handled = TRUE;
	}
}

/* block_mmio() must be called after guest is created */
void block_mmio(uint16_t guest_id, uint64_t start_addr, uint64_t size)
{
	uint32_t i = 0;
	uint32_t page_num = 0;
	ept_attr_t attr;
	guest_handle_t guest;

	VMM_ASSERT_EX(((start_addr & 0xFFF) == 0), "the start address isn't PAGE_4K_SIZE aligned\n");
	VMM_ASSERT_EX(size >= PAGE_4K_SIZE, "the block mmio size is less than PAGE_4K_SIZE\n");
	VMM_ASSERT_EX(g_mmio_dummy_page_hpa, "g_mmio_dummy_page_hpa is NULL\n");

	guest = guest_handle(guest_id);
	page_num = PAGE_4K_ROUNDUP(size);

	attr.uint32 = 0;
	attr.bits.r = 0x1;
	attr.bits.emt = CACHE_TYPE_WB;

	for (i = 0; i < page_num; i++) {
		gpm_set_mapping_with_cache(guest,
					start_addr + i * PAGE_4K_SIZE,
					g_mmio_dummy_page_hpa,
					PAGE_4K_SIZE,
					attr.uint32);
	}
}

#if (LIB_PCI && MODULE_ACPI && MODULE_IO_MONITOR)
inline static uint64_t get_pcix_mmcfg_addr(void)
{
	acpi_table_mcfg_t *mcfg;

	mcfg = (acpi_table_mcfg_t *)acpi_locate_table(ACPI_SIG_MCFG);
	VMM_ASSERT_EX(mcfg, "ERROR: No MCFG table found\n");

	print_trace("MCFG: base address 0x%llX, pci segment number %d, start pci bus %d, end pci bus %d\n",
		mcfg->mmcfg_base_addr, mcfg->pci_segment_num, mcfg->start_bus_num, mcfg->end_bus_num);

	return mcfg->mmcfg_base_addr;
}

inline static pci_block_guest_t *pci_block_guest_lookup(uint16_t guest_id)
{
	pci_block_guest_t *p_pci_block_guest = g_pci_block_guest;

	while (p_pci_block_guest) {
		if (p_pci_block_guest->guest_id == guest_id)
			break;
		p_pci_block_guest = p_pci_block_guest->next;
	}

	return p_pci_block_guest;
}

inline static pci_block_device_t *pci_block_device_lookup(pci_block_guest_t *pci_block_guest,
							uint16_t pci_dev)
{
	uint32_t i;
	pci_block_device_t *p_pci_block_device;

	for (i = 0; i < pci_block_guest->block_dev_num; i++) {
		p_pci_block_device = &pci_block_guest->block_dev_list[i];
		if (p_pci_block_device->pci_dev == pci_dev) {
			return p_pci_block_device;
		}
	}

	return NULL;
}

inline static boolean_t pci_device_exists(uint16_t pci_dev)
{
	uint16_t vendor_id = 0;
	uint16_t device_id = 0;

	vendor_id = pci_read16(pci_dev, PCI_CFG_VID);
	device_id = pci_read16(pci_dev, PCI_CFG_DID);

	if ((vendor_id == 0xFFFF) || (device_id == 0xFFFF)) {
		return FALSE;
	}

	return TRUE;
}

static uint8_t pci_device_bar_decode(uint16_t pci_dev, uint8_t  bar_offset, base_addr_reg_t *bar)
{
	uint32_t bar_value_low = pci_read32(pci_dev, bar_offset);
	uint32_t bar_value_high = 0;
	uint64_t bar_value = 0;
	uint32_t encoded_size_low = 0;
	uint32_t encoded_size_high = 0;
	uint64_t encoded_size = 0;
	uint64_t mask;
	uint32_t address_type = PCI_BAR_MMIO_32; //0:32 bits, 2:64 bits
#if LOG_LEVEL >= LEVEL_TRACE
	pci_dev_t dev_addr;

	dev_addr.u16 = pci_dev;
	print_trace("%x:%x:%x:%x, bar_value_low=0x%x\r\n",
			dev_addr.bits.bus, dev_addr.bits.dev, dev_addr.bits.fun, bar_offset, bar_value_low);
#endif

	if (bar_value_low <= 1) {
		bar->type = PCI_BAR_UNUSED;
		return 4;
	}

	// issue size determination command
	pci_write32(pci_dev, bar_offset, 0xFFFFFFFF);
	encoded_size_low = pci_read32(pci_dev, bar_offset);

	bar->type = bar_value_low & 0x1;
	mask = (PCI_BAR_IO == bar->type) ? (uint64_t)~(0x3) : (uint64_t)~(0xf);

	if(bar->type == PCI_BAR_MMIO) { //the BAR that map into 64bits mmio space

		// valid only for mmio
		address_type = (uint32_t)(bar_value_low & 0x6);

		if (address_type == PCI_BAR_MMIO_64) {
			bar_value_high = pci_read32(pci_dev, bar_offset + 4);
			pci_write32(pci_dev, bar_offset + 4, 0xFFFFFFFF);

			encoded_size_high = pci_read32(pci_dev, bar_offset + 4);
			bar_value = MAKE64(bar_value_high, bar_value_low);
			bar->addr = bar_value & mask;
			encoded_size = MAKE64(encoded_size_high, encoded_size_low);
			encoded_size &= mask;
			bar->length = (~encoded_size) + 1;
			pci_write32(pci_dev, bar_offset, bar_value_low); // restore original value
			pci_write32(pci_dev, bar_offset + 4, bar_value_high); // restore original valuie
			return 8;
		} else {
			VMM_ASSERT_EX((address_type == PCI_BAR_MMIO_32), "invalid BAR type(bar=0x%x)\n", bar_value_low);
		}
	}

	//the BAR that map into 32bits mmio or io space
	bar->addr = (uint64_t)bar_value_low & mask;
	encoded_size = MAKE64(0xFFFFFFFF, encoded_size_low);
	encoded_size &= mask;
	bar->length = (~encoded_size) + 1;
	pci_write32(pci_dev, bar_offset, bar_value_low); // restore original value

	if (PCI_BAR_IO == bar->type) {
		bar->length &= 0xFFFF; // IO space in Intel arch can't exceed 64K bytes
	}

	return 4;
}

static void pci_bridge_bar_decode(uint16_t pci_dev, base_addr_reg_t *bar_mmio, base_addr_reg_t *bar_io)
{
	uint32_t memory_base = ((uint32_t)pci_read16(pci_dev, PCI_CFG_BRIDGE_MEM_BASE) << 16) & 0xFFF00000;
	uint32_t memory_limit = ((uint32_t)pci_read16(pci_dev, PCI_CFG_BRIDGE_MEM_LIMIT) << 16) | 0x000FFFFF;
	uint8_t io_base_low = pci_read8(pci_dev, PCI_CFG_BRIDGE_IO_BASE_LOW);
	uint8_t io_limit_low = pci_read8(pci_dev, PCI_CFG_BRIDGE_IO_LIMIT_LOW);
	uint16_t io_base_high = 0;
	uint16_t io_limit_high = 0;
	uint64_t io_base;
	uint64_t io_limit;
	pci_dev_t dev_addr;

	dev_addr.u16 = pci_dev;

	// mmio
	if (memory_limit < memory_base) {
		bar_mmio->type = PCI_BAR_UNUSED;
	} else {
		bar_mmio->type = PCI_BAR_MMIO;
		bar_mmio->addr = (uint64_t)memory_base;
		bar_mmio->length = (uint64_t)(memory_limit - memory_base +1);
	}

	// io
	if (io_base_low == 0 || io_limit_low == 0 || io_limit_low < io_base_low) {
		bar_io->type = PCI_BAR_UNUSED;
	} else if ((io_base_low & 0xF) > 1) {
		bar_io->type = PCI_BAR_UNUSED;
		print_warn("Reserved IO address capability in bridge (%x:%x:%x) is detected, io_base_low=0x%x\r\n",
			dev_addr.bits.bus, dev_addr.bits.dev, dev_addr.bits.fun, io_base_low);
	} else {
		if ((io_base_low & 0xF) == 1) { // 32 bits IO address
			// update the high 16 bits
			io_base_high = pci_read16(pci_dev, PCI_CFG_BRIDGE_IO_BASE_HIGH);
			io_limit_high = pci_read16(pci_dev, PCI_CFG_BRIDGE_IO_LIMIT_HIGH);
		}

		io_base = (((uint64_t)io_base_high << 16) & 0x00000000FFFF0000ULL) |
			(((uint64_t)io_base_low << 8) & 0x000000000000F000ULL);
		io_limit = (((uint64_t)io_limit_high << 16) & 0x00000000FFFF0000ULL) |
			(((uint64_t)io_limit_low << 8) & 0x000000000000F000ULL) | 0x0000000000000FFFULL;

		bar_io->type = PCI_BAR_IO;
		bar_io->addr = io_base;
		bar_io->length = io_limit - io_base + 1;
	}
}

static void pci_cfg_bars_decode(pci_block_device_t *pci_dev_info)
{
	uint32_t bar_idx;
	uint8_t bar_offset = PCI_CFG_BAR_FIRST;
	uint16_t base_class = pci_read8(pci_dev_info->pci_dev, PCI_CFG_BASE_CLASS);
	uint16_t sub_class = pci_read8(pci_dev_info->pci_dev, PCI_CFG_SUB_CLASS);

	if (PCI_IS_BRIDGE(base_class, sub_class)) {
		for(bar_idx = 0; bar_idx < PCI_BRIDGE_BAR_NUM; bar_idx++) {
			// Assumption: according to PCI bridge spec 1.2, host_pci_decode_pci_device_bar() will only return 4 (as 32 bit) for bridge
			// 64 bit mapping is not supported in bridge
			bar_offset += pci_device_bar_decode(pci_dev_info->pci_dev, bar_offset, &(pci_dev_info->bars[bar_idx]));
		}
		pci_bridge_bar_decode(pci_dev_info->pci_dev, &(pci_dev_info->bars[bar_idx]), &(pci_dev_info->bars[bar_idx+1])); // set io range and mmio range
		bar_idx += 2;
	} else {
		for(bar_idx = 0; bar_idx < PCI_DEVICE_BAR_NUM; bar_idx++) {
			if (bar_offset > PCI_CFG_BAR_LAST) { // total bar size is 0x10~0x24
				break;
			}
			bar_offset += pci_device_bar_decode(pci_dev_info->pci_dev, bar_offset, &(pci_dev_info->bars[bar_idx]));
		}
	}

	// set rest bars as unused
	for (; bar_idx < PCI_DEVICE_BAR_NUM; bar_idx++) {
		pci_dev_info->bars[bar_idx].type = PCI_BAR_UNUSED;
	}
}

static uint32_t io_blocking_read_handler(UNUSED guest_cpu_handle_t gcpu,
				UNUSED uint16_t port_id,
				UNUSED uint32_t port_size)
{
	return 0xFFFFFFFF;
}

static void io_blocking_write_handler(UNUSED guest_cpu_handle_t gcpu,
				UNUSED uint16_t port_id,
				UNUSED uint32_t port_size,
				UNUSED uint32_t p_value)
{
}

static uint32_t pci_io_read_handler(guest_cpu_handle_t gcpu,
				uint16_t port_id,
				uint32_t port_size)
{
	pci_block_guest_t *p_pci_block_guest;
	pci_block_device_t *p_pci_block_device;
	pci_addr_t pci_config_addr;
	uint32_t value = 0;

	pci_config_addr.u32 = asm_in32(PCI_CFG_ADDR);

	p_pci_block_guest = pci_block_guest_lookup(gcpu->guest->id);
	p_pci_block_device = pci_block_device_lookup(p_pci_block_guest, pci_config_addr.bits.pci_dev);

	if (p_pci_block_device && pci_config_addr.bits.enable) {
		value = io_blocking_read_handler(gcpu, port_id, port_size);
	} else {
		value = io_transparent_read_handler(gcpu, port_id, port_size);
	}

	return value;
}

static void pci_io_write_handler(guest_cpu_handle_t gcpu,
				uint16_t port_id,
				uint32_t port_size,
				uint32_t p_value)
{
	pci_block_guest_t *p_pci_block_guest;
	pci_block_device_t *p_pci_block_device;
	pci_addr_t pci_config_addr;

	pci_config_addr.u32 = asm_in32(PCI_CFG_ADDR);

	p_pci_block_guest = pci_block_guest_lookup(gcpu->guest->id);
	p_pci_block_device = pci_block_device_lookup(p_pci_block_guest, pci_config_addr.bits.pci_dev);

	if (p_pci_block_device && pci_config_addr.bits.enable) {
		io_blocking_write_handler(gcpu, port_id, port_size, p_value);
	} else {
		io_transparent_write_handler(gcpu, port_id, port_size, p_value);
	}
}

static void pci_hide_mmio(uint16_t guest_id, uint64_t start_addr, uint64_t size)
{
	if (size < PAGE_4K_SIZE){
		print_warn("pci block memory size is less than PAGE_4K_SIZE\n");
		return;
	}

	if ((start_addr & 0xFFF) != 0) {
		print_warn("pci block start address isn't PAGE_4K_SIZE aligned\n");
		return;
	}

	block_mmio(guest_id, start_addr, size);
}

static pci_block_device_t *pci_block_device_add(uint16_t guest_id, uint16_t pci_dev)
{
	uint32_t i;
	pci_block_guest_t *p_pci_block_guest;
	pci_block_device_t *p_pci_block_device;
	pci_dev_t dev_addr;

	p_pci_block_guest = pci_block_guest_lookup(guest_id);

	if (p_pci_block_guest == NULL) {
		p_pci_block_guest = (pci_block_guest_t *)mem_alloc(sizeof(pci_block_guest_t));
		p_pci_block_guest->guest_id = guest_id;
		p_pci_block_guest->block_dev_num = 0;
		memset((void *)p_pci_block_guest->block_dev_list, 0 ,
			sizeof(pci_block_device_t) * PCI_BLOCK_DEV_NUM);
		p_pci_block_guest->next = g_pci_block_guest;
		g_pci_block_guest = p_pci_block_guest;

		for (i = PCI_CFG_DATA; i < (PCI_CFG_DATA + 4); i++) {
			io_monitor_register(guest_id,
						i,
						pci_io_read_handler,
						pci_io_write_handler);
		}
	}

	p_pci_block_device = pci_block_device_lookup(p_pci_block_guest, pci_dev);

	if (p_pci_block_device == NULL) {
		p_pci_block_device = &p_pci_block_guest->block_dev_list[p_pci_block_guest->block_dev_num++];
		p_pci_block_device->pci_dev = pci_dev;
		pci_cfg_bars_decode(p_pci_block_device);
	} else {
		dev_addr.u16 = pci_dev;
		print_warn("pci block device %x:%x:%x is re-registered\n", dev_addr.bits.bus, dev_addr.bits.dev, dev_addr.bits.fun);
	}

	return p_pci_block_device;
}

/* block_pci_device() must be called between guest creation and gcpu initialization */
void block_pci_device(uint16_t guest_id, uint16_t pci_dev)
{
	uint32_t i;
	uint32_t bar_idx;
	uint64_t mmcfg_addr;
	pci_block_device_t *p_pci_block_device;
	pci_dev_t dev_addr;

	VMM_ASSERT_EX(pcix_mmcfg_addr, "pcix_mmcfg_addr is NULL\n");
	VMM_ASSERT_EX(g_mmio_dummy_page_hpa, "g_mmio_dummy_page_hpa is NULL\n");

	dev_addr.u16 = pci_dev;
	VMM_ASSERT_EX(((dev_addr.bits.bus < 256) && (dev_addr.bits.dev < 32) && (dev_addr.bits.fun < 8)),
		"the value of %x:%x:%x is invalid\n", dev_addr.bits.bus, dev_addr.bits.dev, dev_addr.bits.fun);

	p_pci_block_device = pci_block_device_add(guest_id, pci_dev);

	mmcfg_addr = pcix_mmcfg_addr + (uint64_t)PCIE_BASE_OFFSET(pci_dev);

	pci_hide_mmio(guest_id, mmcfg_addr, PAGE_4K_SIZE);

	if (pci_device_exists(pci_dev)) {

		for (bar_idx = 0; bar_idx < PCI_DEVICE_BAR_NUM; bar_idx++) {

			switch (p_pci_block_device->bars[bar_idx].type) {
			case PCI_BAR_IO:
				for (i = p_pci_block_device->bars[bar_idx].addr; i < (p_pci_block_device->bars[bar_idx].addr +
					p_pci_block_device->bars[bar_idx].length); i++) {
					io_monitor_register(guest_id,
								i,
								io_blocking_read_handler,
								io_blocking_write_handler);
				}
				break;

			case PCI_BAR_MMIO:
				pci_hide_mmio(guest_id,
						p_pci_block_device->bars[bar_idx].addr,
						p_pci_block_device->bars[bar_idx].length);
				break;
			default:
				break;
			}
		}
	} else {
		print_warn("pci block device %x:%x:%x don't exist\n", dev_addr.bits.bus, dev_addr.bits.dev, dev_addr.bits.fun);
	}
}
#endif

void device_block_init() {
	void *hva;

#if (LIB_PCI && MODULE_ACPI && MODULE_IO_MONITOR)
	pcix_mmcfg_addr = get_pcix_mmcfg_addr();
#endif

	hva = page_alloc(1);
	memset(hva, 0xff, PAGE_4K_SIZE);
	VMM_ASSERT_EX(hmm_hva_to_hpa((uint64_t)hva, &g_mmio_dummy_page_hpa, NULL),
			"hva(0x%llX) to hpa conversion failed in %s\n", hva, __FUNCTION__);
	D(hmm_unmap_hpa(g_mmio_dummy_page_hpa, PAGE_4K_SIZE));

	event_register(EVENT_EPT_VIOLATION, device_block_event);
}
