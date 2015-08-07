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

#include "file_codes.h"
#define MON_DEADLOOP()          MON_DEADLOOP_LOG(MON_C)
#define MON_ASSERT(__condition) MON_ASSERT_LOG(MON_C, __condition)
#include "mon_defs.h"
#include "mon_startup.h"
#include "mon_globals.h"
#include "mon_callback.h"
#include "libc.h"
#include "mon_serial.h"
#include "cli.h"
#include "address.h"
#include "lock.h"
#include "hw_includes.h"
#include "heap.h"
#include "gdt.h"
#include "isr.h"
#include "mon_stack_api.h"
#include "e820_abstraction.h"
#include "host_memory_manager_api.h"
#include "cli_monitor.h"
#include "vmcs_init.h"
#include "efer_msr_abstraction.h"
#include "mtrrs_abstraction.h"
#include "guest.h"
#include "policy_manager.h"
#include "host_cpu.h"
#include "scheduler.h"
#include "mon_bootstrap_utils.h"
#include "ipc.h"
#include "vmexit.h"
#include "parse_image.h"
#include "mon_dbg.h"
#include "vmx_trace.h"
#include "event_mgr.h"
#include <pat_manager.h>
#include "host_pci_configuration.h"
#include "guest_pci_configuration.h"
#include "vtd.h"
#include "ept.h"
#include "device_drivers_manager.h"
#include "vmx_nmi.h"
#include "vmdb.h"
#include "vmx_timer.h"
#include "guest/guest_cpu/unrestricted_guest.h"
#include "vmcs_api.h"
#include "fvs.h"
#include "mon_acpi.h"
#include "gpm_api.h"

boolean_t vmcs_sw_shadow_disable[MON_MAX_CPU_SUPPORTED];

typedef struct {
	uint64_t	local_apic_id;
	uint64_t	startup_struct;
	uint64_t	application_params_struct;
} mon_input_params_t;

/*------------------------- globals ------------------------- */

mon_startup_struct_t mon_startup_data;

cpu_id_t g_num_of_cpus = 0;

static
volatile uint32_t g_application_procs_may_be_launched = FALSE;

static
volatile uint32_t g_application_procs_launch_the_guest = 0;

mon_paging_policy_t g_pg_policy;

uint32_t g_is_post_launch = 0;

extern uint64_t g_debug_gpa;
extern void setup_data_for_s3(void);

uint64_t g_additional_heap_pa = 0;
uint32_t g_heap_pa_num = 0;
uint64_t g_additional_heap_base = 0;
extern boolean_t build_extend_heap_hpa_to_hva(void);

/*-------------------------- macros ------------------------- */
#define WAIT_FOR_APPLICATION_PROCS_LAUNCH()                    \
	{ while (!g_application_procs_may_be_launched) { hw_pause(); } }

#define LAUNCH_APPLICATION_PROCS()                             \
	{ hw_assign_as_barrier(&g_application_procs_may_be_launched, TRUE); }

#define WAIT_FOR_APPLICATION_PROCS_LAUNCHED_THE_GUEST(count) \
	{ while (g_application_procs_launch_the_guest != \
		 (uint32_t)(count)) { hw_pause(); } }

#define APPLICATION_PROC_LAUNCHING_THE_GUEST()                 \
	{ hw_interlocked_increment((int32_t *)(& \
					       g_application_procs_launch_the_guest)); \
	}

/*------------------------- forwards ------------------------ */

/* main for BSP - should never return.  local_apic_id is always 0 */
static
void mon_bsp_proc_main(uint32_t local_apic_id,
		       const mon_startup_struct_t *startup_struct,
		       const mon_application_params_struct_t *
		       application_params_struct);

/* main for APs - should never return */
static
void mon_application_procs_main(uint32_t local_apic_id);

static
int cli_show_memory_layout(unsigned argc, char *args[]);

static
void make_guest_state_compliant(guest_cpu_handle_t gcpu);

#if defined DEBUG
/*---------------------- implementation --------------------- */
INLINE uint8_t lapic_id(void)
{
	cpuid_params_t cpuid_params;

	cpuid_params.m_rax = 1;
	hw_cpuid(&cpuid_params);
	return (uint8_t)(cpuid_params.m_rbx >> 24) & 0xFF;
}
#endif

INLINE void enable_fx_ops(void)
{
	uint64_t cr0_value = hw_read_cr0();

	BITMAP_CLR64(cr0_value, CR0_TS);

	BITMAP_CLR64(cr0_value, CR0_MP);
	hw_write_cr0(cr0_value);
}

