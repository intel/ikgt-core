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

#ifndef _HW_UTILS_H_
#define _HW_UTILS_H_

#include "mon_defs.h"
#include "gdt.h"

boolean_t hw_rdmsr_safe(uint32_t msr_id,
			uint64_t *value,
			vector_id_t *fault_vector,
			uint32_t *error_code);
boolean_t hw_wrmsr_safe(uint32_t msr_id,
			uint64_t value,
			vector_id_t *fault_vector,
			uint32_t *error_code);

uint8_t hw_read_port_8(uint16_t port);
uint16_t hw_read_port_16(uint16_t port);
uint32_t hw_read_port_32(uint16_t port);
void hw_write_port_8(uint16_t port, uint8_t val8);
void hw_write_port_16(uint16_t port, uint16_t val16);
void hw_write_port_32(uint16_t port, uint32_t val32);

void hw_lidt(void *source);
void hw_sidt(void *destination);
void hw_write_msr(uint32_t msr_id, uint64_t value);
uint64_t hw_read_msr(uint32_t msr_id);

/*-------------------------------------------------------------------------
 * find first bit set
 *
 * forward: LSB->MSB
 * backward: MSB->LSB
 *
 * Return 0 if no bits set
 * Fills "bit_number" with the set bit position zero based
 *
 * boolean_t hw_scan_bit_forward( uint32_t& bit_number, uint32_t bitset );
 * boolean_t hw_scan_bit_backward( uint32_t& bit_number, uint32_t bitset );
 *
 * boolean_t hw_scan_bit_forward64( uint32_t& bit_number, uint64_t bitset );
 * boolean_t hw_scan_bit_backward64( uint32_t& bit_number, uint64_t bitset );
 *------------------------------------------------------------------------- */
boolean_t hw_scan_bit_forward(uint32_t *bit_number_ptr, uint32_t bitset);
boolean_t hw_scan_bit_backward(uint32_t *bit_number_ptr, uint32_t bitset);
boolean_t hw_scan_bit_forward64(uint32_t *bit_number_ptr, uint64_t bitset);
boolean_t hw_scan_bit_backward64(uint32_t *bit_number_ptr, uint64_t bitset);

uint64_t hw_rdtsc(void);
uint64_t hw_read_cr0(void);
uint64_t hw_read_cr2(void);
uint64_t hw_read_cr3(void);
uint64_t hw_read_cr4(void);
uint64_t hw_read_cr8(void);
void hw_write_cr0(uint64_t data);
void hw_write_cr3(uint64_t data);
void hw_write_cr4(uint64_t data);
void hw_write_cr8(uint64_t data);

uint64_t hw_read_dr0(void);
uint64_t hw_read_dr1(void);
uint64_t hw_read_dr2(void);
uint64_t hw_read_dr3(void);
uint64_t hw_read_dr4(void);
uint64_t hw_read_dr5(void);
uint64_t hw_read_dr6(void);
uint64_t hw_read_dr7(void);
void hw_write_dr0(uint64_t value);
void hw_write_dr1(uint64_t value);
void hw_write_dr2(uint64_t value);
void hw_write_dr3(uint64_t value);
void hw_write_dr4(uint64_t value);
void hw_write_dr5(uint64_t value);
void hw_write_dr6(uint64_t value);
void hw_write_dr7(uint64_t value);
#define hw_read_dr(__dbg_reg) hw_read_dr ## __dbg_reg()
#define hw_write_dr(__dbg_reg, __value) hw_write_dr ## __dbg_reg(__value)

void hw_invlpg(void *address);
void hw_wbinvd(void);
void hw_halt(void);

void ASM_FUNCTION hw_cpuid(cpuid_params_t *);

/*-------------------------------------------------------------------------
 * void ASM_FUNCTION cpuid( cpuid_info_struct_t* p_cpuid_info, uint32_t type);
 *------------------------------------------------------------------------- */
typedef struct {
	int data[4];
} cpuid_info_struct_t;

