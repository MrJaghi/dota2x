#pragma once
#include <windows.h>
#include <stdint.h>
#include <vector>
#include <iostream>

namespace portable_executable
{
	PIMAGE_NT_HEADERS GetNtHeaders(void* image_base);
	PIMAGE_SECTION_HEADER GetSectionHeader(void* image_base, ULONG index);
	bool RelocateImageByDelta(PIMAGE_NT_HEADERS nt_headers, void* local_image, uint64_t delta);
	bool ResolveImports(HANDLE device_handle, void* local_image, PIMAGE_NT_HEADERS nt_headers);
}
