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

#include "mon_defs.h"
#include "mon_dbg.h"
#include "heap.h"
#include "hw_utils.h"
#include "hw_interlocked.h"
#include "hw_vmx_utils.h"
#include "em64t_defs.h"
#include "ia32_defs.h"
#include "gdt.h"
#include "isr.h"
#include "guest_cpu.h"
#include "mon_objects.h"
#include "vmcs_api.h"
#include "vmcs_init.h"
#include "scheduler.h"
#include "vmexit_io.h"
#include "ipc.h"
#include "host_memory_manager_api.h"
#include "pat_manager.h"
#include "host_cpu.h"
#include "mon_stack_api.h"
#include "mon_bootstrap_utils.h"
#include "event_mgr.h"

#include "mon_globals.h"
#include "ept.h"
#include "mon_events_data.h"

#include "unrestricted_guest.h"
#include "file_codes.h"

#include "mon_acpi.h"
#include "mon_callback.h"

#define MON_DEADLOOP()          MON_DEADLOOP_LOG(MON_ACPI_PM_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(MON_ACPI_PM_C, __condition)

extern mon_startup_struct_t mon_startup_data;

extern void mon_debug_port_clear(void);
extern void mon_io_init(void);
extern void memory_dump(const void *mem_location, uint32_t count,
			uint32_t size);
extern void mon_acpi_resume_after_s3_c_main_gnu(void);
uint32_t g_s3_resume_flag = 0;

/*------------------------------Types and Macros---------------------------*/

/* must be patched */
#define PTCH 0x00

#define PTR_TO_U32(__p) (uint32_t)(size_t)(__p)

#define WAIT_FOR_MP_CONDITION(__cond) { while (!(__cond)) hw_pause(); }

#define REALMODE_SEGMENT_SELECTOR_TO_BASE(__selector)    \
	(((uint32_t)(__selector)) << 4)

#define MAX_ACPI_CALLBACKS      10

/* See "4.7.3.2.1 PM1 Control Registers" in ACPIspec30b */
#define SLP_EN(PM1x_CNT_BLK) (((PM1x_CNT_BLK) >> 13) & 0x1)
#define SLP_TYP(PM1x_CNT_BLK) (((PM1x_CNT_BLK) >> 10) & 0x7)

/* Describes GP, flags, Segment and XDT registers as they are stored on the
 * stack. The order is important and defined partially by HW (pusha
 * implementation)and partially by hardocded piece of code. */
typedef struct {
	ia32_gdtr_t	gdtr;
	ia32_gdtr_t	idtr;
	uint16_t	gs;
	uint16_t	fs;
	uint16_t	es;
	uint16_t	ss;
	uint16_t	ds;
	uint16_t	cs;
	uint32_t	eflags;
	uint32_t	edi;
	uint32_t	esi;
	uint32_t	ebp;
	uint32_t	esp;
	uint32_t	ebx;
	uint32_t	edx;
	uint32_t	ecx;
	uint32_t	eax;
} cpu_snapshot_t;

typedef struct {
	uint8_t		code[72];
	ia32_gdtr_t	mon_gdtr[1];
} mon_s3_protected_resume_code_2_t;

typedef struct {
	/* Currently 82-byte is long enough to hold the
	 * s3_resume_real_mode_code. */
	uint8_t					real_code[82];
	ia32_gdtr_t				gdtr[1];
	uint8_t					pad[8];
	/* aligned on 16 bytes */
	uint8_t					low_memory_gdt[
		TSS_FIRST_GDT_ENTRY_OFFSET];
	mon_s3_protected_resume_code_2_t	protected_code[1];
	uint8_t					stack[sizeof(cpu_snapshot_t) +
						      16];
} mon_s3_real_mode_resume_layout_t;

typedef struct {
	uint8_t					protected_code_1[30];
	mon_s3_protected_resume_code_2_t	protected_code_2[1];
	uint8_t					stack[sizeof(cpu_snapshot_t) +
						      16];
} mon_s3_protected_resume_layout_t;

typedef struct {
	uint32_t	i32_low_memory_page;    /* address of page in low memory, used for AP bootstrap */
	uint16_t	i32_num_of_aps;         /* number of detected APs (Application Processors) */
	uint16_t	i32_pad;
	uint32_t	i32_esp[MAX_CPUS];      /* array of 32-bit SPs (SP - top of the stack) */
} init32_struct_t;

typedef struct {
	uint16_t	i64_cs;         /* 64-bit code segment selector */
	ia32_gdtr_t	i64_gdtr;       /* still in 32-bit format */
	uint64_t	i64_efer;       /* EFER minimal required value */
	uint32_t	i64_cr3;        /* 32-bit value of CR3 */
} init64_struct_t;

#ifdef BREAK_AT_FIRST_COMMAND
#define REAL_MODE_BASE_OFFSET 2
#else
#define REAL_MODE_BASE_OFFSET 0
#endif

#define REALMODE_GDTR_OFFSET   OFFSET_OF(mon_s3_real_mode_resume_layout_t, gdtr)

/* this offset is used to fix the S3 failure issue on Lenovo T410i laptop */
#define ZERO_ESP_CODE_BYTE_SIZE 3

#define REALMODE_SS_VALUE_PATCH_LOCATION                \
	(REAL_MODE_BASE_OFFSET + ZERO_ESP_CODE_BYTE_SIZE + 2)
#define REALMODE_SP_VALUE_PATCH_LOCATION                \
	(REAL_MODE_BASE_OFFSET + ZERO_ESP_CODE_BYTE_SIZE + 7)
#define REALMODE_CODE_START_ALIGNED_16_PATCH_LOCATION   \
	(REAL_MODE_BASE_OFFSET + ZERO_ESP_CODE_BYTE_SIZE + 51)
#define REALMODE_GDTR_OFFSET_PATCH_LOCATION             \
	(REAL_MODE_BASE_OFFSET + ZERO_ESP_CODE_BYTE_SIZE + 57)
#define REALMODE_PROTECTED_OFFSET_PATCH_LOCATION        \
	(REAL_MODE_BASE_OFFSET + ZERO_ESP_CODE_BYTE_SIZE + 72)
