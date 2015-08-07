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

#ifndef _VMEXIT_MSR_H_
#define _VMEXIT_MSR_H_

#include "list.h"

/* return TRUE if instruction was executed, FAULSE in case of exception */
typedef boolean_t (*msr_access_handler_t) (guest_cpu_handle_t gcpu,
					   msr_id_t msr_id, uint64_t *p_value,
					   void *context);

typedef struct {
	uint8_t		*msr_bitmap;
	list_element_t	msr_list[1];
} msr_vmexit_control_t;

/*-----------------------------------------------------------------------*
*  FUNCTION : msr_vmexit_on_all()
*  PURPOSE  : Turns VMEXIT on all ON/OFF
*  ARGUMENTS: guest_cpu_handle_t gcpu
*           : boolean_t enable
*  RETURNS  : none, must succeed.
*-----------------------------------------------------------------------*/
void msr_vmexit_on_all(guest_cpu_handle_t gcpu, boolean_t enable);

/*-----------------------------------------------------------------------*
*  FUNCTION : msr_vmexit_guest_setup()
*  PURPOSE  : Allocates structures for MSR virtualization
*           : Must be called prior any other function from the package on this
*             gcpu,
*           : but after gcpu VMCS was loaded
*  ARGUMENTS: guest_handle_t guest
*  RETURNS  : none, must succeed.
*-----------------------------------------------------------------------*/
void msr_vmexit_guest_setup(guest_handle_t guest);

/*-----------------------------------------------------------------------*
*  FUNCTION : msr_vmexit_activate()
*  PURPOSE  : Register MSR related structures with HW (VMCS)
*  ARGUMENTS: guest_cpu_handle_t gcpu
*  RETURNS  : none, must succeed.
*-----------------------------------------------------------------------*/
void msr_vmexit_activate(guest_cpu_handle_t gcpu);

/*-----------------------------------------------------------------------*
*  FUNCTION : mon_msr_vmexit_handler_register()
*  PURPOSE  : Register specific MSR handler with VMEXIT
*  ARGUMENTS: guest_handle_t        guest
*           : msr_id_t              msr_id
*           : msr_access_handler_t  msr_handler,
*           : rw_access_t           access
*           : void               *context
*  RETURNS  : MON_OK if succeeded
*-----------------------------------------------------------------------*/
mon_status_t mon_msr_vmexit_handler_register(guest_handle_t guest,
					     msr_id_t msr_id,
					     msr_access_handler_t msr_handler,
					     rw_access_t access,
					     void *context);

/*-----------------------------------------------------------------------*
*  FUNCTION : msr_vmexit_handler_unregister()
*  PURPOSE  : Unregister specific MSR VMEXIT handler
*  ARGUMENTS: guest_handle_t  guest
*           : msr_id_t        msr_id
*  RETURNS  : MON_OK if succeeded
*-----------------------------------------------------------------------*/
mon_status_t msr_vmexit_handler_unregister(guest_handle_t guest,
					   msr_id_t msr_id,
					   rw_access_t access);

/*-----------------------------------------------------------------------*
*  FUNCTION : msr_guest_access_inhibit()
*  PURPOSE  : Install handler which prevents access to MSR from the guest space
*  ARGUMENTS: guest_handle_t  guest
*           : msr_id_t        msr_id
*  RETURNS  : MON_OK if succeeded
*-----------------------------------------------------------------------*/
mon_status_t msr_guest_access_inhibit(guest_handle_t guest, msr_id_t msr_id);

/*-----------------------------------------------------------------------*
*  FUNCTION : msr_trial_access()
*  PURPOSE  : Try to execute real MSR read/write
*           : If exception was generated, inject it into guest
*  ARGUMENTS: guest_cpu_handle_t    gcpu
*           : msr_id_t              msr_id
*           : rw_access_t           access
*  RETURNS  : TRUE if instruction was executed, FALSE otherwise (fault occured)
*-----------------------------------------------------------------------*/
boolean_t msr_trial_access(guest_cpu_handle_t gcpu,
			   msr_id_t msr_id,
			   rw_access_t access,
			   uint64_t *msr_value);

/*-----------------------------------------------------------------------*
*  FUNCTION : mon_vmexit_enable_disable_for_msr_in_exclude_list()
*  PURPOSE  : enable/disable msr read/write vmexit for msrs in the exclude list
*  ARGUMENTS: guest_cpu_handle_t    gcpu
*           : msr_id_t              msr_id
*           : rw_access_t           access
*           : boolean_t TRUE to enable write/read vmexit, FALSE to disable vmexit
*  RETURNS  : TRUE if parameters are correct.
*-----------------------------------------------------------------------*/
boolean_t mon_vmexit_register_unregister_for_efer(guest_handle_t guest,
						  msr_id_t msr_id,
						  rw_access_t access,
						  boolean_t reg_dereg);

#endif                          /* _VMEXIT_MSR_H_ */
