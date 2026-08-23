#pragma once
#define DEBUGPRINT TRUE
#define RVA(addr, size) ((PVOID)((PBYTE)(addr) + *(PINT)((PBYTE)(addr) + ((size) - (INT)sizeof(INT))) + (size)))

// CORE-WDK
#include <ntifs.h>
#include <ntddk.h>
#include <ntdef.h>
#include <ntimage.h>
#include <minwindef.h>
#include <stdarg.h>
#include "ia32.h"
#include "NTOS.h"
#include "EXPORTS.h"
#include "c_xor.h"
#include "imports.h"
#include "encrypt.h"
#include "oxorany_include.h"
#include "bootstrap.h"
// CORE-DRIVER
 
 

 
 
 
extern "C" NTKERNELAPI PPEB NTAPI PsGetProcessPeb(PEPROCESS Process);

extern "C" NTSYSCALLAPI NTSTATUS NTAPI ZwQuerySystemInformation(ULONG InfoClass, PVOID Buffer, ULONG Length, PULONG ReturnLength);
extern "C" NTSYSCALLAPI NTSTATUS NTAPI ZwProtectVirtualMemory(HANDLE ProcessHandle, PVOID* BaseAddress, PSIZE_T RegionSize, ULONG NewAccessProtection, PULONG OldAccessProtection);
inline ULONG NtBuildNumber;
namespace ntBuild {
    inline constexpr auto Win1803 = 17134;
    inline constexpr auto Win1809 = 17763;
    inline constexpr auto Win1903 = 18362;
    inline constexpr auto Win1909 = 18363;
    inline constexpr auto Win2004 = 19041;
    inline constexpr auto Win20H2 = 19569;
    inline constexpr auto Win21H1 = 20180;
    inline constexpr auto Win11_21H2 = 22000;
    inline constexpr auto Win11_22H2 = 22621;
}

namespace kernel_KPROCESS {

    // !_KPROCESS.UserDirectoryTableBase
    inline unsigned long long UserDirectoryTableBase = 0;
}

namespace kernel_EPROCESS {

    // !_EPROCESS.SectionBaseAddress
    inline unsigned long long SectionBaseAddress = 0;
}

inline bool initalizeOffsets() {

    RTL_OSVERSIONINFOW os_version_info;
    RtlGetVersion(&os_version_info);
    NtBuildNumber = os_version_info.dwBuildNumber;

    if (NtBuildNumber == ntBuild::Win1803)
    {
        kernel_KPROCESS::UserDirectoryTableBase = 0x278;
        kernel_EPROCESS::SectionBaseAddress = 0x3c0;
    }
    else if (NtBuildNumber == ntBuild::Win1809)
    {
        kernel_KPROCESS::UserDirectoryTableBase = 0x278;
        kernel_EPROCESS::SectionBaseAddress = 0x3c0;
    }
    else if (NtBuildNumber == ntBuild::Win1903)
    {
        kernel_KPROCESS::UserDirectoryTableBase = 0x280;
        kernel_EPROCESS::SectionBaseAddress = 0x3c8;
    }
    else if (NtBuildNumber == ntBuild::Win1909)
    {
        kernel_KPROCESS::UserDirectoryTableBase = 0x280;
        kernel_EPROCESS::SectionBaseAddress = 0x3c8;
    }
    else if (NtBuildNumber == ntBuild::Win2004)
    {
        kernel_KPROCESS::UserDirectoryTableBase = 0x388;
        kernel_EPROCESS::SectionBaseAddress = 0x520;
    }
    else if (NtBuildNumber == ntBuild::Win21H1)
    {
        kernel_KPROCESS::UserDirectoryTableBase = 0x388;
        kernel_EPROCESS::SectionBaseAddress = 0x520;
    }
    else if (NtBuildNumber == ntBuild::Win11_21H2)
    {
        kernel_KPROCESS::UserDirectoryTableBase = 0x388;
        kernel_EPROCESS::SectionBaseAddress = 0x520;
    }
    else if (NtBuildNumber == ntBuild::Win11_22H2)
    {
        kernel_KPROCESS::UserDirectoryTableBase = 0x388;
        kernel_EPROCESS::SectionBaseAddress = 0x520;
    }
    else
    {
        kernel_KPROCESS::UserDirectoryTableBase = 0x388;
        kernel_EPROCESS::SectionBaseAddress = 0x520;
    }

    return (kernel_KPROCESS::UserDirectoryTableBase != 0 && kernel_EPROCESS::SectionBaseAddress != 0);
}

