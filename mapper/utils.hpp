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
	bool ReadFileToMemory(const std::string& file_path, std::vector<uint8_t>* out_buffer);
	bool CreateFileFromMemory(const std::string& desired_file_path, const char* address, size_t size);

	uint64_t GetKernelModuleAddress(const std::string& module_name);
	uint64_t GetKernelModuleExport(uint64_t kernel_module_base, const std::string& function_name);

	bool SetSystemPrivilege(const std::wstring& privilege_name, bool enable);
}