INLINE void enable_ept_during_launch(guest_cpu_handle_t initial_gcpu)
{
	uint64_t guest_cr4;

	ept_acquire_lock();

	/* Enable EPT, if it is currently not enabled */
	if (!mon_ept_is_ept_enabled(initial_gcpu)) {
		mon_ept_enable(initial_gcpu);

		/* set the right pdtprs into the vmcs. */
		guest_cr4 =
			gcpu_get_guest_visible_control_reg(initial_gcpu,
				IA32_CTRL_CR4);

		ept_set_pdtprs(initial_gcpu, guest_cr4);
	}

	ept_release_lock();
}

/*------------------------------------------------------------------------
 *
 * Per CPU type policy setup
 * Sets policies depending on host CPU features. Should be called
 * on BSP only
 *
 *------------------------------------------------------------------------ */
void mon_setup_cpu_specific_policies(mon_policy_t *p_policy)
{
	cpuid_info_struct_t info;
	uint32_t cpuid_1_eax;

	/* get version info */
	cpuid(&info, 1);
	cpuid_1_eax = CPUID_VALUE_EAX(info);

	/* WSM CPUs has ucode bug that crashes CPU if VTx is ON and CR0.CD=1
	 * prevent this
	 * WSM cpu ids
	 * wsm_a0 - 0x00020650
	 * wsm_b0 - 0x00020651
	 * wsm_e0 - 0x00020654
	 * wsm_t0 - 0x000206c0
	 * wsm_u0 - 0x000206c0 - temp */
	switch (cpuid_1_eax & ~0xF) {
	case 0x00020650:
	case 0x000206c0:
		MON_LOG(mask_mon, level_trace,
			"Switching ON policy to disable CR0.cd=1 settings\n");
		set_cache_policy(p_policy, POL_CACHE_DIS_VIRTUALIZATION);
		break;

	default:
		set_cache_policy(p_policy, POL_CACHE_DIS_NO_INTERVENING);
		break;
	}
}

extern void ASM_FUNCTION ITP_JMP_DEADLOOP(void);
/*------------------------------------------------------------------------
 *
 * THE MAIN !!!!
 *
 * Started in parallel for all available processors
 *
 * Should never return!
 *
 *------------------------------------------------------------------------ */
void mon_main_continue(mon_input_params_t *mon_input_params)
{
	const mon_startup_struct_t *startup_struct =
		(const mon_startup_struct_t *)mon_input_params->startup_struct;
	const mon_application_params_struct_t *application_params_struct =
		(const mon_application_params_struct_t
		 *)(mon_input_params->application_params_struct);

	uint32_t local_apic_id = (uint32_t)(mon_input_params->local_apic_id);

	if (local_apic_id == 0) {
		mon_bsp_proc_main(local_apic_id, startup_struct,
			application_params_struct);
	} else {
		mon_application_procs_main(local_apic_id);
	}

	MON_BREAKPOINT();
}

void CDECL mon_main(uint32_t local_apic_id,
		    uint64_t startup_struct_u,
		    uint64_t application_params_struct_u,
		    uint64_t reserved UNUSED)
{
	const mon_startup_struct_t *startup_struct =
		(const mon_startup_struct_t *)startup_struct_u;
	hva_t new_stack_pointer = 0;
	mon_input_params_t input_params;
	cpu_id_t cpu_id = (cpu_id_t)local_apic_id;

	/* Sanity check */
	MON_ASSERT(startup_struct != NULL);

	/* save for usage during S3 resume */
	mon_startup_data = *startup_struct;

	{
		boolean_t release_mode = TRUE;
		MON_DEBUG_CODE(release_mode = FALSE);
		if (release_mode) {
			/* Removes all verbosity levels in release mode */
			mon_startup_data.debug_params.mask =
				mon_startup_data.debug_params.mask &
				~((1 << mask_cli) +
				  (1 <<
				mask_anonymous) +
				  (1 <<
				mask_emulator) +
				  (1 <<
				mask_gdb) +
				  (1 <<
				mask_ept) +
				  (1 <<
				mask_mon) +
				  (1 <<
				mask_xmon_api) +
				  (1 <<
				mask_plugin));
		}
	}

	host_cpu_enable_usage_of_xmm_regs();

	/* setup stack */
	if (!mon_stack_caclulate_stack_pointer(startup_struct, cpu_id,
		    &new_stack_pointer)) {
		MON_BREAKPOINT();
	}

	input_params.local_apic_id = local_apic_id;
	input_params.startup_struct = startup_struct_u;
	input_params.application_params_struct = application_params_struct_u;

	hw_set_stack_pointer(new_stack_pointer,
		(func_main_continue_t)mon_main_continue, &input_params);
	MON_BREAKPOINT();
}

/*------------------------------------------------------------------------
 *
 * The Boot Strap Processor main routine
 *
 * Should never return!
 *
 *------------------------------------------------------------------------ */
