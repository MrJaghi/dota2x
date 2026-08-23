#pragma once
#include <ntddmou.h>
#include <ntddk.h>
#include <ntifs.h>
#include <minwindef.h>

#define MM_UNLOADED_DRIVERS_SIZE 50
typedef struct _MM_UNLOADED_DRIVER {
    UNICODE_STRING 	Name;
    PVOID 			ModuleStart;
    PVOID 			ModuleEnd;
    ULONG64 		UnloadTime;
} MM_UNLOADED_DRIVER, * PMM_UNLOADED_DRIVER;

struct list_entry_t {
    list_entry_t* m_flink;
    list_entry_t* m_blink;
};

struct unicode_string_t {
    UINT16 m_length;
    UINT16 m_maximum_length;
    wchar_t* m_buffer;
};

enum pe_magic_t {
    dos_header = 0x5a4d,
    nt_headers = 0x4550,
    opt_header = 0x020b
};

struct PiDDBCacheEntry
{
    LIST_ENTRY		List;
    UNICODE_STRING	DriverName;
    ULONG			TimeDateStamp;
    NTSTATUS		LoadStatus;
    char			_0x0028[16]; // data from the shim engine, or uninitialized memory for custom drivers
};

typedef struct _SYSTEM_MODULE {
    HANDLE Section;
    PVOID MappedBase;
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    UCHAR  FullPathName[MAXIMUM_FILENAME_LENGTH];
} SYSTEM_MODULE, * PSYSTEM_MODULE;

typedef struct _SYSTEM_MODULE_INFORMATION {
    ULONG NumberOfModules;
    SYSTEM_MODULE Modules[1];
} SYSTEM_MODULE_INFORMATION, * PSYSTEM_MODULE_INFORMATION;

struct data_directory_t {
    INT32 m_virtual_address;
    INT32 m_size;

    template< class type_t >

    type_t as_rva(
        unsigned char* rva
    ) {
        return reinterpret_cast<type_t>(rva + m_virtual_address);
    }
};

struct dos_header_t {
    INT16 m_magic;
    INT16 m_cblp;
    INT16 m_cp;
    INT16 m_crlc;
    INT16 m_cparhdr;
    INT16 m_minalloc;
    INT16 m_maxalloc;
    INT16 m_ss;
    INT16 m_sp;
    INT16 m_csum;
    INT16 m_ip;
    INT16 m_cs;
    INT16 m_lfarlc;
    INT16 m_ovno;
    INT16 m_res0[0x4];
    INT16 m_oemid;
    INT16 m_oeminfo;
    INT16 m_res1[0xa];
    INT32 m_lfanew;


    constexpr bool is_valid() {
        return m_magic == pe_magic_t::dos_header;
    }
};

struct export_directory_t {
    INT32 m_characteristics;
    INT32 m_time_date_stamp;
    INT16 m_major_version;
    INT16 m_minor_version;
    INT32 m_name;
    INT32 m_base;
    INT32 m_number_of_functions;
    INT32 m_number_of_names;
    INT32 m_address_of_functions;
    INT32 m_address_of_names;
    INT32 m_address_of_names_ordinals;
};

struct nt_headers_t {
    INT32 m_signature;
    INT16 m_machine;
    INT16 m_number_of_sections;
    INT32 m_time_date_stamp;
    INT32 m_pointer_to_symbol_table;
    INT32 m_number_of_symbols;
    INT16 m_size_of_optional_header;
    INT16 m_characteristics;

    INT16 m_magic;
    INT8 m_major_linker_version;
    INT8 m_minor_linker_version;
    INT32 m_size_of_code;
    INT32 m_size_of_initialized_data;
    INT32 m_size_of_uninitialized_data;
    INT32 m_address_of_entry_point;
    INT32 m_base_of_code;
    UINT64 m_image_base;
    INT32 m_section_alignment;
    INT32 m_file_alignment;
    INT16 m_major_operating_system_version;
    INT16 m_minor_operating_system_version;
    INT16 m_major_image_version;
    INT16 m_minor_image_version;
    INT16 m_major_subsystem_version;
    INT16 m_minor_subsystem_version;
    INT32 m_win32_version_value;
    INT32 m_size_of_image;
    INT32 m_size_of_headers;
    INT32 m_check_sum;
    INT16 m_subsystem;
    INT16 m_dll_characteristics;
    UINT64 m_size_of_stack_reserve;
    UINT64 m_size_of_stack_commit;
    UINT64 m_size_of_heap_reserve;
    UINT64 m_size_of_heap_commit;
    INT32 m_loader_flags;
    INT32 m_number_of_rva_and_sizes;

    data_directory_t m_export_table;
    data_directory_t m_import_table;
    data_directory_t m_resource_table;
    data_directory_t m_exception_table;
    data_directory_t m_certificate_table;
    data_directory_t m_base_relocation_table;
    data_directory_t m_debug;
    data_directory_t m_architecture;
    data_directory_t m_global_ptr;
    data_directory_t m_tls_table;
    data_directory_t m_load_config_table;
    data_directory_t m_bound_import;
    data_directory_t m_iat;
    data_directory_t m_delay_import_descriptor;
    data_directory_t m_clr_runtime_header;
    data_directory_t m_reserved;


    constexpr bool is_valid() {
        return m_signature == pe_magic_t::nt_headers
            && m_magic == pe_magic_t::opt_header;
    }
};



// C-WIN

typedef struct _SYSTEM_PROCESS_INFORMATION {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    BYTE Reserved1[48];
    UNICODE_STRING ImageName;
    KPRIORITY BasePriority;
    HANDLE UniqueProcessId;
    PVOID Reserved2;
    ULONG HandleCount;
    ULONG SessionId;
    PVOID Reserved3;
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG Reserved4;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    PVOID Reserved5;
    SIZE_T QuotaPagedPoolUsage;
    PVOID Reserved6;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivatePageCount;
    LARGE_INTEGER Reserved7[6];
} SYSTEM_PROCESS_INFORMATION, * PSYSTEM_PROCESS_INFORMATION;

