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

#include "mon_bootstrap_utils.h"
#include "libc.h"
#include "heap.h"
#include "mon_dbg.h"
#include "mon_startup.h"
#include "file_codes.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(COPY_INPUT_STRUCTS_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(COPY_INPUT_STRUCTS_C, \
	__condition)

/**************************************************************************
 *
 * Copy input params into heap before changing host virtual memory mapping
 * Required in order to avoid input parameters disrupting
 *
 ************************************************************************** */
INLINE void mon_copy_gcpu_startup_state(mon_guest_cpu_startup_state_t *state_to,
					const mon_guest_cpu_startup_state_t *
					state_from)
{
	mon_memcpy(state_to, state_from, state_from->size_of_this_struct);
}

INLINE void mon_copy_guest_device(mon_guest_device_t *guest_device_to,
				  const mon_guest_device_t *guest_device_from)
{
	mon_memcpy(guest_device_to, guest_device_from,
		guest_device_from->size_of_this_struct);
}

static
boolean_t mon_copy_guest_startup(mon_guest_startup_t *guest_startup_to,
				 const mon_guest_startup_t *guest_startup_from)
{
	uint32_t size_of_array = 0;
	uint32_t i;
	void *array;
	mon_guest_cpu_startup_state_t *curr_state_to;
	const mon_guest_cpu_startup_state_t *curr_state_from;
	mon_guest_device_t *curr_device_to;
	const mon_guest_device_t *curr_device_from;

	/* Copy the structure (one to one) */
	mon_memcpy(guest_startup_to, guest_startup_from,
		guest_startup_from->size_of_this_struct);

	/* Create copy of mon_guest_cpu_startup_state_t array */
	for (i = 0; i < guest_startup_from->cpu_states_count; i++) {
		uint64_t addr_of_cpu_state =
			guest_startup_from->cpu_states_array + size_of_array;
		const mon_guest_cpu_startup_state_t *state =
			(const mon_guest_cpu_startup_state_t *)addr_of_cpu_state;

		size_of_array += state->size_of_this_struct;
	}

	guest_startup_to->cpu_states_array = 0;

	if (size_of_array > 0) {
		array = mon_memory_alloc(size_of_array);
		if (array == NULL) {
			return FALSE;
		}
		guest_startup_to->cpu_states_array = (uint64_t)array;

		curr_state_from = (const mon_guest_cpu_startup_state_t *)
				  guest_startup_from->cpu_states_array;
		curr_state_to = (mon_guest_cpu_startup_state_t *)array;
		for (i = 0; i < guest_startup_from->cpu_states_count; i++) {
			mon_copy_gcpu_startup_state(curr_state_to,
				curr_state_from);
			curr_state_from =
				(const mon_guest_cpu_startup_state_t *)(
					(uint64_t)curr_state_from +
					curr_state_from->size_of_this_struct);
			curr_state_to =
				(mon_guest_cpu_startup_state_t *)((uint64_t)
								  curr_state_to
								  +
								  curr_state_to
								  ->
								  size_of_this_struct);
		}
	}

	/* Create copy of mon_guest_device_t array */
	size_of_array = 0;
	for (i = 0; i < guest_startup_from->devices_count; i++) {
		uint64_t addr_of_device_struct =
			guest_startup_from->devices_array + size_of_array;
		const mon_guest_device_t *device =
			(const mon_guest_device_t *)addr_of_device_struct;

		size_of_array += device->size_of_this_struct;
	}

	guest_startup_to->devices_array = 0;

	if (size_of_array > 0) {
		array = mon_memory_alloc(size_of_array);
		if (array == NULL) {
			return FALSE;
		}
		guest_startup_to->devices_array = (uint64_t)array;

		curr_device_from =
			(const mon_guest_device_t *)guest_startup_from->
			devices_array;
		curr_device_to = (mon_guest_device_t *)array;
		for (i = 0; i < guest_startup_from->devices_count; i++) {
			mon_copy_guest_device(curr_device_to, curr_device_from);
			curr_device_from =
				(const mon_guest_device_t *)((uint64_t)
							     curr_device_from +
							     curr_device_from->
							     size_of_this_struct);
			curr_device_to =
				(mon_guest_device_t *)((uint64_t)curr_device_to
						       +
						       curr_device_to->
						       size_of_this_struct);
		}
	}

	/* For SOS copy image into heap */
	if (guest_startup_from->image_size != 0) {
		void *image_heap_addr;

		MON_ASSERT(guest_startup_from->image_address != 0);
		image_heap_addr = mon_memory_alloc(
			guest_startup_from->image_size);
		if (image_heap_addr == NULL) {
			return FALSE;
		}

		mon_memcpy(image_heap_addr,
			(void *)(guest_startup_from->image_address),
			guest_startup_from->image_size);
		guest_startup_to->image_address = (uint64_t)image_heap_addr;
	}

	return TRUE;
}

