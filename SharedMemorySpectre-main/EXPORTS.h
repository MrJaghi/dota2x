#pragma once
#include "includes.h"
#include <cstdint>

using addr_t = unsigned char*;
using double_t = double;
using float_t = float;
using function_t = HANDLE(
	PEPROCESS Process
);

namespace EXPORTS {
	PVOID mm_get_system_routine_address(
		PUNICODE_STRING SystemRoutineName
	);

	KPROCESSOR_MODE ex_get_previous_mode();

	void mm_unmap_locked_pages(PVOID map, PMDL mdl);
	void mm_unlock_pages(PMDL mdl);
	void io_free_mdl(PMDL mdl);
	void mm_protect_mdl_system_address(PMDL mdl, ULONG protection);
	void mm_map_locked_pages_specify_cache(PMDL mdl, KPROCESSOR_MODE mode, ULONG cache_type, ULONG frame_size, ULONG priority);
	void mm_probe_and_lock_pages(PMDL mdl, KPROCESSOR_MODE mode, LOCK_OPERATION operation);
	PMDL io_allocate_mdl(PVOID address, SIZE_T size, BOOLEAN secondary_buffer, BOOLEAN charge_quota, PIRP irp);

	void ke_stack_attach_process(PEPROCESS process, KAPC_STATE* apc_state);

	void ke_unstack_detach_process(KAPC_STATE* apc_state);

	void ob_dereference_object(PVOID object);

	NTSTATUS rtl_compare_unicode_string(PUNICODE_STRING string1, PUNICODE_STRING string2, BOOLEAN case_insensitive);

	PVOID ex_allocate_pool_with_tag(POOL_TYPE pool_type, SIZE_T size, ULONG tag);

	ULONG get_current_processor_index();

	PVOID get_virtual_for_physical(PHYSICAL_ADDRESS physical_address);

	void* mm_allocate_contiguous_memory(
		SIZE_T NumberOfBytes,
		PHYSICAL_ADDRESS HighestAcceptableAddress
	);
	NTSTATUS zw_query_system_information(
		SYSTEM_INFORMATION_CLASS SystemInformationClass,
		PVOID                    SystemInformation,
		ULONG                    SystemInformationLength,
		PULONG                   ReturnLength
	);
	NTSTATUS ps_lookup_process_by_process_id(HANDLE ProcessID, PEPROCESS* Process);

	void ex_release_resource_lite(PERESOURCE Resource);

	BOOLEAN rtl_delete_element_generic_table_avl(PRTL_AVL_TABLE Table, PVOID Buffer);
	PVOID rtl_lookup_element_generic_table_avl(PRTL_AVL_TABLE Table, PVOID Buffer);

	void ke_query_system_time_precise(PLARGE_INTEGER CurrentTime);

	void ex_free_pool_with_tag(
		void* base_address,
		ULONG tag
	);

	void* ex_allocate_pool(
		POOL_TYPE pool_type,
		SIZE_T number_of_bytes
	);

	BOOLEAN ex_acquire_exclusive_lite(PERESOURCE PiDDBLock, BOOL Wait);

	[[ nodiscard ]]
	void rtl_init_unicode_string(
		PUNICODE_STRING destination_string,
		PCWSTR source_string
	);

	NTSTATUS ZwQuerySystemInformation_impl(
		SYSTEM_INFORMATION_CLASS SystemInformationClass,
		PVOID                    SystemInformation,
		ULONG                    SystemInformationLength,
		PULONG                   ReturnLength
	);

	uintptr_t ps_initial_system_process();

	bool ps_get_process_exit_status(
		uintptr_t Process
	);

	unicode_string_t* ps_query_full_process_image_name(
		std::uintptr_t process
	);

	void* ps_get_process_section_base_address(
		PEPROCESS Process
	);

	PPEB ps_get_process_peb(PEPROCESS Process);

	PKTHREAD ke_get_current_thread();
}