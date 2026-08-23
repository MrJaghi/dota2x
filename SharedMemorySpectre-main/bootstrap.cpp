#include "bootstrap.h"

namespace bootstrap {
	NTSTATUS mm_piddb(UNICODE_STRING DriverName, ULONG timeDateStamp, piddb_type type, PiDDBCacheEntry* replace_link) {
		SPOOF_FUNC;

		PERESOURCE PiDDBLock; PRTL_AVL_TABLE PiDDBCacheTable;

		uintptr_t PiDDBLockPtr = NULL;
		uintptr_t PiDDBCacheTablePtr = NULL;

		/* Win10 Signature */
		PiDDBLockPtr = (uintptr_t) UMEM::find_pattern(
			(PVOID) ntos_image_base,
			E("\x8B\xD8\x85\xC0\x0F\x88\x00\x00\x00\x00\x65\x48\x8B\x04\x25\x00\x00\x00\x00\x66\xFF\x88\x00\x00\x00\x00\xB2\x00\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x4C\x8B\x8C\x24\x00\x00\x00\x00\x48\x8D\x4C\x24"),
			E("xxxxxx????xxxxx????xxx????x?xxx????x????xxxx????xxxx"));
		if (PiDDBLockPtr == NULL) {
			/* Win11 Signature */
			PiDDBLockPtr = (uintptr_t)UMEM::find_pattern(
				(PVOID) ntos_image_base,
				E("\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x0F\x85\x00\x00\x00\x00\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xE8"),
				E("xxx????xxxxx????xxx????x????x"));
			if (PiDDBLockPtr == NULL) {
				/* Both Failed */
				silence(E("[vmm.kernelbootstrap!err] Failed to find any PiDDBLock Address\n"));
				return STATUS_FAIL_CHECK;
			}
			else {
				PiDDBLockPtr = PiDDBLockPtr + 16;
			}
		}
		else {
			PiDDBLockPtr = PiDDBLockPtr + 28;
		}

		silence(E("[vmm.kernelbootstrap] Found PiDDBLock Address\n"));
		PiDDBCacheTablePtr = (uintptr_t)UMEM::find_pattern(
			(PVOID) ntos_image_base,
			E("\x66\x03\xD2\x48\x8D\x0D"),
			E("xxxxxx")
		);
		if (PiDDBCacheTablePtr == NULL) {
			silence(E("[vmm.kernelbootstrap!err] Failed to find any PiDDBCacheTable Address\n"));
			return STATUS_FAIL_CHECK;
		}
		silence(E("[vmm.kernelbootstrap] Found PiDDBCacheTable Address\n"));

		PiDDBCacheTablePtr = PiDDBCacheTablePtr + 3;

		PiDDBLock = (PERESOURCE)(resolve_relative_address((PVOID)PiDDBLockPtr, 3, 7));
		PiDDBCacheTable = (PRTL_AVL_TABLE)(resolve_relative_address((PVOID)PiDDBCacheTablePtr, 3, 7));


		if (PiDDBLock == nullptr || PiDDBCacheTable == nullptr) {

			silence(E("[vmm.kernelbootstrap!err] PiDDBLock [0x%p]\n"), PiDDBLock);
			silence(E("[vmm.kernelbootstrap!err] PiDDBCacheTable [0x%p]\n"), PiDDBCacheTable);

			silence(E("[vmm.kernelbootstrap!err] PiDDBLockPtr [0x%llX]\n"), PiDDBLockPtr);
			silence(E("[vmm.kernelbootstrap!err] PiDDBCacheTablePtr [0x%llX]\n"), PiDDBCacheTablePtr);

			silence(E("[vmm.kernelbootstrap!err] PiDDBLock or PiDDBCacheTable is Invalid\n"));
			return STATUS_FAIL_CHECK;
		}

		//silence(E("[vmm.kernelbootstrap] Found PiDDBLock [0x%p]\n"), PiDDBLock);
		//silence(E("[vmm.kernelbootstrap] Found PiDDBCacheTable [0x%p]\n"), PiDDBCacheTable);

		//silence(E("[vmm.kernelbootstrap] Found PiDDBLockPtr [0x%llX]\n"), PiDDBLockPtr);
		//silence(E("[vmm.kernelbootstrap] Found PiDDBCacheTablePtr [0x%llX]\n"), PiDDBCacheTablePtr);

		//
		PiDDBCacheEntry lookupEntry = { };
		lookupEntry.DriverName = DriverName;
		if (timeDateStamp != NULL) {
			lookupEntry.TimeDateStamp = timeDateStamp;
		}

		EXPORTS::ex_acquire_exclusive_lite(PiDDBLock, TRUE);
		auto pFoundEntry = (PiDDBCacheEntry*)EXPORTS::rtl_lookup_element_generic_table_avl(PiDDBCacheTable, &lookupEntry);
		if (pFoundEntry == nullptr)
		{
			ExReleaseResourceLite(PiDDBLock);
			silence(E("[vmm.kernelbootstrap!err] Could not get AvlTable for Module\n"));
			return STATUS_FAIL_CHECK;
		}

		if (type == piddb_type::UNLINK) {
			RemoveEntryList(&pFoundEntry->List);
			if (!EXPORTS::rtl_delete_element_generic_table_avl(PiDDBCacheTable, pFoundEntry)) {
				ExReleaseResourceLite(PiDDBLock);
				silence(E("[vmm.kernelbootstrap!err] Could not clear AvlTable for Module\n"));
				return STATUS_FAIL_CHECK;
			}
			silence(E("[vmm.kernelbootstrap] Cleared AVL->PiDDB Driver Entry!\n"));
		}
		else if (type == piddb_type::MODIFY) {
			pFoundEntry->DriverName = DriverName;
			pFoundEntry->TimeDateStamp = timeDateStamp;
			silence(E("[vmm.kernelbootstrap] Modified AVL->PiDDB Driver Entry!\n"));
		}
		else if (type == piddb_type::REPLACE) {
			pFoundEntry->DriverName = replace_link->DriverName;
			pFoundEntry->List = replace_link->List;
			pFoundEntry->LoadStatus = replace_link->LoadStatus;
			pFoundEntry->TimeDateStamp = replace_link->TimeDateStamp;
			// pFoundEntry->_0x0028 = replace_link->_0x0028;
		}
		ExReleaseResourceLite(PiDDBLock);

		return STATUS_SUCCESS;
	}


