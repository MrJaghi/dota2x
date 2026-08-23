#include "NTOS.h"

_declspec(noinline) auto resolve_address(
    uintptr_t Instruction,
    ULONG OffsetOffset,
    ULONG InstructionSize) -> uintptr_t 
{
    LONG RipOffset = *(PLONG)(Instruction + OffsetOffset);
    auto ResolvedAddr = (
        Instruction +
        InstructionSize +
        RipOffset);

    return ResolvedAddr;
}

PVOID resolve_relative_address(
    _In_ PVOID Instruction,
    _In_ ULONG OffsetOffset,
    _In_ ULONG InstructionSize
) {
    ULONG_PTR Instr = (ULONG_PTR)Instruction;
    LONG RipOffset = *(PLONG)(Instr + OffsetOffset);
    PVOID ResolvedAddr = (PVOID)(Instr + InstructionSize + RipOffset);

    return ResolvedAddr;
}

namespace crt
{
    INT klower(int c)
    {
        if (c >= 'A' && c <= 'Z')
            return c + 'a' - 'A';
        else
            return c;
    }

    INT kwcscmp(const wchar_t* s1, const wchar_t* s2)
    {
        while (*s1 == *s2++)
            if (*s1++ == '\0')
                return (0);
        /* XXX assumes wchar_t = int */
        return (*(const unsigned int*)s1 - *(const unsigned int*)--s2);
    }

    CHAR* kLowerStr(CHAR* Str)
    {
        for (CHAR* S = Str; *S; ++S)
        {
            *S = (CHAR)klower(*S);
        }
        return Str;
    }

    SIZE_T kstrlen(const char* str)
    {
        const char* s;
        for (s = str; *s; ++s);
        return (s - str);
    }

    INT kstrncmp(const char* s1, const char* s2, size_t n)
    {
        if (n == 0)
            return (0);
        do {
            if (*s1 != *s2++)
                return (*(unsigned char*)s1 - *(unsigned char*)--s2);
            if (*s1++ == 0)
                break;
        } while (--n != 0);
        return (0);
    }

    INT kstrcmp(const char* s1, const char* s2)
    {
        while (*s1 == *s2++)
            if (*s1++ == 0)
                return (0);
        return (*(unsigned char*)s1 - *(unsigned char*)--s2);
    }

    INT kmemcmp(const void* s1, const void* s2, size_t n)
    {
        const unsigned char* p1 = (const unsigned char*)s1;
        const unsigned char* end1 = p1 + n;
        const unsigned char* p2 = (const unsigned char*)s2;
        int                   d = 0;
        for (;;) {
            if (d || p1 >= end1) break;
            d = (int)*p1++ - (int)*p2++;
            if (d || p1 >= end1) break;
            d = (int)*p1++ - (int)*p2++;
            if (d || p1 >= end1) break;
            d = (int)*p1++ - (int)*p2++;
            if (d || p1 >= end1) break;
            d = (int)*p1++ - (int)*p2++;
        }
        return d;
    }

    char* stristr(const char* str1, const char* str2) {
        const char* p1 = str1;
        const char* p2 = str2;
        const char* r = *p2 == 0 ? str1 : 0;

        while (*p1 != 0 && *p2 != 0)
        {
            if (tolower((unsigned char)*p1) == tolower((unsigned char)*p2))
            {
                if (r == 0)
                {
                    r = p1;
                }

                p2++;
            }
            else
            {
                p2 = str2;
                if (r != 0)
                {
                    p1 = r + 1;
                }

                if (tolower((unsigned char)*p1) == tolower((unsigned char)*p2))
                {
                    r = p1;
                    p2++;
                }
                else
                {
                    r = 0;
                }
            }

            p1++;
        }

        return *p2 == 0 ? (char*)r : 0;
    }
}

namespace imports
{
    typedef struct _HANDLE_TABLE_ENTRY_INFO
    {
        ULONG AuditMask;
    } HANDLE_TABLE_ENTRY_INFO, * PHANDLE_TABLE_ENTRY_INFO;

    typedef struct _HANDLE_TABLE_ENTRY
    {
        union
        {
            PVOID Object;
            ULONG_PTR ObAttributes;
            PHANDLE_TABLE_ENTRY_INFO InfoTable;
            ULONG_PTR Value;
        };
        union
        {
            ULONG GrantedAccess;
            struct
            {
                USHORT GrantedAccessIndex;
                USHORT CreatorBackTraceIndex;
            };
            LONG NextFreeTableEntry;
        };
    } HANDLE_TABLE_ENTRY, * PHANDLE_TABLE_ENTRY;

    size_t strlen(const char* String)
    {
        UINT32 Length = 0;

        while (*String)
        {
            Length++;
            String++;
        }

        return (Length);
    }

