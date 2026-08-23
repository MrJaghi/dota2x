#pragma once
#include <windows.h>
#include <stdint.h>
#include <vector>
#include "utils.hpp"

namespace intel_driver
{
	constexpr ULONG intel_driver_device_type = 0x8000;

	HANDLE Load();
	bool Unload(HANDLE device_handle);

	bool MemCopy(HANDLE device_handle, uint64_t destination, uint64_t source, uint64_t size);
	uint64_t AllocatePool(HANDLE device_handle, uint64_t size);
	bool FreePool(HANDLE device_handle, uint64_t address);

	uint64_t CallKernelFunction(HANDLE device_handle, void* kernel_function_address, uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0, uint64_t arg4 = 0);
}
