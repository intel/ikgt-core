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

#ifndef _IA32_DEFS_H_
#define _IA32_DEFS_H_


/*------------------------------
 * Segment Selector Definitions
 *------------------------------ */

#define IA32_SELECTOR_INDEX_MASK 0xFFF8

/* Note About ia32_selector_t
 * ------------------------
 * Although actual selectors are 16-bit fields, the following ia32_selector_t
 * definition is of a 32-bit union.  This is done because standard C requires
 * bit fields to reside within an int-sized variable. */

typedef union {
	struct {
		uint32_t rpl:2;        /* bits 1-0 */
		uint32_t ti:1;         /* Bit 2 */
		uint32_t index:13;     /* bits 3-15 */
		uint32_t dummy:16;     /* Fill up to 32 bits.  Actual selector is 16 bits. */
	} PACKED bits;
	uint16_t	sel16;
	uint32_t	dummy;
} PACKED ia32_selector_t;

/*------------------------
 * Descriptor Definitions
 *------------------------ */

typedef struct {
	uint16_t	limit;
	uint32_t	base;
} PACKED ia32_gdtr_t, ia32_idtr_t;

typedef struct {
	struct {
		uint32_t limit_15_00:16;
		uint32_t base_address_15_00:16;
	} lo;
	struct {
		uint32_t base_address_23_16:8;
		uint32_t accessed:1;
		uint32_t writable:1;
		uint32_t expansion_direction:1;
		uint32_t mbz_11:1;     /* Must Be Zero */
		uint32_t mbo_12:1;     /* Must Be One */
		uint32_t dpl:2;        /* Descriptor Privilege Level */
		uint32_t present:1;
		uint32_t limit_19_16:4;
		uint32_t avl:1;        /* Available to software */
		uint32_t mbz_21:1;     /* Must Be Zero */
		uint32_t big:1;
		uint32_t granularity:1;
		uint32_t base_address_31_24:8;
	} hi;
} PACKED ia32_data_segment_descriptor_t;

typedef struct {
	struct {
		uint32_t limit_15_00:16;
		uint32_t base_address_15_00:16;
	} lo;
	struct {
		uint32_t base_address_23_16:8;
		uint32_t accessed:1;
		uint32_t readable:1;
		uint32_t conforming:1;
		uint32_t mbo_11:1;     /* Must Be One */
		uint32_t mbo_12:1;     /* Must Be One */
		uint32_t dpl:2;        /* Descriptor Privilege Level */
		uint32_t present:1;
		uint32_t limit_19_16:4;
		uint32_t avl:1;           /* Available to software */
		uint32_t mbz_21:1;        /* Must Be Zero */
		uint32_t default_size:1;  /* 0 = 16-bit segment; 1 = 32-bit segment */
		uint32_t granularity:1;
		uint32_t base_address_31_24:8;
	} hi;
} PACKED ia32_code_segment_descriptor_t;

typedef struct {
	struct {
		uint32_t limit_15_00:16;
		uint32_t base_address_15_00:16;
	} lo;
	struct {
		uint32_t base_address_23_16:8;
		uint32_t type:4;
		uint32_t s:1;          /* 0 = system; 1 = code or data */
		uint32_t dpl:2;        /* Descriptor Privilege Level */
		uint32_t present:1;
		uint32_t limit_19_16:4;
		uint32_t avl:1;            /* Available to software */
		uint32_t mbz_21:1;         /* Must Be Zero */
		uint32_t default_size:1;   /* 0 = 16-bit segment; 1 = 32-bit segment */
		uint32_t granularity:1;
		uint32_t base_address_31_24:8;
	} hi;
} PACKED ia32_generic_segment_descriptor_t;