typedef enum _SYSTEM_INFORMATION_CLASS
{
    SystemBasicInformation = 0x0,
    SystemProcessorInformation = 0x1,
    SystemPerformanceInformation = 0x2,
    SystemTimeOfDayInformation = 0x3,
    SystemPathInformation = 0x4,
    SystemProcessInformation = 0x5,
    SystemCallCountInformation = 0x6,
    SystemDeviceInformation = 0x7,
    SystemProcessorPerformanceInformation = 0x8,
    SystemFlagsInformation = 0x9,
    SystemCallTimeInformation = 0xa,
    SystemModuleInformation = 0xb,
    SystemLocksInformation = 0xc,
    SystemStackTraceInformation = 0xd,
    SystemPagedPoolInformation = 0xe,
    SystemNonPagedPoolInformation = 0xf,
    SystemHandleInformation = 0x10,
    SystemObjectInformation = 0x11,
    SystemPageFileInformation = 0x12,
    SystemVdmInstemulInformation = 0x13,
    SystemVdmBopInformation = 0x14,
    SystemFileCacheInformation = 0x15,
    SystemPoolTagInformation = 0x16,
    SystemInterruptInformation = 0x17,
    SystemDpcBehaviorInformation = 0x18,
    SystemFullMemoryInformation = 0x19,
    SystemLoadGdiDriverInformation = 0x1a,
    SystemUnloadGdiDriverInformation = 0x1b,
    SystemTimeAdjustmentInformation = 0x1c,
    SystemSummaryMemoryInformation = 0x1d,
    SystemMirrorMemoryInformation = 0x1e,
    SystemPerformanceTraceInformation = 0x1f,
    SystemObsolete0 = 0x20,
    SystemExceptionInformation = 0x21,
    SystemCrashDumpStateInformation = 0x22,
    SystemKernelDebuggerInformation = 0x23,
    SystemContextSwitchInformation = 0x24,
    SystemRegistryQuotaInformation = 0x25,
    SystemExtendServiceTableInformation = 0x26,
    SystemPrioritySeperation = 0x27,
    SystemVerifierAddDriverInformation = 0x28,
    SystemVerifierRemoveDriverInformation = 0x29,
    SystemProcessorIdleInformation = 0x2a,
    SystemLegacyDriverInformation = 0x2b,
    SystemCurrentTimeZoneInformation = 0x2c,
    SystemLookasideInformation = 0x2d,
    SystemTimeSlipNotification = 0x2e,
    SystemSessionCreate = 0x2f,
    SystemSessionDetach = 0x30,
    SystemSessionInformation = 0x31,
    SystemRangeStartInformation = 0x32,
    SystemVerifierInformation = 0x33,
    SystemVerifierThunkExtend = 0x34,
    SystemSessionProcessInformation = 0x35,
    SystemLoadGdiDriverInSystemSpace = 0x36,
    SystemNumaProcessorMap = 0x37,
    SystemPrefetcherInformation = 0x38,
    SystemExtendedProcessInformation = 0x39,
    SystemRecommendedSharedDataAlignment = 0x3a,
    SystemComPlusPackage = 0x3b,
    SystemNumaAvailableMemory = 0x3c,
    SystemProcessorPowerInformation = 0x3d,
    SystemEmulationBasicInformation = 0x3e,
    SystemEmulationProcessorInformation = 0x3f,
    SystemExtendedHandleInformation = 0x40,
    SystemLostDelayedWriteInformation = 0x41,
    SystemBigPoolInformation = 0x42,
    SystemSessionPoolTagInformation = 0x43,
    SystemSessionMappedViewInformation = 0x44,
    SystemHotpatchInformation = 0x45,
    SystemObjectSecurityMode = 0x46,
    SystemWatchdogTimerHandler = 0x47,
    SystemWatchdogTimerInformation = 0x48,
    SystemLogicalProcessorInformation = 0x49,
    SystemWow64SharedInformationObsolete = 0x4a,
    SystemRegisterFirmwareTableInformationHandler = 0x4b,
    SystemFirmwareTableInformation = 0x4c,
    SystemModuleInformationEx = 0x4d,
    SystemVerifierTriageInformation = 0x4e,
    SystemSuperfetchInformation = 0x4f,
    SystemMemoryListInformation = 0x50,
    SystemFileCacheInformationEx = 0x51,
    SystemThreadPriorityClientIdInformation = 0x52,
    SystemProcessorIdleCycleTimeInformation = 0x53,
    SystemVerifierCancellationInformation = 0x54,
    SystemProcessorPowerInformationEx = 0x55,
    SystemRefTraceInformation = 0x56,
    SystemSpecialPoolInformation = 0x57,
    SystemProcessIdInformation = 0x58,
    SystemErrorPortInformation = 0x59,
    SystemBootEnvironmentInformation = 0x5a,
    SystemHypervisorInformation = 0x5b,
    SystemVerifierInformationEx = 0x5c,
    SystemTimeZoneInformation = 0x5d,
    SystemImageFileExecutionOptionsInformation = 0x5e,
    SystemCoverageInformation = 0x5f,
    SystemPrefetchPatchInformation = 0x60,
    SystemVerifierFaultsInformation = 0x61,
    SystemSystemPartitionInformation = 0x62,
    SystemSystemDiskInformation = 0x63,
    SystemProcessorPerformanceDistribution = 0x64,
    SystemNumaProximityNodeInformation = 0x65,
    SystemDynamicTimeZoneInformation = 0x66,
    SystemCodeIntegrityInformation = 0x67,
    SystemProcessorMicrocodeUpdateInformation = 0x68,
    SystemProcessorBrandString = 0x69,
    SystemVirtualAddressInformation = 0x6a,
    SystemLogicalProcessorAndGroupInformation = 0x6b,
    SystemProcessorCycleTimeInformation = 0x6c,
    SystemStoreInformation = 0x6d,
    SystemRegistryAppendString = 0x6e,
    SystemAitSamplingValue = 0x6f,
    SystemVhdBootInformation = 0x70,
    SystemCpuQuotaInformation = 0x71,
    SystemNativeBasicInformation = 0x72,
    SystemErrorPortTimeouts = 0x73,
    SystemLowPriorityIoInformation = 0x74,
    SystemBootEntropyInformation = 0x75,
    SystemVerifierCountersInformation = 0x76,
    SystemPagedPoolInformationEx = 0x77,
    SystemSystemPtesInformationEx = 0x78,
    SystemNodeDistanceInformation = 0x79,
    SystemAcpiAuditInformation = 0x7a,
    SystemBasicPerformanceInformation = 0x7b,
    SystemQueryPerformanceCounterInformation = 0x7c,
    SystemSessionBigPoolInformation = 0x7d,
    SystemBootGraphicsInformation = 0x7e,
    SystemScrubPhysicalMemoryInformation = 0x7f,
    SystemBadPageInformation = 0x80,
    SystemProcessorProfileControlArea = 0x81,
    SystemCombinePhysicalMemoryInformation = 0x82,
    SystemEntropyInterruptTimingInformation = 0x83,
    SystemConsoleInformation = 0x84,
    SystemPlatformBinaryInformation = 0x85,
    SystemThrottleNotificationInformation = 0x86,
    SystemHypervisorProcessorCountInformation = 0x87,
    SystemDeviceDataInformation = 0x88,
    SystemDeviceDataEnumerationInformation = 0x89,
    SystemMemoryTopologyInformation = 0x8a,
    SystemMemoryChannelInformation = 0x8b,
    SystemBootLogoInformation = 0x8c,
    SystemProcessorPerformanceInformationEx = 0x8d,
    SystemSpare0 = 0x8e,
    SystemSecureBootPolicyInformation = 0x8f,
    SystemPageFileInformationEx = 0x90,
    SystemSecureBootInformation = 0x91,
    SystemEntropyInterruptTimingRawInformation = 0x92,
    SystemPortableWorkspaceEfiLauncherInformation = 0x93,
    SystemFullProcessInformation = 0x94,
    SystemKernelDebuggerInformationEx = 0x95,
    SystemBootMetadataInformation = 0x96,
    SystemSoftRebootInformation = 0x97,
    SystemElamCertificateInformation = 0x98,
    SystemOfflineDumpConfigInformation = 0x99,
    SystemProcessorFeaturesInformation = 0x9a,
    SystemRegistryReconciliationInformation = 0x9b,
    MaxSystemInfoClass = 0x9c,
} SYSTEM_INFORMATION_CLASS;

