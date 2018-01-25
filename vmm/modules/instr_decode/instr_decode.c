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
#include "vmm_base.h"
#include "vmm_objects.h"
#include "gcpu.h"
#include "vmcs.h"
#include "lib/util.h"
#include "dbg.h"

#include "modules/instr_decode.h"

#define MAX_PREFIX_BYTES 4  // in spec, size limit of legacy prefix is 4 bytes.
#define INSTR_MAX_LEN   15  // in spec, instruction max length is 15 bytes.

#define MOD_SHIFT 6
#define RM_SHIFT  3

/* REX prefixs */
#define REX_W           (1 << 3)
#define REX_R           (1 << 2)
#define REX_X           (1 << 1)
#define REX_B           (1 << 0)

typedef enum {
	G1_LOCK = 0xF0,
	G1_REPN = 0xF2,
	G1_REP =  0xF3,
	G2_CS_OVRD = 0x2E,
	G2_SS_OVRD = 0x36,
	G2_DS_OVRD = 0x3E,
	G2_ES_OVRD = 0x26,
	G2_FS_OVRD = 0x64,
	G2_GS_OVRD = 0x65,
	G3_OP_SZ_OVRD = 0x66,
	G4_ADD_SZ_OVRD = 0x67
} legacy_prefix_t;

typedef enum {
	REX_40 = 0x40,
	REX_41 = 0x41,
	REX_42 = 0x42,
	REX_43 = 0x43,
	REX_44 = 0x44,
	REX_45 = 0x45,
	REX_46 = 0x46,
	REX_47 = 0x47,
	REX_48 = 0x48,
	REX_49 = 0x49,
	REX_4A = 0x4A,
	REX_4B = 0x4B,
	REX_4C = 0x4C,
	REX_4D = 0x4D,
	REX_4E = 0x4E,
	REX_4F = 0x4F
} rex_prefix_t;

/* Instruction context */
typedef struct {
	uint8_t		instr[INSTR_MAX_LEN];
	uint8_t		index;
	boolean_t	is_64_bits;
	uint8_t 	operand_size;
	uint8_t		addr_size;
	uint8_t		pad[2];
	uint32_t	opcode;
	uint8_t		rex_b;
	uint8_t		mod;
	uint8_t		rm;
	uint8_t		reg;
} instr_ctx_t;

static void sign_extend_to_64(uint64_t *value, uint8_t size)
{
	uint64_t mask = 0;

	mask = 1 << (size * 8 - 1);
	if (*value & mask) {
		*value += (0xFFFFFFFFFFFFFFFF << size );
	}
}

/*
 * Get default operand/address size from processor mode, check if in 64-bit mode.
 * Fill "operand_size", "addr_size", "is_64_bits" into instr_ctx.
 * [IN] gcpu: pointer to guest cpu
 * [IN][OUT] *pinstr_ctx: pointer to instr_ctx
 */
static void get_default_operand_size(guest_cpu_handle_t gcpu, instr_ctx_t *pinstr_ctx)
{
	uint32_t cs_d = 0;
	uint32_t cs_l = 0;
	uint32_t cs_ar = 0; // CS attribute.
	vmcs_obj_t vmcs;

	vmcs = gcpu->vmcs;
	cs_ar = (uint32_t) vmcs_read(vmcs, VMCS_GUEST_CS_AR);
	cs_d = (cs_ar & 0x2000) >> 13;  // bit #13 in guest cs access right
	cs_l = (cs_ar & 0x4000) >> 14;  // bit #14 in guest cs access right

	/* by default, 32 bits mode, or IA32e compatibility mode */
	pinstr_ctx->is_64_bits = FALSE;
	pinstr_ctx->addr_size = pinstr_ctx->operand_size = 4;

	/* 64 bits mode, default operand size is 4 */
	if (cs_d == 0 && cs_l == 1)
		pinstr_ctx->is_64_bits = TRUE;

	/* 16 bits mode */
	if (cs_d == 0 && cs_l == 0)
		pinstr_ctx->addr_size = pinstr_ctx->operand_size = 2;

	return;
}