static
void mon_bsp_proc_main(uint32_t local_apic_id,
		       const mon_startup_struct_t *startup_struct,
		       const mon_application_params_struct_t *
		       application_params_struct)
{
	hva_t lowest_stacks_addr = 0;
	uint32_t stacks_size = 0;
	hva_t heap_address;
	uint32_t heap_size;
	hva_t heap_last_occupied_address;
	cpu_id_t num_of_cpus =
		(cpu_id_t)startup_struct->number_of_processors_at_boot_time;
	cpu_id_t cpu_id = (cpu_id_t)local_apic_id;
	hpa_t new_cr3 = 0;
	const mon_startup_struct_t *startup_struct_heap;
	const mon_application_params_struct_t *application_params_heap;
	const mon_guest_startup_t *primary_guest_startup;
	const mon_guest_startup_t *secondary_guests_array;
	guest_handle_t nmi_owner_guest, device_default_owner_guest,
		acpi_owner_guest;
	uint32_t num_of_guests;
	guest_cpu_handle_t initial_gcpu = NULL;
	mon_policy_t policy;
	boolean_t debug_port_params_error;
	boolean_t acpi_init_status;
	report_initialization_data_t initialization_data;
	guest_handle_t guest;
	guest_econtext_t guest_ctx;
	uint32_t i = 0;
	memory_config_t mem_config;

	hva_t fadt_hva = 0;

	/* save number of CPUs */
	g_num_of_cpus = num_of_cpus;

	/* get post launch status */
	g_is_post_launch =
		(BITMAP_GET(startup_struct->flags,
			 MON_STARTUP_POST_OS_LAUNCH_MODE) !=
		 0);

	hw_calibrate_tsc_ticks_per_second();

	/* Init the debug port.  If the version is too low, there's no debug
	 * parameters.  Use the defaults and later on assert. */

	if (startup_struct->version_of_this_struct >=
	    MON_STARTUP_STRUCT_MIN_VERSION_WITH_DEBUG) {
		debug_port_params_error =
			mon_debug_port_init_params(
				&startup_struct->debug_params.port);
		g_debug_gpa = startup_struct->debug_params.debug_data;
	} else {
		debug_port_params_error = mon_debug_port_init_params(NULL);
	}

	/* init the LIBC library */
	mon_libc_init();

	/* Now we have a functional debug output */

	if (debug_port_params_error) {
		MON_LOG(mask_mon,
			level_error,
			"\nFAILURE: Loader-MON version mismatch (no debug port parameters)\n");
		MON_DEADLOOP();
	}
	;

	if (g_is_post_launch) {
		if (application_params_struct) {
			if (application_params_struct->size_of_this_struct !=
			    sizeof(mon_application_params_struct_t)) {
				MON_LOG(mask_mon,
					level_error,
					"\nFAILURE: application params structure"
					" size mismatch)\n");
				MON_DEADLOOP();
			}
			;

			g_heap_pa_num =
				(uint32_t)(application_params_struct->
					   entry_number);
			g_additional_heap_pa =
				application_params_struct->address_entry_list;
		}
	}

	/* Print global version message */
	mon_version_print();

	MON_LOG(mask_mon,
		level_trace,
		"\nBSP: MON image base address = %P, entry point address = %P\n",
		startup_struct->mon_memory_layout[mon_image].base_address,
		startup_struct->mon_memory_layout[mon_image].entry_point);

	MON_LOG(mask_mon, level_trace,
		"\nBSP: Initializing all data structures...\n");

	MON_LOG(mask_mon, level_trace, "\n\nBSP: Alive.  Local APIC ID=%P\n",
		lapic_id());

	/* check input structure */

	if (startup_struct->version_of_this_struct !=
	    MON_STARTUP_STRUCT_VERSION) {
		MON_LOG(mask_mon, level_error,
			"\nFAILURE: Loader-MON version mismatch (init structure"
			" version mismatch)\n");
		MON_DEADLOOP();
	}
	;
	if (startup_struct->size_of_this_struct !=
	    sizeof(mon_startup_struct_t)) {
		MON_LOG(mask_mon, level_error,
			"\nFAILURE: Loader-MON version mismatch (init structure"
			" size mismatch)\n");
		MON_DEADLOOP();
	}
	;

	addr_setup_address_space();

	if (!mon_stack_initialize(startup_struct)) {
		MON_LOG(mask_mon, level_error,
			"\nFAILURE: Stack initialization failed\n");
		MON_DEADLOOP();
	}

	MON_ASSERT(mon_stack_is_initialized());
	mon_stacks_get_details(&lowest_stacks_addr, &stacks_size);
	MON_LOG(mask_mon, level_trace,
		"\nBSP:Stacks are successfully initialized:\n");
	MON_LOG(mask_mon, level_trace,
		"\tlowest address of all stacks area = %P\n",
		lowest_stacks_addr);
	MON_LOG(mask_mon, level_trace, "\tsize of whole stacks area = %P\n",
		stacks_size);
	MON_DEBUG_CODE(mon_stacks_print());

	/* Initialize Heap */
	heap_address = lowest_stacks_addr + stacks_size;
	heap_size =
		(uint32_t)((startup_struct->mon_memory_layout[mon_image].
			    base_address +
			    startup_struct->mon_memory_layout[mon_image].
			    total_size) -
			   heap_address);
	heap_last_occupied_address =
		mon_heap_initialize(heap_address, heap_size);
	MON_LOG(mask_mon, level_trace,
		"\nBSP:Heap is successfully initialized: \n");
	MON_LOG(mask_mon, level_trace, "\theap base address = %P \n",
		heap_address);
	MON_LOG(mask_mon, level_trace, "\theap last occupied address = %P \n",
		heap_last_occupied_address);
	MON_LOG(mask_mon, level_trace,
		"\tactual size is %P, when requested size was %P\n",
		heap_last_occupied_address - heap_address, heap_size);
	MON_ASSERT(heap_last_occupied_address <=
		(startup_struct->mon_memory_layout[mon_image].base_address +
		 startup_struct->mon_memory_layout[mon_image].total_size));

	/* CLI monitor initialization must be called after heap initialization. */
	cli_monitor_init();

	vmdb_initialize();

	mon_serial_cli_init();

#ifdef DEBUG
	cli_add_command(cli_show_memory_layout,
		"debug memory layout",
		"Print overall memory layout", "", CLI_ACCESS_LEVEL_USER);
#endif
	MON_LOG(mask_mon,
		level_trace,
		"BSP: Original mon_startup_struct_t dump\n");
	MON_DEBUG_CODE(print_startup_struct(startup_struct);)

	/* Copy the startup data to heap.
	 * After this point all pointers points to the same structure in the
	 * heap. */
	startup_struct_heap = mon_create_startup_struct_copy(startup_struct);
	/* overwrite the parameter; */
	startup_struct = startup_struct_heap;
	if (startup_struct_heap == NULL) {
		MON_DEADLOOP();
	}

	/* MON_LOG(mask_mon, level_trace,"BSP: Copied mon_startup_struct_t dump\n");
	 *
	 * MON_DEBUG_CODE( print_startup_struct( startup_struct ); ) */

	application_params_heap =
		mon_create_application_params_struct_copy(
			application_params_struct);
	if ((application_params_struct != NULL)
	    && (application_params_heap == NULL)) {
		MON_DEADLOOP();
	}
	/* overwrite the parameter */
	application_params_struct = application_params_heap;

	/* Initialize GDT for all cpus */
	hw_gdt_setup(num_of_cpus);
	MON_LOG(mask_mon, level_trace, "\nBSP: GDT setup is finished.\n");
	/* Load GDT for BSP */
	hw_gdt_load(cpu_id);
	MON_LOG(mask_mon, level_trace, "BSP: GDT is loaded.\n");

	/* Initialize IDT for all cpus */
	isr_setup();
	MON_LOG(mask_mon, level_trace, "\nBSP: ISR setup is finished. \n");
	/* Load IDT for BSP */
	isr_handling_start();
	MON_LOG(mask_mon, level_trace, "BSP: ISR handling started. \n");

	/* Store information about e820 for legacy boot */
	if (!e820_abstraction_initialize((const int15_e820_memory_map_t *)
		    startup_struct->physical_memory_layout_E820,
		    startup_struct->int15_handler_address)) {
		MON_LOG(mask_mon, level_error,
			"BSP FAILURE: there is no proper e820 map\n");
		MON_DEADLOOP();
	}

	if (!mtrrs_abstraction_bsp_initialize()) {
		MON_LOG(mask_mon,
			level_error,
			"BSP FAILURE: failed to cache mtrrs\n");
		MON_DEADLOOP();
	}
	MON_LOG(mask_mon,
		level_trace,
		"\nBSP: MTRRs were successfully cached.\n");

	/* init MON image parser */
	exec_image_initialize();

	/* Initialize Host Memory Manager */
	if (!hmm_initialize(startup_struct)) {
		MON_LOG(mask_mon, level_error,
			"\nBSP FAILURE: Initialization of Host Memory Manager"
			" has failed\n");
		MON_DEADLOOP();
	}
	MON_LOG(mask_mon, level_trace,
		"\nBSP: Host Memory Manager was successfully initialized. \n");

	hmm_set_required_values_to_control_registers();
	/* PCD and PWT bits will be 0; */
	new_cr3 = hmm_get_mon_page_tables();
	MON_ASSERT(new_cr3 != HMM_INVALID_MON_PAGE_TABLES);
	MON_LOG(mask_mon, level_trace, "BSP: New cr3=%P. \n", new_cr3);
	hw_write_cr3(new_cr3);
	MON_LOG(mask_mon, level_trace,
		"BSP: Successfully updated CR3 to new value\n");
	MON_ASSERT(hw_read_cr3() == new_cr3);

	/* Allocates memory from heap for s3 resume structure on AP's
	 * This should be called before calling mon_heap_extend() in order to
	 * ensure identity mapped memory within 4GB for post-OS launch.
	 * Upon heap extending, the memory allocated may be arbitrary mapped. */
	setup_data_for_s3();
	if (g_is_post_launch) {
		/* To create [0~4G] identity mapping
		 * on NO UG machines (NHM), FPT will be used to handle guest non-paged
		 * protected mode. aka. CR0.PE = 1, CR0.PG = 0
		 * make sure the 32bit FPT pagetables located below 4G physical memory.
		 * assume that GPA-HPA mapping won't be changed.
		 * those page tables are cached. */
		if (!mon_is_unrestricted_guest_supported()) {
			boolean_t fpt_32bit_ok =
				fpt_create_32_bit_flat_page_tables_under_4G(
						(uint64_t)4 GIGABYTES - 1);

			MON_ASSERT(fpt_32bit_ok);

			MON_LOG(mask_mon, level_trace,
				"BSP: Successfully created 32bit FPT tables and"
				" cached them\n");
		}

		MON_LOG(mask_mon, level_trace,
			"MON: Image and stack used %dKB memory.\n",
			((uint64_t)heap_address -
			 startup_struct->mon_memory_layout[mon_image].
			 base_address) /
			(1024));

		MON_ASSERT(g_additional_heap_base != 0);
		heap_last_occupied_address = mon_heap_extend(
			g_additional_heap_base,
			g_heap_pa_num *
			PAGE_4KB_SIZE);

		if (g_additional_heap_pa) {
			build_extend_heap_hpa_to_hva();
		}
	}

	MON_DEBUG_CODE(mon_trace_init(MON_MAX_GUESTS_SUPPORTED, num_of_cpus));
#ifdef PCI_SCAN
	host_pci_initialize();
#endif
	vmcs_hw_init();

	MON_ASSERT(vmcs_hw_is_cpu_vmx_capable());

	/* init CR0/CR4 to the VMX compatible values */
	hw_write_cr0(vmcs_hw_make_compliant_cr0(hw_read_cr0()));
	if (g_is_post_launch) {
		/* clear TS bit, since we need to operate on XMM registers. */
		enable_fx_ops();
	}
	hw_write_cr4(vmcs_hw_make_compliant_cr4(hw_read_cr4()));

	num_of_guests = startup_struct->number_of_secondary_guests + 1;

	clear_policy(&policy);

	if (ept_is_ept_supported()) {
		set_paging_policy(&policy, POL_PG_EPT);
	} else {
		MON_LOG(mask_mon, level_error, "BSP: EPT is not supported\n");

		MON_DEADLOOP();
	}

	mon_setup_cpu_specific_policies(&policy);

	global_policy_setup(&policy);

	scheduler_init((uint16_t)num_of_cpus);
	host_cpu_manager_init(num_of_cpus);
	guest_manager_init((uint16_t)num_of_cpus, (uint16_t)num_of_cpus);
	local_apic_init((uint16_t)num_of_cpus);

	/* create VMEXIT-related data */
	vmexit_initialize();

	/* init current host CPU */
	host_cpu_init();
	local_apic_cpu_init();

	MON_LOG(mask_mon, level_trace, "BSP: Create guests\n");
	primary_guest_startup =
		(const mon_guest_startup_t *)startup_struct->
		primary_guest_startup_state;
	MON_ASSERT(primary_guest_startup);

	secondary_guests_array = (const mon_guest_startup_t *)
				 startup_struct->
				 secondary_guests_startup_state_array;

	MON_ASSERT((num_of_guests == 1) || (secondary_guests_array != 0));

	if (!initialize_all_guests(num_of_cpus,
		    &(startup_struct->mon_memory_layout[mon_image]),
		    primary_guest_startup,
		    num_of_guests - 1,
		    secondary_guests_array,
		    application_params_heap)) {
		MON_LOG(mask_mon, level_error,
			"BSP: Error initializing guests. Halt.\n");
		MON_DEADLOOP();
	}

	MON_LOG(mask_mon, level_trace,
		"BSP: Guests created succefully. number of guests: %d\n",
		guest_count());

	/* should be set only after guests initialized */
	mon_set_state(MON_STATE_BOOT);

	acpi_init_status = mon_acpi_init(fadt_hva);

	/* get important guest ids */
	nmi_owner_guest =
		guest_handle_by_magic_number(startup_struct->nmi_owner);
	acpi_owner_guest = guest_handle_by_magic_number(
		startup_struct->acpi_owner);
	device_default_owner_guest =
		guest_handle_by_magic_number(
			startup_struct->default_device_owner);

	MON_ASSERT(nmi_owner_guest);
	MON_ASSERT(acpi_owner_guest);
	MON_ASSERT(device_default_owner_guest);

	guest_set_nmi_owner(nmi_owner_guest);
	if (acpi_init_status) {
		guest_set_acpi_owner(acpi_owner_guest);
	}
	guest_set_default_device_owner(device_default_owner_guest);

	MON_LOG(mask_mon, level_trace,
		"BSP: nmi owning guest ID=%d \tMagic number = %#x\n",
		guest_get_id(nmi_owner_guest),
		guest_magic_number(nmi_owner_guest));

	MON_LOG(mask_mon, level_trace,
		"BSP: ACPI owning guest ID=%d \tMagic number = %#x\n",
		guest_get_id(acpi_owner_guest),
		guest_magic_number(acpi_owner_guest));

	MON_LOG(mask_mon, level_trace,
		"BSP: Default device owning guest ID=%d \tMagic number = %#x\n",
		guest_get_id(device_default_owner_guest),
		guest_magic_number(device_default_owner_guest));

	/* Initialize Event Manager
	 * must be called after heap and CLI initialization */
	event_manager_initialize(num_of_cpus);
#ifdef PCI_SCAN
	gpci_initialize();
#endif

	if (!nmi_manager_initialize(num_of_cpus)) {
		MON_LOG(mask_mon, level_trace,
			"\nFAILURE: IPC initialization failed\n");
		MON_DEADLOOP();
	}
	for (i = 0; i < MON_MAX_CPU_SUPPORTED; i++)
		vmcs_sw_shadow_disable[i] = FALSE;

	if (g_is_post_launch) {
		if (INVALID_PHYSICAL_ADDRESS ==
		    application_params_struct->fadt_gpa ||
		    !gpm_gpa_to_hva(gcpu_get_current_gpm(acpi_owner_guest),
			    (gpa_t)(application_params_struct->fadt_gpa),
			    &fadt_hva)) {
			fadt_hva = 0;
		}
	}

	/* init all addon packages */
	start_addons(num_of_cpus, startup_struct_heap, application_params_heap);

	mem_config.img_start_gpa =
		startup_struct->mon_memory_layout[mon_image].base_address;
	mem_config.img_end_gpa = mem_config.img_start_gpa +
				 (uint64_t)(startup_struct->mon_memory_layout[
						    mon_image].image_size);
	mem_config.heap_start_gpa = (uint64_t)heap_address;
	mem_config.heap_end_gpa = (uint64_t)heap_last_occupied_address;

	/* Destroy startup structures, which reside in heap */
	mon_destroy_startup_struct(startup_struct_heap);
	startup_struct = NULL;
	startup_struct_heap = NULL;

	mon_destroy_application_params_struct(application_params_heap);
	application_params_struct = NULL;
	application_params_heap = NULL;

	vmcs_hw_allocate_vmxon_regions(num_of_cpus);

	/* Initialize guest data */
	initialization_data.num_of_cpus = (uint16_t)num_of_cpus;
	initialization_data.mem_config = &mem_config;
	for (i = 0; i < MON_MAX_GUESTS_SUPPORTED; i++) {
		initialization_data.guest_data[i].guest_id = INVALID_GUEST_ID;
		initialization_data.guest_data[i].primary_guest = FALSE;
	}
	if (num_of_guests > MON_MAX_GUESTS_SUPPORTED) {
		MON_LOG(mask_mon,
			level_error,
			"%s: %d guests not supported by MON.\n",
			__FUNCTION__,
			num_of_guests);
	} else {
		for (guest = guest_first(&guest_ctx), i = 0; guest;
		     guest = guest_next(&guest_ctx), i++) {
			initialization_data.guest_data[i].guest_id =
				guest_get_id(guest);
			if (guest_is_primary(guest)) {
				initialization_data.guest_data[i].primary_guest
					= TRUE;
			}
		}
	}
	if (!report_mon_event
		    (MON_EVENT_INITIALIZATION_BEFORE_APS_STARTED, NULL, NULL,
		    (void *)&initialization_data)) {
		MON_LOG(mask_mon,
			level_trace,
			"report_initialization failed before the APs have started\n");
	}

	MON_LOG(mask_mon, level_trace,
		"BSP: Successfully finished single-core initializations\n");

	mon_set_state(MON_STATE_WAIT_FOR_APS);

	LAUNCH_APPLICATION_PROCS();

	initialize_host_vmcs_regions(cpu_id);

	MON_LOG(mask_mon, level_trace,
		"BSP: Successfully finished initializations\n");

	vmcs_hw_vmx_on();
	MON_LOG(mask_mon, level_trace, "BSP: VMXON\n");

	/* schedule first gcpu */
	initial_gcpu = scheduler_select_initial_gcpu();
	MON_ASSERT(initial_gcpu != NULL);
	MON_LOG(mask_mon,
		level_trace,
		"BSP: initial guest selected: guest_id_t: %d GUEST_CPU_ID: %d\n",
		mon_guest_vcpu(initial_gcpu)->guest_id,
		mon_guest_vcpu(initial_gcpu)->guest_cpu_id);

	ipc_change_state_to_active(initial_gcpu);

	MON_LOG(mask_mon, level_trace,
		"BSP: Wait for APs to launch the first Guest CPU\n");

	WAIT_FOR_APPLICATION_PROCS_LAUNCHED_THE_GUEST(num_of_cpus - 1);

	/* Assumption: initialization_data was not changed */
	if (!report_mon_event
		    (MON_EVENT_INITIALIZATION_AFTER_APS_STARTED,
		    (mon_identification_data_t)initial_gcpu,
		    (const guest_vcpu_t *)mon_guest_vcpu(initial_gcpu),
		    (void *)&initialization_data)) {
		MON_LOG(mask_mon, level_trace,
			"report_initialization failed after the APs have"
			" launched the guest\n");
	}

	mon_set_state(MON_STATE_RUN);

	MON_LOG(mask_mon, level_trace, "BSP: Resuming the first Guest CPU\n");

	event_raise(EVENT_GUEST_LAUNCH, initial_gcpu, &local_apic_id);

	/* enable unrestricted guest support in early boot
	 * make guest state compliant for code execution
	 * On systems w/o UG, emulator takes care of it */
	if (mon_is_unrestricted_guest_supported()) {
		make_guest_state_compliant(initial_gcpu);
		mon_unrestricted_guest_enable(initial_gcpu);
	} else {
		/* For non-UG systems enable EPT, if guest is in paging mode */
		em64t_cr0_t guest_cr0;
		guest_cr0.uint64 =
			gcpu_get_guest_visible_control_reg(initial_gcpu,
				IA32_CTRL_CR0);
		if (guest_cr0.bits.pg) {
			enable_ept_during_launch(initial_gcpu);
		}
	}

	if (fvs_is_eptp_switching_supported()) {
		fvs_guest_vmfunc_enable(initial_gcpu);
		fvs_vmfunc_vmcs_init(initial_gcpu);
	}

	vmcs_store_initial(initial_gcpu, cpu_id);
	gcpu_resume(initial_gcpu);

	MON_LOG(mask_mon, level_error, "BSP: Resume initial guest cpu failed\n",
		cpu_id);
	MON_DEADLOOP();
}