static
const mon_guest_startup_t *mon_create_guest_startup_copy(const mon_guest_startup_t *
							 guest_startup_stack)
{
	mon_guest_startup_t *guest_startup_heap = NULL;

	MON_ASSERT(guest_startup_stack->size_of_this_struct >=
		sizeof(mon_guest_startup_t));
	guest_startup_heap = (mon_guest_startup_t *)
			     mon_memory_alloc(
		guest_startup_stack->size_of_this_struct);
	if (guest_startup_heap == NULL) {
		return NULL;
	}

	if (!mon_copy_guest_startup(guest_startup_heap, guest_startup_stack)) {
		return NULL;
	}

	return (const mon_guest_startup_t *)guest_startup_heap;
}

static
void mon_destroy_guest_startup_struct(const mon_guest_startup_t *guest_startup)
{
	if (guest_startup == NULL) {
		return;
	}

	/* For SOS: if the image is in heap, destroy it */
	if (guest_startup->image_size != 0) {
		MON_ASSERT(guest_startup->image_address != 0);
		mon_memory_free((void *)guest_startup->image_address);
	}

	/* Destory all devices */
	if (guest_startup->devices_array != 0) {
		mon_memory_free((void *)guest_startup->devices_array);
	}

	/* Destory all cpu state structs */
	if (guest_startup->cpu_states_array != 0) {
		mon_memory_free((void *)guest_startup->cpu_states_array);
	}
}