#define REALMODE_PROTECTED_SEGMENT_PATCH_LOCATION       \
	(REAL_MODE_BASE_OFFSET + ZERO_ESP_CODE_BYTE_SIZE + 76)

static uint8_t s3_resume_real_mode_code[] = {
#ifdef BREAK_AT_FIRST_COMMAND
	0xEB, 0xFE, /* jmp $ */
#endif
	/* at the end of the sequence ESI points to the beginning of
	 * the snapshot */
	0xFA, /* 00: cli */

	/* define ZERO_ESP_CODE_BYTE_SIZE = 3, as the byte size is 3 */
	0x66, 0x33, 0xE4,       /* xor esp, esp ;; do not assume the high part of esp is zero */

	0xBC, PTCH, PTCH,       /* 01: mov sp, immediate */
	0x8E, 0xD4,             /* 04: mov ss, sp */
	0xBC, PTCH, PTCH,       /* 06: mov sp, immediate */

	0x66, 0x60,             /* 09: pushad */
	0x66, 0x9C,             /* 11: pushfd */
	0x0E,                   /* 13: push cs */
	0x1E,                   /* 14: push ds */
	0x16,                   /* 15: push ss */
	0x06,                   /* 16: push es */
	0x0F, 0xA0,             /* 17: push fs */
	0x0F, 0xA8,             /* 19: push gs */
	0x66, 0x33, 0xF6,       /* 21: xor esi, esi ;; provide high part of esi is zero */
	0x8B, 0xF4,             /* 24: mov si, sp */
	0x16,                   /* 26: push ss */
	0x1F,                   /* 27: pop ds ;; now ds:si points to ss:sp */
	0x83, 0xEE, 0x06,       /* 28: sub si, 6 */
	0x0F, 0x01, 0x0C,       /* 31: sidt fword ptr [si] */
	0x83, 0xEE, 0x06,       /* 34: sub si, 6 */
	0x0F, 0x01, 0x04,       /* 37: sgdt fword ptr [si] */
	/* at this point all data is stored. ds:si points to the start of the
	 * snapshot
	 * We need to deliver address further. Convert it to linear and put into
	 * esi */
	0x66, 0x8C, 0xD8,       /* 40: mov eax, ds */
	0x66, 0xC1, 0xE0, 0x04, /* 43: shl eax, 4 */
	0x66, 0x03, 0xF0,       /* 47: add esi, eax ;; now esi contains linear address of snapshot */
	0xB8, PTCH, PTCH,       /* 50: mov ax, CODE_START_ALIGNED_16 */
	0x8E, 0xD8,             /* 53: mov ds, ax */
	0x8D, 0x3E, PTCH, PTCH, /* 55: lea di, REALMODE_GDTR_OFFSET */
	0x0F, 0x01, 0x15,       /* 59: lgdt fword ptr [di] */
	0x0F, 0x20, 0xC0,       /* 62: mov eax, cr0 */
	0x0C, 0x01,             /* 65: or al, 1 */
	0x0F, 0x22, 0xC0,       /* 67: mov cr0, eax */
	0x66, 0xEA,             /* 70: fjmp PROTECTED_SEGMENT, PROTECTED_OFFSET */
	PTCH, PTCH, PTCH, PTCH, /* 72: PROTECTED_OFFSET */
	PTCH, PTCH,             /* 76: PROTECTED_SEGMENT */
};

static uint8_t s3_resume_protected_code_1[] = {
#ifdef BREAK_AT_FIRST_COMMAND
	0xEB, 0xFE,     /* 00: jmp $ */
#else
	0x90, 0x90,     /* 00: nop, nop */
#endif
	/* Save CPU registers in the old stack. */
	0xFA,                   /* 02: cli */
	0x60,                   /* 03: pushad */
	0x9C,                   /* 04: pushfd */
	0x0E,                   /* 05: push cs */
	0x1E,                   /* 06: push ds */
	0x16,                   /* 07: push ss */
	0x06,                   /* 08: push es */
	0x0F, 0xA0,             /* 09: push fs */
	0x0F, 0xA8,             /* 11: push gs */
	0x8B, 0xF4,             /* 13: mov esi, esp */
	0x16,                   /* 15: push ss */
	0x1F,                   /* 16: pop ds ;; now ds:si points to ss:sp */
	0x83, 0xEE, 0x06,       /* 17: sub esi, 6 */
	0x0F, 0x01, 0x0E,       /* 20: sidt fword ptr [esi] */
	0x83, 0xEE, 0x06,       /* 23: sub esi, 6 */
	0x0F, 0x01, 0x06,       /* 26: sgdt fword ptr [esi] */
	/* at this point all data is stored. ds:si points to the start of the
	 * snapshot
	 * We need to deliver address further. Convert it to linear and put into
	 * esi. tbd */
};

#define PROTECTED_CODE_DS_VALUE_PATCH_LOCATION      2
#define PROTECTED_CODE_GDTR_PATCH_LOCATION          22
#define PROTECTED_CODE_CPU_SNAPSHOT_ADDR_PATCH_LOCATION 28
#define PROTECTED_CODE_CPU_SNAPSHOT_SIZE_PATCH_LOCATION 33
#define PROTECTED_CODE_ESP_PATCH_LOCATION           40
#define PROTECTED_CODE_ENTRY_POINT_PATCH_LOCATION   45
#define PROTECTED_CODE_STARTUP_PTR_PATCH_LOCATION   50
#define PROTECTED_CODE_INIT64_PATCH_LOCATION        55
#define PROTECTED_CODE_INIT32_PATCH_LOCATION        60
#define PROTECTED_CODE_STARTAP_MAIN_PATCH_LOCATION  65

