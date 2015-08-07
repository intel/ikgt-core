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

#ifndef _VMCALL_API_H_
#define _VMCALL_API_H_

#include "mon_defs.h"

typedef enum {
	VMCALL_IS_MON_RUNNING,
	VMCALL_EMULATOR_TERMINATE,
	VMCALL_EMULATOR_CLI_ACTIVATE,
	VMCALL_EMULATOR_PUTS,
	VMCALL_REGISTER_DEVICE_DRIVER,
	VMCALL_UNREGISTER_DEVICE_DRIVER,
	VMCALL_DEVICE_DRIVER_IOCTL,
	VMCALL_DEVICE_DRIVER_ACK_NOTIFICATION,
	VMCALL_PRINT_DEBUG_MESSAGE,
	VMCALL_ADD_SHARED_MEM,
	VMCALL_REMOVE_SHARED_MEM,
	VMCALL_WRITE_STRING,
	VMCALL_PLUGIN,

	VMCALL_UPDATE_LVT,                      /* Temporary for TSC deadline debugging */

	VMCALL_LAST_USED_INTERNAL = 1024        /* must be the last */
} vmcall_id_t;

#define API_FUNCTION
#define ASM_FUNCTION
#define CDECL
#define STDCALL

#define MON_NATIVE_VMCALL_SIGNATURE                                            \
	(((uint32_t)'$' << 24)                                                     \
	 | ((uint32_t)'i' << 16)                                                     \
	 | ((uint32_t)'M' << 8)                                                      \
	 | ((uint32_t)'@' << 0)                                                      \
	)

/*---------------------------------------------------------------------------*
*  FUNCTION : hw_vmcall()
*  PURPOSE  : Call for MON service from the guest environment
*  ARGUMENTS: vmcall_id_t vmcall_id + 3 extra arguments
*  RETURNS  : MON_OK = ok, other - error code
*---------------------------------------------------------------------------*/
#ifndef MON_DRIVER_BUILD
mon_status_t ASM_FUNCTION hw_vmcall(vmcall_id_t vmcall_id,
				    void *arg1,
				    void *arg2,
				    void *arg3);
#else
mon_status_t CDECL hw_vmcall(vmcall_id_t vmcall_id,
			     void *arg1,
			     void *arg2,
			     void *arg3);
#endif

/*========================================================================== */

typedef struct {
	vmcall_id_t	vmcall_id;      /* IN must be "VMCALL_IS_MON_RUNNING" */

	uint32_t	version;        /* OUT - currently will be 0 */
} mon_is_mon_running_params_t;

/*---------------------------------------------------------------------------*
*  FUNCTION : hw_vmcall_is_mon_running()
*  PURPOSE  : Call for MON service for quering whether MON is running
*  ARGUMENTS: param - pointer to "mon_is_mon_running_params_t" structure
*  RETURNS  : MON_OK = ok, other - error code
*
*  mon_status_t hw_vmcall_is_mon_running(mon_is_mon_running_params_t* param);
*---------------------------------------------------------------------------*/
#define hw_vmcall_is_mon_running(is_mon_running_params_ptr) \
	hw_vmcall(VMCALL_IS_MON_RUNNING, (is_mon_running_params_ptr), NULL, \
	NULL)

/*========================================================================== */
typedef struct {
	vmcall_id_t		vmcall_id;              /* IN must be "VMCALL_REGISTER_DEVICE_DRIVER" */
	boolean_t		is_initially_masked;    /* IN */
	volatile uint64_t	notification_area_gva;  /* IN pointer to boolean_t polling variable */
	uint64_t		descriptor_handle;      /* OUT */
} mon_device_driver_registration_params_t;

/*---------------------------------------------------------------------------*
 *  FUNCTION : hw_vmcall_register_driver()
 *  PURPOSE  : Call for MON service for registering device driver
 *  ARGUMENTS: param - pointer to "mon_device_driver_registration_params_t"
 *                     structure
 *  RETURNS  : MON_OK = ok, other - error code
 *
 *  mon_status_t hw_vmcall_register_driver(
 *      mon_device_driver_registration_params_t* param);
 *--------------------------------------------------------------------------*/
#define hw_vmcall_register_driver(driver_registration_params_ptr)             \
	hw_vmcall(VMCALL_REGISTER_DEVICE_DRIVER, \
	(driver_registration_params_ptr), \
	NULL, \
	NULL)

/*========================================================================== */

typedef struct {
	vmcall_id_t	vmcall_id;              /* IN must be "VMCALL_UNREGISTER_DEVICE_DRIVER" */
	uint8_t		padding[4];
	uint64_t	descriptor_handle;      /* IN descriptor_handle received upon registration */
} mon_device_driver_unregistration_params_t;

/*--------------------------------------------------------------------------*
 *  FUNCTION : hw_vmcall_unregister_driver()
 *  PURPOSE  : Call for MON service for unregistering device driver
 *  ARGUMENTS: param - pointer to "mon_device_driver_unregistration_params_t"
 *                    structure
 *  RETURNS  : MON_OK = ok, other - error code
 *
 *  mon_status_t hw_vmcall_unregister_driver(
 *      mon_device_driver_unregistration_params_t* param);
 *---------------------------------------------------------------------------*/