union ularge_integer_t
{
    struct
    {
        UINT32  LowPart;                                                      //0x0
        UINT32  HighPart;                                                     //0x4
    };
    struct
    {
        UINT32  LowPart;                                                      //0x0
        UINT32  HighPart;                                                     //0x4
    } u;                                                                    //0x0
    UINT64 QuadPart;                                                     //0x0
};

union _PS_CLIENT_SECURITY_CONTEXT
{
    UINT64 ImpersonationData;                                            //0x0
    void* ImpersonationToken;                                               //0x0
    UINT64 ImpersonationLevel : 2;                                         //0x0
    UINT64 EffectiveOnly : 1;                                              //0x0
};

struct single_list_entry_t {
    single_list_entry_t* m_next;
};

struct _PS_PROPERTY_SET
{
    struct list_entry_t ListHead;                                            //0x0
    UINT64 Lock;                                                         //0x10
};

struct _EX_PUSH_LOCK
{
    union
    {
        struct
        {
            unsigned long Locked : 1;                                                 //0x0
            unsigned long Waiting : 1;                                                //0x0
            unsigned long Waking : 1;                                                 //0x0
            unsigned long MultipleShared : 1;                                         //0x0
            unsigned long Shared : 28;                                                //0x0
        };
        unsigned long Value;                                                        //0x0
        void* Ptr;                                                          //0x0
    };
};

struct _RTL_RB_TREE
{
    struct _RTL_BALANCED_NODE* Root;                                        //0x0
    union
    {
        UINT8 Encoded : 1;                                                    //0x8
        struct _RTL_BALANCED_NODE* Min;                                     //0x8
    };
};

struct _KLOCK_ENTRY_LOCK_STATE
{
    union
    {
        struct
        {
            UINT64 CrossThreadReleasable : 1;                              //0x0
            UINT64 Busy : 1;                                               //0x0
            UINT64 Reserved : 61;                                          //0x0
            UINT64 InTree : 1;                                             //0x0
        };
        void* LockState;                                                    //0x0
    };
    union
    {
        void* SessionState;                                                 //0x8
        struct
        {
            unsigned long SessionId;                                                //0x8
            unsigned long SessionPad;                                               //0xc
        };
    };
};

struct dispatcher_header_t
{
    union
    {
        volatile INT32 Lock;                                                 //0x0
        INT32 LockNV;                                                        //0x0
        struct
        {
            UINT8 Type;                                                     //0x0
            UINT8 Signalling;                                               //0x1
            UINT8 Size;                                                     //0x2
            UINT8 Reserved1;                                                //0x3
        };
        struct
        {
            UINT8 TimerType;                                                //0x0
            union
            {
                UINT8 TimerControlFlags;                                    //0x1
                struct
                {
                    UINT8 Absolute : 1;                                       //0x1
                    UINT8 Wake : 1;                                           //0x1
                    UINT8 EncodedTolerableDelay : 6;                          //0x1
                };
            };
            UINT8 Hand;                                                     //0x2
            union
            {
                UINT8 TimerMiscFlags;                                       //0x3
                struct
                {
                    UINT8 Index : 6;                                          //0x3
                    UINT8 Inserted : 1;                                       //0x3
                    volatile UINT8 Expired : 1;                               //0x3
                };
            };
        };
        struct
        {
            UINT8 Timer2Type;                                               //0x0
            union
            {
                UINT8 Timer2Flags;                                          //0x1
                struct
                {
                    UINT8 Timer2Inserted : 1;                                 //0x1
                    UINT8 Timer2Expiring : 1;                                 //0x1
                    UINT8 Timer2CancelPending : 1;                            //0x1
                    UINT8 Timer2SetPending : 1;                               //0x1
                    UINT8 Timer2Running : 1;                                  //0x1
                    UINT8 Timer2Disabled : 1;                                 //0x1
                    UINT8 Timer2ReservedFlags : 2;                            //0x1
                };
            };
            UINT8 Timer2ComponentId;                                        //0x2
            UINT8 Timer2RelativeId;                                         //0x3
        };
        struct
        {
            UINT8 QueueType;                                                //0x0
            union
            {
                UINT8 QueueControlFlags;                                    //0x1
                struct
                {
                    UINT8 Abandoned : 1;                                      //0x1
                    UINT8 DisableIncrement : 1;                               //0x1
                    UINT8 QueueReservedControlFlags : 6;                      //0x1
                };
            };
            UINT8 QueueSize;                                                //0x2
            UINT8 QueueReserved;                                            //0x3
        };
        struct
        {
            UINT8 ThreadType;                                               //0x0
            UINT8 ThreadReserved;                                           //0x1
            union
            {
                UINT8 ThreadControlFlags;                                   //0x2
                struct
                {
                    UINT8 CycleProfiling : 1;                                 //0x2
                    UINT8 CounterProfiling : 1;                               //0x2
                    UINT8 GroupScheduling : 1;                                //0x2
                    UINT8 AffinitySet : 1;                                    //0x2
                    UINT8 Tagged : 1;                                         //0x2
                    UINT8 EnergyProfiling : 1;                                //0x2
                    UINT8 SchedulerAssist : 1;                                //0x2
                    UINT8 ThreadReservedControlFlags : 1;                     //0x2
                };
            };
            union
            {
                UINT8 DebugActive;                                          //0x3
                struct
                {
                    UINT8 ActiveDR7 : 1;                                      //0x3
                    UINT8 Instrumented : 1;                                   //0x3
                    UINT8 Minimal : 1;                                        //0x3
                    UINT8 Reserved4 : 2;                                      //0x3
                    UINT8 AltSyscall : 1;                                     //0x3
                    UINT8 UmsScheduled : 1;                                   //0x3
                    UINT8 UmsPrimary : 1;                                     //0x3
                };
            };
        };
        struct
        {
            UINT8 MutantType;                                               //0x0
            UINT8 MutantSize;                                               //0x1
            UINT8 DpcActive;                                                //0x2
            UINT8 MutantReserved;                                           //0x3
        };
    };
    INT32 SignalState;                                                       //0x4
    list_entry_t WaitListHead;                                        //0x8
};

