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

#ifndef _MSR_DEFS_H_
#define _MSR_DEFS_H_

/*
 * Performance Counter MSR Indexes
 */
#define IA32_MSR_CRU_ESCR1      0x3B9
#define IA32_MSR_IQ_CCCR2       0x36E
#define IA32_MSR_IQ_COUNTER2    0x30E

/*
 * Standard MSR Indexes
 */
#define IA32_INVALID_MSR_INDEX          ((uint32_t)0xffffffff)
#define IA32_MSR_TIME_STAMP_COUNTER     ((uint32_t)0x010)
#define IA32_MSR_APIC_BASE              (0x01B)
#define IA32_MSR_FEATURE_CONTROL        ((uint32_t)0x03A)
#define IA32_MSR_x2APIC_BASE            ((uint32_t)0x800)
#define IA32_MSR_SYSENTER_CS            ((uint32_t)0x174)
#define IA32_MSR_SYSENTER_ESP           ((uint32_t)0x175)
#define IA32_MSR_SYSENTER_EIP           ((uint32_t)0x176)
#define IA32_MSR_MISC_ENABLE            ((uint32_t)0x1A0)

#define IA32_MSR_DEBUGCTL               ((uint32_t)0x1D9)
#define IA32_MSR_DEBUGCTL_LBR                   BIT_VALUE64(0)
#define IA32_MSR_DEBUGCTL_BTF                   BIT_VALUE64(1)
#define IA32_MSR_DEBUGCTL_TR                    BIT_VALUE64(6)
#define IA32_MSR_DEBUGCTL_BTS                   BIT_VALUE64(7)
#define IA32_MSR_DEBUGCTL_BTINT                 BIT_VALUE64(8)
#define IA32_MSR_DEBUGCTL_BTS_OFF_OS            BIT_VALUE64(9)
#define IA32_MSR_DEBUGCTL_BTS_OFF_USR           BIT_VALUE64(10)
#define IA32_MSR_DEBUGCTL_FREEZE_LBRS_ON_PMI    BIT_VALUE64(11)
#define IA32_MSR_DEBUGCTL_FREEZE_PERFMON_ON_PMI BIT_VALUE64(12)
#define IA32_MSR_DEBUGCTL_FREEZE_WHILE_SMM_EN   BIT_VALUE64(14)
#define IA32_MSR_DEBUGCTL_RESERVED                                             \
	~(IA32_MSR_DEBUGCTL_LBR                                                \
	  | IA32_MSR_DEBUGCTL_BTF                                                \
	  | IA32_MSR_DEBUGCTL_TR                                                 \
	  | IA32_MSR_DEBUGCTL_BTS                                                \
	  | IA32_MSR_DEBUGCTL_BTINT                                              \
	  | IA32_MSR_DEBUGCTL_BTS_OFF_OS                                         \
	  | IA32_MSR_DEBUGCTL_BTS_OFF_USR                                        \
	  | IA32_MSR_DEBUGCTL_FREEZE_LBRS_ON_PMI                                 \
	  | IA32_MSR_DEBUGCTL_FREEZE_PERFMON_ON_PMI                              \
	  | IA32_MSR_DEBUGCTL_FREEZE_WHILE_SMM_EN)

#define IA32_MSR_PAT                    ((uint32_t)0x277)

#define IA32_MSR_PERF_GLOBAL_CTRL       ((uint32_t)0x38F)
#define IA32_MSR_PERF_GLOBAL_CTRL_PMC0          BIT_VALUE64(0)
#define IA32_MSR_PERF_GLOBAL_CTRL_PMC1          BIT_VALUE64(1)
#define IA32_MSR_PERF_GLOBAL_CTRL_FIXED_CTR0    BIT_VALUE64(31)
#define IA32_MSR_PERF_GLOBAL_CTRL_FIXED_CTR1    BIT_VALUE64(32)
#define IA32_MSR_PERF_GLOBAL_CTRL_FIXED_CTR2    BIT_VALUE64(33)
#define IA32_MSR_PERF_GLOBAL_CTRL_RESERVED                                     \
	(IA32_MSR_PERF_GLOBAL_CTRL_PMC0                                       \
	 | IA32_MSR_PERF_GLOBAL_CTRL_PMC1                                       \
	 | IA32_MSR_PERF_GLOBAL_CTRL_FIXED_CTR0                                 \
	 | IA32_MSR_PERF_GLOBAL_CTRL_FIXED_CTR1                                 \
	 | IA32_MSR_PERF_GLOBAL_CTRL_FIXED_CTR2)

#define IA32_MSR_PEBS_ENABLE            ((uint32_t)0x3F1)
#define IA32_MSR_EFER                   ((uint32_t)0xC0000080)

#define IA32_MSR_FS_BASE                ((uint32_t)0xC0000100)
#define IA32_MSR_GS_BASE                ((uint32_t)0xC0000101)

/*
 * MTRR MSR Indexes
 */
#define IA32_MSR_MTRRCAP               0xFE
#define IA32_MSR_MTRR_DEF_TYPE         0x2FF
#define IA32_MSR_VARIABLE_MTRR         0x200
#define IA32_MSR_FIXED_MTRR_64K_00000  0x250
#define IA32_MSR_FIXED_MTRR_16K_80000  0x258
#define IA32_MSR_FIXED_MTRR_16K_A0000  0x259
#define IA32_MSR_FIXED_MTRR_4K_C0000   0x268
#define IA32_MSR_FIXED_MTRR_4K_C8000   0x269
#define IA32_MSR_FIXED_MTRR_4K_D0000   0x26A
#define IA32_MSR_FIXED_MTRR_4K_D8000   0x26B
#define IA32_MSR_FIXED_MTRR_4K_E0000   0x26C
#define IA32_MSR_FIXED_MTRR_4K_E8000   0x26D
#define IA32_MSR_FIXED_MTRR_4K_F0000   0x26E
#define IA32_MSR_FIXED_MTRR_4K_F8000   0x26F

/*
 * Microcode Update MSR Indexs
 */
#define IA32_MSR_BIOS_SIGNATURE      0x8B
#define IA32_MSR_BIOS_UPDATE_TRIGGER 0x79

/*
 * Yonah/Merom specific MSRs
 */
#define IA32_MSR_PMG_IO_CAPTURE      0xE4

#endif    /* _MSR_DEFS_H_ */