/*------------------------------------------------------------------------
 *
 * The Application Processor main routine
 *
 * Should never return!
 *
 *------------------------------------------------------------------------ */
static
void mon_application_procs_main(uint32_t local_apic_id)
{
	cpu_id_t cpu_id = (cpu_id_t)local_apic_id;
	hpa_t new_cr3 = 0;
	guest_cpu_handle_t initial_gcpu = NULL;

	WAIT_FOR_APPLICATION_PROCS_LAUNCH();

	MON_LOG(mask_mon, level_trace, "\n\nAP%d: Alive.  Local APIC ID=%P\n",
		cpu_id, lapic_id());

	/* Load GDT/IDT */
	hw_gdt_load(cpu_id);
	MON_LOG(mask_mon, level_trace, "AP%d: GDT is loaded.\n", cpu_id);
	isr_handling_start();
	MON_LOG(mask_mon, level_trace, "AP%d: ISR handling started.\n", cpu_id);

	if (!mtrrs_abstraction_ap_initialize()) {
		MON_LOG(mask_mon,
			level_error,
			"AP%d FAILURE: Failed to cache MTRRs\n",
			cpu_id);
		MON_DEADLOOP();
	}
	MON_LOG(mask_mon, level_trace, "AP%d: MTRRs were successfully cached\n",
		cpu_id);

	/* Set new CR3 to MON page tables */
	hmm_set_required_values_to_control_registers();
	new_cr3 = hmm_get_mon_page_tables();
	MON_ASSERT(new_cr3 != HMM_INVALID_MON_PAGE_TABLES);
	MON_LOG(mask_mon, level_trace, "AP%d: New cr3=%P. \n", cpu_id, new_cr3);
	hw_write_cr3(new_cr3);
	MON_LOG(mask_mon, level_trace,
		"AP%d: Successfully updated CR3 to new value\n", cpu_id);
	MON_ASSERT(hw_read_cr3() == new_cr3);

	MON_ASSERT(vmcs_hw_is_cpu_vmx_capable());

	/* init CR0/CR4 to the VMX compatible values */
	hw_write_cr0(vmcs_hw_make_compliant_cr0(hw_read_cr0()));
	if (g_is_post_launch) {
		/* clear TS bit, since we need to operate on XMM registers. */
		enable_fx_ops();
	}
	hw_write_cr4(vmcs_hw_make_compliant_cr4(hw_read_cr4()));

	host_cpu_init();
	local_apic_cpu_init();

	initialize_host_vmcs_regions(cpu_id);
	MON_LOG(mask_mon, level_trace,
		"AP%d: Successfully finished initializations\n", cpu_id);

	vmcs_hw_vmx_on();
	MON_LOG(mask_mon, level_trace, "AP%d: VMXON\n", cpu_id);

	/* schedule first gcpu */
	initial_gcpu = scheduler_select_initial_gcpu();
	MON_ASSERT(initial_gcpu != NULL);
	MON_LOG(mask_mon,
		level_trace,
		"AP%d: initial guest selected: guest_id_t: %d GUEST_CPU_ID: %d\n",
		cpu_id,
		mon_guest_vcpu(initial_gcpu)->guest_id,
		mon_guest_vcpu(initial_gcpu)->guest_cpu_id);

	ipc_change_state_to_active(initial_gcpu);

	APPLICATION_PROC_LAUNCHING_THE_GUEST();

	MON_LOG(mask_mon, level_trace, "AP%d: Resuming the first Guest CPU\n",
		cpu_id);

	event_raise(EVENT_GUEST_LAUNCH, initial_gcpu, &local_apic_id);
	/* enable unrestricted guest support in early boot
	 * make guest state compliant for code execution
	 * On systems w/o UG, emulator takes care of it */
	if (mon_is_unrestricted_guest_supported()) {
		make_guest_state_compliant(initial_gcpu);
		mon_unrestricted_guest_enable(initial_gcpu);
	} else {
		/* For non-UG systems enable EPT, if guest is in paging mode */
		em64t_cr0_t guest_cr0;
		guest_cr0.uint64 =
			gcpu_get_guest_visible_control_reg(initial_gcpu,
				IA32_CTRL_CR0);
		if (guest_cr0.bits.pg) {
			enable_ept_during_launch(initial_gcpu);
		}
	}

	if (fvs_is_eptp_switching_supported()) {
		fvs_guest_vmfunc_enable(initial_gcpu);
		fvs_vmfunc_vmcs_init(initial_gcpu);
	}

	vmcs_store_initial(initial_gcpu, cpu_id);
	gcpu_resume(initial_gcpu);

	MON_LOG(mask_mon,
		level_error,
		"AP%d: Resume initial guest cpu failed\n",
		cpu_id);
	MON_DEADLOOP();
}