/*
 * Get instruction from guest cpu.
 * Fill "instr" into instr_ctx.
 * [IN] gcpu: pointer to guest cpu
 * [IN][OUT] *pinstr_ctx: pointer to instr_ctx
 */
static boolean_t get_instruction_from_gcpu(guest_cpu_handle_t gcpu, instr_ctx_t *pinstr_ctx)
{
	uint64_t    g_rip;
	vmcs_obj_t  vmcs;
	pf_info_t   pfinfo;
	uint64_t    instr_len;

	if (!pinstr_ctx) {
		print_warn("%s NULL pointer\n", __func__);
		return FALSE;
	}

	memset ((void *)pinstr_ctx, 0, sizeof(instr_ctx_t));

	vmcs = gcpu->vmcs;

	/* Get instruction length */
	instr_len = vmcs_read(vmcs, VMCS_EXIT_INSTR_LEN);
	if (instr_len > INSTR_MAX_LEN) {
		print_warn("%s instruction length %u exceeds max (%d)\n", __func__, instr_len, INSTR_MAX_LEN);
		return FALSE;
	}

	g_rip = vmcs_read(vmcs, VMCS_GUEST_RIP);

	/* Copy instructions to the array */
	if (!gcpu_copy_from_gva(gcpu, g_rip, (uint64_t)pinstr_ctx->instr, instr_len, &pfinfo)) {
		print_warn("%s failed to copy\n", __func__);
		return FALSE;
	}

#ifdef LOCAL_DEBUG
	{
		uint8_t i;
		print_info("instruction: \n");
		for (i=0; i <= INSTR_MAX_LEN; i++) {
			print_info("%c ", pinstr_ctx->instr[i]);
		}

		print_info("\n", pinstr_ctx->instr[i]);
	}
#endif

	return TRUE;
}

/*
 * Analyze legacy prefix, REX prefix, calculate operand/address size
 * Get opcode, mod
 * Update "operand_size", "addr_size", fill "rex_b", "opcode", "mod" into instr_ctx.
 * [IN][OUT] *pinstr_ctx: pointer to instr_ctx.
 */
