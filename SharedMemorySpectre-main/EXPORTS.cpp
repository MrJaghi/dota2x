#include "EXPORTS.h"



namespace EXPORTS {
	PVOID mm_get_system_routine_address(
		PUNICODE_STRING SystemRoutineName
	) {


		auto function_address = NTOS::find_export<addr_t>(E("MmGetSystemRoutineAddress"));
		if (!function_address) {
			return {};
		}
		using function_t = PVOID(
			PUNICODE_STRING SystemRoutineName
		);
		return reinterpret_cast<function_t*>(function_address)(SystemRoutineName);
	}

	inline addr_t c_ex_get_previous_mode = NULL;
	KPROCESSOR_MODE ex_get_previous_mode() {
		if (c_ex_get_previous_mode == NULL) {
			c_ex_get_previous_mode = NTOS::find_export<addr_t>(E("ExGetPreviousMode"));
			if (!c_ex_get_previous_mode) {
				return NULL;
			}
		}

		using function_t = KPROCESSOR_MODE(*)();
		KPROCESSOR_MODE mode = reinterpret_cast<function_t>(c_ex_get_previous_mode)();
		return mode;
	}

	NTSTATUS zw_query_system_information(
		SYSTEM_INFORMATION_CLASS SystemInformationClass,
		PVOID                    SystemInformation,
		ULONG                    SystemInformationLength,
		PULONG                   ReturnLength
	) {


		auto function_address = NTOS::find_export<addr_t>(E("ZwQuerySystemInformation"));

		if (!function_address) {
			return STATUS_UNSUCCESSFUL;
		}

		using function_t = NTSTATUS(
			SYSTEM_INFORMATION_CLASS,
			PVOID,
			ULONG,
			PULONG
		);

		return reinterpret_cast<function_t*>(function_address)(
			SystemInformationClass,
			SystemInformation,
			SystemInformationLength,
			ReturnLength
			);
	}
	void mm_unmap_locked_pages(PVOID map, PMDL mdl) {
		auto function_address = NTOS::find_export<addr_t>(E("MmUnmapLockedPages"));
		if (!function_address) {
			return;
		}

		using function_t = void(*)(PVOID, PMDL);
		reinterpret_cast<function_t>(function_address)(map, mdl);
	}

	void mm_unlock_pages(PMDL mdl) {
		auto function_address = NTOS::find_export<addr_t>(E("MmUnlockPages"));
		if (!function_address) {
			return;
		}

		using function_t = void(*)(PMDL);
		reinterpret_cast<function_t>(function_address)(mdl);
	}

	void io_free_mdl(PMDL mdl) {
		auto function_address = NTOS::find_export<addr_t>(E("IoFreeMdl"));
		if (!function_address) {
			return;
		}

		using function_t = void(*)(PMDL);
		reinterpret_cast<function_t>(function_address)(mdl);
	}

	void mm_protect_mdl_system_address(PMDL mdl, ULONG protection) {
		auto function_address = NTOS::find_export<addr_t>(E("MmProtectMdlSystemAddress"));
		if (!function_address) {
			return;
		}

		using function_t = void(*)(PMDL, ULONG);
		reinterpret_cast<function_t>(function_address)(mdl, protection);
	}

	void mm_map_locked_pages_specify_cache(PMDL mdl, KPROCESSOR_MODE mode, ULONG cache_type, ULONG frame_size, ULONG priority) {
		auto function_address = NTOS::find_export<addr_t>(E("MmMapLockedPagesSpecifyCache"));
		if (!function_address) {
			return;
		}

		using function_t = PVOID(*)(PMDL, KPROCESSOR_MODE, ULONG, ULONG, ULONG);
		reinterpret_cast<function_t>(function_address)(mdl, mode, cache_type, frame_size, priority);
	}

