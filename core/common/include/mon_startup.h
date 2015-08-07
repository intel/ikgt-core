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

#ifndef _MON_STARTUP_H_
#define _MON_STARTUP_H_

/*==========================================================================
 *
 * MON Startup Definitions
 *
 * This files contains definitions of the interaction between the MON and its
 * loader.
 *
 *========================================================================== */

#include "mon_defs.h"
#include "mon_arch_defs.h"


/*==========================================================================
 *
 * MON Sturtup Constants
 *
 * Note: Constants that are related to specific structures below are defined
 * near their respective structures.
 *
 *========================================================================== */

/* Default size for MON footprint in memory, that should be
 * allocated by the loader.  Includes the MON executable image and work work
 * area, but not the 32bit-to-64bit Thunk image. */

#define MON_DEFAULT_FOOTPRINT          (75 MEGABYTES)

/* Default size for MON stack, in pages */

#define MON_DEFAULT_STACK_SIZE_PAGES   10

/*===========================================================================
 *
 *  MON Startup Structure Types
 *
 * Notes:
 *
 * These structures are used both in 32-bit and 64-bit modes, therefore:
 *
 * - Structure sizes are 64-bit aligned
 * - All pointers are defined as 64-bit, and must be set so their higher 32
 * bits
 * are 0 (< 4GB).  This ensures their usability in both 32-bit and 64-bit
 * modes.
 * - All pointers are in a loader virtual memory space (if applicable).
 *
 * Major terms:
 *
 * Primary guest - the guest that owns the platform and platform was
 * booted originally to run this guest
 *
 * Secondary guest - the guest that is used to perform some dedicated tasks
 * on behalf of the primary guest
 *
 * Following is the structure hierarchy (---> denotes a pointer):
 *
 * mon_startup_struct_t
 * +---- mon_memory_layout_t mon_memory_layout[]
 * +---> int15_e820_memory_map_t physical_memory_layout_E820
 * +---> mon_guest_startup_t primary_guest_startup_state
 * | +---> mon_guest_cpu_startup_state_t cpu_states_array[]
 * | | +---- mon_gp_registers_t gp
 * | | +---- mon_xmm_registers_t xmm
 * | | +---- mon_segments_t seg
 * | | +---- mon_control_registers_t control
 * | | +---- mon_model_specific_registers_t msr
 * | +---> mon_guest_device_t devices_array[]
 * +---> mon_guest_startup_t secondary_guests_startup_state_array[]
 * | +... as above
 * +---- mon_debug_params_t debug_params
 * +---- mon_debug_port_params_t port
 * mon_application_params_struct_t
 *
 *========================================================================== */

/*--------------------------------------------------------------------------
 *
 * mon_memory_layout_t
 *
 * MON bounding box - mon memory layout as it was built by loader
 * Data about sizes is part of installer info
 *
 * Mon image occupies area [base_address .. base_address+image_size]
 * Area [base_address+image_size .. base_address+total_size] is used for
 * mon heaps and stacks
 *-------------------------------------------------------------------------- */

typedef struct {
	uint32_t	total_size;
	uint32_t	image_size;
	uint64_t	base_address;
	uint64_t	entry_point;
} PACKED mon_memory_layout_t;

/*--------------------------------------------------------------------------
 *
 * mon_guest_cpu_startup_state_t: Initial Guest CPU State
 *
 * Specified per each active CPU for each guest, and will be put into Guest
 * VMCS
 * at guest launch time. All addresses are absolute values that should be put
 * in VMCS.
 *
 * If for some guest CPU mon_guest_cpu_startup_state_t is not specified,
 * this guest CPU is put into the Wait-for-SIPI state.
 * mon_guest_cpu_startup_state_t must be specified for at least first processor
 * (BSP) of each guest.
 *
 * Guest initial entry point should be set as the CS:RIP pair:
 *
 * - CS is specified in the seg[IA32_SEG_CS].selector value
 * - RIP is specified in the gp[IA32_REG_RIP] value
 *
 * These values are specified as:
 *
 * 1. If guest paging is active CS:RIP is in the GVA notation
 * 2. If guest is in protected non-paged mode CS:RIP is in the GPA notation
 * 3. If guest is in the real mode CS:RIP is in the GPA notation and CS
 *    specifies the GPA value of the segment base, shifted right 4 bits.
 *
 *------------------------------------------------------------------------- */