/* CPUID leaf and ext leaf definitions */
#define CPUID_LEAF_1H       0x1
#define CPUID_LEAF_3H       0x3
#define CPUID_LEAF_7H       0x7

#define CPUID_SUB_LEAF_0H   0x0 /* sub leaf input ECX = 0 */

#define CPUID_EXT_LEAF_1H   0x80000001
#define CPUID_EXT_LEAF_2H   0x80000002

/* CPUID bit support for h/w features */
#define CPUID_LEAF_1H_ECX_VMX_SUPPORT        5  /* ecx bit 5 for VMX */
#define CPUID_LEAF_1H_ECX_SMX_SUPPORT        6  /* ecx bit 6 for SMX */
#define CPUID_LEAF_1H_ECX_PCID_SUPPORT       17 /* ecx bit 17 for PCID (CR4.PCIDE) */

#define CPUID_EXT_LEAF_1H_EDX_SYSCALL_SYSRET 11 /* edx bit 11 for syscall/ret */
#define CPUID_EXT_LEAF_1H_EDX_RDTSCP_BIT     27 /* edx bit 27 for rdtscp */

/* ebx bit 10 for INVPCID (INPUT leaf EAX=07H, ECX=0H) */
#define CPUID_LEAF_7H_0H_EBX_INVPCID_BIT     10

/* ebx bit 0 for supporting RDFSBASE/RDGSBASE/WRFSBASE/WRGSBASE */
#define CPUID_LEAF_7H_0H_EBX_FSGSBASE_BIT     0

/* ebx bit 20 for supporting SMAP */
#define CPUID_LEAF_7H_0H_EBX_SMAP_BIT         20

/* ebx bit 7 for supporting SMEP */
#define CPUID_LEAF_7H_0H_EBX_SMEP_BIT          7


#define CPUID_VALUE_EAX(cpuid_info) ((uint32_t)((cpuid_info).data[0]))
#define CPUID_VALUE_EBX(cpuid_info) ((uint32_t)((cpuid_info).data[1]))
#define CPUID_VALUE_ECX(cpuid_info) ((uint32_t)((cpuid_info).data[2]))
#define CPUID_VALUE_EDX(cpuid_info) ((uint32_t)((cpuid_info).data[3]))

#define cpuid(p_cpuid_info, type)                                            \
	{                                                                    \
		cpuid_params_t __cpuid_params;                               \
		__cpuid_params.m_rax = type;                                 \
		hw_cpuid(&__cpuid_params);                                   \
		(p_cpuid_info)->data[0] = (uint32_t)__cpuid_params.m_rax;    \
		(p_cpuid_info)->data[1] = (uint32_t)__cpuid_params.m_rbx;    \
		(p_cpuid_info)->data[2] = (uint32_t)__cpuid_params.m_rcx;    \
		(p_cpuid_info)->data[3] = (uint32_t)__cpuid_params.m_rdx;    \
	}

/*-------------------------------------------------------------------------
 * uint32_t ASM_FUNCTION hw_read_address_size(void);
 *------------------------------------------------------------------------- */
INLINE uint32_t hw_read_address_size(void)
{
	cpuid_info_struct_t cpu_info;

	cpuid(&cpu_info, 0x80000008);
	return CPUID_VALUE_EAX(cpu_info);
}

/*-------------------------------------------------------------------------
 * check rdtscp is hw supported
 * if CPUID.80000001H:EDX.RDTSCP[bit 27] = 1.
 *------------------------------------------------------------------------- */
INLINE boolean_t is_rdtscp_supported(void)
{
	cpuid_params_t cpuid_params = { 0 };

	cpuid_params.m_rax = CPUID_EXT_LEAF_1H;

	hw_cpuid(&cpuid_params);

	return BIT_GET64(cpuid_params.m_rdx,
		CPUID_EXT_LEAF_1H_EDX_RDTSCP_BIT) ? TRUE : FALSE;
}

/*-------------------------------------------------------------------------
 * check invpcid is hw supported
 * if CPUID.(EAX=07H, ECX=0H):EBX.INVPCID (bit 10) = 1
 *------------------------------------------------------------------------- */