	void mm_probe_and_lock_pages(PMDL mdl, KPROCESSOR_MODE mode, LOCK_OPERATION operation) {
		auto function_address = NTOS::find_export<addr_t>(E("MmProbeAndLockPages"));
		if (!function_address) {
			return;
		}

		using function_t = NTSTATUS(*)(PMDL, KPROCESSOR_MODE, LOCK_OPERATION);
		reinterpret_cast<function_t>(function_address)(mdl, mode, operation);
	}

	PMDL io_allocate_mdl(PVOID address, SIZE_T size, BOOLEAN secondary_buffer, BOOLEAN charge_quota, PIRP irp) {
		auto function_address = NTOS::find_export<addr_t>(E("IoAllocateMdl"));
		if (!function_address) {
			return nullptr;
		}

		using function_t = PMDL(*)(PVOID, SIZE_T, BOOLEAN, BOOLEAN, PIRP);
		return reinterpret_cast<function_t>(function_address)(address, size, secondary_buffer, charge_quota, irp);
	}

	void ke_stack_attach_process(PEPROCESS process, KAPC_STATE* apc_state) {
		auto function_address = NTOS::find_export<addr_t>(E("KeStackAttachProcess"));
		if (!function_address) {
			return;
		}

		using function_t = void(*)(PEPROCESS, KAPC_STATE*);
		reinterpret_cast<function_t>(function_address)(process, apc_state);
	}

	void ke_unstack_detach_process(KAPC_STATE* apc_state) {
		auto function_address = NTOS::find_export<addr_t>(E("KeUnstackDetachProcess"));
		if (!function_address) {
			return;
		}

		using function_t = void(*)(KAPC_STATE*);
		reinterpret_cast<function_t>(function_address)(apc_state);
	}

	void ob_dereference_object(PVOID object) {
		auto function_address = NTOS::find_export<addr_t>(E("ObDereferenceObject"));
		if (!function_address) {
			return;
		}

		using function_t = void(*)(PVOID);
		reinterpret_cast<function_t>(function_address)(object);
	}

	NTSTATUS rtl_compare_unicode_string(PUNICODE_STRING string1, PUNICODE_STRING string2, BOOLEAN case_insensitive) {
		auto function_address = NTOS::find_export<addr_t>(E("RtlCompareUnicodeString"));
		if (!function_address) {
			return 0;
		}

		using function_t = NTSTATUS(*)(PUNICODE_STRING, PUNICODE_STRING, BOOLEAN);
		return reinterpret_cast<function_t>(function_address)(string1, string2, case_insensitive);
	}


	PVOID ex_allocate_pool_with_tag(POOL_TYPE pool_type, SIZE_T size, ULONG tag) {
		auto function_address = NTOS::find_export<addr_t>(E("ExAllocatePoolWithTag"));
		if (!function_address) {
			return nullptr;
		}

		using function_t = PVOID(*)(POOL_TYPE, SIZE_T, ULONG);
		return reinterpret_cast<function_t>(function_address)(pool_type, size, tag);
	}


	ULONG get_current_processor_index() {
		auto function_address = NTOS::find_export<addr_t>(E("KeGetCurrentProcessorIndex"));
		if (!function_address) {
			return 0;
		}

		using function_t = ULONG(*)();
		return reinterpret_cast<function_t>(function_address)();
	}


	inline addr_t c_get_virtual_for_physical = NULL;
	PVOID get_virtual_for_physical(PHYSICAL_ADDRESS physical_address) {
		if (c_get_virtual_for_physical == NULL) {
			c_get_virtual_for_physical = NTOS::find_export<addr_t>(E("MmGetVirtualForPhysical"));
			if (!c_get_virtual_for_physical) {
				return nullptr;
			}
		}


		using function_t = PVOID(*)(PHYSICAL_ADDRESS);
		return reinterpret_cast<function_t>(c_get_virtual_for_physical)(physical_address);
	}

