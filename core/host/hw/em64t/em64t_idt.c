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

/*
 * This code is assumed to be running in 32-bit mode,
 * but configure IDT for 64-bit mode.
 */

#include "mon_defs.h"
#include "common_libc.h"
#include "heap.h"
#include "em64t_defs.h"
#include "hw_utils.h"
#include "isr_generated.h"
#include "idt.h"

extern void dump_memory(const void *mem_location, uint32_t count,
			uint32_t size);

#define IDT_VECTOR_COUNT 256

#define IA32E_IDT_GATE_TYPE_INTERRUPT_GATE  0xE
#define IA32E_IDT_GATE_TYPE_TRAP_GATE       0xF

/* pointer to IDTs for all CPUs */
static em64t_idt_table_t idt;
static uint8_t ist_used[32] = {
	0,
	0,
	1, /* NMI */
	0,
	0,
	0,
	2,      /* UNDEFINED_OPCODE */
	0,
	3,      /* DOUBLE_FAULT */
	0,
	0,
	0,
	4,      /* STACK_SEGMENT_FAULT */
	5,      /* GENERAL_PROTECTION_FAULT */
	6,      /* PAGE_FAULT */
};

static address_t isr_handler_table[256] = {
	(address_t)isr_entry_00,
	(address_t)isr_entry_01,
	(address_t)isr_entry_02,
	(address_t)isr_entry_03,
	(address_t)isr_entry_04,
	(address_t)isr_entry_05,
	(address_t)isr_entry_06,
	(address_t)isr_entry_07,
	(address_t)isr_entry_08,
	(address_t)isr_entry_09,
	(address_t)isr_entry_0a,
	(address_t)isr_entry_0b,
	(address_t)isr_entry_0c,
	(address_t)isr_entry_0d,
	(address_t)isr_entry_0e,
	(address_t)isr_entry_0f,
	(address_t)isr_entry_10,
	(address_t)isr_entry_11,
	(address_t)isr_entry_12,
	(address_t)isr_entry_13,
	(address_t)isr_entry_14,
	(address_t)isr_entry_15,
	(address_t)isr_entry_16,
	(address_t)isr_entry_17,
	(address_t)isr_entry_18,
	(address_t)isr_entry_19,
	(address_t)isr_entry_1a,
	(address_t)isr_entry_1b,
	(address_t)isr_entry_1c,
	(address_t)isr_entry_1d,
	(address_t)isr_entry_1e,
	(address_t)isr_entry_1f,
	(address_t)isr_entry_20,
	(address_t)isr_entry_21,
	(address_t)isr_entry_22,
	(address_t)isr_entry_23,
	(address_t)isr_entry_24,
	(address_t)isr_entry_25,
	(address_t)isr_entry_26,
	(address_t)isr_entry_27,
	(address_t)isr_entry_28,
	(address_t)isr_entry_29,
	(address_t)isr_entry_2a,
	(address_t)isr_entry_2b,
	(address_t)isr_entry_2c,
	(address_t)isr_entry_2d,
	(address_t)isr_entry_2e,
	(address_t)isr_entry_2f,
	(address_t)isr_entry_30,
	(address_t)isr_entry_31,
	(address_t)isr_entry_32,
	(address_t)isr_entry_33,
	(address_t)isr_entry_34,
	(address_t)isr_entry_35,
	(address_t)isr_entry_36,
	(address_t)isr_entry_37,
	(address_t)isr_entry_38,
	(address_t)isr_entry_39,
	(address_t)isr_entry_3a,
	(address_t)isr_entry_3b,
	(address_t)isr_entry_3c,
	(address_t)isr_entry_3d,
	(address_t)isr_entry_3e,
	(address_t)isr_entry_3f,
	(address_t)isr_entry_40,
	(address_t)isr_entry_41,
	(address_t)isr_entry_42,
	(address_t)isr_entry_43,
	(address_t)isr_entry_44,
	(address_t)isr_entry_45,
	(address_t)isr_entry_46,
	(address_t)isr_entry_47,
	(address_t)isr_entry_48,
	(address_t)isr_entry_49,
	(address_t)isr_entry_4a,
	(address_t)isr_entry_4b,
	(address_t)isr_entry_4c,
	(address_t)isr_entry_4d,
	(address_t)isr_entry_4e,
	(address_t)isr_entry_4f,
	(address_t)isr_entry_50,
	(address_t)isr_entry_51,
	(address_t)isr_entry_52,
	(address_t)isr_entry_53,
	(address_t)isr_entry_54,
	(address_t)isr_entry_55,
	(address_t)isr_entry_56,
	(address_t)isr_entry_57,
	(address_t)isr_entry_58,
	(address_t)isr_entry_59,
	(address_t)isr_entry_5a,
	(address_t)isr_entry_5b,
	(address_t)isr_entry_5c,
	(address_t)isr_entry_5d,
	(address_t)isr_entry_5e,
	(address_t)isr_entry_5f,
	(address_t)isr_entry_60,
	(address_t)isr_entry_61,
	(address_t)isr_entry_62,
	(address_t)isr_entry_63,
	(address_t)isr_entry_64,
	(address_t)isr_entry_65,
	(address_t)isr_entry_66,
	(address_t)isr_entry_67,
	(address_t)isr_entry_68,
	(address_t)isr_entry_69,
	(address_t)isr_entry_6a,
	(address_t)isr_entry_6b,
	(address_t)isr_entry_6c,
	(address_t)isr_entry_6d,
	(address_t)isr_entry_6e,
	(address_t)isr_entry_6f,
	(address_t)isr_entry_70,
	(address_t)isr_entry_71,
	(address_t)isr_entry_72,
	(address_t)isr_entry_73,
	(address_t)isr_entry_74,
	(address_t)isr_entry_75,
	(address_t)isr_entry_76,
	(address_t)isr_entry_77,
	(address_t)isr_entry_78,
	(address_t)isr_entry_79,
	(address_t)isr_entry_7a,
	(address_t)isr_entry_7b,
	(address_t)isr_entry_7c,
	(address_t)isr_entry_7d,
	(address_t)isr_entry_7e,
	(address_t)isr_entry_7f,
	(address_t)isr_entry_80,
	(address_t)isr_entry_81,
	(address_t)isr_entry_82,
	(address_t)isr_entry_83,
	(address_t)isr_entry_84,
	(address_t)isr_entry_85,
	(address_t)isr_entry_86,
	(address_t)isr_entry_87,
	(address_t)isr_entry_88,
	(address_t)isr_entry_89,
	(address_t)isr_entry_8a,
	(address_t)isr_entry_8b,
	(address_t)isr_entry_8c,
	(address_t)isr_entry_8d,
	(address_t)isr_entry_8e,
	(address_t)isr_entry_8f,
	(address_t)isr_entry_90,
	(address_t)isr_entry_91,
	(address_t)isr_entry_92,
	(address_t)isr_entry_93,
	(address_t)isr_entry_94,
	(address_t)isr_entry_95,
	(address_t)isr_entry_96,
	(address_t)isr_entry_97,
	(address_t)isr_entry_98,
	(address_t)isr_entry_99,
	(address_t)isr_entry_9a,
	(address_t)isr_entry_9b,
	(address_t)isr_entry_9c,
	(address_t)isr_entry_9d,
	(address_t)isr_entry_9e,
	(address_t)isr_entry_9f,
	(address_t)isr_entry_a0,
	(address_t)isr_entry_a1,
	(address_t)isr_entry_a2,
	(address_t)isr_entry_a3,
	(address_t)isr_entry_a4,
	(address_t)isr_entry_a5,
	(address_t)isr_entry_a6,
	(address_t)isr_entry_a7,
	(address_t)isr_entry_a8,
	(address_t)isr_entry_a9,
	(address_t)isr_entry_aa,
	(address_t)isr_entry_ab,
	(address_t)isr_entry_ac,
	(address_t)isr_entry_ad,
	(address_t)isr_entry_ae,
	(address_t)isr_entry_af,
	(address_t)isr_entry_b0,
	(address_t)isr_entry_b1,
	(address_t)isr_entry_b2,
	(address_t)isr_entry_b3,
	(address_t)isr_entry_b4,
	(address_t)isr_entry_b5,
	(address_t)isr_entry_b6,
	(address_t)isr_entry_b7,
	(address_t)isr_entry_b8,
	(address_t)isr_entry_b9,
	(address_t)isr_entry_ba,
	(address_t)isr_entry_bb,
	(address_t)isr_entry_bc,
	(address_t)isr_entry_bd,
	(address_t)isr_entry_be,
	(address_t)isr_entry_bf,
	(address_t)isr_entry_c0,
	(address_t)isr_entry_c1,
	(address_t)isr_entry_c2,
	(address_t)isr_entry_c3,
	(address_t)isr_entry_c4,
	(address_t)isr_entry_c5,
	(address_t)isr_entry_c6,
	(address_t)isr_entry_c7,
	(address_t)isr_entry_c8,
	(address_t)isr_entry_c9,
	(address_t)isr_entry_ca,
	(address_t)isr_entry_cb,
	(address_t)isr_entry_cc,
	(address_t)isr_entry_cd,
	(address_t)isr_entry_ce,
	(address_t)isr_entry_cf,
	(address_t)isr_entry_d0,
	(address_t)isr_entry_d1,
	(address_t)isr_entry_d2,
	(address_t)isr_entry_d3,
	(address_t)isr_entry_d4,
	(address_t)isr_entry_d5,
	(address_t)isr_entry_d6,
	(address_t)isr_entry_d7,
	(address_t)isr_entry_d8,
	(address_t)isr_entry_d9,
	(address_t)isr_entry_da,
	(address_t)isr_entry_db,
	(address_t)isr_entry_dc,
	(address_t)isr_entry_dd,
	(address_t)isr_entry_de,
	(address_t)isr_entry_df,
	(address_t)isr_entry_e0,
	(address_t)isr_entry_e1,
	(address_t)isr_entry_e2,
	(address_t)isr_entry_e3,
	(address_t)isr_entry_e4,
	(address_t)isr_entry_e5,
	(address_t)isr_entry_e6,
	(address_t)isr_entry_e7,
	(address_t)isr_entry_e8,
	(address_t)isr_entry_e9,
	(address_t)isr_entry_ea,
	(address_t)isr_entry_eb,
	(address_t)isr_entry_ec,
	(address_t)isr_entry_ed,
	(address_t)isr_entry_ee,
	(address_t)isr_entry_ef,
	(address_t)isr_entry_f0,
	(address_t)isr_entry_f1,
	(address_t)isr_entry_f2,
	(address_t)isr_entry_f3,
	(address_t)isr_entry_f4,
	(address_t)isr_entry_f5,
	(address_t)isr_entry_f6,
	(address_t)isr_entry_f7,
	(address_t)isr_entry_f8,
	(address_t)isr_entry_f9,
	(address_t)isr_entry_fa,
	(address_t)isr_entry_fb,
	(address_t)isr_entry_fc,
	(address_t)isr_entry_fd,
	(address_t)isr_entry_fe,
	(address_t)isr_entry_ff
};

