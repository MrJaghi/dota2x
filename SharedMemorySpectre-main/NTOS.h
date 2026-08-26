#pragma once
#include "includes.h"

_declspec(noinline) auto resolve_address(
    ULONG_PTR Instruction,
    ULONG OffsetOffset,
    ULONG InstructionSize
) -> ULONG_PTR;

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
    auto find_pattern_in_section(ULONG_PTR Base, CHAR* Pattern, CHAR* Mask, char* Scan_Section) -> ULONG_PTR;
}

namespace NTOS {
    UCHAR RandomNumber();
    PERESOURCE GetPsLoaded();

    PVOID GetProcessIdByNameKernel(PWSTR processName);
    ULONG_PTR get_kernel_module(const char* name);
    _declspec(noinline) auto get_ntos_base_address() -> ULONG_PTR;

    template<class type_t>
    type_t find_export(const char* export_name);

    ULONG_PTR get_eprocess(const wchar_t* process_name);
}