const mon_startup_struct_t *mon_create_startup_struct_copy(const
							   mon_startup_struct_t *
							   startup_struct_stack)
{
	mon_startup_struct_t *startup_struct_heap = NULL;
	const mon_guest_startup_t *guest_startup_heap = NULL;
	void *secondary_guests_array;
	uint32_t size_of_array = 0;
	uint32_t i;

	if (startup_struct_stack == NULL) {
		return NULL;
	}

	MON_ASSERT(startup_struct_stack->size_of_this_struct >=
		sizeof(mon_startup_struct_t));
	MON_ASSERT(ALIGN_BACKWARD
			((uint64_t)startup_struct_stack,
			MON_STARTUP_STRUCT_ALIGNMENT) ==
		(uint64_t)startup_struct_stack);
	startup_struct_heap = (mon_startup_struct_t *)
			      mon_memory_alloc(
		startup_struct_stack->size_of_this_struct);
	if (startup_struct_heap == NULL) {
		return NULL;
	}
	MON_ASSERT(ALIGN_BACKWARD
			((uint64_t)startup_struct_heap,
			MON_STARTUP_STRUCT_ALIGNMENT) ==
		(uint64_t)startup_struct_heap);
	mon_memcpy(startup_struct_heap, startup_struct_stack,
		startup_struct_stack->size_of_this_struct);

	/* Create copy of guest startup struct */
	if (startup_struct_stack->primary_guest_startup_state != 0) {
		MON_ASSERT(ALIGN_BACKWARD
				(startup_struct_stack->
				primary_guest_startup_state,
				MON_GUEST_STARTUP_ALIGNMENT) ==
			startup_struct_stack->primary_guest_startup_state);
		guest_startup_heap =
			mon_create_guest_startup_copy(
				(const mon_guest_startup_t *)
				startup_struct_stack->primary_guest_startup_state);
		if (guest_startup_heap == NULL) {
			return NULL;
		}
		MON_ASSERT(ALIGN_BACKWARD
				((uint64_t)guest_startup_heap,
				MON_GUEST_STARTUP_ALIGNMENT) ==
			(uint64_t)guest_startup_heap);
		startup_struct_heap->primary_guest_startup_state =
			(uint64_t)guest_startup_heap;
	}

	/* Create copies of SOSes start up struct */
	if (startup_struct_stack->number_of_secondary_guests > 0) {
		const mon_guest_startup_t *curr_guest_struct = NULL;
		mon_guest_startup_t *curr_guest_struct_heap = NULL;

		for (i =
			     0;
		     i < startup_struct_stack->number_of_secondary_guests;
		     i++) {
			uint64_t addr_of_guest_struct =
				startup_struct_stack->
				secondary_guests_startup_state_array +
				size_of_array;

			curr_guest_struct =
				(const mon_guest_startup_t *)
				addr_of_guest_struct;

			MON_ASSERT(ALIGN_BACKWARD
					(addr_of_guest_struct,
					MON_GUEST_STARTUP_ALIGNMENT) ==
				addr_of_guest_struct);

			size_of_array += curr_guest_struct->size_of_this_struct;
		}

		secondary_guests_array = mon_memory_alloc(size_of_array);
		if (secondary_guests_array == NULL) {
			return NULL;
		}
		startup_struct_heap->secondary_guests_startup_state_array =
			(uint64_t)secondary_guests_array;

		curr_guest_struct = (const mon_guest_startup_t *)
				    startup_struct_stack->
				    secondary_guests_startup_state_array;
		curr_guest_struct_heap =
			(mon_guest_startup_t *)secondary_guests_array;

		for (i =
			     0;
		     i < startup_struct_stack->number_of_secondary_guests;
		     i++) {
			if (!mon_copy_guest_startup
				    (curr_guest_struct_heap,
				    curr_guest_struct)) {
				return NULL;
			}

			curr_guest_struct =
				(const mon_guest_startup_t *)(
					(uint64_t)curr_guest_struct +
					curr_guest_struct->size_of_this_struct);
			curr_guest_struct_heap =
				(mon_guest_startup_t *)(
					(uint64_t)curr_guest_struct_heap +
					curr_guest_struct_heap->
					size_of_this_struct);
		}
	}

	return (const mon_startup_struct_t *)startup_struct_heap;
}

void mon_destroy_startup_struct(const mon_startup_struct_t *startup_struct)
{
	uint32_t i;

	if (startup_struct == NULL) {
		return;
	}

	/* Destroy SOSes guest structs */
	if (startup_struct->number_of_secondary_guests > 0) {
		const mon_guest_startup_t *curr_guest_struct =
			(const mon_guest_startup_t *)
			startup_struct->
			secondary_guests_startup_state_array;

		for (i = 0; i < startup_struct->number_of_secondary_guests;
		     i++) {
			mon_destroy_guest_startup_struct(curr_guest_struct);
			curr_guest_struct =
				(const mon_guest_startup_t *)(
					(uint64_t)curr_guest_struct +
					curr_guest_struct->size_of_this_struct);
		}
		mon_memory_free((void *)
			startup_struct->secondary_guests_startup_state_array);
	}

	/* Destroy primary guest struct */
	if (startup_struct->primary_guest_startup_state != 0) {
		mon_destroy_guest_startup_struct(
			(const mon_guest_startup_t *)
			startup_struct->primary_guest_startup_state);
		mon_memory_free(
			(void *)startup_struct->primary_guest_startup_state);
	}

	/* Destory struct itself */
	mon_memory_free((void *)startup_struct);
}

const mon_application_params_struct_t *
mon_create_application_params_struct_copy(const mon_application_params_struct_t *
					  application_params_stack)
{
	mon_application_params_struct_t *application_params_heap;

	if (application_params_stack == NULL) {
		return NULL;
	}

	application_params_heap = (mon_application_params_struct_t *)
				  mon_memory_alloc(
		application_params_stack->size_of_this_struct);
	if (application_params_heap == NULL) {
		return NULL;
	}
	mon_memcpy(application_params_heap, application_params_stack,
		application_params_stack->size_of_this_struct);
	return (mon_application_params_struct_t *)application_params_heap;
}

