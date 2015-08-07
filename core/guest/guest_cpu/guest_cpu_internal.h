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

#ifndef _GUEST_CPU_INTERNAL_H
#define _GUEST_CPU_INTERNAL_H

#include "mon_defs.h"
#include "guest_cpu.h"
#include "guest_cpu_control.h"
#include <common_libc.h>
#include "vmcs_hierarchy.h"
#include "vmcs_actual.h"
#include "flat_page_tables.h"

/****************************************************************************
 *
 * Guest CPU
 *
 * Guest CPU may be in 2 different modes:
 * 16 mode - run under emulator
 * any other mode - run native
 *
 **************************************************************************** */

/* -------------------------- types ----------------------------------------- */

/****************************************************************************
 *
 * Defines save area for guest registers, not saved in VMCS
 *
 **************************************************************************** */
/*
 * Data structure to access IA-32 General Purpose Registers referenced by VM
 * Exit Handlers
 * This is also the structure used to save/restore general purpose registers in
 * assembly code for the VMEXIT and VMENTER handlers
 */

/* Do not show to the guest the real values of the following bits +
 * perform VMEXIT on writes to this bits */
#define GCPU_CR4_MON_CONTROLLED_BITS     (CR4_PAE | CR4_SMXE)
#define GCPU_CR0_MON_CONTROLLED_BITS     0

/* main save area */
#define CR2_SAVE_AREA IA32_REG_RSP
#define CR3_SAVE_AREA IA32_REG_RFLAGS
#define CR8_SAVE_AREA IA32_REG_RIP

typedef struct {
	uint64_t	pat;
	uint64_t	padding;
} PACKED mon_other_msrs_t;

typedef struct {
	/* the next 2 fields must be the first in this structure because
	 * they are referenced in assembler */
	/* note:
	 * RSP, RIP and RFLAGS are not used - use VMCS
	 * RSP entry is used for CR2
	 * RFLAGS entry is used for CR3
	 * RIP entry is used for CR8 */
	mon_gp_registers_t	gp;
	ALIGN16(mon_xmm_registers_t, xmm); /* restored AFTER FXRSTOR */

	/* not referenced in assembler */
	mon_debug_register_t	debug; /* dr7 is not used - use VMCS */

	/* must be aligned on 16-byte boundary */
	ALIGN16(uint8_t, fxsave_area[512]);

	mon_other_msrs_t	temporary_cached_msrs;
} PACKED guest_cpu_save_area_t;


typedef struct {
	uint64_t	ve_info_hva;
	uint64_t	ve_info_hpa;
	boolean_t	ve_enabled;
	uint8_t		pad[4];
} ve_descriptor_t;

/* per-cpu data/state */
typedef struct {
	uint64_t	vmentry_eptp;

	boolean_t	enabled;
	uint32_t	padding;
} fvs_cpu_descriptor_t;

/* invalid CR3 value used to specify that CR3_SAVE_AREA is not up-to-date */
#define INVALID_CR3_SAVED_VALUE     UINT64_ALL_ONES

typedef struct guest_cpu_t {
	/* save_area and vmcs must come first due to alignment. Do not move ! */
	guest_cpu_save_area_t		save_area;
	vmcs_hierarchy_t		vmcs_hierarchy;
	guest_handle_t			guest_handle;
	fpt_flat_page_tables_handle_t	active_flat_pt_handle;
	uint64_t			active_flat_pt_hpa;

	virtual_cpu_id_t		vcpu;
	uint8_t				last_guest_level;       /* get values from GUEST_LEVEL */
	uint8_t				next_guest_level;       /* get values from GUEST_LEVEL */
	uint8_t				state_flags;            /* gcpu_state_t */
	uint8_t				caching_flags;          /* gcpu_caching_flags_t */
	uint32_t			hw_enforcements;
	uint8_t				merge_required;
	uint8_t				cached_activity_state; /* Used to determine activity state switch */
	uint8_t				pad;
	uint8_t				use_host_page_tables;

	gcpu_vmexit_controls_t		vmexit_setup;
	struct guest_cpu_t		*next_gcpu;
	func_gcpu_resume_t		resume_func;
	func_gcpu_vmexit_t		vmexit_func;
	void				*vmdb;  /* guest debugger handler */
	void				*timer;

	gpm_handle_t			active_gpm;

	fvs_cpu_descriptor_t		fvs_cpu_desc;

	uint32_t			trigger_log_event;
	uint8_t				pad2[4];
	ve_descriptor_t			ve_desc;
} guest_cpu_t;

/* ----------------------- state ------------------------------------------- */

