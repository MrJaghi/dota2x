#include <iostream>
#include <vector>
#include <string>
#include "utils.hpp"
#include "intel_driver.hpp"
#include "kdmapper.hpp"
#include "driver_resource.hpp"

static bool IsRunAsAdmin()
{
	BOOL isAdmin = FALSE;
	PSID adminGroup = NULL;
	SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
	if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
		DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup))
	{
		CheckTokenMembership(NULL, adminGroup, &isAdmin);
		FreeSid(adminGroup);
	}
	return isAdmin == TRUE;
}

int wmain(int argc, wchar_t** argv)
{
	std::cout << "[+] DragonBurn Kernel Mapper" << std::endl;

	if (!IsRunAsAdmin())
	{
		std::cout << "[-] This mapper must be run as Administrator." << std::endl;
		return 1;
	}

	std::wstring driver_path = L"driver.sys";
	if (argc > 1)
	{
		driver_path = argv[1];
	}

	std::vector<uint8_t> raw_driver_image;
	if (!utils::ReadFileToMemory(driver_path, &raw_driver_image))
	{
		std::cout << "[*] External driver.sys not found, using embedded kernel driver bytes..." << std::endl;
		raw_driver_image.assign(driver_resource::driver_bytes,
			driver_resource::driver_bytes + driver_resource::driver_bytes_size);
	}

	if (raw_driver_image.empty())
	{
		std::cout << "[-] Driver image is empty" << std::endl;
		return 1;
	}

	HANDLE device_handle = intel_driver::Load();
	if (device_handle == INVALID_HANDLE_VALUE)
	{
		std::cout << "[-] Failed to load intel vulnerable driver" << std::endl;
		return 1;
	}

	uint64_t mapped_address = kdmapper::MapDriver(device_handle, raw_driver_image);
	if (!mapped_address)
	{
		std::cout << "[-] Mapping driver failed" << std::endl;
		intel_driver::Unload(device_handle);
		return 1;
	}

	std::cout << "[+] Driver successfully mapped at 0x" << std::hex << mapped_address << std::dec << std::endl;

	intel_driver::Unload(device_handle);
	return 0;
}