static boolean_t fill_prefix_opcode_mod(instr_ctx_t *pinstr_ctx)
{
	uint8_t     index;
	uint8_t     *instr;
	uint8_t     use_operand_size_prefix = 0;
	uint8_t     use_addr_size_prefix = 0;
	uint8_t     rex_w = 0, rex_r = 0; //rex_x is not used here.
	boolean_t   is_legacy_prefix;

	if (!pinstr_ctx) {
		print_warn("%s NULL pointer\n", __func__);
		return FALSE;
	}

	instr = pinstr_ctx->instr;

	index = 0;

	/* step1: get legacy prefix */

	is_legacy_prefix = TRUE;

	while (index < MAX_PREFIX_BYTES && is_legacy_prefix) {

		switch (instr[index]) {
			case G3_OP_SZ_OVRD:
				/* vol 2.2.1.2 Group 3: operand-size prefix is encoded using 66H
				 * For non-byte operations: if a 66H prefix is used with prefix (REX.W = 1), 66H is ignored
				 * If a 66H override is used with REX and REX.W = 0, the operand size change from 32 bits into 16 bits.
				 */
				use_operand_size_prefix = 1;
				index ++;
				break;

			case G4_ADD_SZ_OVRD:
				/* vol 2.2.1.2 Group 4: prefix is encoded using 67H
				 * Address-size override prefix
				 */
				use_addr_size_prefix = 1;
				index ++;
				break;

			case G1_LOCK:
			case G1_REPN:
			case G1_REP:
			case G2_CS_OVRD:
			case G2_SS_OVRD:
			case G2_DS_OVRD:
			case G2_ES_OVRD:
			case G2_FS_OVRD:
			case G2_GS_OVRD:
				print_warn("%s Unsupport legacy prefix \n", __func__);
				return FALSE;

			default:
				/* legacy prefix has been go through finished */
				is_legacy_prefix = FALSE;
				break;
		}
	}

	/* step2: get rex prefix if IA32e-mode 64-bit
	 * For 64-bit code, 0x40 ~ 0x4f are size/reg prefix.
	 * For 32-bit code, 0x40 ~ 0x4f are inc/dec to one of
	 * general purpose registers.  This does not apply.
	 */
	if ((instr[index] >= REX_40) && (instr[index] <= REX_4F) && pinstr_ctx->is_64_bits) {
		/* REX prefix Fileds [Bits:0100WRXB]
		 * w == 0 : Operand size determined by CS.D
		 * w == 1 : 64 bit operand size
		 */
		if (instr[index] & REX_W) {
			rex_w = 4;
			use_operand_size_prefix = 0; //66H is ignored if REX.W == 1
			use_addr_size_prefix = 0;    //address override prefix only fit to 16-bit, 32-bit, check spec
		} else {
			rex_w = 0;
		}

		/*
		 *  R bit : Extension of the ModR/M reg field
		 */
		if (instr[index] & REX_R) {
			rex_r = 8;
		} else {
			rex_r = 0;
		}

		/* rex_x = (instr[index] & REX_X) << 2;	//rex.x not used here.*/

		pinstr_ctx->rex_b = (instr[index] & REX_B) << 3;

		index ++;
	}

	/*
	 * if operand size override prefix is used,
	 * in 16-bit mode, operand size is changed into 6 - 2 = 4.
	 * in 32-bit mode, operand size is changed into 6 - 4 = 2.
	 * in 64-bit mode, if REX.w == 0, it is changed into 6 - 4 = 2.
	 *                 if REX.w == 1, prefix is ignored, it is changed into 4 + 4 = 8;
	 */
	pinstr_ctx->operand_size = (use_operand_size_prefix != 0) ?
				(6-pinstr_ctx->operand_size): pinstr_ctx->operand_size + rex_w;

	/*
	 * For displacement, if address override prefix is used,
	 * in 16-bit mode, address size is changed into 6 - 2 = 4.
	 * in 32-bit mode, address size is changed into 6 - 4 = 2.
	 * in 64-bit mode, use existing 32-bit ModR/M and SIB encodings.
	 */
	pinstr_ctx->addr_size = (use_addr_size_prefix != 0) ? (6-pinstr_ctx->addr_size): pinstr_ctx->addr_size;

	/* step3: get opcode which maybe 1 2 or 3 bytes
	 * Check for escape sequence and get opcode
	 */

	/* escape byte for two and three bytes opcode */
	if (instr[index] == (uint8_t)0x0f) {
		pinstr_ctx->opcode = ((uint32_t)instr[index]) << 8;
		index ++;
	}
	/* escapte bytes for three bytes opcode */
	if (instr[index] == (uint8_t)0x38 || instr[index] == (uint8_t)0x3A) {
		pinstr_ctx->opcode = pinstr_ctx->opcode  << 8;
		pinstr_ctx->opcode += ((uint32_t)instr[index]) << 8;
		index ++;
	}

	pinstr_ctx->opcode += instr[index];
	index ++;

	/* Step4: get ModR/M byte, if required
	 * mod: high 2 bits
	 * reg: medium 3 bits
	 * r/m: low 3 bits
	 */
	pinstr_ctx->mod = instr[index] >> MOD_SHIFT;
	pinstr_ctx->reg = (instr[index] & 0x38) >> RM_SHIFT;
	pinstr_ctx->reg = pinstr_ctx->reg + rex_r; //consider 64-bit mode, extend GPR.
	pinstr_ctx->rm = instr[index] & 0x7;
	index ++;

	if (index > INSTR_MAX_LEN) {
		print_warn("%s %d extend instr max length 15 bytes\n", __func__, index);
		return FALSE;
	}

	pinstr_ctx->index = index;

	return TRUE;
}

/*
 * Analyze whether there are SIB and displacement follow mod.
 * If so, bypass SIB, disp, move index to the beginning of immediate.
 * [IN][OUT] *pinstr_ctx: pointer to instr_ctx.
 */