class _GLOBAL_UEX {
private:

public:
    PEPROCESS TargetProcess;
    UINT32 pageIndex;
    ULONG64 TargetCr3;
    PVOID pid;


    _GLOBAL_UEX* operator->()
    {
        return this;
    }
};
extern _GLOBAL_UEX GLOBAL_UEX;
extern _GLOBAL_UEX GLOBAL_UEX2;

template <typename... Args>
void silence(PCSTR Msg, Args... args) {
    if (DEBUGPRINT == TRUE) {
        DbgPrintEx(0, 0, Msg, args...);
    }
}

extern uintptr_t ntos_image_base;
extern uintptr_t kernel_base;
extern PVOID sig;


// MEM

/* MEM */
#define PFN_TO_PAGE(pfn) (pfn << PAGE_SHIFT)
#define PAGE_TO_PFN(pfn) (pfn >> PAGE_SHIFT)

#pragma warning(push)
#pragma warning(disable : 4201) // nonstandard extension used: nameless struct/union

#pragma pack(push, 1)
typedef union CR3_
{
    ULONG64 Value;
    struct
    {
        ULONG64 Ignored1 : 3;
        ULONG64 WriteThrough : 1;
        ULONG64 CacheDisable : 1;
        ULONG64 Ignored2 : 7;
        ULONG64 Pml4 : 40;
        ULONG64 Reserved : 12;
    };
} PTE_CR3;

typedef union VIRT_ADDR_
{
    ULONG64 Value;
    void* Pointer;
    struct
    {
        ULONG64 Offset : 12;
        ULONG64 PtIndex : 9;
        ULONG64 PdIndex : 9;
        ULONG64 PdptIndex : 9;
        ULONG64 Pml4Index : 9;
        ULONG64 Reserved : 16;
    };
} VIRTUAL_ADDRESS;

typedef union PML4E_
{
    ULONG64 Value;
    struct
    {
        ULONG64 Present : 1;
        ULONG64 Rw : 1;
        ULONG64 User : 1;
        ULONG64 WriteThrough : 1;
        ULONG64 CacheDisable : 1;
        ULONG64 Accessed : 1;
        ULONG64 Ignored1 : 1;
        ULONG64 Reserved1 : 1;
        ULONG64 Ignored2 : 4;
        ULONG64 Pdpt : 40;
        ULONG64 Ignored3 : 11;
        ULONG64 Xd : 1;
    };
} PML4E;

typedef union PDPTE_
{
    ULONG64 Value;
    struct
    {
        ULONG64 Present : 1;
        ULONG64 Rw : 1;
        ULONG64 User : 1;
        ULONG64 WriteThrough : 1;
        ULONG64 CacheDisable : 1;
        ULONG64 Accessed : 1;
        ULONG64 Dirty : 1;
        ULONG64 PageSize : 1;
        ULONG64 Ignored2 : 4;
        ULONG64 Pd : 40;
        ULONG64 Ignored3 : 11;
        ULONG64 Xd : 1;
    };
} PDPTE;