struct kevent_t
{
    struct dispatcher_header_t Header;                                       //0x0
};

union kwait_status_register_t
{
    UINT8 Flags;                                                            //0x0
    UINT8 State : 3;                                                          //0x0
    UINT8 Affinity : 1;                                                       //0x0
    UINT8 Priority : 1;                                                       //0x0
    UINT8 Apc : 1;                                                            //0x0
    UINT8 UserApc : 1;                                                        //0x0
    UINT8 Alert : 1;                                                          //0x0
};

struct kapc_state_t
{
    list_entry_t ApcListHead[2];                                      //0x0
    struct _KPROCESS* Process;                                              //0x20
    union
    {
        UINT8 InProgressFlags;                                              //0x28
        struct
        {
            UINT8 KernelApcInProgress : 1;                                    //0x28
            UINT8 SpecialApcInProgress : 1;                                   //0x28
        };
    };
    UINT8 KernelApcPending;                                                 //0x29
    union
    {
        UINT8 UserApcPendingAll;                                            //0x2a
        struct
        {
            UINT8 SpecialUserApcPending : 1;                                  //0x2a
            UINT8 UserApcPending : 1;                                         //0x2a
        };
    };
};

struct ktimer_t
{
    struct dispatcher_header_t Header;                                       //0x0
    union ularge_integer_t DueTime;                                          //0x18
    struct list_entry_t TimerListEntry;                                      //0x20
    struct _KDPC* Dpc;                                                      //0x30
    UINT16 Processor;                                                       //0x38
    UINT16 TimerType;                                                       //0x3a
    UINT32 Period;                                                           //0x3c
};

struct group_affinity_t
{
    UINT64 Mask;                                                         //0x0
    UINT16 Group;                                                           //0x8
    UINT16 Reserved[3];                                                     //0xa
};

struct kwait_block_t
{
    struct list_entry_t WaitListEntry;                                       //0x0
    UINT8 WaitType;                                                         //0x10
    volatile UINT8 BlockState;                                              //0x11
    UINT16 WaitKey;                                                         //0x12
    INT32 SpareLong;                                                         //0x14
    union
    {
        struct kthread* Thread;                                            //0x18
        struct _KQUEUE* NotificationQueue;                                  //0x18
    };
    void* Object;                                                           //0x20
    void* SparePtr;                                                         //0x28
};

struct kapc_t
{
    UINT8 Type;                                                             //0x0
    UINT8 SpareByte0;                                                       //0x1
    UINT8 Size;                                                             //0x2
    UINT8 SpareByte1;                                                       //0x3
    UINT32 SpareLong0;                                                       //0x4
    struct kthread* Thread;                                                //0x8
    struct list_entry_t ApcListEntry;                                        //0x10
    union
    {
        struct
        {
            void(*KernelRoutine)(struct kapc_t* arg1, void(**arg2)(void* arg1, void* arg2, void* arg3), void** arg3, void** arg4, void** arg5); //0x20
            void(*RundownRoutine)(struct kapc_t* arg1);                     //0x28
            void(*NormalRoutine)(void* arg1, void* arg2, void* arg3);      //0x30
        };
        void* Reserved[3];                                                  //0x20
    };
    void* NormalContext;                                                    //0x38
    void* SystemArgument1;                                                  //0x40
    void* SystemArgument2;                                                  //0x48
    UINT8 ApcStateIndex;                                                     //0x50
    UINT8 ApcMode;                                                           //0x51
    UINT8 Inserted;                                                         //0x52
};

namespace IA32 {
    typedef struct _TEB {
        PVOID Reserved1[12];
        PPEB  ProcessEnvironmentBlock;
        PVOID Reserved2[399];
        BYTE  Reserved3[1952];
        PVOID TlsSlots[64];
        BYTE  Reserved4[8];
        PVOID Reserved5[26];
        PVOID ReservedForOle;
        PVOID Reserved6[4];
        PVOID TlsExpansionSlots;
    } TEB, * PTEB;
}





