#define MON_GUEST_CPU_STARTUP_STATE_VERSION       1

/* This structure should be aligned on 8 byte */
#define MON_GUEST_CPU_STARTUP_STATE_ALIGNMENT     8

typedef struct {
	uint16_t	size_of_this_struct;
	uint16_t	version_of_this_struct;
	uint32_t	reserved_1;

	/* 64-bit aligned */

	/* there are additional registers in the CPU that are not passed here.
	 * it is assumed that for the new guest the state of such registers is
	 * the same, as it was at the MON entry point. */

	mon_gp_registers_t		gp;
	mon_xmm_registers_t		xmm;
	mon_segments_t			seg;
	mon_control_registers_t		control;
	mon_model_specific_registers_t	msr;
} PACKED mon_guest_cpu_startup_state_t;

/*-------------------------------------------------------------------------
 *
 * mon_guest_device_t: Guest Devices
 *
 * Describes virtualized, hidden or attached device
 *
 * If device is assinged to this guest is may be exposed using its real
 * id or virtualized id.
 * If the same real_vendor_id/real_device_id is specified for
 * number of guests, this device will be exposed to each of this guests.
 * If VT-d is active, this device will be hidden from all other guests.
 * If VT-d is not active, it will be exposed using "unsupported vendor/device
 * id"
 *
 * *** THIS FEATURE IS NOT CURRENTLY SUPPORTED ***
 *
 *-------------------------------------------------------------------------- */

#define MON_GUEST_DEVICE_VERSION                  1

typedef struct {
	uint16_t	size_of_this_struct;
	uint16_t	version_of_this_struct;
	uint32_t	reserved_1;

	/* Real device data */

	uint16_t	real_vendor_id;
	uint16_t	real_device_id;

	/* Virtual device data */

	uint16_t	virtual_vendor_id;
	uint16_t	virtual_device_id;
} PACKED mon_guest_device_t;

/*--------------------------------------------------------------------------
 *
 * mon_guest_startup_t: Describes One Guest
 *
 *-------------------------------------------------------------------------- */

#define MON_GUEST_STARTUP_VERSION                 1

/*-------------
 * Guest flags
 *------------- */

/* 1 - allow execution of 'int' instructions in real mode
 * 0 - stop guest scheduling on first 'int' intruction execution in real mode */
#define MON_GUEST_FLAG_REAL_BIOS_ACCESS_ENABLE    BIT_VALUE(0)

/* 1 - start the guest as soon as possible without any additional request
 * 0 - start the guest on a specific request of some other guest
 *     using the appropriate VmCall( guest_magic_number )
 * at least one guest have to be configured as 'start_immediately' */
#define MON_GUEST_FLAG_LAUNCH_IMMEDIATELY         BIT_VALUE(1)

/* 1 - image is compressed. Should be uncompressed before execution. */
#define MON_GUEST_FLAG_IMAGE_COMPRESSED           BIT_VALUE(2)

/* This structure should be aligned on 8 bytes */
#define MON_GUEST_STARTUP_ALIGNMENT               8