INLINE boolean_t is_invpcid_supported(void)
{
	cpuid_params_t cpuid_params = { 0 };

	cpuid_params.m_rax = CPUID_LEAF_7H;
	cpuid_params.m_rcx = CPUID_SUB_LEAF_0H;

	hw_cpuid(&cpuid_params);

	return BIT_GET64(cpuid_params.m_rbx,
		CPUID_LEAF_7H_0H_EBX_INVPCID_BIT) ? TRUE : FALSE;
}

/*-------------------------------------------------------------------------
 * check FSGSBASE is hw supported
 * if CPUID.(EAX=07H, ECX=0H):EBX.FSGSBASE[bit 0] = 1
 *------------------------------------------------------------------------- */
INLINE boolean_t is_fsgsbase_supported(void)
{
	cpuid_params_t cpuid_params = { 0 };

	cpuid_params.m_rax = CPUID_LEAF_7H;
	cpuid_params.m_rcx = CPUID_SUB_LEAF_0H;

	hw_cpuid(&cpuid_params);

	return BIT_GET64(cpuid_params.m_rbx,
		CPUID_LEAF_7H_0H_EBX_FSGSBASE_BIT) ? TRUE : FALSE;
}

/*-------------------------------------------------------------------------
 * check "Process-context identifiers" is hw supported
 * if CPUID.(EAX=01H):ECX.PCID[bit 17] = 1
 *------------------------------------------------------------------------- */
INLINE boolean_t is_pcid_supported(void)
{
	cpuid_params_t cpuid_params = { 0 };

	cpuid_params.m_rax = CPUID_LEAF_1H;

	hw_cpuid(&cpuid_params);

	return BIT_GET64(cpuid_params.m_rcx,
		CPUID_LEAF_1H_ECX_PCID_SUPPORT) ? TRUE : FALSE;
}

/*-------------------------------------------------------------------------
 * Perform IRET instruction.
 * void ASM_FUNCTION hw_perform_asm_iret(void);
 *------------------------------------------------------------------------- */
void ASM_FUNCTION hw_perform_asm_iret(void);

/*-------------------------------------------------------------------------
 * write GDTR register
 *
 * Note: gdtr has to point to the m16:m32 (x86) or m16:m64 (em64t)
 *------------------------------------------------------------------------- */
void ASM_FUNCTION hw_lgdt(void *gdtr);

/*-------------------------------------------------------------------------
 * read GDTR register
 *
 * Note: gdtr has to point to the m16:m32 (x86) or m16:m64 (em64t)
 *------------------------------------------------------------------------- */
void ASM_FUNCTION hw_sgdt(void *gdtr);

/*-------------------------------------------------------------------------
 * read/write segment registers
 *------------------------------------------------------------------------- */
uint16_t ASM_FUNCTION hw_read_cs(void);
void ASM_FUNCTION hw_write_cs(uint16_t);

uint16_t ASM_FUNCTION hw_read_ds(void);
void ASM_FUNCTION hw_write_ds(uint16_t);

uint16_t ASM_FUNCTION hw_read_es(void);
void ASM_FUNCTION hw_write_es(uint16_t);

uint16_t ASM_FUNCTION hw_read_ss(void);
void ASM_FUNCTION hw_write_ss(uint16_t);

uint16_t ASM_FUNCTION hw_read_fs(void);
void ASM_FUNCTION hw_write_fs(uint16_t);

uint16_t ASM_FUNCTION hw_read_gs(void);
void ASM_FUNCTION hw_write_gs(uint16_t);

uint16_t ASM_FUNCTION hw_read_tr(void);
void ASM_FUNCTION hw_write_tr(uint16_t);

uint16_t ASM_FUNCTION hw_read_ldtr(void);
void ASM_FUNCTION hw_write_ldtr(uint16_t);

/*-------------------------------------------------------------------------
 * sets new hw stack pointer (esp/rsp), jumps to the given function and
 * passes the given param to the function. This function never returns.
 * the function "func" should also never return.
 *
 *------------------------------------------------------------------------- */