/*-------------------------------------------------------*
*  FUNCTION     : hw_idt_register_handler()
*  PURPOSE      : Register interrupt handler at spec. vector
*  ARGUMENTS    : uint8_t vector_id
*               : address_t isr_handler_address - address of function
*  RETURNS      : void
*-------------------------------------------------------*/
void hw_idt_register_handler(vector_id_t vector_id,
			     address_t isr_handler_address)
{
	/* fill IDT entries with it */
	idt[vector_id].offset_0_15 =
		(uint32_t)GET_2BYTE(isr_handler_address, 0);
	idt[vector_id].offset_15_31 =
		(uint32_t)GET_2BYTE(isr_handler_address, 1);
	idt[vector_id].offset_32_63 =
		(uint32_t)GET_4BYTE(isr_handler_address, 1);
	idt[vector_id].ist =
		vector_id < NELEMENTS(ist_used) ? ist_used[vector_id] : 0;
	idt[vector_id].gate_type = IA32E_IDT_GATE_TYPE_INTERRUPT_GATE;
	idt[vector_id].dpl = 0;
	idt[vector_id].present = 1;
	idt[vector_id].css = CODE64_GDT_ENTRY_OFFSET;
}

/*-------------------------------------------------------*
*  FUNCTION     : hw_idt_setup()
*  PURPOSE      : Build and populate IDT table, used by all CPUs
*  ARGUMENTS    : void
*  RETURNS      : void
*-------------------------------------------------------*/
void hw_idt_setup(void)
{
	unsigned vector_id;

	for (vector_id = 0; vector_id < IDT_VECTOR_COUNT; ++vector_id)
		hw_idt_register_handler((vector_id_t)vector_id,
			isr_handler_table[vector_id]);
}

/*-------------------------------------------------------*
*  FUNCTION     : hw_idt_load()
*  PURPOSE      : Load IDT descriptor into IDTR of CPU, currently excuted
*  ARGUMENTS    : void
*  RETURNS      : void
*-------------------------------------------------------*/
void hw_idt_load(void)
{
	em64t_idt_descriptor_t idt_desc;

	idt_desc.base = (address_t)idt;
	idt_desc.limit = sizeof(idt) - 1;
	hw_lidt((void *)&idt_desc);
}

/*----------------------------------------------------*
 *  FUNCTION     : idt_get_extra_stacks_required()
 *  PURPOSE      : Returns the number of extra stacks required by ISRs
 *  ARGUMENTS    : void
 *  RETURNS      : number between 0..7
 *  NOTES        : per CPU
 *-------------------------------------------------------*/
uint8_t idt_get_extra_stacks_required(void)
{
	/* the number of no-zero elements in array <ist_used> */
	return 6;
}
