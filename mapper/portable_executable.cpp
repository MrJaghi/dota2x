#include "portable_executable.hpp"
#include "utils.hpp"
#include "intel_driver.hpp"

namespace portable_executable
{
	PIMAGE_NT_HEADERS GetNtHeaders(void* image_base)
	{
		PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)image_base;
		if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
			return nullptr;

		PIMAGE_NT_HEADERS nt_headers = (PIMAGE_NT_HEADERS)((uint64_t)image_base + dos_header->e_lfanew);
		if (nt_headers->Signature != IMAGE_NT_SIGNATURE)
			return nullptr;

		return nt_headers;
	}

	PIMAGE_SECTION_HEADER GetSectionHeader(void* image_base, ULONG index)
	{
		PIMAGE_NT_HEADERS nt_headers = GetNtHeaders(image_base);
		if (!nt_headers)
			return nullptr;

		return IMAGE_FIRST_SECTION(nt_headers) + index;
	}

	bool RelocateImageByDelta(PIMAGE_NT_HEADERS nt_headers, void* local_image, uint64_t delta)
	{
		if (delta == 0)
			return true;

		PIMAGE_DATA_DIRECTORY reloc_dir = &nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
		if (!reloc_dir->Size || !reloc_dir->VirtualAddress)
			return true;

		PIMAGE_BASE_RELOCATION reloc = (PIMAGE_BASE_RELOCATION)((uint64_t)local_image + reloc_dir->VirtualAddress);
		while (reloc->VirtualAddress)
		{
			uint32_t count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);
			uint16_t* reloc_list = (uint16_t*)(reloc + 1);

			for (uint32_t i = 0; i < count; ++i)
			{
				uint16_t type = reloc_list[i] >> 12;
				uint16_t offset = reloc_list[i] & 0xFFF;

				if (type == IMAGE_REL_BASED_DIR64)
				{
					uint64_t* target = (uint64_t*)((uint64_t)local_image + reloc->VirtualAddress + offset);
					*target += delta;
				}
			}

			reloc = (PIMAGE_BASE_RELOCATION)((uint64_t)reloc + reloc->SizeOfBlock);
		}

		return true;
	}

	bool ResolveImports(HANDLE /*device_handle*/, void* local_image, PIMAGE_NT_HEADERS nt_headers)
	{
		PIMAGE_DATA_DIRECTORY import_dir = &nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (!import_dir->Size)
			return true;

		PIMAGE_IMPORT_DESCRIPTOR import_desc = (PIMAGE_IMPORT_DESCRIPTOR)((uint64_t)local_image + import_dir->VirtualAddress);

		while (import_desc->Name)
		{
			const char* module_name = (const char*)((uint64_t)local_image + import_desc->Name);
			uint64_t kernel_module_base = utils::GetKernelModuleAddress(module_name);
			if (!kernel_module_base)
			{
				// Some imports (like "ntoskrnl.exe") might be spelled with different
				// case; utils::GetKernelModuleAddress already does case-insensitive
				// compare, so if we fail here the module truly isn't loaded.
				std::cout << "[-] Failed to get kernel module: " << module_name << std::endl;
				return false;
			}

			PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((uint64_t)local_image + import_desc->FirstThunk);
			PIMAGE_THUNK_DATA original_thunk = (PIMAGE_THUNK_DATA)((uint64_t)local_image + import_desc->OriginalFirstThunk);
			if (!original_thunk)
				original_thunk = thunk;

			while (original_thunk->u1.AddressOfData)
			{
				if (IMAGE_SNAP_BY_ORDINAL(original_thunk->u1.Ordinal))
				{
					std::cout << "[-] Import by ordinal not supported" << std::endl;
					return false;
				}

				PIMAGE_IMPORT_BY_NAME import_by_name = (PIMAGE_IMPORT_BY_NAME)((uint64_t)local_image + original_thunk->u1.AddressOfData);
				std::string func_name = (const char*)import_by_name->Name;

				// Resolve against the dependency module first, then fall back to ntoskrnl.
				uint64_t func_address = intel_driver::GetKernelModuleExport(kernel_module_base, func_name);
				if (!func_address && kernel_module_base != intel_driver::g_ntoskrnlAddr)
					func_address = intel_driver::GetKernelModuleExport(intel_driver::g_ntoskrnlAddr, func_name);
				if (!func_address)
				{
					std::cout << "[-] Failed to resolve import: " << func_name << " (" << module_name << ")" << std::endl;
					return false;
				}

				thunk->u1.Function = func_address;
				++thunk;
				++original_thunk;
			}

			++import_desc;
		}

		return true;
	}
}