typedef struct {
	uint16_t	size_of_this_struct;
	uint16_t	version_of_this_struct;

	/* set of flags that define policies for this guest, see definition
	 * above */
	uint32_t	flags;

	/* 64-bit aligned */

	/* guest unique id in the current application. */
	uint32_t	guest_magic_number;

	/* set bit to 1 for each physical CPU, where GuestCPU should run.
	 * Guest should have num_of_virtual_CPUs == num of 1-bits in the mask
	 * ex. 0x3 means that guest has 2 CPUs that run on physical-0 and
	 * physical-1 CPUs
	 *
	 * if -1 - run on all available CPUs
	 *
	 * if number of 1 bits is more than cpu_states_count, all other guest
	 * CPUs will be initialized in the Wait-for-SIPI state.
	 *
	 * if 1 is set for bit-number greater than physically available CPU count,
	 * the whole guest is discarded. The only exception is -1. */
	uint32_t cpu_affinity;

	/* 64-bit aligned */

	/* number of MON_GUEST_CPU_STATE structures provided.
	 * if number of MON_GUEST_CPU_STATE structures is less than number
	 * of processors used by this guest, all other processors will
	 * be initialized in the Wait-for-SIPI state */
	uint32_t cpu_states_count;

	/* 64-bit aligned */

	/* number of virtualized or hidden devices for specific guest
	 * if count == 0 - guest is deviceless,
	 * except the case that the guest is also signed as default_device_owner */
	uint32_t	devices_count;

	/* guest image as loaded by the loader
	 * For primary guest it must be zeroed */
	uint32_t	image_size;
	uint64_t	image_address;

	/* 64-bit aligned */

	/* amount of physical memory for this guest
	 * For primary guest it must be zeroed */
	uint32_t	physical_memory_size;

	/* load address of the image in the guest physical memory
	 * For primary guest it must be zeroed */
	uint32_t	image_offset_in_guest_physical_memory;

	/* pointer to an array of initial CPU states for guest CPUs
	 * First entry is for the guest BSP processor, that is a least-numbered
	 * 1 bit in the cpu_affinity. At least one such structure has to exist.
	 * Guest CPUs that does not have this structure are started in the
	 * Wait-for-SIPI state.
	 * mon_guest_cpu_startup_state_t* */
	uint64_t	cpu_states_array;

	/* pointer to an array of guest devices
	 * mon_guest_device_t* */
	uint64_t	devices_array;
} PACKED mon_guest_startup_t;

/*---------------------------------------------------------------------------
 *
 * mon_debug_params_t: Debug Parameters
 *
 * Controls various parameters for MON debug
 *
 * Note: There are no 'size' and 'version' fields in mon_debug_params_t, since
 * this strucure is included in MON_STURTUP_STRUCT.  Set the version
 * there!
 *
 *-------------------------------------------------------------------------- */

/* mon_debug_port_type_t: Type of debug port */

typedef enum {
	/* No debug port is used */
	MON_DEBUG_PORT_NONE = 0,

	/* The debug port is a generic 16450-compatible serial controller */
	MON_DEBUG_PORT_SERIAL,

	MON_DEBUG_PORT_TYPE_LAST
} mon_debug_port_type_t;

/* mon_debug_port_ident_type_t: How the debug port is identified */

#define MON_DEBUG_PORT_IDENT_PCI_INDEX_MAX 15   /* See below */

typedef enum {
	/* No debug port is identified, use the MON default */
	MON_DEBUG_PORT_IDENT_DEFAULT = 0,

	/* The debug port is identified using its h/w base address in the I/O space
	 */
	MON_DEBUG_PORT_IDENT_IO,

	/* The debug port is identified as the N'th debug port (of type
	 * mon_debug_port_type_t) on the PCI bus.
	 * Range is 0 to MON_DEBUG_PORT_IDENT_PCI_INDEX_MAX
	 * *** NOTES ***:
	 *     1. This is not directly supported by MON yet.
	 *     2. Loaders may support this, but must detect devices and
	 *        convert to MON_DEBUG_PORT_IDENT_IO before invoking MON */
	MON_DEBUG_PORT_IDENT_PCI_INDEX,

	MON_DEBUG_PORT_IDENT_LAST
} mon_debug_port_ident_type_t;

/* mon_debug_port_virt_mode_t: How the debug port is virtualized */

typedef enum {
	/* No virtaulization */
	MON_DEBUG_PORT_VIRT_NONE = 0,

	/* Hide the port.  Reads return all 1, writes do nothing.  This mode is
	 * useful when all the guests are expected to discover the port before they
	 */
	/* attempt to use it. */
	MON_DEBUG_PORT_VIRT_HIDE,

	/* Port acts as /dev/null: Status reads emulate ready for output, no
	 * available input.  Writes do nothing.  This modes may be useful for late
	 * launch, to avoid hanging the primary guest if it tries to use the same
	 * port.
	 * **** THIS MODE IS NOT SUPPORTED YET **** */
	MON_DEBUG_PORT_VIRT_NULL,

	MON_DEBUG_PORT_VIRT_LAST
} mon_debug_port_virt_mode_t;

