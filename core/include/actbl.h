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

#ifndef __ACTBL_H__
#define __ACTBL_H__


/*******************************************************************************
 *
 * Fundamental ACPI tables
 *
 * This file contains definitions for the ACPI tables that are directly consumed
 * by ACPICA. All other tables are consumed by the OS-dependent ACPI-related
 * device drivers and other OS support code.
 *
 * The RSDP and FACS do not use the common ACPI table header. All other ACPI
 * tables use the header.
 *
 ******************************************************************************/


/*
 * Values for description table header signatures for tables defined in this
 * file. Useful because they make it more difficult to inadvertently type in
 * the wrong signature.
 */
#define ACPI_SIG_DSDT           "DSDT"          /* Differentiated System Description Table */
#define ACPI_SIG_FADT           "FACP"          /* Fixed ACPI Description Table */
#define ACPI_SIG_FACS           "FACS"          /* Firmware ACPI Control Structure */
#define ACPI_SIG_PSDT           "PSDT"          /* Persistent System Description Table */
#define ACPI_SIG_RSDP           "RSD PTR "      /* Root System Description Pointer */
#define ACPI_SIG_RSDT           "RSDT"          /* Root System Description Table */
#define ACPI_SIG_XSDT           "XSDT"          /* Extended  System Description Table */
#define ACPI_SIG_SSDT           "SSDT"          /* Secondary System Description Table */
#define ACPI_RSDP_NAME          "RSDP"          /* Short name for RSDP, not signature */


/*
 * All tables and structures must be byte-packed to match the ACPI
 * specification, since the tables are provided by the system BIOS
 */

/*
 * Note about bitfields: The uint8_t type is used for bitfields in ACPI tables.
 * This is the only type that is even remotely portable. Anything else is not
 * portable, so do not use any other bitfield types.
 */


/*******************************************************************************
 *
 * Master ACPI Table header. This common header is used by all ACPI tables
 * except the RSDP and FACS.
 *
 ******************************************************************************/

typedef struct {
	char		signature[ACPI_NAME_SIZE];                      /* ASCII table signature */
	uint32_t	length;                                         /* length of table in bytes, including this header */
	uint8_t		revision;                                       /* ACPI Specification minor version # */
	uint8_t		check_sum;                                      /* To make sum of entire table == 0 */
	char		oem_id[ACPI_OEM_ID_SIZE];                       /* ASCII OEM identification */
	char		oem_table_id[ACPI_OEM_TABLE_ID_SIZE];           /* ASCII OEM table identification */
	uint32_t	oem_revision;                                   /* OEM revision number */
	char		asl_compiler_id[ACPI_NAME_SIZE];                /* ASCII ASL compiler vendor ID */
	uint32_t	asl_compiler_revision;                          /* ASL compiler version */
} acpi_table_header_t;


/*******************************************************************************
 *
 * GAS - Generic Address Structure (ACPI 2.0+)
 *
 * Note: Since this structure is used in the ACPI tables, it is byte aligned.
 * If misaliged access is not supported by the hardware, accesses to the
 * 64-bit address field must be performed with care.
 *
 ******************************************************************************/

typedef struct {
	uint8_t		space_id;                       /* address space where struct or register exists */
	uint8_t		bit_width;                      /* Size in bits of given register */
	uint8_t		bit_offset;                     /* Bit offset within the register */
	uint8_t		access_width;                   /* Minimum Access size (ACPI 3.0) */
	uint64_t	address;                        /* 64-bit address of struct or register */
} acpi_generic_address_t;


/*******************************************************************************
 *
 * RSDP - Root System Description Pointer (signature is "RSD PTR ")
 *        Version 2
 *
 ******************************************************************************/

typedef struct {
	char		signature[8];                           /* ACPI signature, contains "RSD PTR " */
	uint8_t		check_sum;                              /* ACPI 1.0 checksum */
	char		oem_id[ACPI_OEM_ID_SIZE];               /* OEM identification */
	uint8_t		revision;                               /* Must be (0) for ACPI 1.0 or (2) for ACPI 2.0+ */
	uint32_t	rsdt_physical_address;                  /* 32-bit physical address of the RSDT */
	uint32_t	length;                                 /* Table length in bytes, including header (ACPI 2.0+) */
	uint64_t	xsdt_physical_address;                  /* 64-bit physical address of the XSDT (ACPI 2.0+) */
	uint8_t		extended_checksum;                      /* checksum of entire table (ACPI 2.0+) */
	uint8_t		reserved[3];                            /* reserved, must be zero */
} acpi_table_rsdp_t;

/*******************************************************************************
 *
 * FACS - Firmware ACPI Control Structure (FACS)
 *
 ******************************************************************************/