static boolean_t bypass_sib_disp(instr_ctx_t *pinstr_ctx)
{
	uint8_t index;

	index = pinstr_ctx->index;

	if (pinstr_ctx->mod == 3) { // reg to reg, no sib, no disp.
		return TRUE;
	}

	/* Step5: bypass SIB byte, if required
	 * in 16-bit, no SIB.
	 * in 32-bit, mod!=11b and r/m == 100b
	 * in 64-bit, following 32-bit encoding
	 */
	if (pinstr_ctx->rm == 4 && pinstr_ctx->addr_size == 4) {
		index ++;
	}

	/* Step6: bypass displacement, if required
	 * only mod is 00b (r/m is 110b in 16-bit, 101b in 32-bit), 01b, 10b, can follow displacement
	 * Chapter 2.2.1.3, in 64-bit mode, use existing 32-bit ModR/M and SIB encodings, sizes do not change.
	 * they remain 8 bits or 32 bits and are sign-extended to 64 bits.
	 */
	if (pinstr_ctx->mod == 1) {
		index ++;
	} else if ((pinstr_ctx->mod == 2)
		|| (pinstr_ctx->mod == 0 && pinstr_ctx->rm == 6 && pinstr_ctx->addr_size == 2)
		|| (pinstr_ctx->mod == 0 && pinstr_ctx->rm == 5 && pinstr_ctx->addr_size == 4)) {

		index += pinstr_ctx->addr_size;
	}

	/* Step7: get immediate
	 * for immediate, depends on opcode, will not get here.
	 */

	if (index > INSTR_MAX_LEN) {
		print_warn("%s %d extend instr max length 15 bytes\n", __func__, index);
		return FALSE;
	}

	pinstr_ctx->index = index;

	return TRUE;
}

/* Supportted MOV:
 * 0x88: // mov r8 to m8
 * 0x8a: // mov m8 to r8
 * 0x89: // mov reg to mem
 * 0x8b: // mov mem to reg
 * 0xa1: // mov word/double word at (seg:offset) to AX/EAX
 * 0xa3: // mov AX/EAX to word/double word at (seg:offset)
 * 0xa0: // move byte at (seg:offset) to reg
 * 0xa2: // move reg to byte at (seg:offset)
 * 0xc7: // move imm16/imm32 to m16/m32/m64
 * 0xc6: // move imm8 to m8
 */
boolean_t decode_mov_from_mem(guest_cpu_handle_t gcpu, uint32_t *reg_id, uint32_t *size)
{
	instr_ctx_t instr_ctx;

	D(VMM_ASSERT(gcpu));

	if (!get_instruction_from_gcpu(gcpu, &instr_ctx)) {
		print_warn("%s Get instruction failed!\n", __func__);
		return FALSE;
	}

	get_default_operand_size(gcpu, &instr_ctx);

	if (!fill_prefix_opcode_mod(&instr_ctx)) {
		print_warn("%s Analyze instruction failed!\n", __func__);
		return FALSE;
	}

	/* opcode analyze */
	switch (instr_ctx.opcode)
	{
		case 0x8a: // (Gb, Eb) mov m8 to r8

			if (instr_ctx.mod == 3) {
				print_warn("Should not get reg-to-reg move!\n");
				return FALSE;
			}

			if ((!instr_ctx.is_64_bits) && (instr_ctx.reg / 4 == 1)) {
				print_warn("Not support the register id for AH, CH, DH, BH, ignore!\n");
				return FALSE;
			}

			*reg_id = instr_ctx.reg;
			*size = 1;

			break;

		case 0x8b: // (Gv, Ev) mov mem to reg

			if (instr_ctx.mod == 3) {
				print_warn("Should not get reg-to-reg move!\n");
				return FALSE;
			}

			*reg_id = instr_ctx.reg;
			*size = instr_ctx.operand_size;

			break;

		case 0xa1: // (rAX, Ob) mov word/double word at (seg:offset) to AX/EAX

			*reg_id = REG_RAX;
			*size = instr_ctx.operand_size;

			break;

		case 0xa0: // (AL, Ob) move byte at (seg:offset) to AL

			*reg_id = REG_RAX;
			*size = 1;

			break;

		default:
			print_warn("Unsupport instruction to decode\n");
			return FALSE;
	}

	return TRUE;
}

