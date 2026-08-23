#include <iostream>
#include <vector>
#include <string>
#include "utils.hpp"
#include "intel_driver.hpp"
#include "kdmapper.hpp"

int main(int argc, char** argv)
{
	std::cout << "[+] DragonBurn Kernel Mapper" << std::endl;

	if (!utils::SetSystemPrivilege(L"SeDebugPrivilege", true) || !utils::SetSystemPrivilege(L"SeLoadDriverPrivilege", true))
	{
		std::cout << "[-] Failed to acquire required privileges (Run as Administrator)" << std::endl;
		return 1;
	}

	std::string driver_path = "driver.sys";
	if (argc > 1)
	{
		driver_path = argv[1];
	}

	std::vector<uint8_t> raw_driver_image;
	if (!utils::ReadFileToMemory(driver_path, &raw_driver_image))
	{
		std::cout << "[-] Failed to read driver file: " << driver_path << std::endl;
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
