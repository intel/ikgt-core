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

#ifndef _FILE_CODES_H_
#define _FILE_CODES_H_

/* appliances\tsc_deadline_emulator\src */
#define TSCDT_DRV_C                      1001
#define TSCDT_EMULATOR_C                 1002

/* common\pci */
#define PCI_UTILS_C                      1003

/* common\utils */
#define BASE64_C                         1004

/* mon */
#define MON_C                            1005

/* mon\acpi */
#define MON_ACPI_C                       1006
#define MON_ACPI_PM_C                    1008

/* mon\arch */
#define E820_ABSTRACTION_C               1009
#define MTRRS_ABSTRACTION_C              1010
#define PAT_MANAGER_C                    1011

/* mon\dbg */
#define CLI_MONITOR_C                    1012
#define GDB_STUB_C                       1013
#define VMDB_C                           1014
#define VMENTER_CHECKS_C                 1015
#define VMX_TRACE_C                      1016

/* mon\emulator\emulator64 */
#define EMULATOR64_DBG_C                 1017
#define EMULATOR64_HANDLERS_C            1018
#define EMULATOR64_IF_C                  1019

/* mon\guest */
#define GUEST_C                          1020
#define GUEST_CONTROL_C                  1021
#define GUEST_PCI_CONFIGURATION_C        1022

/* mon\guest\guest_cpu */
#define GUEST_CPU_C                      1023
#define GUEST_CPU_ACCESS_C               1024
#define GUEST_CPU_CONTROL_C              1025
#define GUEST_CPU_SWITCH_C               1026
#define GUEST_CPU_VMENTER_EVENT_C        1027
#define UNRESTRICTED_GUEST_C             1028

/* mon\guest\scheduler */
#define SCHEDULER_C                      1029

/* mon\host */
#define DEVICE_DRIVERS_MANAGER_C         1030
#define HOST_CPU_C                       1031
#define ISR_C                            1032
#define POLICY_MANAGER_C                 1033
#define SHAREDMEMORYTEST_C               1034
#define TRIAL_EXEC_C                     1035
#define MON_GLOBALS_C                    1036

/* mon\host\hw */
#define _8259A_PIC_C                     1037
#define HOST_PCI_CONFIGURATION_C         1038
#define HW_UTILS_C                       1039
#define LOCAL_APIC_C                     1040
#define RESET_C                          1041
#define VMCS_INIT_C                      1042

/* mon\host\hw\em64t */
#define EM64T_GDT_C                      1043

/* mon\ipc */
#define IPC_C                            1044
#define IPC_API_C                        1045

/* mon\libc */
#define MON_SERIAL_LIBC                  1046

/* mon\memory\ept */
#define EPT_C                            1047
#define EPT_HW_LAYER_C                   1048

/* mon\memory\memory_manager */
#define FLAT_PAGE_TABLES_C               1049
#define GPM_C                            1050
#define HOST_MEMORY_MANAGER_C            1051
#define MEMORY_ADDRESS_MAPPER_C          1052
#define PAGE_WALKER_C                    1053
#define POOL_C                           1054
#define MON_STACK_C                      1055

/* mon\memory\vtlb 1058 to 1061 */

/* mon\samples\guest_create_addon */
#define GUEST_CREATE_ADDON_C             1062

/* mon\startup */
#define ADDONS_C                         1063
#define COPY_INPUT_STRUCTS_C             1064
#define CREATE_GUESTS_C                  1004
#define LAYOUT_HOST_MEMORY_FOR_MBR_LOADER_C 1065
#define PARSE_ELF_IMAGE_C                1066
#define PARSE_PE_IMAGE_C                 1067
#define ELF_INFO_C                       1068

/* mon\utils */
#define CACHE64_C                        1069
#define EVENT_MGR_C                      1070
#define HASH64_C                         1071
#define HEAP_C                           1072
#define LOCK_C                           1073
#define MEMORY_ALLOCATOR_C               1074

/* mon\vmexit */
#define VMCALL_C                         1075
#define VMEXIT_C                         1076
#define VMEXIT_ANALYSIS_C                1077
#define VMEXIT_CPUID_C                   1078
#define VMEXIT_CR_ACCESS_C               1079
#define VMEXIT_DBG_C                     1080
#define VMEXIT_DTR_TR_ACCESS_C           1081
#define VMEXIT_EPT_C                     1082
#define VMEXIT_INIT_C                    1083
#define VMEXIT_INTERRUPT_EXCEPTION_NMI_C 1084
#define VMEXIT_IO_C                      1085
#define VMEXIT_MSR_C                     1086
#define VMEXIT_TRIPLE_fault_C            1087
#define VMEXIT_VMX_C                     1088
#define VMX_TEARDOWN_C                   1089

/* mon\mon_io */
#define VIRTUAL_IO_C                     1090
#define VIRTUAL_SERIAL_C                 1091
#define MON_IO_C                         1092
#define MON_IO_CHANNEL_C                 1093
#define MON_IO_MUX_C                     1094
#define MON_SERIAL_C                     1095

/* mon\vmx */
#define VMCS_C                           1096
#define VMCS_ACTUAL_C                    1097
#define VMCS_HIERARCHY_C                 1098
#define VMCS_MERGE_SPLIT_C               1099
#define VMCS_SW_OBJECT_C                 1100
#define VMX_TIMER_C                      1101
#define VMX_NMI_C                        1102

/* mon\vtd */
#define VTD_C                            1103
#define VTD_ACPI_DMAR_C                  1104
#define VTD_DOMAIN_C                     1105
#define VTD_HW_LAYER_C                   1106

/* mon\memory\ept */
#define FVS_C                            1108
#define VE_C                             1230

/* mon\profiling */
#define PROFILING_C                      2000

#endif
