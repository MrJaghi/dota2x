#include "kdmapper.hpp"
#include "portable_executable.hpp"
#include "utils.hpp"
#include <iostream>

namespace kdmapper
{
	uint64_t MapDriver(HANDLE device_handle, const std::vector<uint8_t>& raw_image)
	{
		if (raw_image.empty())
		{
			std::cout << "[-] Raw image is empty" << std::endl;
			return 0;
		}

		PIMAGE_NT_HEADERS nt_headers = portable_executable::GetNtHeaders((void*)raw_image.data());
		if (!nt_headers)
		{
			std::cout << "[-] Invalid NT headers" << std::endl;
			return 0;
		}

		if (nt_headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
		{
			std::cout << "[-] Image is not 64-bit" << std::endl;
			return 0;
		}

		uint32_t image_size = nt_headers->OptionalHeader.SizeOfImage;
		void* local_image_base = VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

		if (!local_image_base)
		{
			std::cout << "[-] Failed to allocate local memory" << std::endl;
			return 0;
		}

		memcpy(local_image_base, raw_image.data(), nt_headers->OptionalHeader.SizeOfHeaders);

		PIMAGE_SECTION_HEADER section_header = IMAGE_FIRST_SECTION(nt_headers);
		for (WORD i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i)
		{
			void* section_destination = (void*)((uint64_t)local_image_base + section_header[i].VirtualAddress);
			void* section_source = (void*)((uint64_t)raw_image.data() + section_header[i].PointerToRawData);

			if (section_header[i].SizeOfRawData)
			{
				memcpy(section_destination, section_source, section_header[i].SizeOfRawData);
			}
			else
			{
				memset(section_destination, 0, section_header[i].Misc.VirtualSize);
			}
		}

		uint64_t kernel_image_base = intel_driver::AllocatePool(device_handle, image_size);
		if (!kernel_image_base)
		{
			std::cout << "[-] Failed to allocate kernel pool" << std::endl;
			VirtualFree(local_image_base, 0, MEM_RELEASE);
			return 0;
		}

		std::cout << "[+] Kernel memory allocated at: 0x" << std::hex << kernel_image_base << std::dec << std::endl;

		uint64_t delta = kernel_image_base - nt_headers->OptionalHeader.ImageBase;
		if (!portable_executable::RelocateImageByDelta(nt_headers, local_image_base, delta))
		{
			std::cout << "[-] Failed to relocate image" << std::endl;
			intel_driver::FreePool(device_handle, kernel_image_base);
			VirtualFree(local_image_base, 0, MEM_RELEASE);
			return 0;
		}

		if (!portable_executable::ResolveImports(device_handle, local_image_base, nt_headers))
		{
			std::cout << "[-] Failed to resolve imports" << std::endl;
			intel_driver::FreePool(device_handle, kernel_image_base);
			VirtualFree(local_image_base, 0, MEM_RELEASE);
			return 0;
		}

		if (!intel_driver::MemCopy(device_handle, kernel_image_base, (uint64_t)local_image_base, image_size))
		{
			std::cout << "[-] Failed to copy image to kernel" << std::endl;
			intel_driver::FreePool(device_handle, kernel_image_base);
			VirtualFree(local_image_base, 0, MEM_RELEASE);
			return 0;
		}

		VirtualFree(local_image_base, 0, MEM_RELEASE);

		uint64_t entry_point = kernel_image_base + nt_headers->OptionalHeader.AddressOfEntryPoint;
		std::cout << "[+] Calling DriverEntry at: 0x" << std::hex << entry_point << std::dec << std::endl;

		NTSTATUS status = (NTSTATUS)intel_driver::CallKernelFunction(device_handle, (void*)entry_point, kernel_image_base, 0, 0, 0);
		std::cout << "[+] DriverEntry status: 0x" << std::hex << status << std::dec << std::endl;

		return kernel_image_base;
	}
}