void mon_destroy_application_params_struct(const mon_application_params_struct_t *
					   application_params_struct)
{
	if (application_params_struct == NULL) {
		return;
	}
	mon_memory_free((void *)application_params_struct);
}

/*-------------------------------- debug print ------------------------------ */

#define PRINT_STARTUP_FIELD8(tabs, root, name)          \
	(MON_LOG(mask_anonymous, level_trace, "%s%-42s = 0x%02X\n", \
		tabs, #name, root->name))
#define PRINT_STARTUP_FIELD16(tabs, root, name)         \
	(MON_LOG(mask_anonymous, level_trace, "%s%-42s = 0x%04X\n", \
		tabs, #name, root->name))
#define PRINT_STARTUP_FIELD32(tabs, root, name)         \
	(MON_LOG(mask_anonymous, level_trace, "%s%-42s = 0x%08X\n", \
		tabs, #name, root->name))
#define PRINT_STARTUP_FIELD64(tabs, root, name)         \
	(MON_LOG(mask_anonymous, level_trace, "%s%-42s = 0x%016lX\n", \
		tabs,  #name,  root->name))
#define PRINT_STARTUP_FIELD128(tabs, root, name)         \
	(MON_LOG(mask_anonymous,  level_trace,  "%s%-42s = 0x%016lX%016lX\n", \
		tabs, #name, ((uint64_t *)&(root->name))[1], \
		((uint64_t *)&(root->name))[0]))

#ifdef DEBUG
static void print_guest_device_struct(const mon_guest_device_t *startup_struct,
				      uint32_t dev_idx)
{
	const char *prefix = "    .";

	MON_LOG(mask_anonymous,
		level_trace,
		"\n------------------ mon_guest_device_t ---------------------\n\n");

	MON_LOG(mask_anonymous,
		level_trace,
		"     =========> Guest device #%d\n",
		dev_idx);

	if (startup_struct == NULL) {
		MON_LOG(mask_anonymous,
			level_trace,
			"    mon_guest_device_t is NULL\n");
		goto end;
	}

	PRINT_STARTUP_FIELD16(prefix, startup_struct, size_of_this_struct);
	PRINT_STARTUP_FIELD16(prefix, startup_struct, version_of_this_struct);

	PRINT_STARTUP_FIELD16(prefix, startup_struct, real_vendor_id);
	PRINT_STARTUP_FIELD16(prefix, startup_struct, real_device_id);

	PRINT_STARTUP_FIELD16(prefix, startup_struct, virtual_vendor_id);
	PRINT_STARTUP_FIELD16(prefix, startup_struct, virtual_device_id);

end:
	MON_LOG(mask_anonymous,
		level_trace,
		"\n------------------ END of mon_guest_device_t --------------\n\n");
}


static void print_guest_cpu_startup_struct(const mon_guest_cpu_startup_state_t *
					   startup_struct, uint32_t gcpu_idx)
{
	const char *prefix = "    .";

	MON_LOG(mask_anonymous,
		level_trace,
		"\n-------------- mon_guest_cpu_startup_state_t ---------------\n\n");

	MON_LOG(mask_anonymous,
		level_trace,
		"     =========> Guest CPU #%d %s\n",
		gcpu_idx,
		(gcpu_idx == 0) ? "(BSP)" : "");

	if (startup_struct == NULL) {
		MON_LOG(mask_anonymous, level_trace,
			"    mon_guest_cpu_startup_state_t is NULL\n");
		goto end;
	}

	PRINT_STARTUP_FIELD16(prefix, startup_struct, size_of_this_struct);
	PRINT_STARTUP_FIELD16(prefix, startup_struct, version_of_this_struct);

	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_RAX]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_RBX]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_RCX]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_RDX]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_RDI]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_RSI]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_RBP]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_RSP]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_R8]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_R9]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_R10]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_R11]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_R12]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_R13]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_R14]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_R15]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_RIP]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, gp.reg[IA32_REG_RFLAGS]);

	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM0]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM1]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM2]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM3]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM4]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM5]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM6]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM7]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM8]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM9]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM10]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM11]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM12]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM13]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM14]);
	PRINT_STARTUP_FIELD128(prefix, startup_struct, xmm.reg[IA32_REG_XMM15]);

	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		seg.segment[IA32_SEG_CS].base);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_CS].limit);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_CS].attributes);
	PRINT_STARTUP_FIELD16(prefix, startup_struct,
		seg.segment[IA32_SEG_CS].selector);

	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		seg.segment[IA32_SEG_DS].base);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_DS].limit);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_DS].attributes);
	PRINT_STARTUP_FIELD16(prefix, startup_struct,
		seg.segment[IA32_SEG_DS].selector);

	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		seg.segment[IA32_SEG_SS].base);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_SS].limit);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_SS].attributes);
	PRINT_STARTUP_FIELD16(prefix, startup_struct,
		seg.segment[IA32_SEG_SS].selector);

	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		seg.segment[IA32_SEG_ES].base);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_ES].limit);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_ES].attributes);
	PRINT_STARTUP_FIELD16(prefix, startup_struct,
		seg.segment[IA32_SEG_ES].selector);

	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		seg.segment[IA32_SEG_FS].base);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_FS].limit);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_FS].attributes);
	PRINT_STARTUP_FIELD16(prefix, startup_struct,
		seg.segment[IA32_SEG_FS].selector);

	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		seg.segment[IA32_SEG_GS].base);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_GS].limit);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_GS].attributes);
	PRINT_STARTUP_FIELD16(prefix, startup_struct,
		seg.segment[IA32_SEG_GS].selector);

	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		seg.segment[IA32_SEG_LDTR].base);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_LDTR].limit);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_LDTR].attributes);
	PRINT_STARTUP_FIELD16(prefix, startup_struct,
		seg.segment[IA32_SEG_LDTR].selector);

	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		seg.segment[IA32_SEG_TR].base);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_TR].limit);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		seg.segment[IA32_SEG_TR].attributes);
	PRINT_STARTUP_FIELD16(prefix, startup_struct,
		seg.segment[IA32_SEG_TR].selector);

	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		control.cr[IA32_CTRL_CR0]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		control.cr[IA32_CTRL_CR2]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		control.cr[IA32_CTRL_CR3]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		control.cr[IA32_CTRL_CR4]);
	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		control.cr[IA32_CTRL_CR8]);

	PRINT_STARTUP_FIELD64(prefix, startup_struct, control.gdtr.base);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, control.gdtr.limit);

	PRINT_STARTUP_FIELD64(prefix, startup_struct, control.idtr.base);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, control.idtr.limit);

	PRINT_STARTUP_FIELD64(prefix, startup_struct, msr.msr_debugctl);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, msr.msr_efer);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, msr.msr_pat);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, msr.msr_sysenter_esp);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, msr.msr_sysenter_eip);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, msr.pending_exceptions);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, msr.msr_sysenter_cs);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		msr.interruptibility_state);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, msr.activity_state);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, msr.smbase);

