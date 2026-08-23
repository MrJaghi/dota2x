#include "utils.hpp"
#include <fstream>
#include <TlHelp32.h>

typedef struct _SYSTEM_HANDLE
{
	ULONG ProcessId;
	UCHAR ObjectTypeNumber;
	UCHAR Flags;
	USHORT Handle;
	PVOID Object;
	ACCESS_MASK GrantedAccess;
} SYSTEM_HANDLE, * PSYSTEM_HANDLE;

typedef struct _SYSTEM_HANDLE_INFORMATION
{
	ULONG HandleCount;
	SYSTEM_HANDLE Handles[1];
} SYSTEM_HANDLE_INFORMATION, * PSYSTEM_HANDLE_INFORMATION;

typedef struct _RTL_PROCESS_MODULE_INFORMATION
{
	HANDLE Section;
	PVOID MappedBase;
	PVOID ImageBase;
	ULONG ImageSize;
	ULONG Flags;
	USHORT LoadOrderIndex;
	USHORT InitOrderIndex;
	USHORT LoadCount;
	USHORT OffsetToFileName;
	UCHAR FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION, * PRTL_PROCESS_MODULE_INFORMATION;

typedef struct _RTL_PROCESS_MODULES
{
	ULONG NumberOfModules;
	RTL_PROCESS_MODULE_INFORMATION Modules[1];
} RTL_PROCESS_MODULES, * PRTL_PROCESS_MODULES;

namespace utils
{
	bool ReadFileToMemory(const std::string& file_path, std::vector<uint8_t>* out_buffer)
	{
		std::ifstream file_ifstream(file_path, std::ios::binary | std::ios::ate);

		if (!file_ifstream.is_open())
			return false;

		size_t file_size = (size_t)file_ifstream.tellg();
		out_buffer->resize(file_size);

		file_ifstream.seekg(0, std::ios::beg);
		file_ifstream.read((char*)out_buffer->data(), file_size);
		file_ifstream.close();

		return true;
	}

	bool CreateFileFromMemory(const std::string& desired_file_path, const char* address, size_t size)
	{
		std::ofstream file_ofstream(desired_file_path, std::ios::binary);
		if (!file_ofstream.is_open())
			return false;

		file_ofstream.write(address, size);
		file_ofstream.close();
		return true;
	}

	uint64_t GetKernelModuleAddress(const std::string& module_name)
	{
		ULONG size = 0;
		NTSTATUS status = NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)11, nullptr, 0, &size);
		if (size == 0)
			return 0;

		PRTL_PROCESS_MODULES modules = (PRTL_PROCESS_MODULES)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!modules)
			return 0;

		if (!NT_SUCCESS(NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)11, modules, size, &size)))
		{
			VirtualFree(modules, 0, MEM_RELEASE);
			return 0;
		}

		uint64_t module_base = 0;

		for (ULONG i = 0; i < modules->NumberOfModules; i++)
		{
			std::string current_module_name = (char*)modules->Modules[i].FullPathName + modules->Modules[i].OffsetToFileName;
			if (_stricmp(current_module_name.c_str(), module_name.c_str()) == 0)
			{
				module_base = (uint64_t)modules->Modules[i].ImageBase;
				break;
			}
		}

		VirtualFree(modules, 0, MEM_RELEASE);
		return module_base;
	}

	uint64_t GetKernelModuleExport(uint64_t kernel_module_base, const std::string& function_name)
	{
		if (!kernel_module_base)
			return 0;

		HMODULE user_module_base = LoadLibraryA("ntoskrnl.exe");
		if (!user_module_base)
			return 0;

		uint64_t export_address = (uint64_t)GetProcAddress(user_module_base, function_name.c_str());
		if (!export_address)
		{
			FreeLibrary(user_module_base);
			return 0;
		}

		uint64_t offset = export_address - (uint64_t)user_module_base;
		FreeLibrary(user_module_base);

		return kernel_module_base + offset;
	}

	bool SetSystemPrivilege(const std::wstring& privilege_name, bool enable)
	{
		HANDLE token_handle = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token_handle))
			return false;

		TOKEN_PRIVILEGES tp;
		LUID luid;

		if (!LookupPrivilegeValueW(nullptr, privilege_name.c_str(), &luid))
		{
			CloseHandle(token_handle);
			return false;
		}

		tp.PrivilegeCount = 1;
		tp.Privileges[0].Luid = luid;
		tp.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;

		if (!AdjustTokenPrivileges(token_handle, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr))
		{
			CloseHandle(token_handle);
			return false;
		}

		CloseHandle(token_handle);
		return true;
	}
}