static uint8_t s3_resume_protected_code_2[] = {
	/*==Initialize MON environment. It is the entry point from RealMode code== */
	0x66, 0xB8, PTCH, PTCH,                         /* 00: mov ax,ds_value */
	0x66, 0x8E, 0xD8,                               /* 04: mov ds,ax */
	0x66, 0x8E, 0xC0,                               /* 07: mov es,ax */
	0x66, 0x8E, 0xE0,                               /* 10: mov fs,ax */
	0x66, 0x8E, 0xE8,                               /* 13: mov gs,ax */
	0x66, 0x8E, 0xD0,                               /* 16: mov ss,ax */
	0x0F, 0x01, 0x15, PTCH, PTCH, PTCH, PTCH,       /* 19: lgdt fword ptr [gdtr] */
	/* copy saved CPU snapshot from old stack to static buffer */
	0x8D, 0x3D, PTCH, PTCH, PTCH, PTCH,             /* 26: lea edi, cpu_saved_state */
	0xB9, PTCH, PTCH, PTCH, PTCH,                   /* 32: mov ecx, sizeof(cpu_saved_state) */
	0xF3, 0xA4,                                     /* 37: rep movsb */
	/* Prepare arguments prior calling thunk */
	0xBC, PTCH, PTCH, PTCH, PTCH,                   /* 39: mov esp,im32 */
	0x68, PTCH, PTCH, PTCH, PTCH,                   /* 44: push entry_point */
	0x68, PTCH, PTCH, PTCH, PTCH,                   /* 49: push p_startup */
	0x68, PTCH, PTCH, PTCH, PTCH,                   /* 54: push p_init64 */
	0x68, PTCH, PTCH, PTCH, PTCH,                   /* 59: push p_init32 */
	0xB9, PTCH, PTCH, PTCH, PTCH,                   /* 64: mov ecx,startap_main */
	0xFF, 0xD1                                      /* 69: call dword ptr [ecx] */
};

/*------------------------------Local Variables-------------------------------*/
static io_port_id_t pm_port[ACPI_PM1_CNTRL_REG_COUNT];
/* compiler will set it as NULL, it is only useful in MP mode (StartAP uses it)
 * and set by setup_data_for_s3() when boot processor > 1; */
static init32_struct_t *init32_data_p;
static init64_struct_t init64_data;
static uint8_t s3_original_waking_code[sizeof(mon_s3_real_mode_resume_layout_t)];
static void *mon_waking_vector;
static void *mon_memory_for_apstartup;
static int32_t number_of_started_cpus;
static int32_t number_of_stopped_cpus;
static mon_guest_cpu_startup_state_t s3_resume_bsp_gcpu_initial_state;
static cpu_snapshot_t cpu_saved_state;
static mon_acpi_callback_t suspend_callbacks[MAX_ACPI_CALLBACKS] = { 0 };
static mon_acpi_callback_t resume_callbacks[MAX_ACPI_CALLBACKS] = { 0 };

/*------------------Forward Declarations for Local Functions---------------*/
static boolean_t mon_acpi_pm1x_handler(guest_cpu_handle_t gcpu,
				       uint16_t port_id,
				       unsigned port_size,
				       rw_access_t access, /* ins/outs */
				       boolean_t string_intr,
				       boolean_t rep_prefix,
				       uint32_t rep_count,
				       void *p_value,
				       void *context);

static mon_status_t mon_acpi_prepare_for_s3(guest_cpu_handle_t gcpu);

void CDECL mon_acpi_resume_after_s3_c_main(uint32_t cpu_id,
					   uint64_t startup_struct_u,
					   uint64_t application_params_struct_u,
					   uint64_t reserved);
static void mon_acpi_prepare_init64_data(void);
static void mon_acpi_prepare_init32_data(uint32_t low_memory_page_address);
static void mon_acpi_build_s3_resume_real_mode_layout(uint32_t waking_vector);
static void mon_acpi_build_s3_resume_protected_layout(void *p_waking_vector);
static void
mon_acpi_build_s3_resume_protected_code(mon_s3_protected_resume_code_2_t *);
static void mon_acpi_prepare_cpu_for_s3(cpu_id_t from, void *unused);
static void mon_acpi_save_original_waking_code(void *p_waking_vector);
static void mon_acpi_restore_original_waking_code(void);
static void mon_acpi_fill_bsp_gcpu_initial_state(guest_cpu_handle_t gcpu);
static void mon_acpi_notify_on_platform_suspend(void);
static void mon_acpi_notify_on_platform_resume(void);

/*-----------------------------C-Code Starts Here--------------------------*/

void mon_acpi_save_original_waking_code(void *p_waking_vector)
{
	mon_waking_vector = p_waking_vector;
	mon_memcpy(s3_original_waking_code, mon_waking_vector,
		sizeof(s3_original_waking_code));
}

void mon_acpi_restore_original_waking_code(void)
{
	mon_memcpy(mon_waking_vector, s3_original_waking_code,
		sizeof(s3_original_waking_code));
}

void mon_acpi_save_memory_for_apstartup(void *addr_to_save)
{
	MON_ASSERT(mon_memory_for_apstartup);
	mon_memcpy(mon_memory_for_apstartup, addr_to_save,
		AP_STARTUP_CODE_SIZE);
}

void mon_acpi_restore_memory_for_apstartup(void *addr_to_restore)
{
	mon_memcpy(addr_to_restore,
		mon_memory_for_apstartup,
		AP_STARTUP_CODE_SIZE);
}