typedef struct {
	char		signature[4];                   /* ASCII table signature */
	uint32_t	length;                         /* length of structure, in bytes */
	uint32_t	hardware_signature;             /* Hardware configuration signature */
	uint32_t	firmware_waking_vector;         /* 32-bit physical address of the Firmware Waking Vector */
	uint32_t	global_lock;                    /* Global Lock for shared hardware resources */
	uint32_t	flags;
	uint64_t	xfirmware_waking_vector;        /* 64-bit version of the Firmware Waking Vector (ACPI 2.0+) */
	uint8_t		version;                        /* Version of this table (ACPI 2.0+) */
	uint8_t		reserved[3];                    /* reserved, must be zero */
	uint32_t	ospm_flags;                     /* flags to be set by OSPM (ACPI 4.0) */
	uint8_t		reserved1[24];                  /* reserved, must be zero */
} acpi_table_facs_t;

/* Firmware Control Structure Feature Flags */
/* 00: Indicates whether the platform supports S4BIOS_REQ. */
#define ACPI_FACS_S4BIOS                    (1)
/* 01: Indicates that the platform firmware supports a 64 bit execution
 * environment for the waking vector.*/
#define ACPI_FACS_64BIT_WAKE_SUPPORTED      (1 << 1)

/* OSPM Enabled FirmwareControl Structure Feature Flags */
/* 00: OSPM sets this bit to indicate to platform firmware that the
 * X_Firmware_Waking_Vector requires a 64 bit execution environment.*/
#define ACPI_FACS_OSPM_64BIT_WAKE           (1)

/*******************************************************************************
 *
 * FADT - Fixed ACPI Description Table (signature "FACP")
 *        Version 4
 *
 ******************************************************************************/

/* Fields common to all versions of the FADT */

typedef struct {
	acpi_table_header_t	header;                 /* Common ACPI table header */
	uint32_t		facs;                   /* 32-bit physical address of FACS */
	uint32_t		dsdt;                   /* 32-bit physical address of DSDT */
	uint8_t			model;                  /* System Interrupt Model (ACPI 1.0) - not used in ACPI 2.0+ */
	uint8_t			preferred_profile;      /* Conveys preferred power management profile to OSPM. */
	uint16_t		sci_interrupt;          /* System vector of SCI interrupt */
	uint32_t		smi_command;            /* 32-bit Port address of SMI command port */
	uint8_t			acpi_enable;            /* Value to write to smi_cmd to enable ACPI */
	uint8_t			acpi_disable;           /* Value to write to smi_cmd to disable ACPI */
	uint8_t			s4_bios_request;        /* Value to write to SMI CMD to enter S4BIOS state */
	uint8_t			pstate_control;         /* Processor performance state control*/
	uint32_t		pm1a_event_block;       /* 32-bit Port address of Power Mgt 1a Event Reg Blk */
	uint32_t		pm1b_event_block;       /* 32-bit Port address of Power Mgt 1b Event Reg Blk */
	uint32_t		pm1a_control_block;     /* 32-bit Port address of Power Mgt 1a Control Reg Blk */
	uint32_t		pm1b_control_block;     /* 32-bit Port address of Power Mgt 1b Control Reg Blk */
	uint32_t		pm2_control_block;      /* 32-bit Port address of Power Mgt 2 Control Reg Blk */
	uint32_t		pm_timer_block;         /* 32-bit Port address of Power Mgt Timer Ctrl Reg Blk */
	uint32_t		gpe0_block;             /* 32-bit Port address of General Purpose Event 0 Reg Blk */
	uint32_t		gpe1_block;             /* 32-bit Port address of General Purpose Event 1 Reg Blk */
	uint8_t			pm1_event_length;       /* Byte length of ports at Pm1xEventBlock */
	uint8_t			pm1_control_length;     /* Byte length of ports at Pm1xControlBlock */
	uint8_t			pm2_control_length;     /* Byte length of ports at Pm2ControlBlock */
	uint8_t			pm_timer_length;        /* Byte length of ports at PmTimerBlock */
	uint8_t			gpe0_block_length;      /* Byte length of ports at Gpe0Block */
	uint8_t			gpe1_block_length;      /* Byte length of ports at Gpe1Block */
	uint8_t			gpe1_base;              /* Offset in GPE number space where GPE1 events start */
	uint8_t			cst_control;            /* Support for the _CST object and C States change notification */
	uint16_t		c2_latency;             /* Worst case HW latency to enter/exit C2 state */
	uint16_t		c3_latency;             /* Worst case HW latency to enter/exit C3 state */
	uint16_t		flush_size;             /* Processor's memory cache line width, in bytes */
	uint16_t		flush_stride;           /* Number of flush strides that need to be read */
	uint8_t			duty_offset;            /* Processor duty cycle index in processor's P_CNT reg */
	uint8_t			duty_width;             /* Processor duty cycle value bit width in P_CNT register */
	uint8_t			day_alarm;              /* Index to day-of-month alarm in RTC CMOS RAM */
	uint8_t			month_alarm;            /* Index to month-of-year alarm in RTC CMOS RAM */
	uint8_t			century;                /* Index to century in RTC CMOS RAM */
	uint16_t		boot_flags;             /* IA-PC Boot Architecture flags (see below for individual flags) */
	uint8_t			reserved;               /* reserved, must be zero */
	uint32_t		flags;                  /* Miscellaneous flag bits (see below for individual flags) */
	acpi_generic_address_t	reset_register;         /* 64-bit address of the Reset register */
	uint8_t			reset_value;            /* Value to write to the reset_register port to reset the system */
	uint8_t			reserved4[3];           /* reserved, must be zero */
	uint64_t		xfacs;                  /* 64-bit physical address of FACS */
	uint64_t		xdsdt;                  /* 64-bit physical address of DSDT */
	acpi_generic_address_t	xpm1a_event_block;      /* 64-bit Extended Power Mgt 1a Event Reg Blk address */
	acpi_generic_address_t	xpm1b_event_block;      /* 64-bit Extended Power Mgt 1b Event Reg Blk address */
	acpi_generic_address_t	xpm1a_control_block;    /* 64-bit Extended Power Mgt 1a Control Reg Blk address */
	acpi_generic_address_t	xpm1b_control_block;    /* 64-bit Extended Power Mgt 1b Control Reg Blk address */
	acpi_generic_address_t	xpm2c_control_block;    /* 64-bit Extended Power Mgt 2 Control Reg Blk address */
	acpi_generic_address_t	xpm_timer_block;        /* 64-bit Extended Power Mgt Timer Ctrl Reg Blk address */
	acpi_generic_address_t	xgpe0_block;            /* 64-bit Extended General Purpose Event 0 Reg Blk address */
	acpi_generic_address_t	xgpe1_block;            /* 64-bit Extended General Purpose Event 1 Reg Blk address */
} acpi_table_fadt_t;