typedef union PDE_
{
    ULONG64 Value;
    struct
    {
        ULONG64 Present : 1;
        ULONG64 Rw : 1;
        ULONG64 User : 1;
        ULONG64 WriteThrough : 1;
        ULONG64 CacheDisable : 1;
        ULONG64 Accessed : 1;
        ULONG64 Dirty : 1;
        ULONG64 PageSize : 1;
        ULONG64 Ignored2 : 4;
        ULONG64 Pt : 40;
        ULONG64 Ignored3 : 11;
        ULONG64 Xd : 1;
    };
} PDE;
typedef union PTE_
{
    ULONG64 Value;
    VIRTUAL_ADDRESS VirtualAddress;
    struct
    {
        ULONG64 Present : 1;
        ULONG64 Rw : 1;
        ULONG64 User : 1;
        ULONG64 WriteThrough : 1;
        ULONG64 CacheDisable : 1;
        ULONG64 Accessed : 1;
        ULONG64 Dirty : 1;
        ULONG64 Pat : 1;
        ULONG64 Global : 1;
        ULONG64 Ignored1 : 3;
        ULONG64 PFN : 40;
        ULONG64 Ignored3 : 11;
        ULONG64 Xd : 1;
    };
} PTE;
typedef struct _MMPTE_HARDWARE
{
    struct /* bitfield */
    {
        /* 0x0000 */ unsigned __int64 Valid : 1; /* bit position: 0 */
        /* 0x0000 */ unsigned __int64 Dirty1 : 1; /* bit position: 1 */
        /* 0x0000 */ unsigned __int64 Owner : 1; /* bit position: 2 */
        /* 0x0000 */ unsigned __int64 WriteThrough : 1; /* bit position: 3 */
        /* 0x0000 */ unsigned __int64 CacheDisable : 1; /* bit position: 4 */
        /* 0x0000 */ unsigned __int64 Accessed : 1; /* bit position: 5 */
        /* 0x0000 */ unsigned __int64 Dirty : 1; /* bit position: 6 */
        /* 0x0000 */ unsigned __int64 LargePage : 1; /* bit position: 7 */
        /* 0x0000 */ unsigned __int64 Global : 1; /* bit position: 8 */
        /* 0x0000 */ unsigned __int64 CopyOnWrite : 1; /* bit position: 9 */
        /* 0x0000 */ unsigned __int64 Unused : 1; /* bit position: 10 */
        /* 0x0000 */ unsigned __int64 Write : 1; /* bit position: 11 */
        /* 0x0000 */ unsigned __int64 PageFrameNumber : 36; /* bit position: 12 */
        /* 0x0000 */ unsigned __int64 ReservedForHardware : 4; /* bit position: 48 */
        /* 0x0000 */ unsigned __int64 ReservedForSoftware : 4; /* bit position: 52 */
        /* 0x0000 */ unsigned __int64 WsleAge : 4; /* bit position: 56 */
        /* 0x0000 */ unsigned __int64 WsleProtection : 3; /* bit position: 60 */
        /* 0x0000 */ unsigned __int64 NoExecute : 1; /* bit position: 63 */
    }; /* bitfield */
} MMPTE_HARDWARE, * PMMPTE_HARDWARE; /* size: 0x0008 */

typedef struct _MMPTE_PROTOTYPE
{
    struct /* bitfield */
    {
        /* 0x0000 */ unsigned __int64 Valid : 1; /* bit position: 0 */
        /* 0x0000 */ unsigned __int64 DemandFillProto : 1; /* bit position: 1 */
        /* 0x0000 */ unsigned __int64 HiberVerifyConverted : 1; /* bit position: 2 */
        /* 0x0000 */ unsigned __int64 ReadOnly : 1; /* bit position: 3 */
        /* 0x0000 */ unsigned __int64 SwizzleBit : 1; /* bit position: 4 */
        /* 0x0000 */ unsigned __int64 Protection : 5; /* bit position: 5 */
        /* 0x0000 */ unsigned __int64 Prototype : 1; /* bit position: 10 */
        /* 0x0000 */ unsigned __int64 Combined : 1; /* bit position: 11 */
        /* 0x0000 */ unsigned __int64 Unused1 : 4; /* bit position: 12 */
        /* 0x0000 */ __int64 ProtoAddress : 48; /* bit position: 16 */
    }; /* bitfield */
} MMPTE_PROTOTYPE, * PMMPTE_PROTOTYPE; /* size: 0x0008 */

