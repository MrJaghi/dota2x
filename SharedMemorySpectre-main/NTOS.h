#pragma once
#include "includes.h"

_declspec(noinline) auto resolve_address(
    uintptr_t Instruction,
    ULONG OffsetOffset,
    ULONG InstructionSize
) -> uintptr_t;

PVOID resolve_relative_address(
    _In_ PVOID Instruction,
    _In_ ULONG OffsetOffset,
    _In_ ULONG InstructionSize
);

namespace crt
{
    char* stristr(const char* str1, const char* str2);
}

namespace UMEM {
    PBYTE find_pattern(PVOID base, LPCSTR pattern, LPCSTR mask);
    auto find_pattern_in_section(uintptr_t Base, CHAR* Pattern, CHAR* Mask, char* Scan_Section) -> uintptr_t;
}

namespace NTOS {
    UCHAR RandomNumber();
    PERESOURCE GetPsLoaded();

    PVOID GetProcessIdByNameKernel(PWSTR processName);
    uintptr_t get_kernel_module(const char* name);
    _declspec(noinline) auto get_ntos_base_address() -> uintptr_t;

    template<class type_t>
    type_t find_export(const char* export_name);

    uintptr_t get_eprocess(const wchar_t* process_name);
}