typedef struct {
	struct {
		uint32_t limit_15_00:16;
		uint32_t base_address_15_00:16;
	} lo;
	struct {
		uint32_t base_address_23_16:8;
		uint32_t mbo_8:1;      /* Must Be One */
		uint32_t busy:1;
		uint32_t mbz_10:1;     /* Must Be Zero */
		uint32_t mbo_11:1;     /* Must Be One */
		uint32_t mbz_12:1;     /* Must Be Zero */
		uint32_t dpl:2;        /* Descriptor Privilege Level */
		uint32_t present:1;
		uint32_t limit_19_16:4;
		uint32_t avl:1;        /* Available to software */
		uint32_t mbz_21:1;     /* Must Be Zero */
		uint32_t mbz_22:1;     /* Must Be Zero */
		uint32_t granularity:1;
		uint32_t base_address_31_24:8;
	} hi;
} PACKED ia32_stack_segment_descriptor_t;

typedef struct {
	uint16_t	limit_15_00;
	uint16_t	base_address_15_00;
	uint8_t		base_address_23_16;
	uint16_t	attributes;
	uint8_t		base_address_31_24;
} PACKED ia32_generic_segment_descriptor_attr_t;

typedef union {
	ia32_generic_segment_descriptor_t	gen;
	ia32_generic_segment_descriptor_attr_t	gen_attr;
	ia32_data_segment_descriptor_t		ds;
	ia32_code_segment_descriptor_t		cs;
	ia32_stack_segment_descriptor_t		tss;
	struct {
		uint32_t	lo;
		uint32_t	hi;
	} PACKED desc32;
	uint64_t				desc64;
} PACKED ia32_segment_descriptor_t;

/* Note About ia32_segment_descriptor_attr_t
 * ---------------------------------------
 * Although actual attributes are 16-bit fields, the following
 * definition is of a 32-bit union.  This is done because standard C requires
 * bit fields to reside within an int-sized variable. */

typedef union {
	struct {
		uint32_t type:4;
		uint32_t s:1;          /* 0 = system; 1 = code or data */
		uint32_t dpl:2;        /* Descriptor Privilege Level */
		uint32_t present:1;
		uint32_t limit_19_16:4;
		uint32_t avl:1;           /* Available to software */
		uint32_t mbz_21:1;        /* Must Be Zero */
		uint32_t default_size:1;  /* 0 = 16-bit segment; 1 = 32-bit segment */
		uint32_t granularity:1;
		uint32_t dummy:16;        /* Fill up to 32 bits.  Actual attributes are 16 bits. */
	} PACKED bits;
	uint16_t	attr16;
	uint32_t	dummy;  /* Fill up to 32 bits.  Actual attributes are 16 bits. */
} PACKED ia32_segment_descriptor_attr_t;

/*------------------
 * ICR Definitions
 *------------------*/

typedef union {
	struct {
		uint32_t reserved_1:24;
		uint32_t destination:8;
	} PACKED bits;
	uint32_t uint32;
} PACKED ia32_icr_high_t;

typedef union {
	struct {
		uint32_t vector:8;
		uint32_t delivery_mode:3;
		uint32_t destination_mode:1;
		uint32_t delivery_status:1;
		uint32_t reserved_1:1;
		uint32_t level:1;
		uint32_t trigger_mode:1;
		uint32_t reserved_2:2;
		uint32_t destination_shorthand:2;
		uint32_t reserved_3:12;
	} PACKED bits;
	uint32_t uint32;
} PACKED ia32_icr_low_t;

typedef struct {
	ia32_icr_low_t	lo_dword;
	ia32_icr_high_t hi_dword;
} PACKED ia32_icr_t;

/*-----------------------------------------------
 * Local APIC Memory Mapped I/O register offsets
 *----------------------------------------------- */

#define LOCAL_APIC_IDENTIFICATION_OFFSET                    0x020
#define LOCAL_APIC_IDENTIFICATION_OFFSET_HIGH               \
	(LOCAL_APIC_IDENTIFICATION_OFFSET + 0x3)