typedef struct _MMPTE_SOFTWARE
{
    struct /* bitfield */
    {
        /* 0x0000 */ unsigned __int64 Valid : 1; /* bit position: 0 */
        /* 0x0000 */ unsigned __int64 PageFileReserved : 1; /* bit position: 1 */
        /* 0x0000 */ unsigned __int64 PageFileAllocated : 1; /* bit position: 2 */
        /* 0x0000 */ unsigned __int64 ColdPage : 1; /* bit position: 3 */
        /* 0x0000 */ unsigned __int64 SwizzleBit : 1; /* bit position: 4 */
        /* 0x0000 */ unsigned __int64 Protection : 5; /* bit position: 5 */
        /* 0x0000 */ unsigned __int64 Prototype : 1; /* bit position: 10 */
        /* 0x0000 */ unsigned __int64 Transition : 1; /* bit position: 11 */
        /* 0x0000 */ unsigned __int64 PageFileLow : 4; /* bit position: 12 */
        /* 0x0000 */ unsigned __int64 UsedPageTableEntries : 10; /* bit position: 16 */
        /* 0x0000 */ unsigned __int64 ShadowStack : 1; /* bit position: 26 */
        /* 0x0000 */ unsigned __int64 Unused : 5; /* bit position: 27 */
        /* 0x0000 */ unsigned __int64 PageFileHigh : 32; /* bit position: 32 */
    }; /* bitfield */
} MMPTE_SOFTWARE, * PMMPTE_SOFTWARE; /* size: 0x0008 */

typedef struct _MMPTE_TIMESTAMP
{
    struct /* bitfield */
    {
        /* 0x0000 */ unsigned __int64 MustBeZero : 1; /* bit position: 0 */
        /* 0x0000 */ unsigned __int64 Unused : 3; /* bit position: 1 */
        /* 0x0000 */ unsigned __int64 SwizzleBit : 1; /* bit position: 4 */
        /* 0x0000 */ unsigned __int64 Protection : 5; /* bit position: 5 */
        /* 0x0000 */ unsigned __int64 Prototype : 1; /* bit position: 10 */
        /* 0x0000 */ unsigned __int64 Transition : 1; /* bit position: 11 */
        /* 0x0000 */ unsigned __int64 PageFileLow : 4; /* bit position: 12 */
        /* 0x0000 */ unsigned __int64 Reserved : 16; /* bit position: 16 */
        /* 0x0000 */ unsigned __int64 GlobalTimeStamp : 32; /* bit position: 32 */
    }; /* bitfield */
} MMPTE_TIMESTAMP, * PMMPTE_TIMESTAMP; /* size: 0x0008 */

typedef struct _MMPTE_TRANSITION
{
    struct /* bitfield */
    {
        /* 0x0000 */ unsigned __int64 Valid : 1; /* bit position: 0 */
        /* 0x0000 */ unsigned __int64 Write : 1; /* bit position: 1 */
        /* 0x0000 */ unsigned __int64 Spare : 1; /* bit position: 2 */
        /* 0x0000 */ unsigned __int64 IoTracker : 1; /* bit position: 3 */
        /* 0x0000 */ unsigned __int64 SwizzleBit : 1; /* bit position: 4 */
        /* 0x0000 */ unsigned __int64 Protection : 5; /* bit position: 5 */
        /* 0x0000 */ unsigned __int64 Prototype : 1; /* bit position: 10 */
        /* 0x0000 */ unsigned __int64 Transition : 1; /* bit position: 11 */
        /* 0x0000 */ unsigned __int64 PageFrameNumber : 36; /* bit position: 12 */
        /* 0x0000 */ unsigned __int64 Unused : 16; /* bit position: 48 */
    }; /* bitfield */
} MMPTE_TRANSITION, * PMMPTE_TRANSITION; /* size: 0x0008 */