end:
	MON_LOG(mask_anonymous,
		level_trace,
		"\n------------ END of mon_guest_cpu_startup_state_t ----------\n\n");
}


/* if guest_idx = -1 - primary */
static void print_guest_startup_struct(
	const mon_guest_startup_t *startup_struct,
	uint32_t guest_idx)
{
	const char *prefix = "  .";
	const mon_guest_cpu_startup_state_t *gcpu;
	const mon_guest_device_t *dev;
	uint32_t i;

	MON_LOG(mask_anonymous,
		level_trace,
		"\n------------------ mon_guest_startup_t --------------------\n\n");

	if (guest_idx == (uint32_t)-1) {
		MON_LOG(mask_anonymous, level_trace,
			"   =========> The PRIMARY guest\n");
	} else {
		MON_LOG(mask_anonymous, level_trace,
			"   =========> Secondary guest #%d\n", guest_idx);
	}

	if (startup_struct == NULL) {
		MON_LOG(mask_anonymous,
			level_trace,
			"  mon_guest_startup_t is NULL\n");
		goto end;
	}

	PRINT_STARTUP_FIELD16(prefix, startup_struct, size_of_this_struct);
	PRINT_STARTUP_FIELD16(prefix, startup_struct, version_of_this_struct);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, flags);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, guest_magic_number);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, cpu_affinity);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, cpu_states_count);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, devices_count);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, image_size);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, image_address);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, physical_memory_size);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		image_offset_in_guest_physical_memory);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, cpu_states_array);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, devices_array);

	gcpu =
		(const mon_guest_cpu_startup_state_t *)(startup_struct->
							cpu_states_array);

	for (i = 0; i < startup_struct->cpu_states_count; ++i)
		print_guest_cpu_startup_struct(gcpu + i, i);

	dev = (const mon_guest_device_t *)(startup_struct->devices_array);

	for (i = 0; i < startup_struct->devices_count; ++i)
		print_guest_device_struct(dev + i, i);

