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
#ifndef _COMMON_TYPES_H_
#define _COMMON_TYPES_H_

/*
 *
 *  Calling conventions
 *
 */

#define API_FUNCTION
#define ASM_FUNCTION
#define CDECL
#define STDCALL

#define PACKED  __attribute((packed))
#define PACK_ON
#define PACK_OFF
#define UNUSED  __attribute__((unused))

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef long long int64_t;

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

typedef void void_t;
typedef int32_t boolean_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef uint8_t char8_t;
typedef uint16_t char16_t;


typedef struct {
	uint64_t uint64[2];
} uint128_t;

typedef uint32_t bool32_t;

#define MAX_CPUS       80

#define TOTAL_INT_VECTORS       256 /* vectors in the IDT */

#define MAX_INSTRUCTION_LENGTH 15

/* Num/Len of 64-bit array requred to represent
 * the bitmap vectors of max count n (n >= 1). */
#define NUM_OF_64BIT_ARRAY(n)  (((n) + 63) >> 6)

#define CPU_BITMAP_MAX  NUM_OF_64BIT_ARRAY(MAX_CPUS)

#define MSR_LOW_FIRST   0
#define MSR_LOW_LAST    0x1FFF
#define MSR_HIGH_FIRST  0xC0000000
#define MSR_HIGH_LAST   0xC0001FFF

/* used for CPUID leaf 0x3.
 * if the signature is matched, then xmon is running. */
#define XMON_RUNNING_SIGNATURE_CORP 0x43544E49  /* "INTC", edx */
#define XMON_RUNNING_SIGNATURE_MON  0x4D4D5645  /* "XMON", ecx */

typedef struct {
	uint64_t	m_rax;
	uint64_t	m_rbx;
	uint64_t	m_rcx;
	uint64_t	m_rdx;
} cpuid_params_t;

typedef struct {
	uint64_t	img_start_gpa;
	uint64_t	img_end_gpa;
	uint64_t	heap_start_gpa;
	uint64_t	heap_end_gpa;
} memory_config_t;

#ifdef EFI32
#define ARCH_ADDRESS_WIDTH 4
#endif

#ifndef ARCH_ADDRESS_WIDTH
#define ARCH_ADDRESS_WIDTH 8
#endif

#if 8 == ARCH_ADDRESS_WIDTH
typedef uint64_t address_t;
#else
typedef uint32_t address_t;
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef uint16_t cpu_id_t;
typedef uint16_t guest_id_t;
typedef uint8_t vector_id_t;
typedef address_t hva_t;
typedef address_t hpa_t;
typedef address_t gva_t;
typedef address_t gpa_t;
typedef uint32_t msr_id_t;
typedef uint16_t io_port_id_t;

typedef struct {
	guest_id_t	guest_id;
	cpu_id_t	guest_cpu_id; /* guest cpu id and not host */
} guest_vcpu_t;

typedef struct {
	boolean_t	primary_guest;
	guest_id_t	guest_id;
	uint16_t	padding;
} guest_data_t;

typedef enum {
	MON_ERROR = -1,
	MON_OK
} mon_status_t;

#endif
