#pragma once
#include "cr3.h"
#include "../ia32.h"
#include "../includes.h"
 
#define RVA(addr, size) ((PVOID)((PBYTE)(addr) + *(PINT)((PBYTE)(addr) + ((size) - (INT)sizeof(INT))) + (size)))
#define PRINT(fmt, ...) DbgPrintEx(0, 0, skCrypt("[RAGE] " fmt), __VA_ARGS__)

typedef struct _LDR_DATA_TABLE_ENTRYx
{
	LIST_ENTRY InLoadOrderLinks;
	LIST_ENTRY InMemoryOrderLinks;
	LIST_ENTRY InInitializationOrderLinks;
	PVOID DllBase;
	PVOID EntryPoint;
	ULONG SizeOfImage;
	UNICODE_STRING FullDllName;
	UNICODE_STRING BaseDllName;
	// ...
} LDR_DATA_TABLE_ENTRYx, * PLDR_DATA_TABLE_ENTRYx;

typedef struct _PEB_LDR_DATA
{
	unsigned int Length;
	int Initialized;
	void* SSHandle;
	LIST_ENTRY InLoadOrderLinks;
	LIST_ENTRY InMemoryOrderLinks;
	LIST_ENTRY InInitializationOrderLinks;
	// ...
} PEB_LDR_DATA, * PPEB_LDR_DATA;

typedef struct _PEB64
{
	unsigned char InheritedAddressSpace;	// 0x0000 
	unsigned char ReadImageFileExecOptions;	// 0x0001 
	unsigned char BeingDebugged;			// 0x0002 
	unsigned char BitField;					// 0x0003 
	unsigned char pad_0x0004[0x4];			// 0x0004
	PVOID Mutant;							// 0x0008 
	PVOID ImageBaseAddress;					// 0x0010 
	PPEB_LDR_DATA Ldr;						// 0x0018
	// ...
} PEB64, * PPEB64;

typedef struct _SYSTEM_PROCESS_INFORMATIONx {
	ULONG NextEntryOffset;
	ULONG NumberOfThreads;
	BYTE Reserved1[48];
	UNICODE_STRING ImageName;
	KPRIORITY BasePriority;
	HANDLE UniqueProcessId;
	PVOID Reserved2;
	ULONG HandleCount;
	ULONG SessionId;
	PVOID Reserved3;
	SIZE_T PeakVirtualSize;
	SIZE_T VirtualSize;
	ULONG Reserved4;
	SIZE_T PeakWorkingSetSize;
	SIZE_T WorkingSetSize;
	PVOID Reserved5;
	SIZE_T QuotaPagedPoolUsage;
	PVOID Reserved6;
	SIZE_T QuotaNonPagedPoolUsage;
	SIZE_T PagefileUsage;
	SIZE_T PeakPagefileUsage;
	SIZE_T PrivatePageCount;
	LARGE_INTEGER Reserved7[6];
} SYSTEM_PROCESS_INFORMATIONx, * PSYSTEM_PROCESS_INFORMATIONx;

typedef struct _SYSTEM_MODULEx {
	HANDLE Section;
	PVOID MappedBase;
	PVOID ImageBase;
	ULONG ImageSize;
	ULONG Flags;
	USHORT LoadOrderIndex;
	USHORT InitOrderIndex;
	USHORT LoadCount;
	USHORT OffsetToFileName;
	UCHAR  FullPathName[MAXIMUM_FILENAME_LENGTH];
} SYSTEM_MODULEx, * PSYSTEM_MODULEx;