	PMM_UNLOADED_DRIVER GetMmuAddress() {
		SPOOF_FUNC;

		PVOID MmUnloadedDriversInstr = (PVOID)UMEM::find_pattern((PVOID)ntos_image_base, E("\x4C\x8B\x15\x00\x00\x00\x00\x4C\x8B\xC9"), E("xxx????xxx"));

		if (MmUnloadedDriversInstr == NULL)
			return { };

		return *(PMM_UNLOADED_DRIVER*)resolve_relative_address(MmUnloadedDriversInstr, 3, 7);
	}

	PULONG GetMmlAddress() {
		SPOOF_FUNC;

		PVOID mmlastunloadeddriverinst = (PVOID)UMEM::find_pattern((PVOID)ntos_image_base, E("\x8B\x05\x00\x00\x00\x00\x83\xF8\x32"), E("xx????xxx"));

		if (mmlastunloadeddriverinst == NULL)
			return { };

		return (PULONG)resolve_relative_address(mmlastunloadeddriverinst, 2, 6);
	}

	BOOL VerifyMmu() {
		SPOOF_FUNC;

		return (GetMmuAddress() != NULL && GetMmlAddress() != NULL);
	}

	BOOL IsUnloadEmpty(
		PMM_UNLOADED_DRIVER Entry
	) {
		SPOOF_FUNC;

		if (Entry->Name.MaximumLength == 0 || Entry->Name.Length == 0 || Entry->Name.Buffer == NULL)
			return TRUE;

		return FALSE;
	}

	BOOL IsMmuFilled() {
		SPOOF_FUNC;
		for (ULONG Idx = 0; Idx < MM_UNLOADED_DRIVERS_SIZE; ++Idx) {
			PMM_UNLOADED_DRIVER Entry = &GetMmuAddress()[Idx];
			if (IsUnloadEmpty(Entry))
				return FALSE;
		}
		return TRUE;
	}