struct _KLOCK_ENTRY
{
    union
    {
        struct _RTL_BALANCED_NODE TreeNode;                                 //0x0
        struct single_list_entry_t FreeListEntry;                            //0x0
    };
    union
    {
        unsigned long EntryFlags;                                                   //0x18
        struct
        {
            unsigned char EntryOffset;                                              //0x18
            union
            {
                unsigned char ThreadLocalFlags;                                     //0x19
                struct
                {
                    unsigned char WaitingBit : 1;                                     //0x19
                    unsigned char Spare0 : 7;                                         //0x19
                };
            };
            union
            {
                unsigned char AcquiredByte;                                         //0x1a
                unsigned char AcquiredBit : 1;                                        //0x1a
            };
            union
            {
                unsigned char CrossThreadFlags;                                     //0x1b
                struct
                {
                    unsigned char HeadNodeBit : 1;                                    //0x1b
                    unsigned char IoPriorityBit : 1;                                  //0x1b
                    unsigned char Spare1 : 6;                                         //0x1b
                };
            };
        };
        struct
        {
            unsigned long StaticState : 8;                                            //0x18
            unsigned long AllFlags : 24;                                              //0x18
        };
    };
    unsigned long SpareFlags;                                                       //0x1c
    union
    {
        struct _KLOCK_ENTRY_LOCK_STATE LockState;                           //0x20
        void* volatile LockUnsafe;                                          //0x20
        struct
        {
            volatile unsigned char CrossThreadReleasableAndBusyByte;                //0x20
            unsigned char Reserved[6];                                              //0x21
            volatile unsigned char InTreeByte;                                      //0x27
            union
            {
                void* SessionState;                                         //0x28
                struct
                {
                    unsigned long SessionId;                                        //0x28
                    unsigned long SessionPad;                                       //0x2c
                };
            };
        };
    };
    union
    {
        struct
        {
            struct _RTL_RB_TREE OwnerTree;                                  //0x30
            struct _RTL_RB_TREE WaiterTree;                                 //0x40
        };
        char CpuPriorityKey;                                                //0x30
    };
    UINT64 EntryLock;                                                    //0x50
    union
    {
        unsigned short AllBoosts;                                                   //0x58
        struct
        {
            unsigned short IoBoost : 1;                                               //0x58
            unsigned short CpuBoostsBitmap : 15;                                      //0x58
        };
    };
    unsigned short IoNormalPriorityWaiterCount;                                     //0x5a
    unsigned short SparePad;                                                        //0x5c
};