typedef struct _SYSTEM_MODULE_INFORMATIONx {
	ULONG NumberOfModules;
	SYSTEM_MODULE Modules[1];
} SYSTEM_MODULE_INFORMATIONx, * PSYSTEM_MODULE_INFORMATIONx;

 
typedef struct _KTHREAD_FLAGS
{
	union
	{
		struct
		{
			ULONG AutoBoostActive : 1;                                        //0x74
			ULONG ReadyTransition : 1;                                        //0x74
			ULONG WaitNext : 1;                                               //0x74
			ULONG SystemAffinityActive : 1;                                   //0x74
			ULONG Alertable : 1;                                              //0x74
			ULONG UserStackWalkActive : 1;                                    //0x74
			ULONG ApcInterruptRequest : 1;                                    //0x74
			ULONG QuantumEndMigrate : 1;                                      //0x74
			ULONG UmsDirectedSwitchEnable : 1;                                //0x74
			ULONG TimerActive : 1;                                            //0x74
			ULONG SystemThread : 1;                                           //0x74
			ULONG ProcessDetachActive : 1;                                    //0x74
			ULONG CalloutActive : 1;                                          //0x74
			ULONG ScbReadyQueue : 1;                                          //0x74
			ULONG ApcQueueable : 1;                                           //0x74
			ULONG ReservedStackInUse : 1;                                     //0x74
			ULONG UmsPerformingSyscall : 1;                                   //0x74
			ULONG TimerSuspended : 1;                                         //0x74
			ULONG SuspendedWaitMode : 1;                                      //0x74
			ULONG SuspendSchedulerApcWait : 1;                                //0x74
			ULONG CetUserShadowStack : 1;                                     //0x74
			ULONG BypassProcessFreeze : 1;                                    //0x74
			ULONG Reserved : 10;                                              //0x74
		} BitFields;
		LONG MiscFlags;                                                     //0x74
	} Internal;
};


namespace imports
{
	typedef struct _HANDLE_TABLE_ENTRY_INFO
	{
		ULONG AuditMask;
	} HANDLE_TABLE_ENTRY_INFO, * PHANDLE_TABLE_ENTRY_INFO;

	typedef struct _HANDLE_TABLE_ENTRY
	{
		union
		{
			PVOID Object;
			ULONG_PTR ObAttributes;
			PHANDLE_TABLE_ENTRY_INFO InfoTable;
			ULONG_PTR Value;
		};
		union
		{
			ULONG GrantedAccess;
			struct
			{
				USHORT GrantedAccessIndex;
				USHORT CreatorBackTraceIndex;
			};
			LONG NextFreeTableEntry;
		};
	} HANDLE_TABLE_ENTRY, * PHANDLE_TABLE_ENTRY;

	size_t strlenx(const char* String)
	{
		UINT32 Length = 0;

		while (*String)
		{
			Length++;
			String++;
		}

		return (Length);
	}

	int __cdecl memcmpx(const void* s1, const void* s2, size_t n)
	{
		if (n != 0) {
			const unsigned char* p1 = (const unsigned char*)s1, * p2 = (const unsigned char*)s2;
			do {
				if (*p1++ != *p2++)
					return (*--p1 - *--p2);
			} while (--n != 0);
		}
		return 0;
	}

	int strcmpx(const char* String1, const char* String2)
	{
		for (; (*String1 == *String2); String2++)
		{
			if (!*String1++)
			{
				return (0);
			}
		}

		return ((unsigned char)*String1 - (unsigned char)*String2);
	}

	NTSTATUS ZwQuerySystemInformation_impl(ULONG InfoClass, PVOID Buffer, ULONG Length, PULONG ReturnLength)
	{
		return ZwQuerySystemInformation(InfoClass, Buffer, Length, ReturnLength);
	}

	PVOID ExAllocatePool_impl(POOL_TYPE PoolType, SIZE_T NumberOfBytes)
	{
		return ExAllocatePool(PoolType, NumberOfBytes);
	}

	void ExFreePoolWithTag_impl(PVOID P, ULONG Tag)
	{
		ExFreePoolWithTag(P, Tag);
	}
}

namespace memory
{
	inline bool is_valid_process(HANDLE pid)
	{
		if (!pid)
			return false;

		PEPROCESS process;
		if (!NT_SUCCESS(PsLookupProcessByProcessId(pid, &process)))
			return false;

		return true;
	}

	inline PIMAGE_NT_HEADERS get_nt_headers(PVOID module)
	{
		if (!module)
			return nullptr;
		return (PIMAGE_NT_HEADERS)((PBYTE)module + PIMAGE_DOS_HEADER(module)->e_lfanew);
	}