end:
	MON_LOG(mask_anonymous,
		level_trace,
		"\n----------------- END of mon_guest_startup_t --------------\n\n");
}


void print_startup_struct(const mon_startup_struct_t *startup_struct)
{
	const char *prefix = ".";
	uint16_t idx;

	MON_LOG(mask_anonymous,
		level_trace,
		"\n----------------- mon_startup_struct_t --------------------\n\n");
	if (startup_struct == NULL) {
		MON_LOG(mask_anonymous,
			level_trace,
			"mon_startup_struct_t is NULL\n");
		goto end;
	}

	PRINT_STARTUP_FIELD16(prefix, startup_struct, size_of_this_struct);
	PRINT_STARTUP_FIELD16(prefix, startup_struct, version_of_this_struct);
	PRINT_STARTUP_FIELD16(prefix, startup_struct,
		number_of_processors_at_install_time);
	PRINT_STARTUP_FIELD16(prefix, startup_struct,
		number_of_processors_at_boot_time);
	PRINT_STARTUP_FIELD16(prefix, startup_struct,
		number_of_secondary_guests);
	PRINT_STARTUP_FIELD16(prefix, startup_struct, size_of_mon_stack);
	PRINT_STARTUP_FIELD16(prefix, startup_struct, unsupported_vendor_id);
	PRINT_STARTUP_FIELD16(prefix, startup_struct, unsupported_device_id);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, flags);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, default_device_owner);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, acpi_owner);
	PRINT_STARTUP_FIELD32(prefix, startup_struct, nmi_owner);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		mon_memory_layout[mon_image].total_size);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		mon_memory_layout[mon_image].image_size);
	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		mon_memory_layout[mon_image].base_address);
	PRINT_STARTUP_FIELD64(prefix,
		startup_struct,
		physical_memory_layout_E820);
	PRINT_STARTUP_FIELD64(prefix,
		startup_struct,
		primary_guest_startup_state);
	PRINT_STARTUP_FIELD64(prefix, startup_struct,
		secondary_guests_startup_state_array);
	PRINT_STARTUP_FIELD8(prefix, startup_struct, debug_params.verbosity);
	PRINT_STARTUP_FIELD64(prefix, startup_struct, debug_params.mask);
	PRINT_STARTUP_FIELD8(prefix, startup_struct, debug_params.port.type);
	PRINT_STARTUP_FIELD8(prefix, startup_struct,
		debug_params.port.virt_mode);
	PRINT_STARTUP_FIELD8(prefix,
		startup_struct,
		debug_params.port.ident_type);
	PRINT_STARTUP_FIELD32(prefix, startup_struct,
		debug_params.port.ident.ident32);

	print_guest_startup_struct((const mon_guest_startup_t
				    *)(startup_struct->
				       primary_guest_startup_state),
		(uint32_t)-1);

	for (idx = 0; idx < startup_struct->number_of_secondary_guests; ++idx) {
		const mon_guest_startup_t *sec =
			(const mon_guest_startup_t
			 *)(startup_struct->secondary_guests_startup_state_array);
		print_guest_startup_struct(sec + idx, idx);
	}

end:
	MON_LOG(mask_anonymous,
		level_trace,
		"\n----------------- END of mon_startup_struct_t ---------------\n\n");
}

#endif   /* DEBUG */