    int __cdecl memcmp(const void* s1, const void* s2, size_t n)
    {
        if (n != 0) {
            const unsigned char* p1 = (const unsigned char*)s1, * p2 = (const unsigned char*)s2;
            do {
                if (*p1++ != *p2++)
                    return (*--p1 - *--p2);
            } while (--n != 0);
        }
        return 0;
    }

    int strcmp(const char* String1, const char* String2)
    {
        for (; (*String1 == *String2); String2++)
        {
            if (!*String1++)
            {
                return (0);
            }
        }

        return ((unsigned char)*String1 - (unsigned char)*String2);
    }
}

namespace UMEM {
    inline PIMAGE_NT_HEADERS get_nt_headers(PVOID module)
    {
        if (!module)
            return nullptr;
        return (PIMAGE_NT_HEADERS)((PBYTE)module + PIMAGE_DOS_HEADER(module)->e_lfanew);
    }

    PBYTE ex_find_pattern(PVOID module, DWORD size, LPCSTR pattern, LPCSTR mask)
    {
        if (!module)
            return nullptr;

        auto checkMask = [](PBYTE buffer, LPCSTR pattern, LPCSTR mask) -> BOOL
            {
                for (auto x = buffer; *mask; pattern++, mask++, x++) {
                    auto addr = *(BYTE*)(pattern);
                    if (addr != *x && *mask != '?')
                        return FALSE;
                }

                return TRUE;
            };

        for (auto x = 0; x < size - strlen(mask); x++) {

            auto addr = (PBYTE)module + x;
            if (checkMask(addr, pattern, mask)) {
                return addr;
            }
        }

        return NULL;
    }

    PBYTE find_pattern(PVOID base, LPCSTR pattern, LPCSTR mask)
    {
        if (!base) return 0;

        auto header = get_nt_headers(base);
        auto section = IMAGE_FIRST_SECTION(header);

        for (auto x = 0; x < header->FileHeader.NumberOfSections; x++, section++) {

            if (!memcmp(section->Name, skCrypt(".text"), 5) || !memcmp(section->Name, skCrypt("PAGE"), 4))
            {
                auto addr = ex_find_pattern((PBYTE)base + section->VirtualAddress, section->Misc.VirtualSize, pattern, mask);
                if (addr)
                    return addr;
            }
        }

        return NULL;
    }

    auto get_pattern(uintptr_t base, size_t range, const char* pattern, const char* mask) -> uintptr_t
    {
        const auto check_mask = [](const char* base, const char* pattern, const char* mask) -> bool
            {
                for (; *mask; ++base, ++pattern, ++mask)
                {
                    if (*mask == 'x' && *base != *pattern)
                    {
                        return false;
                    }
                }

                return true;
            };

        range = range - crt::kstrlen(mask);

        for (size_t i = 0; i < range; ++i)
        {
            if (check_mask((const char*)base + i, pattern, mask))
            {
                return base + i;
            }
        }

        return NULL;
    }

    auto find_pattern_in_section(uintptr_t Base, CHAR* Pattern, CHAR* Mask, char* Scan_Section) -> uintptr_t
    {
        SPOOF_FUNC;

        IMAGE_NT_HEADERS* Headers{ (PIMAGE_NT_HEADERS)(Base + ((PIMAGE_DOS_HEADER)Base)->e_lfanew) };
        IMAGE_SECTION_HEADER* Sections{ IMAGE_FIRST_SECTION(Headers) };

        for (auto i = 0; i < Headers->FileHeader.NumberOfSections; ++i)
        {
            IMAGE_SECTION_HEADER* Section{ &Sections[i] };

            if (!crt::kmemcmp(Section->Name, Scan_Section, crt::kstrlen(Scan_Section)) || !crt::kmemcmp(Section->Name, E("PAGE"), 4))
            {
                const auto match = get_pattern(Base + Section->VirtualAddress, Section->Misc.VirtualSize, Pattern, Mask);

                if (match) {
                    return (match);
                }
            }
        }

        return 0;
    }
}

namespace NTOS {
    UCHAR RandomNumber() {
        UNICODE_STRING RoutineName = RTL_CONSTANT_STRING(L"RtlRandom");
        auto cRtlRandom = reinterpret_cast<decltype(&RtlRandomEx)>(EXPORTS::mm_get_system_routine_address(&RoutineName));

        ULONG Seed = 5216799;
        ULONG Rand = cRtlRandom(&Seed) % 100;

        UCHAR RandInt = 0;

        if (Rand >= 101 || Rand <= -1)
            RandInt = 72;

        return (UCHAR)(Rand);
    }