/* This structure should be aligned on 8 byte */
#define MON_DEBUG_PORT_PARAMS_ALIGNMENT           8

#define MON_DEBUG_PORT_SERIAL_IO_BASE_DEFAULT 0x3F8 /* std com1 */

typedef struct {
	uint8_t type;                           /* mon_debug_port_type_t */
	uint8_t virt_mode;                      /* mon_debug_port_virt_mode_t */
	uint8_t reserved_1;
	uint8_t ident_type;                     /* mon_debug_port_ident_type_t */
	union {
		uint16_t	io_base;        /* For use with ident_type == MON_DEBUG_PORT_IDENT_IO */
		uint32_t	index;          /* For use with ident_type == MON_DEBUG_PORT_IDENT_PCI_INDEX */
		uint32_t	ident32;        /* Dummy filler */
	} PACKED ident;

	/* 64-bit aligned */
} PACKED mon_debug_port_params_t;

/* This structure should be aligned on 8 byte */
#define MON_DEBUG_PARAMS_ALIGNMENT                8

typedef struct {
	/* Global level filter for debug printouts.  Only messages whose level are
	 * lower or equal than this value are printed.
	 * 0 : Only top-priority messages (e.g., fatal errors) are printed
	 * 1 : In addition to the above, error messages are printed (default)
	 * 2 : In addition to the above, warnings are printed
	 * 3 : In addition to the above, informational messages are printed
	 * 4 : In addition to the above, trace messages are printed */
	uint8_t			verbosity;

	uint8_t			reserved[7];

	/* 64-bit aligned */

	/* Main debug port: used for logging, CLI etc. */
	mon_debug_port_params_t port;

	/* 64-bit aligned */

	/* Auxiliary debug port: used for GDB */
	mon_debug_port_params_t aux_port;

	/* 64-bit aligned */

	/* Global bit-mask filter for debug printouts.  Each bit in the mask
	 * enables printing of one class (documented separately) of printout.
	 * All 0's : nothing is printed
	 * All 1's : everything is printed */
	uint64_t	mask;

	/* 64-bit aligned */

	/* Physical address of debug buffer used during deadloop */
	uint64_t	debug_data;

	/* 64-bit aligned */
} PACKED mon_debug_params_t;

/*--------------------------------------------------------------------------
 *
 * mon_startup_struct_t: Startup Parameters
 *
 * Top level structure that describes MON layout, guests, etc.
 * Passed to MON entry point.
 *
 *------------------------------------------------------------------------- */

#define MON_STARTUP_STRUCT_VERSION                5

/* Minimal version number of mon_startup_struct_t that includes mon_debug_params_t.
 *
 * This is required for proper version checking on MON initialization, as older
 *
 * versions don't have this structure and MON must use defaults. */
#define MON_STARTUP_STRUCT_MIN_VERSION_WITH_DEBUG 2

/* Startup capability flags (max 16 bits) */

#define MON_STARTUP_ACPI_DISCOVERY_CAPABILITY     BIT_VALUE(0)

/* 1 - the MON is launched in a post-os-launch mode
 * 0 - the MON is launched in a pre-os-launch mode */
#define MON_STARTUP_POST_OS_LAUNCH_MODE           BIT_VALUE(1)

/* Images used by MON */
typedef enum {
	mon_image = 0,
	thunk_image,
	mon_images_count
} mon_image_index_t;

/* This structure should be aligned on 8 byte */
#define MON_STARTUP_STRUCT_ALIGNMENT              8