void mon_acpi_build_s3_resume_protected_code(mon_s3_protected_resume_code_2_t *
					     p_protected)
{
	em64t_gdtr_t gdtr;
	hva_t stack_pointer;
	uint8_t *pcode = p_protected->code;

	/* save original GDTR */
	hw_sgdt(&gdtr);
	p_protected->mon_gdtr->base = (uint32_t)gdtr.base;
	p_protected->mon_gdtr->limit = TSS_FIRST_GDT_ENTRY_OFFSET - 1;

	MON_ASSERT(
		sizeof(p_protected->code) > sizeof(s3_resume_protected_code_2));
	/* copy non-patched protected code to waking vector area */
	mon_memcpy(pcode, s3_resume_protected_code_2,
		sizeof(s3_resume_protected_code_2));

	/* patch DS value in mov ax, ds_val */
	*(uint16_t *)&pcode[PROTECTED_CODE_DS_VALUE_PATCH_LOCATION] =
		DATA32_GDT_ENTRY_OFFSET;

	/* patch GDTR */
	*(uint32_t *)&pcode[PROTECTED_CODE_GDTR_PATCH_LOCATION] =
		PTR_TO_U32(p_protected->mon_gdtr);

	/* patch CPU snapshot location and size */
	*(uint32_t *)&pcode[PROTECTED_CODE_CPU_SNAPSHOT_ADDR_PATCH_LOCATION] =
		PTR_TO_U32(&cpu_saved_state);
	*(uint32_t *)&pcode[PROTECTED_CODE_CPU_SNAPSHOT_SIZE_PATCH_LOCATION] =
		sizeof(cpu_saved_state);

	/* patch ESP */
	mon_stack_get_stack_pointer_for_cpu(0, &stack_pointer);
	*(uint32_t *)&pcode[PROTECTED_CODE_ESP_PATCH_LOCATION] =
		(uint32_t)stack_pointer - 512;

	/* patch MON entry point */
	*(uint32_t *)&pcode[PROTECTED_CODE_ENTRY_POINT_PATCH_LOCATION] =
		PTR_TO_U32(mon_acpi_resume_after_s3_c_main_gnu);
	/* patch MON startup structure address */
	*(uint32_t *)&pcode[PROTECTED_CODE_STARTUP_PTR_PATCH_LOCATION] =
		PTR_TO_U32(&mon_startup_data);

	/* patch INIT64 structure address */
	*(uint32_t *)&pcode[PROTECTED_CODE_INIT64_PATCH_LOCATION] =
		PTR_TO_U32(&init64_data);

	/* patch INIT32 structure address */
	*(uint32_t *)&pcode[PROTECTED_CODE_INIT32_PATCH_LOCATION] =
		PTR_TO_U32(init32_data_p);

	/* patch startap module entry point */
	*(uint32_t *)&pcode[PROTECTED_CODE_STARTAP_MAIN_PATCH_LOCATION] =
		(uint32_t)mon_startup_data.mon_memory_layout[thunk_image].
		entry_point;

	MON_LOG(mask_anonymous, level_trace, "startap entry point = %P\n",
		mon_startup_data.mon_memory_layout[thunk_image].entry_point);
}

void mon_acpi_build_s3_resume_real_mode_layout(uint32_t waking_vector)
{
	mon_s3_real_mode_resume_layout_t *p_layout;
	em64t_gdtr_t gdtr;
	uint16_t waking_code_segment;
	uint32_t stack_base;
	uint32_t sp;
	uint32_t ss;

	p_layout = (mon_s3_real_mode_resume_layout_t *)(size_t)waking_vector;

	/* clone GDT into low memory, so real-mode code can access it */
	hw_sgdt(&gdtr);
	mon_memcpy(p_layout->low_memory_gdt, (void *)gdtr.base,
		sizeof(p_layout->low_memory_gdt));

	/* prepare GDTR to point to GDT */
	p_layout->gdtr->base = PTR_TO_U32(p_layout->low_memory_gdt);
	p_layout->gdtr->limit = TSS_FIRST_GDT_ENTRY_OFFSET - 1;

	MON_ASSERT(
		sizeof(p_layout->real_code) > sizeof(s3_resume_real_mode_code));
	/* copy real mode waking code */
	mon_memcpy(p_layout->real_code, s3_resume_real_mode_code,
		sizeof(s3_resume_real_mode_code));

	/* patch SS and SP */
	stack_base = PTR_TO_U32(p_layout->stack);
	ss = stack_base >> 4;
	sp = stack_base + sizeof(p_layout->stack) -
	     REALMODE_SEGMENT_SELECTOR_TO_BASE(ss);

	*(uint16_t *)&p_layout->real_code[REALMODE_SS_VALUE_PATCH_LOCATION] =
		(uint16_t)ss;
	*(uint16_t *)&p_layout->real_code[REALMODE_SP_VALUE_PATCH_LOCATION] =
		(uint16_t)sp;

	/* patch real code segment */
	waking_code_segment = (uint16_t)(waking_vector >> 4);
	*(uint16_t *)&
	p_layout->real_code[REALMODE_CODE_START_ALIGNED_16_PATCH_LOCATION] =
		waking_code_segment;

	/* patch GDTR */
	*(uint16_t *)&p_layout->real_code[REALMODE_GDTR_OFFSET_PATCH_LOCATION] =
		REALMODE_GDTR_OFFSET;

	/* patch protected mode offset and segment */
	*(uint32_t *)&p_layout->real_code[
		REALMODE_PROTECTED_OFFSET_PATCH_LOCATION]
		= PTR_TO_U32(p_layout->protected_code);
	*(uint16_t *)&p_layout->real_code[
		REALMODE_PROTECTED_SEGMENT_PATCH_LOCATION]
		= CODE32_GDT_ENTRY_OFFSET;

	mon_acpi_build_s3_resume_protected_code(p_layout->protected_code);
}

void mon_acpi_build_s3_resume_protected_layout(void *p_waking_vector)
{
	mon_s3_protected_resume_layout_t *p_layout;

	p_layout = (mon_s3_protected_resume_layout_t *)p_waking_vector;

	MON_ASSERT(sizeof(p_layout->protected_code_1) >
		sizeof(s3_resume_protected_code_1));
	/* copy real mode waking code */
	mon_memcpy(p_layout->protected_code_1, s3_resume_protected_code_1,
		sizeof(s3_resume_protected_code_1));
	mon_acpi_build_s3_resume_protected_code(p_layout->protected_code_2);
}