#define hw_vmcall_unregister_driver(driver_unregistration_params_ptr) \
	hw_vmcall(VMCALL_UNREGISTER_DEVICE_DRIVER,                        \
	(driver_unregistration_params_ptr), NULL, NULL)

/*========================================================================= */

typedef enum {
	MON_DEVICE_DRIVER_IOCTL_MASK_NOTIFICATION,
	MON_DEVICE_DRIVER_IOCTL_UNMASK_NOTIFICATION,
} mon_device_driver_ioctl_id_t;

typedef struct {
	vmcall_id_t			vmcall_id;              /* IN must be "VMCALL_DEVICE_DRIVER_IOCTL" */
	mon_device_driver_ioctl_id_t	ioctl_id;               /* IN id of the ioctl operation */
	uint64_t			descriptor_handle;      /* IN descriptor_handle received upon registration */
} mon_device_driver_ioctl_params_t;

/*--------------------------------------------------------------------------*
*  FUNCTION : hw_vmcall_driver_ioctl()
*  PURPOSE  : Call for MON service for controlling device driver
*  ARGUMENTS: param - pointer to "mon_device_driver_ioctl_params_t" structure
*  RETURNS  : MON_OK = ok, other - error code
*
*  mon_status_t hw_vmcall_driver_ioctl(mon_device_driver_ioctl_params_t* param);
*--------------------------------------------------------------------------*/
#define hw_vmcall_driver_ioctl(driver_ioctl_params_ptr) \
	hw_vmcall(VMCALL_DEVICE_DRIVER_IOCTL, \
	(driver_ioctl_params_ptr), \
	NULL, \
	NULL)

/*========================================================================= */

typedef struct {
	vmcall_id_t	vmcall_id;                              /* IN must be "VMCALL_DEVICE_DRIVER_ACK_NOTIFICATION" */
	uint8_t		padding[4];
	uint64_t	descriptor_handle;                      /* IN descriptor_handle received upon registration */
	uint64_t	compontents_that_require_attention;     /* OUT - bitmask of components that require attention */
} mon_device_driver_ack_notification_params_t;

/*---------------------------------------------------------------------------*
 *  FUNCTION : hw_vmcall_driver_ack_notification()
 *  PURPOSE  : Call for MON service for acknoledging notification
 *  ARGUMENTS: param - pointer to "DEVICE_DRIVER_ACK_NOTIFICATION_PARAMS"
 *                     structure
 *  RETURNS  : MON_OK = ok, other - error code
 *
 *  mon_status_t hw_vmcall_driver_ack_notification(
 *                               DEVICE_DRIVER_ACK_NOTIFICATION_PARAMS* param);
 *--------------------------------------------------------------------------*/
#define hw_vmcall_driver_ack_notification(driver_ioctl_params_ptr) \
	hw_vmcall(VMCALL_DEVICE_DRIVER_ACK_NOTIFICATION,       \
	(driver_ioctl_params_ptr), NULL, NULL)

/*========================================================================== */

#define MON_MAX_DEBUG_MESSAGE_SIZE      252
typedef struct {
	vmcall_id_t	vmcall_id; /* IN - must have "VMCALL_PRINT_DEBUG_MESSAGE" value */
	char		message[MON_MAX_DEBUG_MESSAGE_SIZE];
} mon_print_debug_message_params_t;

/*---------------------------------------------------------------------------*
 *  FUNCTION : hw_vmcall_print_debug_message()
 *  PURPOSE  : Call for MON service for printing debug message
 *  ARGUMENTS: param - pointer to "mon_print_debug_message_params_t" structure
 *  RETURNS  : MON_OK = ok, other - error code
 *
 *  mon_status_t hw_vmcall_print_debug_message(mon_print_debug_message_params_t*
 *                                           param);
 *--------------------------------------------------------------------------*/
#define hw_vmcall_print_debug_message(debug_message_params_ptr) \
	hw_vmcall(VMCALL_PRINT_DEBUG_MESSAGE, \
	(debug_message_params_ptr), \
	NULL, \
	NULL)

/***********************************************************************
*  some structures for parameters pass from driver to Mon
*  for testing the driver.
***********************************************************************/
typedef struct {
	vmcall_id_t	vmcall_id;
	uint8_t		padding[4];
	uint64_t	guest_virtual_address;
	uint32_t	buf_size;
	int		mon_mem_handle;
	mon_status_t	status;
	uint8_t		padding2[4];
} mon_add_shared_mem_params_t;

typedef struct {
	vmcall_id_t	vmcall_id;
	int		mon_mem_handle;
	mon_status_t	status;
} mon_remove_shared_mem_params_t;

typedef struct {
	vmcall_id_t	vmcall_id;
	int		mon_mem_handle;
	char		buf[100];
	uint32_t	len;
	mon_status_t	status;
} mon_write_string_params_t;

#endif    /* _VMCALL_API_H_ */
