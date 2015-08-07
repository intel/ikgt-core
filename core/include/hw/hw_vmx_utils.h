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

#ifndef _HW_VMX_UTILS_H_
#define _HW_VMX_UTILS_H_

#include "mon_defs.h"

/*************************************************************************
*
* wrappers for VMX instructions
*
*************************************************************************/

extern void __vmx_vmptrst(uint64_t *vmcs_physical_address);
extern unsigned char __vmx_vmptrld(uint64_t *vmcs_physical_address);
extern unsigned char __vmx_vmclear(uint64_t *vmcs_physical_address);

extern unsigned char __vmx_vmlaunch(void);
extern unsigned char __vmx_vmresume(void);

extern unsigned char __vmx_vmwrite(size_t field, size_t field_value);
extern unsigned char __vmx_vmread(size_t field, size_t *field_value);

extern unsigned char __vmx_on(uint64_t *vmcs_physical_address);
extern void __vmx_off(void);

/*
 * General note: all functions that return value return the same values
 *
 * 0 - The operation succeeded.
 * 1 - The operation failed with extended status available in the
 * VM-instruction error field of the current VMCS.
 * 2 - The operation failed without status available.
 */
typedef enum {
	HW_VMX_SUCCESS = 0,
	HW_VMX_FAILED_WITH_STATUS = 1,
	HW_VMX_FAILED = 2
} hw_vmx_ret_value_t;

/*-------------------------------------------------------------------------
 *
 * VMX ON/OFF
 *
 * hw_vmx_ret_value_t hw_vmx_on( uint64_t* vmx_on_region_physical_address_ptr )
 * void hw_vmx_off( void )
 *
 * vmx_on_region_physical_address_ptr is a POINTER TO the VMXON region POINTER.
 * The VMXON region POINTER must be 4K page aligned. Size of the
 * region is the same as VMCS region size and may be found in IA32_VMX_BASIC
 * MSR
 *
 *------------------------------------------------------------------------- */
#define hw_vmx_on(_vmx_on_region_physical_address_ptr)                     \
	((hw_vmx_ret_value_t)__vmx_on(_vmx_on_region_physical_address_ptr))
#define hw_vmx_off()                   __vmx_off()

/*-------------------------------------------------------------------------
 *
 * Read/write current VMCS pointer
 *
 * hw_vmx_ret_value_t hw_vmx_set_current_vmcs( uint64_t*
 * vmcs_region_physical_address_ptr )
 * void hw_vmx_get_current_vmcs( uint64_t* vmcs_region_physical_address_ptr )
 *
 * vmcs_region_physical_address_ptr is a POINTER TO the VMCS region POINTER.
 * The VMCS region POINTER must be 4K page aligned. Size of the
 * region is the same as VMCS region size and may be found in IA32_VMX_BASIC
 * MSR
 *
 *------------------------------------------------------------------------- */
#define hw_vmx_set_current_vmcs(_vmcs_region_physical_address_ptr)         \
	((hw_vmx_ret_value_t)__vmx_vmptrld(_vmcs_region_physical_address_ptr))

#define hw_vmx_get_current_vmcs(_vmcs_region_physical_address_ptr)         \
	__vmx_vmptrst(_vmcs_region_physical_address_ptr)

/*-------------------------------------------------------------------------
 *
 * Flush current VMCS data + Invalidate current VMCS pointer + Set VMCS launch
 * state
 * to the "clear" value (VMLAUNCH required)
 *
 * hw_vmx_ret_value_t hw_vmx_flush_current_vmcs( uint64_t*
 * vmcs_region_physical_address_ptr )
 *
 * 1. Save VMCS data to the given region (pointer to pointer)
 * 2. If given region is the same as the pointer, that was loaded before using
 * hw_vmx_set_current_vmcs(), the "current VMCS pointer" is set to -1
 * 3. Set the VMCS launch state to "clear", so that VMLAUCH will be required
 * to run it and not VMRESUME
 *
 * vmcs_region_physical_address_ptr is a POINTER TO the VMCS region POINTER.
 * The VMCS region POINTER must be 4K page aligned. Size of the
 * region is the same as VMCS region size and may be found in IA32_VMX_BASIC
 * MSR
 *
 *------------------------------------------------------------------------- */
#define hw_vmx_flush_current_vmcs(_vmcs_region_physical_address_ptr)       \
	((hw_vmx_ret_value_t)__vmx_vmclear(_vmcs_region_physical_address_ptr))

/*-------------------------------------------------------------------------
 *
 * Launch/resume guest using "current VMCS pointer".
 *
 * Launch should be used to first time start this guest on the current physical
 * core
 * If guest is relocated to another core, hw_vmx_flush_current_vmcs() should
 * be used on the original core and hw_vmx_launch() on the target.
 *
 * Subsequent guest resumes on the current core should be done using
 * hw_vmx_launch()
 *
 *------------------------------------------------------------------------- */
#define hw_vmx_launch_guest()     ((hw_vmx_ret_value_t)__vmx_vmlaunch())
#define hw_vmx_resume_guest()     ((hw_vmx_ret_value_t)__vmx_vmresume())
/*-------------------------------------------------------------------------
 *
 * Read/write some field in the "current VMCS"
 *
 * hw_vmx_ret_value_t hw_vmx_write_current_vmcs( size_t field_id, size_t value )
 * hw_vmx_ret_value_t hw_vmx_read_current_vmcs ( size_t field_id, size_t* value )
 *
 *------------------------------------------------------------------------- */
#define hw_vmx_write_current_vmcs(_field_id, _value)                       \
	((hw_vmx_ret_value_t)__vmx_vmwrite(_field_id, _value))

#define hw_vmx_read_current_vmcs(_field_id, _value)                        \
	((hw_vmx_ret_value_t)__vmx_vmread(_field_id, _value))

#endif    /* _HW_VMX_UTILS_H_ */