	inline PBYTE find_pattern(PVOID module, DWORD size, LPCSTR pattern, LPCSTR mask)
	{
		if (!module)
			return nullptr;

		auto checkMask = [](PBYTE buffer, LPCSTR pattern, LPCSTR mask) -> BOOL
			{
				for (auto x = buffer; *mask; pattern++, mask++, x++) {
					auto addr = *(BYTE*)(pattern);
					if (addr != *x && *mask != '?')
						return FALSE;
				}

				return TRUE;
			};

		for (auto x = 0; x < size - imports::strlenx(mask); x++) {

			auto addr = (PBYTE)module + x;
			if (checkMask(addr, pattern, mask)) {
				return addr;
			}
		}

		return NULL;
	}

	inline PBYTE find_pattern(PVOID base, LPCSTR pattern, LPCSTR mask)
	{
		if (!base) return 0;

		auto header = get_nt_headers(base);
		auto section = IMAGE_FIRST_SECTION(header);

		for (auto x = 0; x < header->FileHeader.NumberOfSections; x++, section++) {

			if (!imports::memcmpx(section->Name, skCrypt(".text"), 5) || !imports::memcmpx(section->Name, skCrypt("PAGE"), 4))
			{
				auto addr = find_pattern((PBYTE)base + section->VirtualAddress, section->Misc.VirtualSize, pattern, mask);
				if (addr)
					return addr;
			}
		}

		return NULL;
	}

	inline PVOID get_system_base(const CHAR* system_module)
	{
		PVOID addr = 0;
		ULONG size = 0;

		NTSTATUS status = imports::ZwQuerySystemInformation_impl(SystemModuleInformation, 0, 0, &size);
		if (status != STATUS_INFO_LENGTH_MISMATCH) return 0;

		PSYSTEM_MODULE_INFORMATIONx modules = reinterpret_cast<PSYSTEM_MODULE_INFORMATIONx>(imports::ExAllocatePool_impl(NonPagedPool, size));
		if (!modules) return 0;

		if (!NT_SUCCESS(status = imports::ZwQuerySystemInformation_impl(SystemModuleInformation, modules, size, 0)))
		{
			imports::ExFreePoolWithTag_impl(modules, 0);
			return 0;
		}

		for (int i = 0; i < modules->NumberOfModules; i++)
		{
			if (imports::strcmpx((CHAR*)modules->Modules[i].FullPathName, system_module) == 0)
			{
				addr = modules->Modules[i].ImageBase;
				break;
			}
		}

		imports::ExFreePoolWithTag_impl(modules, 0);
		return addr;
	}

	inline ULONG_PTR get_process_cr3(PEPROCESS pProcess)
	{
		if (!pProcess)
			return 0;
		return	dirbase_from_base_address((void*)(PsGetProcessSectionBaseAddress)(pProcess));
		 
	}

	inline ULONG_PTR get_kernel_dir_base()
	{
		PUCHAR process = (PUCHAR)IoGetCurrentProcess();
		if (!process) return 0;

		ULONG_PTR cr3 = *(PULONG_PTR)(process + 0x28); //dirbase x64, 32bit is 0x18
		return cr3;
	}

	inline NTSTATUS read_phys_addr(PVOID TargetAddress, PVOID lpBuffer, SIZE_T Size, SIZE_T* BytesRead)
	{
		if (!TargetAddress || !lpBuffer || Size <= 0)
			return STATUS_UNSUCCESSFUL;

		MM_COPY_ADDRESS AddrToRead = { 0 };
		AddrToRead.PhysicalAddress.QuadPart = (LONGLONG)TargetAddress;
		return MmCopyMemory(lpBuffer, AddrToRead, Size, MM_COPY_MEMORY_PHYSICAL, BytesRead);
	}

	inline NTSTATUS write_phys_addr(PVOID TargetAddress, PVOID lpBuffer, SIZE_T Size, SIZE_T* BytesWritten)
	{
		if (!TargetAddress || !lpBuffer || Size <= 0)
			return STATUS_UNSUCCESSFUL;

		PHYSICAL_ADDRESS AddrToWrite = { 0 };
		AddrToWrite.QuadPart = (LONGLONG)TargetAddress;

		PVOID pmapped_mem = MmMapIoSpaceEx(AddrToWrite, Size, PAGE_READWRITE);

		if (!pmapped_mem)
			return STATUS_UNSUCCESSFUL;

		memcpy(pmapped_mem, lpBuffer, Size);

		if (BytesWritten)
			*BytesWritten = Size;

		MmUnmapIoSpace(pmapped_mem, Size);
		return STATUS_SUCCESS;
	}

#define PAGE_OFFSET_SIZE 12
	static const UINT64 PMASK = (~0xfull << 8) & 0xfffffffffull;