static
void make_guest_state_compliant(guest_cpu_handle_t initial_gcpu)
{
	uint16_t selector;
	uint64_t base;
	uint32_t limit;
	uint32_t attr;
	uint32_t idx;
	uint64_t cr0;

	cr0 = gcpu_get_guest_visible_control_reg(initial_gcpu, IA32_CTRL_CR0);
	if (!(cr0 & CR0_PE)) {
		/* for guest to execute real mode code
		 * its state needs to be in certain way
		 * this code enforces it */
		for (idx = IA32_SEG_CS; idx < IA32_SEG_COUNT; ++idx) {
			gcpu_get_segment_reg(initial_gcpu,
				(mon_ia32_segment_registers_t)idx,
				&selector, &base, &limit, &attr);
			make_segreg_hw_real_mode_compliant(initial_gcpu,
				selector,
				base,
				limit,
				attr,
				(mon_ia32_segment_registers_t)
				idx);
		}
		MON_LOG(mask_mon,
			level_info,
			"BSP: guest compliant in real mode  for UG early boot.\n");
	}
}

#ifdef DEBUG
int cli_show_memory_layout(unsigned argc UNUSED, char *args[] UNUSED)
{
	CLI_PRINT(" Memory Layout :      MON        :      Thunk\n");
	CLI_PRINT("---------------:------------------:-----------------\n");
	CLI_PRINT(" Base address  : %16P : %16P\n",
		mon_startup_data.mon_memory_layout[mon_image].base_address,
		mon_startup_data.mon_memory_layout[thunk_image].base_address);
	CLI_PRINT(" Entry Point   : %16P : %16P\n",
		mon_startup_data.mon_memory_layout[mon_image].entry_point,
		mon_startup_data.mon_memory_layout[thunk_image].entry_point);
	CLI_PRINT(" Image size    : %16P : %16P\n",
		mon_startup_data.mon_memory_layout[mon_image].image_size,
		mon_startup_data.mon_memory_layout[thunk_image].image_size);
	CLI_PRINT(" Total size    : %16P : %16P\n",
		mon_startup_data.mon_memory_layout[mon_image].total_size,
		mon_startup_data.mon_memory_layout[thunk_image].total_size);
	return 0;
}
#endif