boolean_t decode_mov_to_mem(guest_cpu_handle_t gcpu, uint64_t *value, uint32_t *size)
{
	instr_ctx_t instr_ctx;
	uint32_t    reg_id;

	D(VMM_ASSERT(gcpu));

	if (!get_instruction_from_gcpu(gcpu, &instr_ctx)) {
		print_warn("%s Get instruction failed!\n", __func__);
		return FALSE;
	}

	get_default_operand_size(gcpu, &instr_ctx);

	if (!fill_prefix_opcode_mod(&instr_ctx)) {
		print_warn("%s Analyze instruction prefix, opcode, mod failed!\n", __func__);
		return FALSE;
	}

	/* opcode analyze */
	switch (instr_ctx.opcode)
	{
		case 0x88: // (Eb, Gb) mov r8 to m8

			if (instr_ctx.mod == 3) {
				print_warn("Should not get reg-to-reg move!\n");
				return FALSE;
			}

			reg_id = instr_ctx.reg;

			if (instr_ctx.is_64_bits) {

				if((reg_id / 4 == 1) && (instr_ctx.rex_b == 0)) {
					print_warn("Can't access AH, CH, DH, BH in 64-bit mode!\n");
					return FALSE;
				} else {
					*value = gcpu->gp_reg[reg_id] & 0xFF; // low byte of GPR
				}

			} else {

				if (reg_id / 4 == 0)
					*value = gcpu->gp_reg[reg_id] & 0xFF; // AL, CL, DL, BL
				else if (reg_id / 4 == 1)
					*value = (gcpu->gp_reg[reg_id % 4] & 0xFF00) >> 8; // AH, CH, DH, BH
			}

			*size = 1;

			break;

		case 0x89: // (Ev, Gv) mov reg to mem

			if (instr_ctx.mod == 3) {
				print_warn("Should not get reg-to-reg move!\n");
				return FALSE;
			}

			reg_id = instr_ctx.reg;
			*value = gcpu->gp_reg[reg_id];
			*size = instr_ctx.operand_size;

			break;

		case 0xa3: // (Ov, rAx) mov AX, EAX, RAX to word/double word at (seg:offset)

			*value = gcpu->gp_reg[REG_RAX];
			*size = instr_ctx.operand_size;

			break;

		case 0xa2: // (Ob, AL) move AL to byte at (seg:offset)

			*value = gcpu->gp_reg[REG_RAX];
			*size = 1;

			break;

		case 0xc7: // (Ev Iz) move imm16/imm32 to m16/m32/m64

			if (instr_ctx.mod == 3) {
				print_warn("Invalid mov instruction!\n");
				return FALSE;
			}

			if (!bypass_sib_disp(&instr_ctx)) {
				print_warn("Analyze sib, disp error!\n");
				return FALSE;
			}

			*value = 0;
			memcpy((void*)value, &instr_ctx.instr[instr_ctx.index], instr_ctx.addr_size); //get immediate.

			/* In 64-bit mode, the typical size of immediate operands remians 32bits.
			 * When the operand size is 64 bits, the processor sign-extends all immediates to 64 bits prior to their use.
			 */
			sign_extend_to_64(value, instr_ctx.addr_size);

			*size = instr_ctx.operand_size;

			break;

		case 0xc6: // (Eb, Ib) move imm8 to m8

			if (!bypass_sib_disp(&instr_ctx)) {
				print_warn("Analyze sib, disp error!\n");
				return FALSE;
			}

			*value = *((uint8_t *)&instr_ctx.instr[instr_ctx.index]);
			*size = 1;

			break;

		default:
			print_warn("Unsupport instruction to decode\n");
			return FALSE;
	}

	return TRUE;
}
