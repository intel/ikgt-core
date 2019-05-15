/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef _NESTED_VT_INTERNAL_H_
#define _NESTED_VT_INTERNAL_H_

#define GUEST_L1 0
#define GUEST_L2 1

#define VMX_ON 1
#define VMX_OFF 0

typedef struct nestedvt_data {
	guest_cpu_handle_t gcpu;
	uint64_t gvmcs_gpa;
	uint64_t *gvmcs;
	uint8_t guest_layer;
	uint8_t vmx_on_status;
	uint8_t pad[6];
	struct nestedvt_data *next;
} nestedvt_data_t;


/* API of nested_vt.c */
nestedvt_data_t *get_nestedvt_data(guest_cpu_handle_t gcpu);


/* API of vmx_instr_common.c */
void vm_succeed(guest_cpu_handle_t gcpu);


/* API of vmxon_vmxoff_vmexit.c */
void vmxon_vmexit(guest_cpu_handle_t gcpu);
void vmxoff_vmexit(guest_cpu_handle_t gcpu);


/* API of vmptrld_vmptrst_vmexit.c */
void vmptrld_vmexit(guest_cpu_handle_t gcpu);
void vmptrst_vmexit(guest_cpu_handle_t gcpu);


/* API of vmread_vmwrite_vmexit.c */
void vmread_vmexit(guest_cpu_handle_t gcpu);
void vmwrite_vmexit(guest_cpu_handle_t gcpu);

#endif /* _NESTED_VT_INTERNAL_H_ */