	inline UINT64 translate_linear_address(UINT64 directoryTableBase, UINT64 virtualAddress)
	{
		if (!directoryTableBase || !virtualAddress)
			return 0;
		directoryTableBase &= ~0xf;

		UINT64 pageOffset = virtualAddress & ~(~0ul << PAGE_OFFSET_SIZE);
		UINT64 pte = ((virtualAddress >> 12) & (0x1ffll));
		UINT64 pt = ((virtualAddress >> 21) & (0x1ffll));
		UINT64 pd = ((virtualAddress >> 30) & (0x1ffll));
		UINT64 pdp = ((virtualAddress >> 39) & (0x1ffll));

		SIZE_T readsize = 0;
		UINT64 pdpe = 0;
		read_phys_addr((PVOID)(directoryTableBase + 8 * pdp), &pdpe, sizeof(pdpe), &readsize);
		if (~pdpe & 1)
			return 0;

		UINT64 pde = 0;
		read_phys_addr((PVOID)((pdpe & PMASK) + 8 * pd), &pde, sizeof(pde), &readsize);
		if (~pde & 1)
			return 0;

		/* 1GB large page, use pde's 12-34 bits */
		if (pde & 0x80)
			return (pde & (~0ull << 42 >> 12)) + (virtualAddress & ~(~0ull << 30));

		UINT64 pteAddr = 0;
		read_phys_addr((PVOID)((pde & PMASK) + 8 * pt), &pteAddr, sizeof(pteAddr), &readsize);
		if (~pteAddr & 1)
			return 0;

		/* 2MB large page */
		if (pteAddr & 0x80)
			return (pteAddr & PMASK) + (virtualAddress & ~(~0ull << 21));

		virtualAddress = 0;
		read_phys_addr((PVOID)((pteAddr & PMASK) + 8 * pte), &virtualAddress, sizeof(virtualAddress), &readsize);
		virtualAddress &= PMASK;

		if (!virtualAddress)
			return 0;

		return virtualAddress + pageOffset;
	}

	inline NTSTATUS read_phys_memory(HANDLE pid, ULONG_PTR cr3, PVOID Address, PVOID AllocatedBuffer, SIZE_T size, SIZE_T* read = nullptr)
	{
		PEPROCESS pProcess = NULL;
		if (!pid || !Address || !AllocatedBuffer || size <= 0) return STATUS_UNSUCCESSFUL;

		NTSTATUS NtRet = STATUS_SUCCESS;
		 

 
		SIZE_T CurOffset = 0;
		SIZE_T TotalSize = size;
		while (TotalSize)
		{
			UINT64 CurPhysAddr = translate_linear_address(cr3, (ULONG64)Address + CurOffset);
			if (!CurPhysAddr) return STATUS_UNSUCCESSFUL;

			ULONG64 ReadSize = min(PAGE_SIZE - (CurPhysAddr & 0xFFF), TotalSize);
			SIZE_T BytesRead = 0;
			NtRet = read_phys_addr((PVOID)CurPhysAddr, (PVOID)((ULONG64)AllocatedBuffer + CurOffset), ReadSize, &BytesRead);
			TotalSize -= BytesRead;
			CurOffset += BytesRead;
			if (NtRet != STATUS_SUCCESS) break;
			if (BytesRead == 0) break;
		}

		if (read != nullptr)
			*read = CurOffset;

		return NtRet;
	}