	NTSTATUS mm_mmu_mml(UNICODE_STRING DriverName) {
		SPOOF_FUNC;

		auto ps_loaded = NTOS::GetPsLoaded();
		if (ps_loaded == NULL) {
			silence(E("[vmm.kernelbootstrap!err] Failed to get PsLoaded\n"));
			return STATUS_FAIL_CHECK;
		}

		EXPORTS::ex_acquire_exclusive_lite(ps_loaded, TRUE);

		BOOLEAN Modified = FALSE;
		BOOLEAN Filled = IsMmuFilled();

		for (ULONG Index = 0; Index < MM_UNLOADED_DRIVERS_SIZE; ++Index) {
			PMM_UNLOADED_DRIVER Entry = &GetMmuAddress()[Index];
			if (IsUnloadEmpty(Entry)) {
				continue;
			}
			BOOL empty = IsUnloadEmpty(Entry);
			if (Modified) {
				PMM_UNLOADED_DRIVER PrevEntry = &GetMmuAddress()[Index - 1];
				RtlCopyMemory(PrevEntry, Entry, sizeof(MM_UNLOADED_DRIVER));

				if (Index == MM_UNLOADED_DRIVERS_SIZE - 1) {
					RtlFillMemory(Entry, sizeof(MM_UNLOADED_DRIVER), 0);
				}
			}
			else if (RtlEqualUnicodeString(&DriverName, &Entry->Name, TRUE)) {
				PVOID BufferPool = Entry->Name.Buffer;
				RtlFillMemory(Entry, sizeof(MM_UNLOADED_DRIVER), 0);
				EXPORTS::ex_free_pool_with_tag(BufferPool, 'TDmM');

				*GetMmlAddress() = (Filled ? MM_UNLOADED_DRIVERS_SIZE : *GetMmlAddress()) - 1;
				Modified = TRUE;
			}
		}

		if (Modified) {
			ULONG64 PreviousTime = 0;
			for (LONG Index = MM_UNLOADED_DRIVERS_SIZE - 2; Index >= 0; --Index) {
				PMM_UNLOADED_DRIVER Entry = &GetMmuAddress()[Index];
				if (IsUnloadEmpty(Entry)) {
					continue;
				}
				if (PreviousTime != 0 && Entry->UnloadTime > PreviousTime) {
					silence(E("[vmm.kernelbootstrap] Found & Spoofing Module [%wZ]\n"), Entry->Name);
					Entry->UnloadTime = PreviousTime - NTOS::RandomNumber();
				}
				PreviousTime = Entry->UnloadTime;
			}
			//mm_mmu_mml(DriverName);
		}

		EXPORTS::ex_release_resource_lite(ps_loaded);

		if (Modified == FALSE) {
			silence(E("[vmm.kernelbootstrap] No Modifications Required for Module [%wZ]\n"), DriverName);
		}
		else {
			silence(E("[vmm.kernelbootstrap] Spoofed Module Entries for Module [%wZ]\n"), DriverName);
		}

		return STATUS_SUCCESS;
	}

	NTSTATUS UnicodeStringToAnsiString(
		PUNICODE_STRING UnicodeString,
		PANSI_STRING AnsiString,
		BOOLEAN AllocateDestination
	) {
		NTSTATUS status = RtlUnicodeStringToAnsiString(AnsiString, UnicodeString, AllocateDestination);
		if (!NT_SUCCESS(status)) {

		}
		return status;
	}

	NTSTATUS mm_hashbucket(UNICODE_STRING DriverName) {
		CHAR* encString = E("ci.dll");
		PVOID CiBase = NULL;

		ULONG size = NULL;
		NTSTATUS status = EXPORTS::ZwQuerySystemInformation_impl(SystemModuleInformation, 0, 0, &size);
		if (STATUS_INFO_LENGTH_MISMATCH != status) {
			return STATUS_FAIL_CHECK;
		}

		PSYSTEM_MODULE_INFORMATION Modules = (PSYSTEM_MODULE_INFORMATION)EXPORTS::ex_allocate_pool(NonPagedPool, size);
		if (!Modules) {
			return STATUS_FAIL_CHECK;
		}

		if (!NT_SUCCESS(status = EXPORTS::zw_query_system_information(SystemModuleInformation, Modules, size, 0))) {
			EXPORTS::ex_free_pool_with_tag(Modules, 0);
			return STATUS_FAIL_CHECK;
		}

		for (UINT i = 0; i < Modules->NumberOfModules; i++) {
			CHAR* CurrentModuleName = reinterpret_cast<CHAR*>(Modules->Modules[i].FullPathName);
			if (crt::stristr(CurrentModuleName, encString)) {
				CiBase = Modules->Modules[i].ImageBase;
				break;
			}
		}
		if (CiBase == NULL) {
			EXPORTS::ex_free_pool_with_tag(Modules, 0);
			return STATUS_FAIL_CHECK;
		}
		EXPORTS::ex_free_pool_with_tag(Modules, 0);

		silence(E("[vmm.kernelbootstrap] ci.dll [%p]\n"), CiBase);

		// Win10
		PVOID SignatureAddress = (PVOID)UMEM::find_pattern_in_section(
			(uintptr_t)CiBase,
			E("\x48\x8B\x1D\x00\x00\x00\x00\xEB\x00\xF7\x43\x40\x00\x20\x00\x00"),
			E("xxx????x?xxxxxxx"),
			E("PAGE")
		);
		if (SignatureAddress == NULL) {
			// Win11
			SignatureAddress = (PVOID)UMEM::find_pattern_in_section(
				(uintptr_t)CiBase,
				E("\x48\x8B\x1D\x00\x00\x00\x00\xEB\x00\xF7\x43\x40\x00\x20\x00\x00"),
				E("xxx????x?xxxxxxx"),
				E("PAGE")
			);
			if (SignatureAddress == NULL) {
				// Both Failed
				silence(E("[vmm.kernelbootstrap!err] Failed to find Signature on ci.dll\n"));
				return STATUS_FAIL_CHECK;
			}
		}
		CONST ULONGLONG* g_KernelHashBucketList = (ULONGLONG*)resolve_relative_address(SignatureAddress, 3, 7);
		if (!g_KernelHashBucketList) {
			silence(E("[vmm.kernelbootstrap!err] Failed to resolve signature on ci.dll\n"));
			return STATUS_FAIL_CHECK;
		}

		LARGE_INTEGER Time{};
		EXPORTS::ke_query_system_time_precise(&Time);

		BOOL Status = FALSE;
		for (ULONGLONG i = *g_KernelHashBucketList; i; i = *(ULONGLONG*)i) {
			CONST PWCHAR wsName = PWCH(i + 0x48);
			if (wcsstr(wsName, DriverName.Buffer)) {
				PUCHAR Hash = PUCHAR(i + 0x18);
				for (UINT j = 0; j < 20; j++)
					Hash[j] = UCHAR(RtlRandomEx(&Time.LowPart) % 255);

				Status = TRUE;
			}
		}

		if (Status == FALSE) {
			silence(E("[vmm.kernelbootstrap!err] Failed to Modify Kernel Module Entry in ci.dll\n"));
			return STATUS_FAIL_CHECK;
		}
		else {
			silence(E("[vmm.kernelbootstrap] Modified Kernel Module entry in ci.dll for [%wZ]\n"), DriverName);
			return STATUS_SUCCESS;
		}
		return STATUS_FAIL_CHECK;
	}