struct kthread
{
    struct dispatcher_header_t Header;                                       //0x0
    void* SListFaultAddress;                                                //0x18
    UINT64 QuantumTarget;                                                //0x20
    void* InitialStack;                                                     //0x28
    void* volatile StackLimit;                                              //0x30
    void* StackBase;                                                        //0x38
    UINT64 ThreadLock;                                                   //0x40
    volatile UINT64 CycleTime;                                           //0x48
    UINT64 CurrentRunTime;                                                   //0x50
    UINT64 ExpectedRunTime;                                                  //0x54
    void* KernelStack;                                                      //0x58
    struct _XSAVE_FORMAT* StateSaveArea;                                    //0x60
    struct _KSCHEDULING_GROUP* volatile SchedulingGroup;                    //0x68
    union kwait_status_register_t WaitRegister;                              //0x70
    volatile UINT8 Running;                                                 //0x71
    UINT8 Alerted[2];                                                       //0x72
    union
    {
        struct
        {
            UINT64 AutoBoostActive : 1;                                        //0x74
            UINT64 ReadyTransition : 1;                                        //0x74
            UINT64 WaitNext : 1;                                               //0x74
            UINT64 SystemAffinityActive : 1;                                   //0x74
            UINT64 Alertable : 1;                                              //0x74
            UINT64 UserStackWalkActive : 1;                                    //0x74
            UINT64 ApcInterruptRequest : 1;                                    //0x74
            UINT64 QuantumEndMigrate : 1;                                      //0x74
            UINT64 UmsDirectedSwitchEnable : 1;                                //0x74
            UINT64 TimerActive : 1;                                            //0x74
            UINT64 SystemThread : 1;                                           //0x74
            UINT64 ProcessDetachActive : 1;                                    //0x74
            UINT64 CalloutActive : 1;                                          //0x74
            UINT64 ScbReadyQueue : 1;                                          //0x74
            UINT64 ApcQueueable : 1;                                           //0x74
            UINT64 ReservedStackInUse : 1;                                     //0x74
            UINT64 UmsPerformingSyscall : 1;                                   //0x74
            UINT64 TimerSuspended : 1;                                         //0x74
            UINT64 SuspendedWaitMode : 1;                                      //0x74
            UINT64 SuspendSchedulerApcWait : 1;                                //0x74
            UINT64 CetUserShadowStack : 1;                                     //0x74
            UINT64 BypassProcessFreeze : 1;                                    //0x74
            UINT64 Reserved : 10;                                              //0x74
        };
        INT32 MiscFlags;                                                     //0x74
    };
    union
    {
        struct
        {
            UINT64 ThreadFlagsSpare : 2;                                       //0x78
            UINT64 AutoAlignment : 1;                                          //0x78
            UINT64 DisableBoost : 1;                                           //0x78
            UINT64 AlertedByThreadId : 1;                                      //0x78
            UINT64 QuantumDonation : 1;                                        //0x78
            UINT64 EnableStackSwap : 1;                                        //0x78
            UINT64 GuiThread : 1;                                              //0x78
            UINT64 DisableQuantum : 1;                                         //0x78
            UINT64 ChargeOnlySchedulingGroup : 1;                              //0x78
            UINT64 DeferPreemption : 1;                                        //0x78
            UINT64 QueueDeferPreemption : 1;                                   //0x78
            UINT64 ForceDeferSchedule : 1;                                     //0x78
            UINT64 clientReadyQueueAffinity : 1;                               //0x78
            UINT64 FreezeCount : 1;                                            //0x78
            UINT64 TerminationApcRequest : 1;                                  //0x78
            UINT64 AutoBoostEntriesExhausted : 1;                              //0x78
            UINT64 KernelStackResident : 1;                                    //0x78
            UINT64 TerminateRequestReason : 2;                                 //0x78
            UINT64 ProcessStackCountDecremented : 1;                           //0x78
            UINT64 RestrictedGuiThread : 1;                                    //0x78
            UINT64 VpBackingThread : 1;                                        //0x78
            UINT64 ThreadFlagsSpare2 : 1;                                      //0x78
            UINT64 EtwStackTraceApcInserted : 8;                               //0x78
        };
        volatile INT32 ThreadFlags;                                          //0x78
    };
    volatile UINT8 Tag;                                                     //0x7c
    UINT8 SystemHeteroCpuPolicy;                                            //0x7d
    UINT8 UserHeteroCpuPolicy : 7;                                            //0x7e
    UINT8 ExplicitSystemHeteroCpuPolicy : 1;                                  //0x7e
    union
    {
        struct
        {
            UINT8 RunningNonRetpolineCode : 1;                                //0x7f
            UINT8 SpecCtrlSpare : 7;                                          //0x7f
        };
        UINT8 SpecCtrl;                                                     //0x7f
    };
    UINT64 SystemCallNumber;                                                 //0x80
    UINT64 ReadyTime;                                                        //0x84
    void* FirstArgument;                                                    //0x88
    struct _KTRAP_FRAME* TrapFrame;                                         //0x90
    union
    {
        struct kapc_state_t ApcState;                                        //0x98
        struct
        {
            UINT8 ApcStateFill[43];                                         //0x98
            UINT8 Priority;                                                  //0xc3
            UINT64 UserIdealProcessor;                                       //0xc4
        };
    };
    volatile UINT64 WaitStatus;                                           //0xc8
    struct kwait_block_t* WaitBlockList;                                     //0xd0
    union
    {
        struct list_entry_t WaitListEntry;                                   //0xd8
        struct single_list_entry_t SwapListEntry;                            //0xd8
    };
    struct dispatcher_header_t* volatile Queue;                              //0xe8
    IA32::PTEB Teb;                                                              //0xf0
    UINT64 RelativeTimerBias;                                            //0xf8
    struct ktimer_t Timer;                                                   //0x100
    union
    {
        struct kwait_block_t WaitBlock[4];                                   //0x140
        struct
        {
            UINT8 WaitBlockFill4[20];                                       //0x140
            UINT64 ContextSwitches;                                          //0x154
        };
        struct
        {
            UINT8 WaitBlockFill5[68];                                       //0x140
            volatile UINT8 State;                                           //0x184
            UINT8 Spare13;                                                   //0x185
            UINT8 WaitIrql;                                                 //0x186
            UINT8 WaitMode;                                                  //0x187
        };
        struct
        {
            UINT8 WaitBlockFill6[116];                                      //0x140
            UINT64 WaitTime;                                                 //0x1b4
        };
        struct
        {
            UINT8 WaitBlockFill7[164];                                      //0x140
            union
            {
                struct
                {
                    INT16 KernelApcDisable;                                 //0x1e4
                    INT16 SpecialApcDisable;                                //0x1e6
                };
                UINT64 CombinedApcDisable;                                   //0x1e4
            };
        };
        struct
        {
            UINT8 WaitBlockFill8[40];                                       //0x140
            struct _KTHREAD_COUNTERS* ThreadCounters;                       //0x168
        };
        struct
        {
            UINT8 WaitBlockFill9[88];                                       //0x140
            struct _XSTATE_SAVE* XStateSave;                                //0x198
        };
        struct
        {
            UINT8 WaitBlockFill10[136];                                     //0x140
            void* volatile Win32Thread;                                     //0x1c8
        };
        struct
        {
            UINT8 WaitBlockFill11[176];                                     //0x140
            struct _UMS_CONTROL_BLOCK* Ucb;                                 //0x1f0
            struct _KUMS_CONTEXT_HEADER* volatile Uch;                      //0x1f8
        };
    };
    union
    {
        volatile INT32 ThreadFlags2;                                         //0x200
        struct
        {
            UINT64 BamQosLevel : 8;                                            //0x200
            UINT64 ThreadFlags2Reserved : 24;                                  //0x200
        };
    };
    UINT64 Spare21;                                                          //0x204
    struct list_entry_t QueueListEntry;                                      //0x208
    union
    {
        volatile UINT64 NextProcessor;                                       //0x218
        struct
        {
            UINT64 NextProcessorNumber : 31;                                   //0x218
            UINT64 clientReadyQueue : 1;                                       //0x218
        };
    };
    INT32 QueuePriority;                                                     //0x21c
    struct _KPROCESS* Process;                                              //0x220
    union
    {
        struct group_affinity_t UserAffinity;                                //0x228
        struct
        {
            UINT8 UserAffinityFill[10];                                     //0x228
            UINT8 PreviousMode;                                              //0x232
            UINT8 BasePriority;                                              //0x233
            union
            {
                UINT8 PriorityDecrement;                                     //0x234
                struct
                {
                    UINT8 ForegroundBoost : 4;                                //0x234
                    UINT8 UnusualBoost : 4;                                   //0x234
                };
            };
            UINT8 Preempted;                                                //0x235
            UINT8 AdjustReason;                                             //0x236
            UINT8 AdjustIncrement;                                           //0x237
        };
    };
    UINT64 AffinityVersion;                                              //0x238
    union
    {
        struct group_affinity_t Affinity;                                    //0x240
        struct
        {
            UINT8 AffinityFill[10];                                         //0x240
            UINT8 ApcStateIndex;                                            //0x24a
            UINT8 WaitBlockCount;                                           //0x24b
            UINT64 IdealProcessor;                                           //0x24c
        };
    };
    UINT64 NpxState;                                                     //0x250
    union
    {
        struct kapc_state_t SavedApcState;                                   //0x258
        struct
        {
            UINT8 SavedApcStateFill[43];                                    //0x258
            UINT8 WaitReason;                                               //0x283
            UINT8 SuspendCount;                                              //0x284
            UINT8 Saturation;                                                //0x285
            UINT16 SListFaultCount;                                         //0x286
        };
    };
    union
    {
        struct kapc_t SchedulerApc;                                          //0x288
        struct
        {
            UINT8 SchedulerApcFill0[1];                                     //0x288
            UINT8 ResourceIndex;                                            //0x289
        };
        struct
        {
            UINT8 SchedulerApcFill1[3];                                     //0x288
            UINT8 QuantumReset;                                             //0x28b
        };
        struct
        {
            UINT8 SchedulerApcFill2[4];                                     //0x288
            UINT64 KernelTime;                                               //0x28c
        };
        struct
        {
            UINT8 SchedulerApcFill3[64];                                    //0x288
            struct _KPRCB* volatile WaitPrcb;                               //0x2c8
        };
        struct
        {
            UINT8 SchedulerApcFill4[72];                                    //0x288
            void* LegoData;                                                 //0x2d0
        };
        struct
        {
            UINT8 SchedulerApcFill5[83];                                    //0x288
            UINT8 CallbackNestingLevel;                                     //0x2db
            UINT64 UserTime;                                                 //0x2dc
        };
    };
    struct kevent_t SuspendEvent;                                            //0x2e0
    struct list_entry_t ThreadListEntry;                                     //0x2f8
    struct list_entry_t MutantListHead;                                      //0x308
    UINT8 AbEntrySummary;                                                   //0x318
    UINT8 AbWaitEntryCount;                                                 //0x319
    UINT8 AbAllocationRegionCount;                                          //0x31a
    UINT8 SystemPriority;                                                    //0x31b
    UINT64 SecureThreadCookie;                                               //0x31c
    struct _KLOCK_ENTRY* LockEntries;                                       //0x320
    struct single_list_entry_t PropagateBoostsEntry;                         //0x328
    struct single_list_entry_t IoSelfBoostsEntry;                            //0x330
    UINT8 PriorityFloorCounts[16];                                          //0x338
    UINT8 PriorityFloorCountsReserved[16];                                  //0x348
    UINT64 PriorityFloorSummary;                                             //0x358
    volatile INT32 AbCompletedIoBoostCount;                                  //0x35c
    volatile INT32 AbCompletedIoQoSBoostCount;                               //0x360
    volatile INT16 KeReferenceCount;                                        //0x364
    UINT8 AbOrphanedEntrySummary;                                           //0x366
    UINT8 AbOwnedEntryCount;                                                //0x367
    UINT64 ForegroundLossTime;                                               //0x368
    union
    {
        struct list_entry_t GlobalForegroundListEntry;                       //0x370
        struct
        {
            struct single_list_entry_t ForegroundDpcStackListEntry;          //0x370
            UINT64 InGlobalForegroundList;                               //0x378
        };
    };
    UINT64 ReadOperationCount;                                            //0x380
    UINT64 WriteOperationCount;                                           //0x388
    UINT64 OtherOperationCount;                                           //0x390
    UINT64 ReadTransferCount;                                             //0x398
    UINT64 WriteTransferCount;                                            //0x3a0
    UINT64 OtherTransferCount;                                            //0x3a8
    struct _KSCB* QueuedScb;                                                //0x3b0
    volatile UINT64 ThreadTimerDelay;                                        //0x3b8
    union
    {
        volatile INT32 ThreadFlags3;                                         //0x3bc
        struct
        {
            UINT64 ThreadFlags3Reserved : 8;                                   //0x3bc
            UINT64 PpmPolicy : 2;                                              //0x3bc
            UINT64 ThreadFlags3Reserved2 : 22;                                 //0x3bc
        };
    };
    UINT64 TracingPrivate[1];                                            //0x3c0
    void* SchedulerAssist;                                                  //0x3c8
    void* volatile AbWaitObject;                                            //0x3d0
    UINT64 ReservedPreviousReadyTimeValue;                                   //0x3d8
    UINT64 KernelWaitTime;                                               //0x3e0
    UINT64 UserWaitTime;                                                 //0x3e8
    union
    {
        struct list_entry_t GlobalUpdateVpThreadPriorityListEntry;           //0x3f0
        struct
        {
            struct single_list_entry_t UpdateVpThreadPriorityDpcStackListEntry; //0x3f0
            UINT64 InGlobalUpdateVpThreadPriorityList;                   //0x3f8
        };
    };
    INT32 SchedulerAssistPriorityFloor;                                      //0x400
    UINT64 Spare28;                                                          //0x404
    UINT64 EndPadding[5];                                                //0x408
};