	inline NTSTATUS write_phys_memory(HANDLE pid, ULONG_PTR cr3, PVOID Address, PVOID AllocatedBuffer, SIZE_T size, SIZE_T* written = nullptr)
	{
		PEPROCESS pProcess = NULL;
		if (!pid || !Address || !AllocatedBuffer || size <= 0) return STATUS_UNSUCCESSFUL;

		NTSTATUS NtRet = STATUS_SUCCESS;
	 

		SIZE_T CurOffset = 0;
		SIZE_T TotalSize = size;
		while (TotalSize)
		{
			UINT64 CurPhysAddr = translate_linear_address(cr3, (ULONG64)Address + CurOffset);
			if (!CurPhysAddr) return STATUS_UNSUCCESSFUL;

			ULONG64 WriteSize = min(PAGE_SIZE - (CurPhysAddr & 0xFFF), TotalSize);
			SIZE_T BytesWritten = 0;
			NtRet = write_phys_addr((PVOID)CurPhysAddr, (PVOID)((ULONG64)AllocatedBuffer + CurOffset), WriteSize, &BytesWritten);
			TotalSize -= BytesWritten;
			CurOffset += BytesWritten;
			if (NtRet != STATUS_SUCCESS) break;
			if (BytesWritten == 0) break;
		}

		if (written != nullptr)
			*written = CurOffset;
		return NtRet;
	}

	inline PVOID get_process_base_ud(HANDLE pid)
	{
		PEPROCESS process = 0;
		if (!NT_SUCCESS(PsLookupProcessByProcessId(pid, &process))) return 0;

		// PsGetProcessSectionBaseAddress bypass lol
		unsigned __int64 base_address = *(unsigned __int64*)((ULONG64)process + kernel_EPROCESS::SectionBaseAddress);
		return reinterpret_cast<PVOID>(base_address);
	}

	inline bool spoof_thread(PVOID process_id, ULONG64 thread_id, BOOLEAN enable)
	{
		if ((ULONG64)process_id <= 0 || thread_id <= 0)
			return false;

		PEPROCESS source_process;
		if (!NT_SUCCESS(PsLookupProcessByProcessId(process_id, &source_process)))
			return false;

		_KTHREAD* thread;

		NTSTATUS status = PsLookupThreadByThreadId((HANDLE)thread_id, &thread);
		if (status != STATUS_SUCCESS)
			return false;

		PEPROCESS thread_process = PsGetThreadProcess(thread);
		if (!thread_process)
			return false;

		if (source_process != thread_process)
			return false;

		// _KTHREAD.Process = struct _KPROCESS* Process; //0x220

		// _KTHREAD.SystemCallNumber
		*(ULONG*)((char*)thread + 0x80) = 0;

		_KTHREAD_FLAGS* miscFlags = (_KTHREAD_FLAGS*)((char*)thread + 0x74);
		if (miscFlags)
		{
			//miscFlags->Internal.BitFields.ApcQueueable = (enable ? 1 : 0);
		}


		return true;
	}

	inline PVOID get_module_base_x64(HANDLE pid, LPCWSTR module_name)
	{
		if (!pid || !module_name) return 0;

		PEPROCESS process = 0;
		if (!NT_SUCCESS(PsLookupProcessByProcessId(pid, &process))) return 0;

		PVOID result = 0;
		SIZE_T alloc_size = static_cast<SIZE_T>(wcslen(module_name) * sizeof(wchar_t) + 1);

		PVOID buf = ExAllocatePool(NonPagedPool, alloc_size);
		if (!buf) return 0;

		memcpy(buf, module_name, alloc_size);

		UNICODE_STRING moduleName;
		RtlInitUnicodeString(&moduleName, (PCWSTR)buf);

		PPEB64 peb = (PPEB64)PsGetProcessPeb(process);
		if (!peb) return 0;

		KAPC_STATE state;
		KeStackAttachProcess(process, &state);

		for (PLIST_ENTRY pListEntry = peb->Ldr->InLoadOrderLinks.Flink; pListEntry != &peb->Ldr->InLoadOrderLinks; pListEntry = pListEntry->Flink)
		{
			if (!pListEntry)
				continue;

			PLDR_DATA_TABLE_ENTRYx module_entry = CONTAINING_RECORD(pListEntry, LDR_DATA_TABLE_ENTRYx, InLoadOrderLinks);
			if (RtlCompareUnicodeString(&module_entry->BaseDllName, &moduleName, TRUE) == 0)
				result = module_entry->DllBase;
		}

		KeUnstackDetachProcess(&state);
		if (buf)
			ExFreePoolWithTag(buf, 0);

		return result;
	}
}