void mon_acpi_pm_initialize(guest_id_t guest_id)
{
	unsigned i;
	uint8_t port_size;
	static boolean_t acpi_initialized = FALSE;

	/* BEFORE_VMLAUNCH. CRITICAL check that should not fail. */
	MON_ASSERT(FALSE == acpi_initialized);
	acpi_initialized = TRUE;

	mon_memset(&s3_resume_bsp_gcpu_initial_state, 0,
		sizeof(mon_guest_cpu_startup_state_t));

	port_size = mon_acpi_pm_port_size();

	/* BEFORE_VMLAUNCH. CRITICAL check that should not fail. */
	MON_ASSERT(2 == port_size || 4 == port_size);

	if (2 == port_size || 4 == port_size) {
		pm_port[0] = (io_port_id_t)mon_acpi_pm_port_a();
		pm_port[1] = (io_port_id_t)mon_acpi_pm_port_b();

		for (i = 0; i < NELEMENTS(pm_port); ++i) {
			if (0 != pm_port[i]) {
				MON_LOG(mask_anonymous,
					level_trace,
					"[ACPI] Install handler at Pm1%cControlBlock(%P)\n",
					'a' + i,
					pm_port[i]);
				mon_io_vmexit_handler_register(guest_id,
					pm_port[i],
					mon_acpi_pm1x_handler,
					NULL);
			}
		}
	} else {
		MON_LOG(mask_anonymous,
			level_trace,
			"[ACPI] Failed to intitalize due to bad port size(%d)\n",
			port_size);
	}
}

boolean_t mon_acpi_pm1x_handler(guest_cpu_handle_t gcpu, uint16_t port_id,
				unsigned port_size,
				rw_access_t access, /* ins/outs */
				boolean_t string_intr, boolean_t rep_prefix,
				uint32_t rep_count, void *p_value,
				void *context UNUSED)
{
	unsigned pm_reg_id;
	unsigned sleep_state;
	uint32_t value;
	boolean_t sleep_enable;

	/* validate arguments */

	if (WRITE_ACCESS != access || mon_acpi_pm_port_size() != port_size) {
		goto pass_transparently;
	}

	if (port_id == pm_port[ACPI_PM1_CNTRL_REG_A]) {
		pm_reg_id = ACPI_PM1_CNTRL_REG_A;
	} else if (port_id == pm_port[ACPI_PM1_CNTRL_REG_B]) {
		pm_reg_id = ACPI_PM1_CNTRL_REG_B;
	} else {
		goto pass_transparently;
	}

	switch (port_size) {
	case 2:
		value = *(uint16_t *)p_value;
		break;
	case 4:
		value = *(uint32_t *)p_value;
		break;
	default:
		goto pass_transparently;
	}

	sleep_state = mon_acpi_sleep_type_to_state(pm_reg_id, SLP_TYP(value));
	sleep_enable = SLP_EN(value);

	/* System enters sleep state only if "sleep enable" bit is set */
	if (sleep_enable) {
		MON_LOG(mask_anonymous, level_trace,
			"[ACPI] SleepState(%d) requested at pm_reg_id(%c)"
			" port_id(%P) port_size(%d) access(%d)\n",
			sleep_state, pm_reg_id + 'A', port_id, port_size,
			access);

		switch (sleep_state) {
		case 1:
			break;
		case 2:
			break;
		case 3:
			/* standby */
			if (MON_OK != mon_acpi_prepare_for_s3(gcpu)) {
				MON_LOG(mask_anonymous,
					level_error,
					"[acpi] mon_acpi_prepare_for_s3() failed\n");
			}
			break;
		case 4:
			/* hibernate */
			break;
		case 5:
			/* shutdown */
			break;
		default:
			break;
		}
	}

pass_transparently:
	io_vmexit_transparent_handler(gcpu, port_id, port_size, access, p_value,
		NULL);
	return TRUE;
}


void mon_acpi_prepare_cpu_for_s3(cpu_id_t from UNUSED, void *unused UNUSED)
{
	guest_cpu_handle_t gcpu;
	scheduler_gcpu_iterator_t iterator;
	cpu_id_t cpu_id = hw_cpu_id();

	MON_LOG(mask_anonymous,
		level_trace,
		"[ACPI] CPU(%d) going to go to S3\n",
		cpu_id);

	mon_startup_data.cpu_local_apic_ids[hw_cpu_id()] =
		local_apic_get_current_id();

	/* deactivate active gcpu */
	gcpu = mon_scheduler_current_gcpu();
	MON_ASSERT(gcpu);

	SET_CACHED_ACTIVITY_STATE(gcpu,
		IA32_VMX_VMCS_GUEST_SLEEP_STATE_WAIT_FOR_SIPI);

	gcpu_swap_out(gcpu);

	/* for all GCPUs on this CPU do: */
	for (gcpu = scheduler_same_host_cpu_gcpu_first(&iterator, cpu_id);
	     gcpu != NULL; gcpu =
		     scheduler_same_host_cpu_gcpu_next(&iterator)) {
		vmcs_object_t *vmcs = mon_gcpu_get_vmcs(gcpu);
		MON_ASSERT(vmcs);

		/* Clear VMCS */
		vmcs_flush_to_memory(vmcs);

		event_raise(EVENT_GCPU_ENTERING_S3, gcpu, NULL);
	}

	vmcs_hw_vmx_off();

	/* indicate that CPU is down */
	hw_interlocked_increment(&number_of_stopped_cpus);

	hw_wbinvd();

	if (0 != cpu_id) {
		hw_halt();
	}
}



