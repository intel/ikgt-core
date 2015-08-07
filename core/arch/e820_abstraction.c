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
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(E820_ABSTRACTION_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(E820_ABSTRACTION_C, __condition)
#include <mon_defs.h>
#include <mon_arch_defs.h>
#include <e820_abstraction.h>
#include <heap.h>
#include <common_libc.h>
#include "mon_dbg.h"

#include "mon_objects.h"
#include "gpm_api.h"
#include "guest_cpu.h"
#include "guest.h"
#include "host_memory_manager_api.h"
#include "../guest/guest_cpu/unrestricted_guest.h"

uint32_t g_int15_trapped_page = 0;
uint32_t g_int15_orignal_vector = 0;
e820_map_state_t *g_emap;

/*------------------------------------------------------------*/
#define SMAP    0x534D4150      /* BIOS signature for int 15 E820 call */

static int15_e820_memory_map_t *g_e820_map;

static const char *g_int15_e820_type_name[] = {
	"UNKNOWN",              /* 0 */
	"MEMORY",               /* 1 */
	"RESERVED",             /* 2 */
	"ACPI",                 /* 3 */
	"NVS",                  /* 4 */
	"UNUSABLE"              /* 5 */
};

static uint8_t int15_handler_code[] = {
	0x3d, 0x20, 0xe8,               /* cmp ax, 0xe820 */
	0x74, 0x05,                     /* jz Handler */
	0xea, 0x00, 0x00, 0x00, 0x00,   /* jmp orig_handler */
	0x0f, 0x01, 0xc1,               /* Handler: vmcall */
	0xcf                            /* iret */
};

/* conditional jump can be only */
/* near jump, hence too jumps in */
/* this assembly code */
#define E820_NAMES_COUNT (sizeof(g_int15_e820_type_name) / sizeof(const char *))

/*------------------------------------------------------------*/

/*
 * This function is supposed to setup int15 handling
 * it will write its own handling code to vector area which is
 * not being used or known to not being used. So we only
 * handle E820 type INT15 interrupt, any other type of
 * INT15 will be handled by the original vector.
 */
void update_int15_handling(uint32_t int15_handler_address)
{
	uint32_t i;
	uint32_t *int15_vector_offset =
		(uint32_t *)((uint64_t)INT15_VECTOR_LOCATION);

	/* save original vector location to use in vmexit */
	g_int15_orignal_vector = *(int15_vector_offset);

	/* use some location in INT vector table which is not being used
	 * check the location before use it */
	if (int15_handler_address == (uint32_t)-1) {
		/* a wrong value is passed
		 * can't continue without a valid address */
		MON_DEADLOOP();
	} else if (int15_handler_address == 0) {
		/* no need to hookup INT15 */
		return;
	}

	/* CS:IP format vector */
	g_int15_trapped_page = int15_handler_address;

	/* hookup our INT15 vector(seg:offset) */
	*(int15_vector_offset) = g_int15_trapped_page;

	/* patch the original vector */
	*(uint32_t *)&int15_handler_code[ORIG_HANDLER_OFFSET] =
		g_int15_orignal_vector;

	/* write patched code to the interrupt 15 handling location */
	for (i = 0; i < sizeof(int15_handler_code); i++)
		*(uint8_t *)(uint64_t)(int15_handler_address +
				       i) = int15_handler_code[i];

	MON_LOG(mask_anonymous, level_trace, "E820 Original vector:0x%x\n",
		g_int15_orignal_vector);
	MON_LOG(mask_anonymous, level_trace, "E820 int15 handler vector:0x%x\n",
		g_int15_trapped_page);
}

#ifdef DEBUG
INLINE const char *e820_get_type_name(int15_e820_range_type_t type)
{
	return (type <
		E820_NAMES_COUNT) ? g_int15_e820_type_name[type] : "UNKNOWN";
}
#endif

extern uint32_t g_is_post_launch;
boolean_t e820_abstraction_initialize(const int15_e820_memory_map_t *
				      e820_memory_map,
				      uint32_t int15_handler_address)
{
	/* INT15 handling is only required for pre-OS launch and is not
	 * required for post-OS launch */
	if (!g_is_post_launch && mon_is_unrestricted_guest_supported()) {
		/* initialize int15 handling vectors */
		update_int15_handling(int15_handler_address);
	}

	if (e820_memory_map != NULL) {
		uint32_t size =
			e820_memory_map->memory_map_size +
			sizeof(e820_memory_map->memory_map_size);
		g_e820_map = (int15_e820_memory_map_t *)mon_memory_alloc(size);
		if (g_e820_map == NULL) {
			return FALSE;
		}
		mon_memcpy(g_e820_map, e820_memory_map, size);
		MON_DEBUG_CODE(e820_abstraction_print_memory_map(
				E820_ORIGINAL_MAP));
		return TRUE;
	}
	return FALSE;
}

boolean_t e820_abstraction_is_initialized(void)
{
	return g_e820_map != NULL;
}

const int15_e820_memory_map_t *e820_abstraction_get_map(
	e820_handle_t e820_handle)
{
	if (e820_handle == E820_ORIGINAL_MAP) {
		return g_e820_map;
	}

	return (const int15_e820_memory_map_t *)e820_handle;
}

e820_abstraction_range_iterator_t e820_abstraction_iterator_get_first(e820_handle_t
								      e820_handle)
{
	int15_e820_memory_map_t *e820_map;

	if (e820_handle == E820_ORIGINAL_MAP) {
		e820_map = g_e820_map;
	} else {
		e820_map = (int15_e820_memory_map_t *)e820_handle;
	}

	if (e820_map == NULL) {
		return E820_ABSTRACTION_NULL_ITERATOR;
	}

	if (e820_map->memory_map_size == 0) {
		return E820_ABSTRACTION_NULL_ITERATOR;
	}

	return (e820_abstraction_range_iterator_t)(&(e820_map->memory_map_entry[
							     0]));
}

e820_abstraction_range_iterator_t
e820_abstraction_iterator_get_next(e820_handle_t e820_handle,
				   e820_abstraction_range_iterator_t iter)
{
	uint64_t iter_hva = (uint64_t)iter;
	int15_e820_memory_map_t *e820_map;
	uint64_t e820_entries_hva;

	if (iter == (e820_abstraction_range_iterator_t)NULL) {
		return E820_ABSTRACTION_NULL_ITERATOR;
	}

	if (e820_handle == E820_ORIGINAL_MAP) {
		e820_map = g_e820_map;
	} else {
		e820_map = (int15_e820_memory_map_t *)e820_handle;
	}

	if (e820_map == NULL) {
		return E820_ABSTRACTION_NULL_ITERATOR;
	}

	e820_entries_hva = (uint64_t)(&(e820_map->memory_map_entry[0]));

	iter_hva += sizeof(int15_e820_memory_map_entry_ext_t);
	if (iter_hva >= (e820_entries_hva + e820_map->memory_map_size)) {
		return E820_ABSTRACTION_NULL_ITERATOR;
	}

	return (e820_abstraction_range_iterator_t *)iter_hva;
}

const int15_e820_memory_map_entry_ext_t
*e820_abstraction_iterator_get_range_details(IN
					     e820_abstraction_range_iterator_t
					     iter)
{
	if (iter == (e820_abstraction_range_iterator_t)NULL) {
		return NULL;
	}

	return (int15_e820_memory_map_entry_ext_t *)iter;
}

boolean_t e820_abstraction_create_new_map(OUT e820_handle_t *handle)
{
	int15_e820_memory_map_t *e820_map =
		(int15_e820_memory_map_t *)mon_page_alloc(1);

	if (e820_map == NULL) {
		return FALSE;
	}

	e820_map->memory_map_size = 0;

	*handle = (e820_handle_t)e820_map;
	return TRUE;
}

void e820_abstraction_destroy_map(IN e820_handle_t handle)
{
	if (handle == E820_ORIGINAL_MAP) {
		/* Destroying of original map is forbidden */
		MON_ASSERT(0);
		return;
	}
	mon_page_free((void *)handle);
}

boolean_t e820_abstraction_add_new_range(IN e820_handle_t handle,
					 IN uint64_t base_address,
					 IN uint64_t length,
					 IN int15_e820_range_type_t
					 address_range_type,
					 IN int15_e820_memory_map_ext_attributes_t
					 extended_attributes)
{
	int15_e820_memory_map_t *e820_map = (int15_e820_memory_map_t *)handle;
	int15_e820_memory_map_entry_ext_t *new_entry;
	uint32_t new_entry_index;

	if (handle == E820_ORIGINAL_MAP) {
		MON_ASSERT(0);
		return FALSE;
	}

	if ((e820_map->memory_map_size +
	     sizeof(int15_e820_memory_map_entry_ext_t)) >=
	    PAGE_4KB_SIZE) {
		return FALSE;
	}

	if (length == 0) {
		return FALSE;
	}

	new_entry_index =
		e820_map->memory_map_size /
		sizeof(int15_e820_memory_map_entry_ext_t);

	if (new_entry_index > 0) {
		int15_e820_memory_map_entry_ext_t *last_entry =
			&(e820_map->memory_map_entry[new_entry_index - 1]);
		if ((last_entry->basic_entry.base_address >= base_address)
		    || (last_entry->basic_entry.base_address +
			last_entry->basic_entry.length > base_address)) {
			return FALSE;
		}
	}

	new_entry = &(e820_map->memory_map_entry[new_entry_index]);
	new_entry->basic_entry.base_address = base_address;
	new_entry->basic_entry.length = length;
	new_entry->basic_entry.address_range_type = address_range_type;
	new_entry->extended_attributes.uint32 = extended_attributes.uint32;
	e820_map->memory_map_size += sizeof(int15_e820_memory_map_entry_ext_t);
	return TRUE;
}

/* handle int15 from real mode code
 * we use CS:IP for vmcall instruction to get indication that there is int15
 * check for E820 function, if true, then handle it
 * no other int15 function should come here */
boolean_t handle_int15_vmcall(guest_cpu_handle_t gcpu)
{
	uint16_t selector = 0;
	uint64_t base = 0;
	uint32_t limit = 0;
	uint32_t attr = 0;
	uint32_t expected_lnr_addr;
	uint32_t vmcall_lnr_addr;
	volatile uint64_t r_rax = 0, r_rdx = 0, r_rip = 0;

	if (!(0x1 & gcpu_get_guest_visible_control_reg(gcpu, IA32_CTRL_CR0))) {
		/* PE = 0?  then real mode
		 * need to get CS:IP to make sure that this VMCALL from INT15 handler */
		gcpu_get_segment_reg(gcpu,
			IA32_SEG_CS,
			&selector,
			&base,
			&limit,
			&attr);
		r_rip = gcpu_get_gp_reg(gcpu, IA32_REG_RIP);

		expected_lnr_addr = SEGMENT_OFFSET_TO_LINEAR(
			g_int15_trapped_page >> 16,
			g_int15_trapped_page +
			VMCALL_OFFSET);
		vmcall_lnr_addr =
			SEGMENT_OFFSET_TO_LINEAR((uint32_t)selector,
				(uint32_t)r_rip);

		/* check to see if the CS:IP is same as expected for VMCALL in INT15
		 * handler */
		if (expected_lnr_addr == vmcall_lnr_addr) {
			r_rax = gcpu_get_gp_reg(gcpu, IA32_REG_RAX);
			r_rdx = gcpu_get_gp_reg(gcpu, IA32_REG_RDX);
			if ((0xE820 == r_rax) && (SMAP == r_rdx)) {
				if (g_emap == NULL) {
					g_emap =
						mon_malloc(sizeof(
								e820_map_state_t));
					MON_ASSERT(g_emap != NULL);
					mon_memset(g_emap, 0,
						sizeof(e820_map_state_t));
				}
				e820_save_guest_state(gcpu, g_emap);
				g_emap->guest_handle = mon_gcpu_guest_handle(
					gcpu);
				e820_int15_handler(g_emap);
				e820_restore_guest_state(gcpu, g_emap);
				gcpu_skip_guest_instruction(gcpu);
				return TRUE;
			} else {
				MON_LOG(mask_anonymous,
					level_error,
					"INT15 wasn't handled for function 0x%x\n",
					r_rax);
				MON_DEADLOOP(); /* Should not get here */
				return FALSE;
			}
		}
	}
	return FALSE;
}

/* save Guest state for registers we might be using
 * when handling INT15 E820 */
void e820_save_guest_state(guest_cpu_handle_t gcpu, e820_map_state_t *emap)
{
	uint16_t selector;
	uint64_t base;
	uint32_t limit;
	uint32_t attr;
	e820_guest_state_t *p_arch = &emap->e820_guest_state;

	/* only registers needed for handling int15 are saved */
	p_arch->em_rax = gcpu_get_gp_reg(gcpu, IA32_REG_RAX);
	p_arch->em_rbx = gcpu_get_gp_reg(gcpu, IA32_REG_RBX);
	p_arch->em_rcx = gcpu_get_gp_reg(gcpu, IA32_REG_RCX);
	p_arch->em_rdx = gcpu_get_gp_reg(gcpu, IA32_REG_RDX);
	p_arch->em_rdi = gcpu_get_gp_reg(gcpu, IA32_REG_RDI);
	p_arch->em_rsp = gcpu_get_gp_reg(gcpu, IA32_REG_RSP);
	p_arch->em_rflags = gcpu_get_gp_reg(gcpu, IA32_REG_RFLAGS);

	gcpu_get_segment_reg(gcpu, IA32_SEG_ES, &selector, &base, &limit,
		&attr);
	p_arch->em_es = selector;
	p_arch->es_base = base;
	p_arch->es_lim = limit;
	p_arch->es_attr = attr;
	gcpu_get_segment_reg(gcpu, IA32_SEG_SS, &selector, &base, &limit,
		&attr);
	p_arch->em_ss = selector;
	p_arch->ss_base = base;
	p_arch->ss_lim = limit;
	p_arch->ss_attr = attr;
}

/* update VMCS state after handling INT15 E820 */
void e820_restore_guest_state(guest_cpu_handle_t gcpu, e820_map_state_t *emap)
{
	e820_guest_state_t *p_arch = &emap->e820_guest_state;
	gpa_t sp_gpa_addr;
	hva_t sp_hva_addr;
	uint16_t sp_gpa_val;

	/* only registers which could be modified by the handler
	 * will be restored. */
	gcpu_set_gp_reg(gcpu, IA32_REG_RAX, p_arch->em_rax);
	gcpu_set_gp_reg(gcpu, IA32_REG_RBX, p_arch->em_rbx);
	gcpu_set_gp_reg(gcpu, IA32_REG_RCX, p_arch->em_rcx);
	gcpu_set_gp_reg(gcpu, IA32_REG_RDX, p_arch->em_rdx);
	gcpu_set_gp_reg(gcpu, IA32_REG_RDI, p_arch->em_rdi);

	/* we need to change the modify EFLAGS saved in stack
	 * as when we do IRET, EFLAGS are restored from the stack
	 * only IRET, CPU does pop IP, pop CS_Segment, Pop EFLAGS
	 * in real mode these pos are only 2bytes */
	sp_gpa_addr =
		SEGMENT_OFFSET_TO_LINEAR((uint32_t)p_arch->em_ss,
			p_arch->em_rsp);
	/* RSP points to RIP:SEGMENT:EFLAGS so we increment 4 to get to
	 * EFLAGS register */
	sp_gpa_addr += 4;

	if (FALSE ==
	    gpm_gpa_to_hva(emap->guest_phy_memory, sp_gpa_addr, &sp_hva_addr)) {
		MON_LOG(mask_anonymous,
			level_trace,
			"Translation failed for physical address %P \n",
			sp_gpa_addr);
		MON_DEADLOOP();
		return;
	}

	sp_gpa_val = *(uint16_t *)(uint64_t)sp_hva_addr;

	if (p_arch->em_rflags & RFLAGS_CARRY) {
		BITMAP_SET(sp_gpa_val, RFLAGS_CARRY);
	} else {
		BITMAP_CLR(sp_gpa_val, RFLAGS_CARRY);
	}

	*(uint16_t *)(uint64_t)sp_hva_addr = sp_gpa_val;
}

/* handle INT15 E820 map */
boolean_t e820_int15_handler(e820_map_state_t *emap)
{
	uint64_t dest_gpa;

	e820_guest_state_t *p_arch = &emap->e820_guest_state;
	const int15_e820_memory_map_t *p_mmap = NULL;

	if (emap->guest_phy_memory == NULL) {
		emap->guest_phy_memory =
			gcpu_get_current_gpm(emap->guest_handle);
	}

	MON_ASSERT(emap->guest_phy_memory != NULL);

	if (NULL == emap->emu_e820_handle) {
		if (FALSE ==
		    (gpm_create_e820_map
			     (emap->guest_phy_memory,
			     (e820_handle_t)&emap->emu_e820_handle))) {
			MON_LOG(mask_anonymous, level_error,
				"FATAL ERROR: No E820 Memory map was found\n");
			MON_DEADLOOP();
			return FALSE;
		}
		p_mmap = e820_abstraction_get_map(emap->emu_e820_handle);
		emap->emu_e820_memory_map = p_mmap->memory_map_entry;
		emap->emu_e820_memory_map_size =
			(uint16_t)(p_mmap->memory_map_size /
				   sizeof(int15_e820_memory_map_entry_ext_t));
		MON_ASSERT(NULL != emap->emu_e820_memory_map
			&& 0 != emap->emu_e820_memory_map_size);
		emap->emu_e820_continuation_value = 0;
		MON_LOG(mask_anonymous, level_trace,
			"INT15 vmcall for E820 map initialized!\n");
	}
	/*
	 * Make sure all the arguments are valid
	 */
	if ((p_arch->em_rcx < sizeof(int15_e820_memory_map_entry_t)) ||
	    (p_arch->em_rbx >= emap->emu_e820_memory_map_size) ||
	    (p_arch->em_rbx != 0
	     && p_arch->em_rbx != emap->emu_e820_continuation_value)) {
		/*
		 * Set the carry flag
		 */
		BITMAP_SET(p_arch->em_rflags, RFLAGS_CARRY);
		MON_LOG(mask_anonymous,
			level_error,
			"ERROR>>>>> E820 INT15 rbx=0x%x rcx:0x%x\n",
			p_arch->em_rbx,
			p_arch->em_rcx);
	} else {
		hva_t dest_hva = 0;

		/* CX contains number of bytes to write.
		 * here we select between basic entry and extended */
		p_arch->em_rcx =
			(p_arch->em_rcx >=
			 sizeof(int15_e820_memory_map_entry_ext_t)) ?
			sizeof(int15_e820_memory_map_entry_ext_t) :
			sizeof(int15_e820_memory_map_entry_t);

		/* where to put the result */
		dest_gpa =
			SEGMENT_OFFSET_TO_LINEAR((uint32_t)p_arch->em_es,
				p_arch->em_rdi);

		if (FALSE ==
		    gpm_gpa_to_hva(emap->guest_phy_memory, dest_gpa,
			    &dest_hva)) {
			MON_LOG(mask_anonymous,
				level_trace,
				"Translation failed for physical address %P \n",
				dest_gpa);
			BITMAP_SET(p_arch->em_rflags, RFLAGS_CARRY);
			return FALSE;
		}
		mon_memcpy((void *)dest_hva,
			(unsigned char *)&emap->emu_e820_memory_map[p_arch->
								    em_rbx],
			(unsigned int)p_arch->em_rcx);

		/* keep, to validate next instruction */
		emap->emu_e820_continuation_value = (uint16_t)p_arch->em_rbx +
						    1;

		/* prepare output parameters */
		p_arch->em_rax = SMAP;

		/* Clear the carry flag which means error absence */
		BITMAP_CLR(p_arch->em_rflags, RFLAGS_CARRY);

		if (emap->emu_e820_continuation_value >=
		    emap->emu_e820_memory_map_size) {
			/* Clear EBX to indicate that this is the last entry in the memory
			 * map */
			p_arch->em_rbx = 0;
			emap->emu_e820_continuation_value = 0;
		} else {
			/* Update the EBX continuation value to indicate there are more
			 * entries */
			p_arch->em_rbx = emap->emu_e820_continuation_value;
		}
	}

	return TRUE;
}

#ifdef DEBUG
void e820_abstraction_print_memory_map(IN e820_handle_t handle)
{
	int15_e820_memory_map_t *e820_map = (int15_e820_memory_map_t *)handle;
	uint32_t num_of_entries;
	uint32_t i;

	if (e820_map == E820_ORIGINAL_MAP) {
		e820_map = g_e820_map;
	}

	MON_LOG(mask_anonymous, level_trace, "\nE820 Memory map\n");
	MON_LOG(mask_anonymous, level_trace, "-------------------\n");

	if (e820_map == NULL) {
		MON_LOG(mask_anonymous, level_trace, "DOESN'T EXIST!!!\n");
	}

	num_of_entries =
		e820_map->memory_map_size /
		sizeof(e820_map->memory_map_entry[0]);

	for (i = 0; i < num_of_entries; i++) {
		int15_e820_memory_map_entry_ext_t *entry =
			&(e820_map->memory_map_entry[i]);
		MON_LOG(mask_anonymous,
			level_trace,
			"%2d: [%P : %P] ; type = 0x%x(%8s) ; ext_attr = 0x%x\n",
			i,
			entry->basic_entry.base_address,
			entry->basic_entry.base_address + entry->basic_entry.length,
			entry->basic_entry.address_range_type,
			e820_get_type_name(
				entry->basic_entry.address_range_type),
			entry->extended_attributes.uint32);
	}
	MON_LOG(mask_anonymous, level_trace, "-------------------\n");
}
#endif