    PERESOURCE GetPsLoaded() {
        ERESOURCE PsLoadedModuleResource;

        /* I know this is really retarded, I couldnt find any other way to use static encrypted strings with UNICODE_STRING/RTL */
        UNICODE_STRING routineName;
        routineName.Buffer = (PWSTR)(L"PsLoadedModuleResource");
        routineName.Length = (USHORT)(wcslen((L"PsLoadedModuleResource")) * sizeof(WCHAR));
        routineName.MaximumLength = (USHORT)((wcslen((L"PsLoadedModuleResource")) + oxorany(1)) * sizeof(WCHAR));

        auto cPsLoadedModuleResource = reinterpret_cast<decltype(&PsLoadedModuleResource)>(EXPORTS::mm_get_system_routine_address(&routineName));
        return cPsLoadedModuleResource;
    }

    PVOID GetProcessIdByNameKernel(PWSTR processName)
    {
        NTSTATUS status;
        ULONG returnedLength;
        ULONG bufferLength = 0;
        PVOID buffer = NULL;

        PSYSTEM_PROCESS_INFORMATION spi;
        UNICODE_STRING processUnicodeName, iteratedProcessName;
        PVOID processId = NULL;

        EXPORTS::rtl_init_unicode_string(&processUnicodeName, processName);

        status = EXPORTS::ZwQuerySystemInformation_impl(SystemProcessInformation, 0, 0, &bufferLength);
        if (status != STATUS_INFO_LENGTH_MISMATCH)
        {
            return NULL;
        }

        buffer = EXPORTS::ex_allocate_pool(NonPagedPoolNx, bufferLength);
        if (buffer == NULL)
        {
            return NULL;
        }

        status = EXPORTS::ZwQuerySystemInformation_impl(SystemProcessInformation, buffer, bufferLength, &returnedLength);
        if (!NT_SUCCESS(status))
        {
            EXPORTS::ex_free_pool_with_tag(buffer, 0);
            return NULL;
        }

        spi = (PSYSTEM_PROCESS_INFORMATION)buffer;
        while (TRUE)
        {
            if (spi->ImageName.Buffer != NULL)
            {
                EXPORTS::rtl_init_unicode_string(&iteratedProcessName, spi->ImageName.Buffer);

                if (EXPORTS::rtl_compare_unicode_string(&processUnicodeName, &iteratedProcessName, TRUE) == 0)
                {
                    processId = spi->UniqueProcessId;
                    break;
                }
            }

            if (spi->NextEntryOffset == 0)
            {
                break;
            }
            else
            {
                spi = (PSYSTEM_PROCESS_INFORMATION)(((PCHAR)spi) + spi->NextEntryOffset);
            }
        }

        EXPORTS::ex_free_pool_with_tag(buffer, 0);
        return processId;
    }


    void* get_system_information(SYSTEM_INFORMATION_CLASS information_class)
    {
        unsigned long size = 32;
        char buffer[32];

        EXPORTS::ZwQuerySystemInformation_impl(information_class, buffer, size, &size);

        void* info = ExAllocatePoolZero(NonPagedPool, size, 0);
        if (!info)
            return nullptr;

        if (!NT_SUCCESS(EXPORTS::ZwQuerySystemInformation_impl(information_class, info, size, &size))) {
            ExFreePool(info);
            return nullptr;
        }

        return info;
    }

    uintptr_t get_kernel_module(const char* name)
    {
        const auto to_lower = [](char* string) -> const char* {
            for (char* pointer = string; *pointer != '\0'; ++pointer) {
                *pointer = (char)(short)tolower(*pointer);
            }

            return string;
            };

        const PRTL_PROCESS_MODULES info = (PRTL_PROCESS_MODULES)get_system_information(SystemModuleInformation);

        if (!info)
            return NULL;

        for (size_t i = 0; i < info->NumberOfModules; ++i) {
            const auto& mod = info->Modules[i];

            if (strcmp(to_lower((char*)mod.FullPathName + mod.OffsetToFileName), name) == 0) {
                const void* address = mod.ImageBase;
                ExFreePool(info);
                return (uintptr_t)address;
            }
        }

        ExFreePool(info);
        return NULL;
    }