mon_status_t mon_acpi_prepare_for_s3(guest_cpu_handle_t gcpu UNUSED)
{
	uint32_t waking_vector;
	uint64_t extended_waking_vector;
	void *ap_s3_startup_addr;
	void *p_waking_vector;
	ipc_destination_t ipc_dest;
	uint32_t facs_flags, facs_ospm_flags;

	/* 1. Get original waking vector code */
	if (0 !=
	    mon_acpi_waking_vector(&waking_vector, &extended_waking_vector) ||
	    (0 == waking_vector && 0 == extended_waking_vector)) {
		MON_LOG(mask_anonymous,
			level_trace,
			"[ACPI] Waking vector is NULL. S3 is not supported by the platform\n");
		return MON_ERROR;
	}

	mon_memset(&ipc_dest, 0, sizeof(ipc_dest));
	number_of_started_cpus = 0;
	number_of_stopped_cpus = 0;

	/* 2. Force other CPUs and itself to prepare for S3 */
	ipc_dest.addr_shorthand = IPI_DST_ALL_EXCLUDING_SELF;
	ipc_execute_handler(ipc_dest, mon_acpi_prepare_cpu_for_s3, NULL);

	mon_acpi_prepare_cpu_for_s3(0, NULL);

	/* 3. Prepare init64_struct_t data */
	mon_acpi_prepare_init64_data();

	/* 4. Store original waking vector content aside
	 * Notice that we reuse the same low memory page 3 times
	 * once for BSP transition to protected mode, second time for AP
	 * initialization,
	 * and finally for guest original purpose.
	 * Replace it with MON startup code
	 * Patch MON start up code with running environment values */

	if (0 != extended_waking_vector) {
		/* save original code */
		p_waking_vector = (void *)extended_waking_vector;
		mon_acpi_save_original_waking_code(p_waking_vector);

		ap_s3_startup_addr = (void *)PAGE_ALIGN_4K(
			extended_waking_vector);

		/*
		 * In current implementation, the ap startup code should be below 1MB.
		 * Here only trap the error if the ap startup address is above 1MB.
		 * Need to find better solution to fix this when encounter error.
		 */
		MON_ASSERT((uint64_t)ap_s3_startup_addr < 0x100000);

		/* The AP startup code should be less than AP_STARTUP_CODE_SIZE */
		mon_acpi_save_memory_for_apstartup(ap_s3_startup_addr);

		/* Prepare init32_struct_t data. */
		mon_acpi_prepare_init32_data((uint32_t)(PAGE_ALIGN_4K(
								extended_waking_vector)));

		if (0 != mon_acpi_facs_flag(&facs_flags, &facs_ospm_flags)) {
			MON_LOG(mask_anonymous, level_trace,
				"[ACPI] Can't get FACS flags\n");
			return MON_ERROR;
		}

		if ((facs_ospm_flags & ACPI_FACS_OSPM_64BIT_WAKE) &&
		    (facs_flags & ACPI_FACS_64BIT_WAKE_SUPPORTED)) {
			/* Prepare to resume back to 64bit mode.
			 * Currently, don't have 64bit mode implementation.
			 * Should not come to this path. Trap error for now.
			 */
			MON_ASSERT(0);
		} else {
			/* Prepare to resume back to protected mode */
			mon_acpi_build_s3_resume_protected_layout(
				p_waking_vector);
		}
	} else {
		/* save original code */
		p_waking_vector = (void *)(size_t)waking_vector;
		mon_acpi_save_original_waking_code(p_waking_vector);

		ap_s3_startup_addr = (void *)PAGE_ALIGN_4K(waking_vector);
		/* make sure the ap startup address is below 1MB */
		MON_ASSERT((uint64_t)ap_s3_startup_addr < 0x100000);
		/* The AP startup code should be less than AP_STARTUP_CODE_SIZE */
		mon_acpi_save_memory_for_apstartup(ap_s3_startup_addr);

		/* Prepare init32_struct_t data. */
		mon_acpi_prepare_init32_data(PAGE_ALIGN_4K(waking_vector));

		mon_acpi_build_s3_resume_real_mode_layout(waking_vector);
	}

	/* wait while all APs are down too */
	WAIT_FOR_MP_CONDITION(number_of_stopped_cpus ==
		mon_startup_data.number_of_processors_at_boot_time);

	/* 5. Invalidate caches */
	hw_wbinvd();

	mon_acpi_notify_on_platform_suspend();

	return MON_OK;
}


void mon_acpi_prepare_init64_data(void)
{
	em64t_gdtr_t gdtr_64;

	hw_sgdt(&gdtr_64);
	init64_data.i64_gdtr.base = (uint32_t)gdtr_64.base;
	init64_data.i64_gdtr.limit = gdtr_64.limit;
	init64_data.i64_cr3 = (uint32_t)hw_read_cr3();
	init64_data.i64_cs = hw_read_cs();
	init64_data.i64_efer = hw_read_msr(IA32_MSR_EFER) & EFER_NXE;

	MON_LOG(mask_anonymous, level_trace, "Init64 data\n");
	MON_LOG(mask_anonymous, level_trace, "i64_gdtr.base    =%P\n",
		init64_data.i64_gdtr.base);
	MON_LOG(mask_anonymous, level_trace, "i64_gdtr.limit   =%P\n",
		init64_data.i64_gdtr.limit);
	MON_LOG(mask_anonymous, level_trace, "i64_gdtr.i64_cr3 =%P\n",
		init64_data.i64_cr3);
	MON_LOG(mask_anonymous, level_trace, "i64_gdtr.i64_cs  =%P\n",
		init64_data.i64_cs);
	MON_LOG(mask_anonymous, level_trace, "i64_gdtr.i64_efer=%P\n",
		init64_data.i64_efer);
}

#define ASK_ALL_MEMORY_HOLDERS ((uint32_t)-1)
void setup_data_for_s3(void)
{
	if (mon_startup_data.number_of_processors_at_boot_time > 1) {
		if (!init32_data_p) {
			uint16_t num_of_aps =
				mon_startup_data.
				number_of_processors_at_boot_time - 1;
			init32_data_p =
				mon_memory_alloc_must_succeed(
					ASK_ALL_MEMORY_HOLDERS,
					sizeof(init32_struct_t) +
					num_of_aps * sizeof(uint32_t));
		}
		if (!mon_memory_for_apstartup) {
			mon_memory_for_apstartup =
				mon_memory_alloc_must_succeed(
					ASK_ALL_MEMORY_HOLDERS,
					AP_STARTUP_CODE_SIZE);
		}
	}
}

void mon_acpi_prepare_init32_data(uint32_t low_memory_page_address)
{
	/* if there are Application Processors */
	if (mon_startup_data.number_of_processors_at_boot_time > 1) {
		uint16_t i;
		uint16_t num_of_aps =
			mon_startup_data.number_of_processors_at_boot_time - 1;

		MON_ASSERT(low_memory_page_address);
		MON_ASSERT(init32_data_p);

		init32_data_p->i32_low_memory_page = low_memory_page_address;
		init32_data_p->i32_num_of_aps = num_of_aps;

		for (i = 0; i < num_of_aps; ++i) {
			hva_t stack_pointer;
			boolean_t success =
				mon_stack_get_stack_pointer_for_cpu(i + 1,
					&stack_pointer);
			if (!success) {
				MON_LOG(mask_anonymous,
					level_trace,
					"[acpi] Failed to allocate stacks for APs."
					" Run as a single core\n");
				mon_memory_free(init32_data_p);
				init32_data_p = NULL;
				return;
			}
			init32_data_p->i32_esp[i] = (uint32_t)stack_pointer -
						    512;
		}
	}
}