	NTSTATUS kernelbootstrap() {
		SPOOF_FUNC;

		UNICODE_STRING unicode1; EXPORTS::rtl_init_unicode_string(&unicode1, (L"sys64x.sys"));
		UNICODE_STRING unicode3; EXPORTS::rtl_init_unicode_string(&unicode3, (L"gdrv.sys"));

		NTSTATUS s_piddb = STATUS_SUCCESS;
		NTSTATUS s_mmu_mml = STATUS_SUCCESS;
		NTSTATUS s_hashbucket = STATUS_SUCCESS;


		s_piddb = mm_piddb(unicode1, 0x0, piddb_type::UNLINK);
		if (s_piddb != STATUS_SUCCESS) {
			silence(E("[EROOR] Could not clear PIDDB For module   [%wZ]\n"), unicode1);
		}
		else
		{
			silence(E("[+] Cleared PIDDB for module   [%wZ]\n"), unicode1);
		}
		s_mmu_mml = mm_mmu_mml(unicode1);
		if (s_mmu_mml != STATUS_SUCCESS) {
			silence(E("[EROOR] Could not clear MMU/MML For module  [%wZ]\n"), unicode1);
		}
		else
		{
			silence(E("[+] Cleared MMU/MML for module  [%wZ]\n"), unicode1);
		}

		s_hashbucket = mm_hashbucket(unicode1);
		if (s_hashbucket != STATUS_SUCCESS) {
			silence(E("[EROOR] Could not clear Hashbucket For module   [%wZ]\n"), unicode1);

		}
		else
		{
			silence(E("[+] Cleared Hashbucket for module   [%wZ]\n"), unicode1);
		}

		s_piddb = mm_piddb(unicode3, 0x0, piddb_type::UNLINK);
		if (s_piddb != STATUS_SUCCESS) {
			silence(E("[EROOR] Could not clear PIDDB For module   [%wZ]\n"), unicode3);
		}
		else
		{
			silence(E("[+] Cleared PIDDB for module   [%wZ]\n"), unicode3);
		}
		s_mmu_mml = mm_mmu_mml(unicode3);
		if (s_mmu_mml != STATUS_SUCCESS) {
			silence(E("[EROOR] Could not clear MMU/MML For module   [%wZ]\n"), unicode3);
		}
		else
		{
			silence(E("[+] Cleared MMU/MML for module   [%wZ]\n"), unicode3);
		}
		s_hashbucket = mm_hashbucket(unicode3);
		if (s_hashbucket != STATUS_SUCCESS) {
			silence(E("[EROOR] Could not clear Hashbucket For module  [%wZ]\n"), unicode3);

		}
		else {
			silence(E("[+] Cleared Hashbucket for module   [%wZ]\n"), unicode3);
		}
		silence(E("[EROOR] Finished Kernel Bootstrap\n"));

		return STATUS_SUCCESS;
	}
}