	void* mm_allocate_contiguous_memory(
		SIZE_T NumberOfBytes,
		PHYSICAL_ADDRESS HighestAcceptableAddress
	) {


		auto function_address = NTOS::find_export<addr_t>(E("MmAllocateContiguousMemory"));
		if (!function_address) {
			return nullptr;
		}

		using function_t = void* (
			SIZE_T NumberOfBytes,
			PHYSICAL_ADDRESS HighestAcceptableAddress
			);

		return reinterpret_cast<function_t*>(function_address)(
			NumberOfBytes,
			HighestAcceptableAddress
			);
	};

	inline addr_t c_ps_lookup_process_by_process_id = NULL;
	NTSTATUS ps_lookup_process_by_process_id(HANDLE ProcessID, PEPROCESS* Process) {

		if (c_ps_lookup_process_by_process_id == NULL) {
			c_ps_lookup_process_by_process_id = NTOS::find_export<addr_t>(E("PsLookupProcessByProcessId"));
			if (!c_ps_lookup_process_by_process_id) {
				return STATUS_NOT_FOUND;
			}
		}

		using function_t = NTSTATUS(
			HANDLE ProcessID,
			PEPROCESS* Process
		);
		return reinterpret_cast<function_t*>(c_ps_lookup_process_by_process_id)(ProcessID, Process);
	}

	void ex_release_resource_lite(PERESOURCE Resource) {


		auto function_address = NTOS::find_export<addr_t>(E("ExReleaseResourceLite"));
		if (!function_address) {
			return;
		}

		using function_t = void(
			PERESOURCE Resource
			);

		return reinterpret_cast<function_t*>(function_address)(Resource);
	}

	BOOLEAN rtl_delete_element_generic_table_avl(PRTL_AVL_TABLE Table, PVOID Buffer) {


		auto function_address = NTOS::find_export<addr_t>(E("RtlDeleteElementGenericTableAvl"));
		if (!function_address) {
			return false;
		}

		using function_t = BOOLEAN(
			PRTL_AVL_TABLE Table, PVOID Buffer
		);

		return reinterpret_cast<function_t*>(function_address)(Table, Buffer);
	}

	PVOID rtl_lookup_element_generic_table_avl(PRTL_AVL_TABLE Table, PVOID Buffer) {


		auto function_address = NTOS::find_export<addr_t>(E("RtlLookupElementGenericTableAvl"));
		if (!function_address) {
			return nullptr;
		}

		using function_t = PVOID(
			PRTL_AVL_TABLE Table, PVOID Buffer
		);

		return reinterpret_cast<function_t*>(function_address)(Table, Buffer);
	}

	void ke_query_system_time_precise(PLARGE_INTEGER CurrentTime) {


		auto function_address = NTOS::find_export<addr_t>(E("KeQuerySystemTimePrecise"));
		if (!function_address) {
			return;
		}

		using function_t = void(
			PLARGE_INTEGER CurrentTime
			);

		return reinterpret_cast<function_t*>(function_address)(
			CurrentTime
			);
	}

	void ex_free_pool_with_tag(
		void* base_address,
		ULONG tag
	) {
		auto function_address = NTOS::find_export<addr_t>(E("ExFreePoolWithTag"));
		if (!function_address) {
			return;
		}

		using function_t = void(
			void* base_address,
			ULONG tag
			);

		reinterpret_cast<function_t*>(function_address) (
			base_address,
			tag);
	}

	void* ex_allocate_pool(
		POOL_TYPE pool_type,
		SIZE_T number_of_bytes
	) {
		auto function_address = NTOS::find_export<addr_t>(E("ExAllocatePool"));
		if (!function_address) {
			return nullptr;
		}

		using function_t = void* (
			POOL_TYPE pool_type,
			SIZE_T number_of_bytes
			);

		return reinterpret_cast<function_t*>(function_address)(
			pool_type,
			number_of_bytes);
	}

	BOOLEAN ex_acquire_exclusive_lite(PERESOURCE PiDDBLock, BOOL Wait) {


		auto function_address = NTOS::find_export<addr_t>(E("ExAcquireResourceExclusiveLite"));
		if (!function_address) {
			return false;
		}

		using function_t = BOOLEAN(
			PERESOURCE PiDDBLock, BOOL Wait
		);

		return reinterpret_cast<function_t*>(function_address)(PiDDBLock, Wait);
	}