void CDECL mon_acpi_resume_after_s3_c_main(uint32_t cpu_id,
					   uint64_t startup_struct_u UNUSED,
					   uint64_t application_params_struct_u
					   UNUSED, uint64_t reserved UNUSED)
{
	guest_cpu_handle_t initial_gcpu;
	ept_guest_state_t *ept_guest = NULL;
	ept_guest_cpu_state_t *ept_guest_cpu = NULL;
	const virtual_cpu_id_t *vcpu_id = NULL;

	g_s3_resume_flag = 1;
	mon_debug_port_clear();
	mon_io_init();

	/* hw_gdt_load must be called before MON_LOG,
	 * otherwise it will decide that emulator_is_running_as_guest() */
	hw_gdt_load((cpu_id_t)cpu_id);

	MON_LOG(mask_anonymous, level_trace,
		"\n******************************************\n");
	MON_LOG(mask_anonymous, level_trace,
		"\n\nSystem Resumed after S3 on CPU(%d)\n\n", cpu_id);
	MON_LOG(mask_anonymous, level_trace,
		"\n******************************************\n");

	isr_handling_start();

	hmm_set_required_values_to_control_registers();

	/* init CR0/CR4 to the VMX compatible values */
	hw_write_cr0(vmcs_hw_make_compliant_cr0(hw_read_cr0()));
	hw_write_cr4(vmcs_hw_make_compliant_cr4(hw_read_cr4()));

	host_cpu_enable_usage_of_xmm_regs();

	host_cpu_init();

	local_apic_cpu_init();

	mon_acpi_notify_on_platform_resume();

	vmcs_hw_vmx_on();
	MON_LOG(mask_anonymous, level_trace, "CPU%d: VMXON\n", cpu_id);

	/* schedule first gcpu */
	initial_gcpu = scheduler_select_initial_gcpu();

	MON_ASSERT(initial_gcpu != NULL);
	MON_LOG(mask_anonymous,
		level_trace,
		"CPU%d: initial guest selected: guest_id_t: %d GUEST_CPU_ID: %d\n",
		cpu_id,
		mon_guest_vcpu(initial_gcpu)->guest_id,
		mon_guest_vcpu(initial_gcpu)->guest_cpu_id);

	vcpu_id = mon_guest_vcpu(initial_gcpu);
	ept_guest = ept_find_guest_state(vcpu_id->guest_id);
	MON_ASSERT(ept_guest);
	ept_guest_cpu = ept_guest->gcpu_state[vcpu_id->guest_cpu_id];

	if (0 == cpu_id) {
		/*--------- BSP */
		mon_acpi_restore_original_waking_code();
		mon_acpi_restore_memory_for_apstartup((void *)(PAGE_ALIGN_4K(
								       mon_waking_vector)));

		/* fill s3_resume_bsp_gcpu_initial_state */
		mon_acpi_fill_bsp_gcpu_initial_state(initial_gcpu);

		gcpu_initialize(initial_gcpu,
			&s3_resume_bsp_gcpu_initial_state);

		/* Set ept_guest_cpu->cr0 for BSP to synchronize with guest
		 * visible CR0 */
		ept_guest_cpu->cr0 =
			gcpu_get_guest_visible_control_reg(initial_gcpu,
				IA32_CTRL_CR0);
		ept_guest_cpu->cr4 =
			gcpu_get_guest_visible_control_reg(initial_gcpu,
				IA32_CTRL_CR4);

		/* indicate that CPU is up */
		hw_interlocked_increment(&number_of_started_cpus);

		/* wait while all APs are up too */
		WAIT_FOR_MP_CONDITION(number_of_started_cpus ==
			mon_startup_data.number_of_processors_at_boot_time);
	} else {
		/*--------- AP */
		gcpu_set_activity_state(initial_gcpu,
			IA32_VMX_VMCS_GUEST_SLEEP_STATE_WAIT_FOR_SIPI);
		/* indicate that CPU is up */
		hw_interlocked_increment(&number_of_started_cpus);
	}

	event_raise(EVENT_GCPU_RETURNED_FROM_S3, initial_gcpu, NULL);

	gcpu_resume(initial_gcpu);

	MON_DEADLOOP();
}


boolean_t mon_acpi_register_platform_suspend_callback(mon_acpi_callback_t
						      suspend_cb)
{
	static int available_index;

	if (available_index >= MAX_ACPI_CALLBACKS) {
		MON_LOG(mask_anonymous,
			level_trace,
			"acpi-pm: too many registrations for suspend callback\r\n");
		return FALSE;
	}
	suspend_callbacks[available_index++] = suspend_cb;
	return TRUE;
}

static void mon_acpi_notify_on_platform_suspend(void)
{
	int i;

	for (i = 0; i < MAX_ACPI_CALLBACKS; i++) {
		if (NULL != suspend_callbacks[i]) {
			suspend_callbacks[i] ();
		}
	}
}

boolean_t mon_acpi_register_platform_resume_callback(
	mon_acpi_callback_t resume_cb)
{
	static int available_index;

	if (available_index >= MAX_ACPI_CALLBACKS) {
		MON_LOG(mask_anonymous,
			level_trace,
			"acpi-pm: too many registrations for resume callback\r\n");
		return FALSE;
	}
	resume_callbacks[available_index++] = resume_cb;
	return TRUE;
}

static void mon_acpi_notify_on_platform_resume(void)
{
	int i;

	for (i = 0; i < MAX_ACPI_CALLBACKS; i++) {
		if (NULL != resume_callbacks[i]) {
			resume_callbacks[i] ();
		}
	}
}

/*
 * This function assumes that waking vector was called in Real Mode
 */