typedef struct {
	uint16_t		size_of_this_struct;
	uint16_t		version_of_this_struct;

	/* number of processors/cores at install time.
	 * used to verify correctness of the bootstrap process */
	uint16_t		number_of_processors_at_install_time;

	/* number of processors/cores as was discovered by mon loader
	 * used to verify correctness of the bootstrap process */
	uint16_t		number_of_processors_at_boot_time;

	/* 64-bit aligned */

	/* number of secondary Guests */
	uint16_t		number_of_secondary_guests;

	/* size of stack for MON per processor. In 4K pages. */
	uint16_t		size_of_mon_stack;

	/* values to be used by MON to hide devices if VT-d is not accessable
	 * **** THIS FEATURE IS CURRENTLY NOT SUPPORTED **** */
	uint16_t		unsupported_vendor_id;
	uint16_t		unsupported_device_id;

	/* 64-bit aligned */

	/* set of flags, that define policies for the MON as a whole */
	uint32_t		flags;

	/* magic number of the guest, that owns all platform devices
	 * that were not assigned to any guest */
	uint32_t		default_device_owner;

	/* 64-bit aligned */

	/* magic number of the guest, that serves as OSPM.
	 * SMM code is executed in the context of this guest */
	uint32_t		acpi_owner;

	/* magic number of the guest, that process platform NMIs. */
	uint32_t		nmi_owner;

	/* 64-bit aligned */

	/* mon memory layout */
	mon_memory_layout_t	mon_memory_layout[mon_images_count];

	/* pointer to the int 15 E820 BIOS table
	 * int15_e820_memory_map_t*
	 * Loader must convert the table into the E820 extended format
	 * (each entry 24 bytes long). If BIOS-returned entry was 20 bytes long
	 * the extended attributes should be set to 0x1. */
	uint64_t		physical_memory_layout_E820;

	/* 64-bit aligned
	 * pointer to the primary guest state
	 * mon_guest_startup_t* */
	uint64_t		primary_guest_startup_state;

	/* 64-bit aligned
	 * pointer to the array of secondary guest states
	 * size of array is number_of_secondary_guests
	 * mon_guest_startup_t* */
	uint64_t		secondary_guests_startup_state_array;

	/* 64-bit aligned
	 * Debug parameters */
	mon_debug_params_t	debug_params;

	/* 64-bit aligned
	 * Active cpu local apic ids */
	uint8_t			cpu_local_apic_ids[ALIGN_FORWARD(
							   MON_MAX_CPU_SUPPORTED,
							   8)];

	/* 64-bit aligned
	 * address in INT vector table which is not being used
	 * to save INT15 hookup code for MON reserved memory in E820
	 * only useful for legacy boot (not useful for UEFI or SFI)
	 * so it will be 0 if not in legacy boot
	 * or it will be -1 if not found
	 */
	uint32_t	int15_handler_address;
	/* padding for 64-bit alignment */
	uint32_t	int15_padding;
} PACKED mon_startup_struct_t;

/*--------------------------------------------------------------------------
 *
 * mon_application_params_struct_t: Application Parameters
 *
 * Top level structure that describes application parameters.
 * Used to pass application-related install data from installer to MON-based
 * app.
 *
 *-------------------------------------------------------------------------- */

#define MON_APPLICATION_PARAMS_STRUCT_VERSION   1

typedef struct {
	/* overall, including all params */
	uint32_t	size_of_this_struct;
	/* number of params that will follow */
	uint32_t	number_of_params;
	/* random generated id to avoid mon shutdown by others */
	uint64_t	session_id;
	/* page entry list for the additional heap */
	uint64_t	address_entry_list;
	uint64_t	entry_number;
	uint64_t	fadt_gpa;
	uint64_t	dmar_gpa;
} PACKED mon_application_params_struct_t;

/*=========================================================================
 *
 * MON entry point itself. Must be called by MON loader once for each
 * processor/core in the platform. Parameters to the entry point are different
 * for BSP (boot strap processor) and for each AP (application processor)
 *
 * The order of the calls between processors is not defined, assuming that
 * this function will be called on all number_of_processors defined in the
 * mon_startup_struct_t.
 *
 * Never returns.
 *
 *========================================================================== */

void CDECL mon_main(
	/* logical local apic ID of the current processor
	 * 0 - BSP, != 0 - AP */
	uint32_t local_apic_id,
	/* mon_startup_struct_t should be passed only for BSP */
	uint64_t startup_data,
	/* mon_application_params_struct_t should be passed only
	 * for BSP */
	uint64_t application_params,
	/* must be 0 */
	uint64_t reserved);


#endif  /* _MON_STARTUP_H_ */