#define LOCAL_APIC_VERSION_OFFSET                           0x030
#define LOCAL_APIC_TASK_PRIORITY_OFFSET                     0x080
#define LOCAL_APIC_ARBITRATION_PRIORITY_OFFSET              0x090
#define LOCAL_APIC_PROCESSOR_PRIORITY_OFFSET                0x0A0
#define LOCAL_APIC_EOI_OFFSET                               0x0B0
#define LOCAL_APIC_LOGICAL_DESTINATION_OFFSET               0x0D0
#define LOCAL_APIC_DESTINATION_FORMAT_OFFSET                0x0E0
#define LOCAL_APIC_SPURRIOUS_INTERRUPT_VECTOR_OFFSET        0x0F0
#define LOCAL_APIC_ISR_OFFSET                               0x100
#define LOCAL_APIC_TMR_OFFSET                               0x180
#define LOCAL_APIC_IRR_OFFSET                               0x200
#define LOCAL_APIC_ERROR_STATUS_OFFSET                      0x280
#define LOCAL_APIC_ICR_OFFSET                               0x300
#define LOCAL_APIC_ICR_OFFSET_HIGH                          \
	(LOCAL_APIC_ICR_OFFSET + 0x10)
#define LOCAL_APIC_LVT_TIMER_OFFSET                         0x320
#define LOCAL_APIC_LVT_THERMAL_SENSOR_OFFSET                0x330
#define LOCAL_APIC_LVT_PERFORMANCE_MONITOR_COUNTERS_OFFSET  0x340
#define LOCAL_APIC_LVT_LINT0_OFFSET                         0x350
#define LOCAL_APIC_LVT_LINT1_OFFSET                         0x360
#define LOCAL_APIC_LVT_ERROR_OFFSET                         0x370
#define LOCAL_APIC_INITIAL_COUNT_OFFSET                     0x380
#define LOCAL_APIC_CURRENT_COUNT_OFFSET                     0x390
#define LOCAL_APIC_DIVIDE_CONFIGURATION_OFFSET              0x3E0
#define LOCAL_APIC_MAXIMUM_OFFSET                           0x3E4

#define LOCAL_APIC_ID_LOW_RESERVED_BITS_COUNT               24

#define LOCAL_APIC_DESTINATION_BROADCAST                    0xFF

#define LOCAL_APIC_DESTINATION_MODE_PHYSICAL                0x0
#define LOCAL_APIC_DESTINATION_MODE_LOGICAL                 0x1

#define LOCAL_APIC_DELIVERY_STATUS_IDLE                     0x0
#define LOCAL_APIC_DELIVERY_STATUS_SEND_PENDING             0x1

#define LOCAL_APIC_DELIVERY_MODE_FIXED                      0x0
#define LOCAL_APIC_DELIVERY_MODE_LOWEST_PRIORITY            0x1
#define LOCAL_APIC_DELIVERY_MODE_SMI                        0x2
#define LOCAL_APIC_DELIVERY_MODE_REMOTE_READ                0x3
#define LOCAL_APIC_DELIVERY_MODE_NMI                        0x4
#define LOCAL_APIC_DELIVERY_MODE_INIT                       0x5
#define LOCAL_APIC_DELIVERY_MODE_SIPI                       0x6
#define LOCAL_APIC_DELIVERY_MODE_MAX                        0x7

#define LOCAL_APIC_TRIGGER_MODE_EDGE                        0x0
#define LOCAL_APIC_TRIGGER_MODE_LEVEL                       0x1

#define LOCAL_APIC_DELIVERY_LEVEL_DEASSERT                  0x0
#define LOCAL_APIC_DELIVERY_LEVEL_ASSERT                    0x1

#define LOCAL_APIC_BROADCAST_MODE_SPECIFY_CPU               0x0
#define LOCAL_APIC_BROADCAST_MODE_SELF                      0x1
#define LOCAL_APIC_BROADCAST_MODE_ALL_INCLUDING_SELF        0x2
#define LOCAL_APIC_BROADCAST_MODE_ALL_EXCLUDING_SELF        0x3

/* get LOCAL_APIC_BASE from IA32_MSR_APIC_BASE_INDEX */
#define LOCAL_APIC_BASE_MSR_MASK                            (~0xFFFULL)


#endif     /* _IA32_DEFS_H_ */