typedef struct _MMPTE_SUBSECTION
{
    struct /* bitfield */
    {
        /* 0x0000 */ unsigned __int64 Valid : 1; /* bit position: 0 */
        /* 0x0000 */ unsigned __int64 Unused0 : 3; /* bit position: 1 */
        /* 0x0000 */ unsigned __int64 SwizzleBit : 1; /* bit position: 4 */
        /* 0x0000 */ unsigned __int64 Protection : 5; /* bit position: 5 */
        /* 0x0000 */ unsigned __int64 Prototype : 1; /* bit position: 10 */
        /* 0x0000 */ unsigned __int64 ColdPage : 1; /* bit position: 11 */
        /* 0x0000 */ unsigned __int64 Unused1 : 3; /* bit position: 12 */
        /* 0x0000 */ unsigned __int64 ExecutePrivilege : 1; /* bit position: 15 */
        /* 0x0000 */ __int64 SubsectionAddress : 48; /* bit position: 16 */
    }; /* bitfield */
} MMPTE_SUBSECTION, * PMMPTE_SUBSECTION; /* size: 0x0008 */

typedef struct _MMPTE_LIST
{
    struct /* bitfield */
    {
        /* 0x0000 */ unsigned __int64 Valid : 1; /* bit position: 0 */
        /* 0x0000 */ unsigned __int64 OneEntry : 1; /* bit position: 1 */
        /* 0x0000 */ unsigned __int64 filler0 : 2; /* bit position: 2 */
        /* 0x0000 */ unsigned __int64 SwizzleBit : 1; /* bit position: 4 */
        /* 0x0000 */ unsigned __int64 Protection : 5; /* bit position: 5 */
        /* 0x0000 */ unsigned __int64 Prototype : 1; /* bit position: 10 */
        /* 0x0000 */ unsigned __int64 Transition : 1; /* bit position: 11 */
        /* 0x0000 */ unsigned __int64 filler1 : 16; /* bit position: 12 */
        /* 0x0000 */ unsigned __int64 NextEntry : 36; /* bit position: 28 */
    }; /* bitfield */
} MMPTE_LIST, * PMMPTE_LIST; /* size: 0x0008 */

typedef struct _MMPTE
{
    union
    {
        union
        {
            /* 0x0000 */ unsigned __int64 Long;
            /* 0x0000 */ volatile unsigned __int64 VolatileLong;
            /* 0x0000 */ struct _MMPTE_HARDWARE Hard;
            /* 0x0000 */ struct _MMPTE_PROTOTYPE Proto;
            /* 0x0000 */ struct _MMPTE_SOFTWARE Soft;
            /* 0x0000 */ struct _MMPTE_TIMESTAMP TimeStamp;
            /* 0x0000 */ struct _MMPTE_TRANSITION Trans;
            /* 0x0000 */ struct _MMPTE_SUBSECTION Subsect;
            /* 0x0000 */ struct _MMPTE_LIST List;
        }; /* size: 0x0008 */
    } /* size: 0x0008 */ u;
} MMPTE, * PMMPTE; /* size: 0x0008 */
#pragma pack(pop)

typedef struct _PAGE
{
    PVOID VirtualAddress;
    PTE* PTE;

    ULONG64 PreviousPageFrame;
} _PAGE;


typedef union _pte_t
{
    uintptr_t value;
    struct
    {
        uintptr_t present : 1;
        uintptr_t write : 1;
        uintptr_t user : 1;
        uintptr_t write_through : 1;
        uintptr_t cache_disable : 1;
        uintptr_t accessed : 1;
        uintptr_t dirty : 1;
        uintptr_t large_page : 1;
        uintptr_t global : 1;
        uintptr_t ignored_2 : 3;
        uintptr_t page_frame_number : 40;
        uintptr_t ignored_1 : 11;
        uintptr_t execution_disabled : 1;
    } fields;
} pte_t;