typedef enum {
	GCPU_EMULATOR_FLAG = 0,                         /* 1 - emulator is active, 0 - native */
	GCPU_FLAT_PAGES_TABLES_32_FLAG,                 /* 1 - 32bit flat page tables in use */
	GCPU_FLAT_PAGES_TABLES_64_FLAG,                 /* 1 - 64bit flat page tables in use */
	GCPU_ACTIVITY_STATE_CHANGED_FLAG,               /* 1 - Activity/Sleep state changed */
	/* 1 - VMEXIT caused by exception. Have to handle prior event injection/resume */
	GCPU_EXCEPTION_RESOLUTION_REQUIRED_FLAG,
	GCPU_EXPLICIT_EMULATOR_REQUEST,                 /* 1 - emulator run was requested explicitly */
	/* 1 - Unrestricted guest enabled, 0 - unrestreicted guest disabled */
	GCPU_UNRESTRICTED_GUEST_FLAG,
	GCPU_IMPORTANT_EVENT_OCCURED_FLAG = 7,          /* 1 - CR0/EFER changed */
} gcpu_state_t;

#define SET_EMULATOR_FLAG(gcpu)             \
	BIT_SET((gcpu)->state_flags, GCPU_EMULATOR_FLAG)
#define CLR_EMULATOR_FLAG(gcpu)             \
	BIT_CLR((gcpu)->state_flags, GCPU_EMULATOR_FLAG)
#define GET_EMULATOR_FLAG(gcpu)             \
	BIT_GET((gcpu)->state_flags, GCPU_EMULATOR_FLAG)

#define SET_FLAT_PAGES_TABLES_32_FLAG(gcpu) \
	BIT_SET((gcpu)->state_flags, GCPU_FLAT_PAGES_TABLES_32_FLAG)
#define CLR_FLAT_PAGES_TABLES_32_FLAG(gcpu) \
	BIT_CLR((gcpu)->state_flags, GCPU_FLAT_PAGES_TABLES_32_FLAG)
#define GET_FLAT_PAGES_TABLES_32_FLAG(gcpu) \
	BIT_GET((gcpu)->state_flags, GCPU_FLAT_PAGES_TABLES_32_FLAG)

#define SET_FLAT_PAGES_TABLES_64_FLAG(gcpu) \
	BIT_SET((gcpu)->state_flags, GCPU_FLAT_PAGES_TABLES_64_FLAG)
#define CLR_FLAT_PAGES_TABLES_64_FLAG(gcpu) \
	BIT_CLR((gcpu)->state_flags, GCPU_FLAT_PAGES_TABLES_64_FLAG)
#define GET_FLAT_PAGES_TABLES_64_FLAG(gcpu) \
	BIT_GET((gcpu)->state_flags, GCPU_FLAT_PAGES_TABLES_64_FLAG)

#define SET_ACTIVITY_STATE_CHANGED_FLAG(gcpu) \
	BIT_SET((gcpu)->state_flags, GCPU_ACTIVITY_STATE_CHANGED_FLAG)
#define CLR_ACTIVITY_STATE_CHANGED_FLAG(gcpu) \
	BIT_CLR((gcpu)->state_flags, GCPU_ACTIVITY_STATE_CHANGED_FLAG)
#define GET_ACTIVITY_STATE_CHANGED_FLAG(gcpu) \
	BIT_GET((gcpu)->state_flags, GCPU_ACTIVITY_STATE_CHANGED_FLAG)

#define SET_EXCEPTION_RESOLUTION_REQUIRED_FLAG(gcpu) \
	BIT_SET((gcpu)->state_flags, GCPU_EXCEPTION_RESOLUTION_REQUIRED_FLAG)
#define CLR_EXCEPTION_RESOLUTION_REQUIRED_FLAG(gcpu) \
	BIT_CLR((gcpu)->state_flags, GCPU_EXCEPTION_RESOLUTION_REQUIRED_FLAG)
#define GET_EXCEPTION_RESOLUTION_REQUIRED_FLAG(gcpu) \
	BIT_GET((gcpu)->state_flags, GCPU_EXCEPTION_RESOLUTION_REQUIRED_FLAG)

#define SET_EXPLICIT_EMULATOR_REQUEST_FLAG(gcpu) \
	BIT_SET((gcpu)->state_flags, GCPU_EXPLICIT_EMULATOR_REQUEST)
#define CLR_EXPLICIT_EMULATOR_REQUEST_FLAG(gcpu) \
	BIT_CLR((gcpu)->state_flags, GCPU_EXPLICIT_EMULATOR_REQUEST)
#define GET_EXPLICIT_EMULATOR_REQUEST_FLAG(gcpu)  \
	BIT_GET((gcpu)->state_flags, GCPU_EXPLICIT_EMULATOR_REQUEST)

#define SET_IMPORTANT_EVENT_OCCURED_FLAG(gcpu) \
	BIT_SET((gcpu)->state_flags, GCPU_IMPORTANT_EVENT_OCCURED_FLAG)
#define CLR_IMPORTANT_EVENT_OCCURED_FLAG(gcpu) \
	BIT_CLR((gcpu)->state_flags, GCPU_IMPORTANT_EVENT_OCCURED_FLAG)
#define GET_IMPORTANT_EVENT_OCCURED_FLAG(gcpu) \
	BIT_GET((gcpu)->state_flags, GCPU_IMPORTANT_EVENT_OCCURED_FLAG)