struct ethread
{
    struct kthread Tcb;                                                    //0x0
    union ularge_integer_t CreateTime;                                        //0x430
    union
    {
        union ularge_integer_t ExitTime;                                      //0x438
        struct list_entry_t KeyedWaitChain;                                  //0x438
    };
    union
    {
        struct list_entry_t PostBlockList;                                   //0x448
        struct
        {
            void* ForwardLinkShadow;                                        //0x448
            void* StartAddress;                                             //0x450
        };
    };
    union
    {
        struct _TERMINATION_PORT* TerminationPort;                          //0x458
        struct _ETHREAD* ReaperLink;                                        //0x458
        void* KeyedWaitValue;                                               //0x458
    };
    UINT64 ActiveTimerListLock;                                          //0x460
    struct list_entry_t ActiveTimerListHead;                                 //0x468
    struct _CLIENT_ID Cid;                                                  //0x478
    union
    {
        struct _KSEMAPHORE KeyedWaitSemaphore;                              //0x488
        struct _KSEMAPHORE AlpcWaitSemaphore;                               //0x488
    };
    union _PS_CLIENT_SECURITY_CONTEXT ClientSecurity;                       //0x4a8
    struct list_entry_t IrpList;                                             //0x4b0
    UINT64 TopLevelIrp;                                                  //0x4c0
    struct _DEVICE_OBJECT* DeviceToVerify;                                  //0x4c8
    void* Win32StartAddress;                                                //0x4d0
    void* ChargeOnlySession;                                                //0x4d8
    void* LegacyPowerObject;                                                //0x4e0
    struct list_entry_t ThreadListEntry;                                     //0x4e8
    struct _EX_RUNDOWN_REF RundownProtect;                                  //0x4f8
    struct _EX_PUSH_LOCK ThreadLock;                                        //0x500
    UINT32 ReadClusterSize;                                                  //0x508
    volatile long MmLockOrdering;                                           //0x50c
    union
    {
        UINT32 CrossThreadFlags;                                             //0x510
        struct
        {
            UINT32 Terminated : 1;                                             //0x510
            UINT32 ThreadInserted : 1;                                         //0x510
            UINT32 HideFromDebugger : 1;                                       //0x510
            UINT32 ActiveImpersonationInfo : 1;                                //0x510
            UINT32 HardErrorsAreDisabled : 1;                                  //0x510
            UINT32 BreakOnTermination : 1;                                     //0x510
            UINT32 SkipCreationMsg : 1;                                        //0x510
            UINT32 SkipTerminationMsg : 1;                                     //0x510
            UINT32 CopyTokenOnOpen : 1;                                        //0x510
            UINT32 ThreadIoPriority : 3;                                       //0x510
            UINT32 ThreadPagePriority : 3;                                     //0x510
            UINT32 RundownFail : 1;                                            //0x510
            UINT32 UmsForceQueueTermination : 1;                               //0x510
            UINT32 IndirectCpuSets : 1;                                        //0x510
            UINT32 DisableDynamicCodeOptOut : 1;                               //0x510
            UINT32 ExplicitCaseSensitivity : 1;                                //0x510
            UINT32 PicoNotifyExit : 1;                                         //0x510
            UINT32 DbgWerUserReportActive : 1;                                 //0x510
            UINT32 ForcedSelfTrimActive : 1;                                   //0x510
            UINT32 SamplingCoverage : 1;                                       //0x510
            UINT32 ReservedCrossThreadFlags : 8;                               //0x510
        };
    };
    union
    {
        UINT32 SameThreadPassiveFlags;                                       //0x514
        struct
        {
            UINT32 ActiveExWorker : 1;                                         //0x514
            UINT32 MemoryMaker : 1;                                            //0x514
            UINT32 StoreLockThread : 2;                                        //0x514
            UINT32 ClonedThread : 1;                                           //0x514
            UINT32 KeyedEventInUse : 1;                                        //0x514
            UINT32 SelfTerminate : 1;                                          //0x514
            UINT32 RespectIoPriority : 1;                                      //0x514
            UINT32 ActivePageLists : 1;                                        //0x514
            UINT32 SecureContext : 1;                                          //0x514
            UINT32 ZeroPageThread : 1;                                         //0x514
            UINT32 WorkloadClass : 1;                                          //0x514
            UINT32 ReservedSameThreadPassiveFlags : 20;                        //0x514
        };
    };
    union
    {
        UINT32 SameThreadApcFlags;                                           //0x518
        struct
        {
            UINT8 OwnsProcessAddressSpaceExclusive : 1;                       //0x518
            UINT8 OwnsProcessAddressSpaceShared : 1;                          //0x518
            UINT8 HardFaultBehavior : 1;                                      //0x518
            volatile UINT8 StartAddressInvalid : 1;                           //0x518
            UINT8 EtwCalloutActive : 1;                                       //0x518
            UINT8 SuppressSymbolLoad : 1;                                     //0x518
            UINT8 Prefetching : 1;                                            //0x518
            UINT8 OwnsVadExclusive : 1;                                       //0x518
            UINT8 SystemPagePriorityActive : 1;                               //0x519
            UINT8 SystemPagePriority : 3;                                     //0x519
            UINT8 AllowUserWritesToExecutableMemory : 1;                      //0x519
            UINT8 AllowKernelWritesToExecutableMemory : 1;                    //0x519
            UINT8 OwnsVadShared : 1;                                          //0x519
        };
    };
    UINT8 CacheManagerActive;                                               //0x51c
    UINT8 DisablePageFaultClustering;                                       //0x51d
    UINT8 ActiveFaultCount;                                                 //0x51e
    UINT8 LockOrderState;                                                   //0x51f
    UINT32 PerformanceCountLowReserved;                                      //0x520
    long PerformanceCountHighReserved;                                      //0x524
    UINT64 AlpcMessageId;                                                //0x528
    union
    {
        void* AlpcMessage;                                                  //0x530
        UINT32 AlpcReceiveAttributeSet;                                      //0x530
    };
    struct list_entry_t AlpcWaitListEntry;                                   //0x538
    long ExitStatus;                                                        //0x548
    UINT32 CacheManagerCount;                                                //0x54c
    UINT32 IoBoostCount;                                                     //0x550
    UINT32 IoQoSBoostCount;                                                  //0x554
    UINT32 IoQoSThrottleCount;                                               //0x558
    UINT32 KernelStackReference;                                             //0x55c
    struct list_entry_t BoostList;                                           //0x560
    struct list_entry_t DeboostList;                                         //0x570
    UINT64 BoostListLock;                                                //0x580
    UINT64 IrpListLock;                                                  //0x588
    void* ReservedForSynchTracking;                                         //0x590
    struct single_list_entry_t CmCallbackListHead;                           //0x598
    struct _GUID* ActivityId;                                               //0x5a0
    struct single_list_entry_t SeLearningModeListHead;                       //0x5a8
    void* VerifierContext;                                                  //0x5b0
    void* AdjustedClientToken;                                              //0x5b8
    void* WorkOnBehalfThread;                                               //0x5c0
    struct _PS_PROPERTY_SET PropertySet;                                    //0x5c8
    void* PicoContext;                                                      //0x5e0
    UINT64 UserFsBase;                                                   //0x5e8
    UINT64 UserGsBase;                                                   //0x5f0
    struct _THREAD_ENERGY_VALUES* EnergyValues;                             //0x5f8
    union
    {
        UINT64 SelectedCpuSets;                                          //0x600
        UINT64* SelectedCpuSetsIndirect;                                 //0x600
    };
    struct _EJOB* Silo;                                                     //0x608
    struct unicode_string_t* ThreadName;                                     //0x610
    struct _CONTEXT* SetContextState;                                       //0x618
    UINT32 LastExpectedRunTime;                                              //0x620
    UINT32 HeapData;                                                         //0x624
    struct list_entry_t OwnerEntryListHead;                                  //0x628
    UINT64 DisownedOwnerEntryListLock;                                   //0x638
    struct list_entry_t DisownedOwnerEntryListHead;                          //0x640
    struct _KLOCK_ENTRY LockEntries[6];                                     //0x650
    void* CmDbgInfo;                                                        //0x890
};