	[[ nodiscard ]]
	void rtl_init_unicode_string(
		PUNICODE_STRING destination_string,
		PCWSTR source_string
	) {
		auto function_address = NTOS::find_export<addr_t>(E("RtlInitUnicodeString"));
		if (!function_address) {
			return;
		}

		using function_t = void* (
			PUNICODE_STRING destination_string,
			PCWSTR source_string
			);

		reinterpret_cast<function_t*>(function_address) (
			destination_string,
			source_string);
	}


	NTSTATUS ZwQuerySystemInformation_impl(
		SYSTEM_INFORMATION_CLASS SystemInformationClass,
		PVOID                    SystemInformation,
		ULONG                    SystemInformationLength,
		PULONG                   ReturnLength
	) {
		auto function_address = NTOS::find_export<addr_t>(E("ZwQuerySystemInformation"));
		if (!function_address) {
			return STATUS_UNSUCCESSFUL;
		}
		using function_t = NTSTATUS(
			SYSTEM_INFORMATION_CLASS,
			PVOID,
			ULONG,
			PULONG
		);
		return reinterpret_cast<function_t*>(function_address)(
			SystemInformationClass,
			SystemInformation,
			SystemInformationLength,
			ReturnLength
			);
	}

	uintptr_t ps_initial_system_process() {
		auto function_address = NTOS::find_export<addr_t>(E("PsInitialSystemProcess"));
		if (!function_address) {
			return {};
		}

		return *reinterpret_cast<uintptr_t*>(function_address);
	}

	bool ps_get_process_exit_status(
		uintptr_t Process
	) {
		auto function_address = NTOS::find_export<addr_t>(E("PsGetProcessExitStatus"));
		if (!function_address) {
			return false;
		}

		using function_t = NTSTATUS(
			uintptr_t Process
		);

		return reinterpret_cast<function_t*>(function_address)(Process) == 0x103;
	}

	unicode_string_t* ps_query_full_process_image_name(
		std::uintptr_t process
	) {
		auto function_address = NTOS::find_export<addr_t>(E("SeLocateProcessImageName"));
		if (!function_address) return { };

		while (function_address[0x0] != 0xec
			|| function_address[0x1] != 0x28
			|| function_address[0x2] != 0xe8)
			function_address++;

		auto ps_rva{ &function_address[0x7] + *reinterpret_cast<std::int32_t*>(&function_address[0x3]) };
		if (!ps_rva)
			return {};

		while (ps_rva[0x0] != 0x0f
			|| ps_rva[0x1] != 0x85
			|| ps_rva[0x6] != 0x48)
			ps_rva++;

		return *reinterpret_cast<unicode_string_t**>
			(process + *reinterpret_cast<std::int32_t*>(&ps_rva[0x9]));
	}

	void* ps_get_process_section_base_address(
		PEPROCESS Process
	) {
		auto function_address = NTOS::find_export<addr_t>(E("PsGetProcessSectionBaseAddress"));
		if (!function_address) {
			return nullptr;
		}

		using function_t = void* (
			PEPROCESS Process
			);

		return reinterpret_cast<function_t*>(function_address)(Process);
	}

	PPEB ps_get_process_peb(PEPROCESS Process) {
		auto function_address = NTOS::find_export<addr_t>(E("PsGetProcessPeb"));
		if (!function_address) {
			return nullptr;
		}

		using function_t = PPEB(
			PEPROCESS Process
		);

		return reinterpret_cast<function_t*>(function_address)(Process);
	}

	PKTHREAD ke_get_current_thread() {


		auto function_address = NTOS::find_export<addr_t>(E("KeGetCurrentThread"));
		if (!function_address) {
			return nullptr;
		}

		using function_t = PKTHREAD(
		);
		return reinterpret_cast<function_t*>(function_address)();
	}
}