#define IS_MODE_EMULATOR(gcpu)    (GET_EMULATOR_FLAG(gcpu) == 1)
#define SET_MODE_EMULATOR(gcpu)   SET_EMULATOR_FLAG(gcpu)

#define IS_MODE_NATIVE(gcpu)      (1)
#define SET_MODE_NATIVE(gcpu)     CLR_EMULATOR_FLAG(gcpu)

#define IS_FLAT_PT_INSTALLED(gcpu)                \
	(GET_FLAT_PAGES_TABLES_32_FLAG(gcpu) || \
	 GET_FLAT_PAGES_TABLES_64_FLAG(gcpu))

/* ----------------------- caching ---------------------------------------- */

typedef enum {
	GCPU_FX_STATE_CACHED_FLAG = 0,
	GCPU_DEBUG_REGS_CACHED_FLAG,

	GCPU_FX_STATE_MODIFIED_FLAG,
	GCPU_DEBUG_REGS_MODIFIED_FLAG,
} gcpu_caching_flags_t;

#define SET_FX_STATE_CACHED_FLAG(gcpu)   \
	BIT_SET((gcpu)->caching_flags, GCPU_FX_STATE_CACHED_FLAG)
#define CLR_FX_STATE_CACHED_FLAG(gcpu)   \
	BIT_CLR((gcpu)->caching_flags, GCPU_FX_STATE_CACHED_FLAG)
#define GET_FX_STATE_CACHED_FLAG(gcpu)   \
	BIT_GET((gcpu)->caching_flags, GCPU_FX_STATE_CACHED_FLAG)

#define SET_DEBUG_REGS_CACHED_FLAG(gcpu) \
	BIT_SET((gcpu)->caching_flags, GCPU_DEBUG_REGS_CACHED_FLAG)
#define CLR_DEBUG_REGS_CACHED_FLAG(gcpu) \
	BIT_CLR((gcpu)->caching_flags, GCPU_DEBUG_REGS_CACHED_FLAG)
#define GET_DEBUG_REGS_CACHED_FLAG(gcpu) \
	BIT_GET((gcpu)->caching_flags, GCPU_DEBUG_REGS_CACHED_FLAG)

#define SET_FX_STATE_MODIFIED_FLAG(gcpu) \
	BIT_SET((gcpu)->caching_flags, GCPU_FX_STATE_MODIFIED_FLAG)
#define CLR_FX_STATE_MODIFIED_FLAG(gcpu) \
	BIT_CLR((gcpu)->caching_flags, GCPU_FX_STATE_MODIFIED_FLAG)
#define GET_FX_STATE_MODIFIED_FLAG(gcpu) \
	BIT_GET((gcpu)->caching_flags, GCPU_FX_STATE_MODIFIED_FLAG)

#define SET_DEBUG_REGS_MODIFIED_FLAG(gcpu) \
	BIT_SET((gcpu)->caching_flags, GCPU_DEBUG_REGS_MODIFIED_FLAG)
#define CLR_DEBUG_REGS_MODIFIED_FLAG(gcpu) \
	BIT_CLR((gcpu)->caching_flags, GCPU_DEBUG_REGS_MODIFIED_FLAG)
#define GET_DEBUG_REGS_MODIFIED_FLAG(gcpu) \
	BIT_GET((gcpu)->caching_flags, GCPU_DEBUG_REGS_MODIFIED_FLAG)

#define SET_ALL_MODIFIED(gcpu)     { (gcpu)->caching_flags = (uint8_t)-1; }
#define CLR_ALL_CACHED(gcpu)       { (gcpu)->caching_flags = 0; }

/* ---------------------------- globals ------------------------------------- */

/* this is a shortcut pointer for assembler code */
extern guest_cpu_save_area_t **g_guest_regs_save_area;

/* ---------------------------- internal API ----------------------------------
 */
void cache_debug_registers(const guest_cpu_t *gcpu);
void cache_fx_state(const guest_cpu_t *gcpu);

INLINE uint64_t
gcpu_get_msr_reg_internal(const guest_cpu_handle_t gcpu,
			  mon_ia32_model_specific_registers_t reg)
{
	return gcpu_get_msr_reg_internal_layered(gcpu, reg, VMCS_MERGED);
}

#define SET_CACHED_ACTIVITY_STATE(__gcpu, __value)                    \
	{ (__gcpu)->cached_activity_state = (uint8_t)(__value); }

#define GET_CACHED_ACTIVITY_STATE(__gcpu)                             \
	((ia32_vmx_vmcs_guest_sleep_state_t)((__gcpu)->cached_activity_state))

#define IS_STATE_INACTIVE(activity_state)                             \
	(IA32_VMX_VMCS_GUEST_SLEEP_STATE_WAIT_FOR_SIPI == (activity_state))

#endif  /* _GUEST_CPU_INTERNAL_H */