    _declspec(noinline) auto get_ntos_base_address() -> uintptr_t {
        typedef unsigned char uint8_t;
        auto Idt_base = reinterpret_cast<uintptr_t>(KeGetPcr()->IdtBase);
        auto align_page = *reinterpret_cast<uintptr_t*>(Idt_base + oxorany(4)) >> oxorany(0xc) << oxorany(0xc);

        for (; align_page; align_page -= oxorany(PAGE_SIZE))
        {
            for (int index = 0; index < oxorany(PAGE_SIZE) - 0x7; index++)
            {
                auto current_address = static_cast<intptr_t>(align_page) + index;

                if (*reinterpret_cast<uint8_t*>(current_address) == oxorany(0x48)
                    && *reinterpret_cast<uint8_t*>(current_address + oxorany(1)) == oxorany(0x8D)
                    && *reinterpret_cast<uint8_t*>(current_address + oxorany(2)) == oxorany(0x1D)
                    && *reinterpret_cast<uint8_t*>(current_address + oxorany(6)) == oxorany(0xFF)) //48 8d 1D ?? ?? ?? FF
                {
                    // rva our virtual address lol
                    auto Ntosbase = resolve_address(current_address, oxorany(3), oxorany(7));
                    if (!((UINT64)Ntosbase & 0xfff))
                    {
                        return Ntosbase;
                    }
                }
            }
        }
        return 0;
    }

    template<class type_t>
    type_t find_export(const char* export_name) {
        auto NTOS = ntos_image_base;
        if (NTOS == NULL) {
            NTOS = NTOS::get_ntos_base_address();
            ntos_image_base = NTOS;
            kernel_base = NTOS;
        }

        auto dos_header = reinterpret_cast<dos_header_t*>(NTOS);
        if (!dos_header->is_valid()) {
            return nullptr;  // Return nullptr on failure instead of `false`.
        }

        auto m_nt_header = reinterpret_cast<nt_headers_t*>(reinterpret_cast<uint64_t>(dos_header) + dos_header->m_lfanew);
        if (!m_nt_header->is_valid()) {
            return nullptr;  // Return nullptr on failure instead of `false`.
        }

        auto library{ reinterpret_cast<int8_t*>(dos_header) };
        auto export_directory =
            reinterpret_cast<export_directory_t*>(NTOS + m_nt_header->m_export_table.m_virtual_address);
        if (!export_directory->m_address_of_functions
            || !export_directory->m_address_of_names
            || !export_directory->m_address_of_names_ordinals) {
            return nullptr;  // Return nullptr on failure instead of an empty object.
        }

        auto names{ reinterpret_cast<int32_t*>(library + export_directory->m_address_of_names) };
        auto functions{ reinterpret_cast<int32_t*>(library + export_directory->m_address_of_functions) };
        auto ordinals{ reinterpret_cast<int16_t*>(library + export_directory->m_address_of_names_ordinals) };

        for (int32_t i = 0; i < export_directory->m_number_of_names; i++) {
            auto current_name{ library + names[i] };
            auto current_function{ library + functions[ordinals[i]] };


            if (!strcmp(export_name, reinterpret_cast<char*>(current_name))) {
                return reinterpret_cast<type_t>(current_function);
            }
        }
        return nullptr;  // Return nullptr on failure instead of 0.
    }

    uintptr_t get_eprocess(const wchar_t* process_name)
    {
        static auto KeCapturePersistentThreadState_t = NTOS::find_export<addr_t>(E("KeCapturePersistentThreadState"));
        if (KeCapturePersistentThreadState_t == NULL) {
            return 0;
        }

        while (KeCapturePersistentThreadState_t[0x0] != oxorany(0x20)
            || KeCapturePersistentThreadState_t[0x1] != oxorany(0x48)
            || KeCapturePersistentThreadState_t[0x2] != oxorany(0x8d))
            KeCapturePersistentThreadState_t++;
        auto PsActiveProcessHead = *reinterpret_cast<list_entry_t**>
            (&KeCapturePersistentThreadState_t[0x8] + *reinterpret_cast<int32_t*>(&KeCapturePersistentThreadState_t[0x4]));

        auto process_list_head = PsActiveProcessHead;
        if (!process_list_head) {
            return 0;
        }

        const auto linkage_va =
            reinterpret_cast<addr_t>(process_list_head) -
            EXPORTS::ps_initial_system_process();
        if (!linkage_va) {
            return 0;
        }

        for (auto flink = process_list_head->m_flink; flink; flink = flink->m_flink)
        {
            const auto next_eprocess = reinterpret_cast<addr_t>(flink) - linkage_va;
            if (!next_eprocess || !EXPORTS::ps_get_process_exit_status(next_eprocess)) {
                continue;
            }

            auto unicode_name{ EXPORTS::ps_query_full_process_image_name(next_eprocess) };
            if (!unicode_name->m_buffer
                || !unicode_name->m_length
                || !unicode_name->m_maximum_length)
                continue;

            if (wcscmp(unicode_name->m_buffer, process_name))
            {
                return next_eprocess;
            }
        }

        return 0;
    }
}

template unsigned char* NTOS::find_export<unsigned char*>(const char* export_name);