typedef void (*func_main_continue_t) (void *params);
void ASM_FUNCTION hw_set_stack_pointer(hva_t new_stack_pointer,
				       func_main_continue_t func,
				       void *params);

uint64_t ASM_FUNCTION hw_read_rsp(void);

/*-------------------------------------------------------------------------
 *
 * Get current host cpu id
 *
 * Note: calulations are based on FS register
 *
 *------------------------------------------------------------------------- */
cpu_id_t ASM_FUNCTION hw_cpu_id(void);

/*-------------------------------------------------------------------------
 * write CR2
 *
 * void hw_write_cr2( uint64_t value );
 *------------------------------------------------------------------------- */
void ASM_FUNCTION hw_write_cr2(uint64_t _value);

#define hw_flash_tlb()      hw_write_cr3(hw_read_cr3())

/*------------------------------------------------------------------------- */

void hw_reset_platform(void);

/*================================== hw_stall() ==========================  */
/*
 * Stall (busy loop) for a given time, using the platform's speaker port
 * h/w.  Should only be called at initialization, since a guest OS may
 * change the platform setting. */

void hw_stall(uint32_t stall_usec);

/*======================= hw_calibrate_tsc_ticks_per_second() ============  */
/*
 * Calibrate the internal variable holding the number of TSC ticks pers second.
 *
 * Should only be called at initialization, as it relies on hw_stall() */

void hw_calibrate_tsc_ticks_per_second(void);

/*======================= hw_calibrate_tsc_ticks_per_second() ============  */
/*
 * Retrieve the internal variable holding the number of TSC ticks pers second.
 * Note that, depending on the CPU and ASCI modes, this may only be used as a
 * rough estimate. */

uint64_t hw_get_tsc_ticks_per_second(void);

/*========================== hw_stall_using_tsc() ======================== */
/*
 * Stall (busy loop) for a given time, using the CPU TSC register.
 * Note that, depending on the CPU and ASCI modes, the stall accuracy may be
 * rough. */

void hw_stall_using_tsc(uint32_t stall_usec);

/* Test for ready-to-be-accepted fixed interrupts. */
boolean_t hw_is_ready_interrupt_exist(void);

void ASM_FUNCTION hw_write_to_smi_port(uint64_t *p_rax,                 /* rcx */
				       uint64_t *p_rbx,                 /* rdx */
				       uint64_t *p_rcx,                 /* r8 */
				       uint64_t *p_rdx,                 /* r9 */
				       uint64_t *p_rsi,                 /* on the stack */
				       uint64_t *p_rdi,                 /* on the stack */
				       uint64_t *p_rflags);             /* on the stack */

void ASM_FUNCTION hw_enable_interrupts(void);
void ASM_FUNCTION hw_disable_interrupts(void);

/*-------------------------------------------------------------------------
 * Save/Restore FPU/MMX/SSE state
 * Note: saves restores XMM registers also
 *
 * Argument buffer should point to the address of the buffer 512 bytes long
 * and 16 bytes alinged
 *
 *------------------------------------------------------------------------- */
void ASM_FUNCTION hw_fxsave(void *buffer);
void ASM_FUNCTION hw_fxrestore(void *buffer);

INLINE uint32_t hw_read_memory_mapped_register(address_t base, address_t offset)
{
	return *((volatile uint32_t *)(base + offset));
}

INLINE uint32_t hw_write_memory_mapped_register(address_t base,
						address_t offset,
						uint32_t value)
{
	return *((volatile uint32_t *)(base + offset)) = value;
}

INLINE uint64_t hw_read_memory_mapped_register64(address_t base,
						 address_t offset)
{
	return *((volatile uint64_t *)(base + offset));
}

INLINE uint64_t hw_write_memory_mapped_register64(address_t base,
						  address_t offset,
						  uint64_t value)
{
	return *((volatile uint64_t *)(base + offset)) = value;
}

#endif   /* _HW_UTILS_H_ */