typedef struct _LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;   // Pointers to the next and previous loaded module
    VOID* DllBase;                 // Base address of the module
    VOID* EntryPoint;              // Entry point of the module
    ULONG SizeOfImage;             // Size of the image
    UNICODE_STRING FullDllName;    // Full path to the module
    UNICODE_STRING BaseDllName;    // Module name (e.g., "win32kbase.sys")
    ULONG Flags;
    USHORT LoadCount;
    USHORT TlsIndex;
    LIST_ENTRY HashLinks;
    ULONG TimeDateStamp;
} LDR_DATA_TABLE_ENTRY, * PLDR_DATA_TABLE_ENTRY;

typedef struct _RTL_PROCESS_MODULE_INFORMATION
{
    HANDLE 	Section;
    PVOID 	MappedBase;
    PVOID 	ImageBase;
    ULONG 	ImageSize;
    ULONG 	Flags;
    USHORT 	LoadOrderIndex;
    USHORT 	InitOrderIndex;
    USHORT 	LoadCount;
    USHORT 	OffsetToFileName;
    UCHAR 	FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION, * PRTL_PROCESS_MODULE_INFORMATION;

typedef struct _RTL_PROCESS_MODULES
{
    ULONG NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[1];
} RTL_PROCESS_MODULES, * PRTL_PROCESS_MODULES;