/* Masks for FADT flags */

#define ACPI_FADT_WBINVD            (1)         /* 00: [V1] The wbinvd instruction works properly */
#define ACPI_FADT_WBINVD_FLUSH      (1 << 1)    /* 01: [V1] wbinvd flushes but does not invalidate caches */
#define ACPI_FADT_C1_SUPPORTED      (1 << 2)    /* 02: [V1] All processors support C1 state */
#define ACPI_FADT_C2_MP_SUPPORTED   (1 << 3)    /* 03: [V1] C2 state works on MP system */
#define ACPI_FADT_POWER_BUTTON      (1 << 4)    /* 04: [V1] Power button is handled as a control method device */
#define ACPI_FADT_SLEEP_BUTTON      (1 << 5)    /* 05: [V1] Sleep button is handled as a control method device */
#define ACPI_FADT_FIXED_RTC         (1 << 6)    /* 06: [V1] RTC wakeup status not in fixed register space */
#define ACPI_FADT_S4_RTC_WAKE       (1 << 7)    /* 07: [V1] RTC alarm can wake system from S4 */
#define ACPI_FADT_32BIT_TIMER       (1 << 8)    /* 08: [V1] ACPI timer width is 32-bit (0=24-bit) */
#define ACPI_FADT_DOCKING_SUPPORTED (1 << 9)    /* 09: [V1] Docking supported */
#define ACPI_FADT_RESET_REGISTER    (1 << 10)   /* 10: [V2] System reset via the FADT RESET_REG supported */
#define ACPI_FADT_SEALED_CASE       (1 << 11)   /* 11: [V3] No internal expansion capabilities and case is sealed */
#define ACPI_FADT_HEADLESS          (1 << 12)   /* 12: [V3] No local video capabilities or local input devices */
#define ACPI_FADT_SLEEP_TYPE        (1 << 13)   /* 13: [V3] Must execute native instruction after writing SLP_TYPx register */
#define ACPI_FADT_PCI_EXPRESS_WAKE  (1 << 14)   /* 14: [V4] System supports PCIEXP_WAKE (STS/EN) bits (ACPI 3.0) */
#define ACPI_FADT_PLATFORM_CLOCK    (1 << 15)   /* 15: [V4] OSPM should use platform-provided timer (ACPI 3.0) */
#define ACPI_FADT_S4_RTC_VALID      (1 << 16)   /* 16: [V4] Contents of RTC_STS valid after S4 wake (ACPI 3.0) */
#define ACPI_FADT_REMOTE_POWER_ON   (1 << 17)   /* 17: [V4] System is compatible with remote power on (ACPI 3.0) */
#define ACPI_FADT_APIC_CLUSTER      (1 << 18)   /* 18: [V4] All local APICs must use cluster model (ACPI 3.0) */
#define ACPI_FADT_APIC_PHYSICAL     (1 << 19)   /* 19: [V4] All local xAPICs must use physical dest mode (ACPI 3.0) */

/* Reset to default packing */


#endif                          /* __ACTBL_H__ */