void mon_acpi_fill_bsp_gcpu_initial_state(guest_cpu_handle_t gcpu)
{
	uint64_t offset = (uint64_t)mon_waking_vector -
			  (uint64_t)REALMODE_SEGMENT_SELECTOR_TO_BASE(
		cpu_saved_state.cs);

	s3_resume_bsp_gcpu_initial_state.size_of_this_struct =
		sizeof(s3_resume_bsp_gcpu_initial_state);
	s3_resume_bsp_gcpu_initial_state.version_of_this_struct = 0x01;
	s3_resume_bsp_gcpu_initial_state.gp.reg[IA32_REG_RAX] =
		cpu_saved_state.eax;
	s3_resume_bsp_gcpu_initial_state.gp.reg[IA32_REG_RBX] =
		cpu_saved_state.ebx;
	s3_resume_bsp_gcpu_initial_state.gp.reg[IA32_REG_RCX] =
		cpu_saved_state.ecx;
	s3_resume_bsp_gcpu_initial_state.gp.reg[IA32_REG_RDX] =
		cpu_saved_state.edx;
	s3_resume_bsp_gcpu_initial_state.gp.reg[IA32_REG_RDI] =
		cpu_saved_state.edi;
	s3_resume_bsp_gcpu_initial_state.gp.reg[IA32_REG_RSI] =
		cpu_saved_state.esi;
	s3_resume_bsp_gcpu_initial_state.gp.reg[IA32_REG_RBP] =
		cpu_saved_state.ebp;
	s3_resume_bsp_gcpu_initial_state.gp.reg[IA32_REG_RSP] =
		cpu_saved_state.esp;
	s3_resume_bsp_gcpu_initial_state.gp.reg[IA32_REG_RIP] = offset;
	s3_resume_bsp_gcpu_initial_state.gp.reg[IA32_REG_RFLAGS] =
		cpu_saved_state.eflags;

	/* The attributes of CS,DS,SS,ES,FS,GS,LDTR and TR are set based on
	 * Volume 3B, System Programming Guide, section 23.3.1.2 */
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_CS].selector =
		cpu_saved_state.cs;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_CS].limit =
		0x0000FFFF;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_CS].attributes =
		0x9b;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_CS].base =
		(uint64_t)REALMODE_SEGMENT_SELECTOR_TO_BASE(cpu_saved_state.cs);

	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_DS].selector =
		cpu_saved_state.ds;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_DS].limit =
		0xFFFFFFFF;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_DS].attributes =
		0x8091;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_DS].base =
		(uint64_t)REALMODE_SEGMENT_SELECTOR_TO_BASE(cpu_saved_state.ds);

	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_SS].selector =
		cpu_saved_state.ss;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_SS].limit =
		0xFFFFFFFF;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_SS].attributes =
		0x8093;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_SS].base =
		(uint64_t)REALMODE_SEGMENT_SELECTOR_TO_BASE(cpu_saved_state.ss);

	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_ES].selector =
		cpu_saved_state.es;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_ES].limit =
		0xFFFFFFFF;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_ES].attributes =
		0x8093;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_ES].base =
		(uint64_t)REALMODE_SEGMENT_SELECTOR_TO_BASE(cpu_saved_state.es);

	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_FS].selector =
		cpu_saved_state.fs;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_FS].limit =
		0xFFFFFFFF;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_FS].attributes =
		0x8091;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_FS].base =
		(uint64_t)REALMODE_SEGMENT_SELECTOR_TO_BASE(cpu_saved_state.fs);

	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_GS].selector =
		cpu_saved_state.gs;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_GS].limit =
		0xFFFFFFFF;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_GS].attributes =
		0x8091;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_GS].base =
		(uint64_t)REALMODE_SEGMENT_SELECTOR_TO_BASE(cpu_saved_state.gs);

	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_LDTR].base = 0;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_LDTR].limit = 0;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_LDTR].attributes =
		0x10000;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_LDTR].selector =
		0;

	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_TR].base = 0;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_TR].limit = 0;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_TR].attributes =
		0x8b;
	s3_resume_bsp_gcpu_initial_state.seg.segment[IA32_SEG_TR].selector = 0;

	s3_resume_bsp_gcpu_initial_state.control.cr[IA32_CTRL_CR0] = 0x00000010;
	s3_resume_bsp_gcpu_initial_state.control.cr[IA32_CTRL_CR2] = 0;
	s3_resume_bsp_gcpu_initial_state.control.cr[IA32_CTRL_CR3] = 0;
	s3_resume_bsp_gcpu_initial_state.control.cr[IA32_CTRL_CR4] = 0x00000050;

	s3_resume_bsp_gcpu_initial_state.control.gdtr.base =
		cpu_saved_state.gdtr.base;
	s3_resume_bsp_gcpu_initial_state.control.gdtr.limit =
		cpu_saved_state.gdtr.limit;

	s3_resume_bsp_gcpu_initial_state.control.idtr.base =
		cpu_saved_state.idtr.base;
	s3_resume_bsp_gcpu_initial_state.control.idtr.limit =
		cpu_saved_state.idtr.limit;

	s3_resume_bsp_gcpu_initial_state.msr.msr_debugctl = 0x00000001;
	s3_resume_bsp_gcpu_initial_state.msr.msr_efer = 0;
	s3_resume_bsp_gcpu_initial_state.msr.msr_pat =
		gcpu_get_msr_reg(gcpu, IA32_MON_MSR_PAT);
	s3_resume_bsp_gcpu_initial_state.msr.msr_sysenter_esp = 0;
	s3_resume_bsp_gcpu_initial_state.msr.msr_sysenter_eip = 0;
	s3_resume_bsp_gcpu_initial_state.msr.pending_exceptions = 0;
	s3_resume_bsp_gcpu_initial_state.msr.msr_sysenter_cs = 0;
	s3_resume_bsp_gcpu_initial_state.msr.interruptibility_state = 0;
	s3_resume_bsp_gcpu_initial_state.msr.activity_state = 0;
	s3_resume_bsp_gcpu_initial_state.msr.smbase = 0;
}
