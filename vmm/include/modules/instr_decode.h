/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _INSTRUCTION_DECODE_H_
#define _INSTRUCTION_DECODE_H_

#ifndef MODULE_INSTR_DECODE
#error "MODULE_INSTR_DECODE is not defined"
#endif

/*
 * Decode the opcode like MOV from MEM to REG, return REG id.
 * Note: the decode only support part of MOV instruction
 * [IN] gcpu: pointer to guest cpu
 * [OUT]*reg_id: register id
 * [OUT]*size: operand size
 */
boolean_t decode_mov_from_mem(guest_cpu_handle_t gcpu, uint32_t *reg_id, uint32_t *size);

/*
 * Decode the opcode like MOV to MEM, return the value.
 * Note: the decode only support part of MOV instruction
 * [IN] gcpu: pointer to guest cpu
 * [OUT]*value: the value move to MEM.
 * [OUT]*size: operand size
 */
boolean_t decode_mov_to_mem(guest_cpu_handle_t gcpu, uint64_t *value, uint32_t *size);


#endif
