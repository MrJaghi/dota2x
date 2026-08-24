#pragma once
#include <windows.h>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <string>
#include <winternl.h>

#pragma comment(lib, "ntdll.lib")

namespace utils
{
	bool ReadFileToMemory(const std::wstring& file_path, std::vector<uint8_t>* out_buffer);
	bool CreateFileFromMemory(const std::wstring& desired_file_path, const char* address, size_t size);

	std::wstring GetTempPathW();
	std::wstring GetFullTempPath();
	std::wstring GetExeDirW();

	std::wstring RandomAlphaNumW(size_t len);

	uint64_t GetKernelModuleAddress(const std::string& module_name);

	bool SetSystemPrivilege(const std::wstring& privilege_name, bool enable);

	// Returns true if any auto-fix was applied that requires a reboot (HVCI/blocklist).
	// When it returns true, caller should abort the load and tell the user to reboot.
	bool EnvironmentPreflight();
}
