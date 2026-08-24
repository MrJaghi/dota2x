#include "driver.h"
#include "communication.hpp"
ULONG_PTR ntos_image_base;
ULONG_PTR kernel_base;
PVOID sig;

_GLOBAL_UEX GLOBAL_UEX;
_GLOBAL_UEX GLOBAL_UEX2;

// Forward declaration so the system-thunk can call it.
extern "C" NTSTATUS kernel_main();

// ---------------------------------------------------------------------------
// System-thread thunk: PsCreateSystemThread expects a VOID(*)(PVOID) entry.
// We delegate straight into kernel_main (the existing message loop) and
// terminate the thread when it returns (e.g. after op_unmap/op_exit).
//
// Spawning a system thread is CRITICAL for kdmapper-style mapping: if we
// block inside DriverEntry, the user-mode CallKernelFunction that invoked
// DriverEntry never returns, which means the post-call NtAddAtom restore
// never runs. On the next driver reload CKF sees the 12-byte trampoline
// still in place and fails with "NtAddAtom is already hooked". Returning
// STATUS_SUCCESS immediately from DriverEntry lets CKF restore the hook
// bytes while our kernel thread keeps running independently.
// ---------------------------------------------------------------------------
VOID KernelThreadRoutine(PVOID /*StartContext*/) {
    NTSTATUS st = kernel_main();
    (void)st;
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS kernel_main() {
    bool UMalive = true;
    const UINT32 pageIndex = KeGetCurrentProcessorIndex();
    printfx("[+] page index: %u\n", pageIndex);

    PEPROCESS target_process = nullptr;
    bool firsttimepid = false;
    int target_pid = 0;

    while (UMalive) {
        if (InterlockedCompareExchange(&req->Ready, 0, 1) == 1) {
            InterlockedExchange(&req->Ready, 0);

            switch (req->Op) {
            case op_base:
                game::_baseAddress();
                break;
            case op_peb:
                game::_peb();
                break;
            case op_r:
                game::read();
                break;
            case op_w:
                game::write();
                break;
            case op_exit:
                UMalive = false;
                break;
            case op_unmap:
                usermode::unmap((PVOID)req->TargetAddress, (SIZE_T)req->Size);
                UMalive = false;
                break;
            case op_cr3:
                game::_cr3();
                break;
            case op_a:
                req->buffer = (PVOID)212;
                target_pid = req->g_pid;
                printfx("[+] target pid: %d\n", target_pid);
                InterlockedExchange(&req->uready, 1);
                break;
            }
            if (!UMalive) {
                break;
            }
        }
        YieldProcessor(); // Prevent CPU hogging
    }
    usermode::exit();

    return STATUS_SUCCESS;
}

NTSTATUS EP() {
    // Zero the global state blocks before anyone touches them (fresh start
    // on every map; avoids dereferencing stale PEPROCESS pointers).
    RtlZeroMemory(&GLOBAL_UEX, sizeof(GLOBAL_UEX));
    RtlZeroMemory(&GLOBAL_UEX2, sizeof(GLOBAL_UEX2));

    //getting ntos
    ntos_image_base = NTOS::get_ntos_base_address();
    if (ntos_image_base == NULL) {
        silence(E("[vmm] failed to capture ntos information\n"));
        return STATUS_FAIL_CHECK;
    }
    kernel_base = ntos_image_base;

    NTSTATUS response_kboot = bootstrap::kernelbootstrap();
    if (response_kboot != STATUS_SUCCESS) {
        silence(E("[vmm] KBOOT Response Failed.\n"));
        return STATUS_FAIL_CHECK;
    }
    printfx("Start Connection\n");

    if (!NT_SUCCESS(usermode::InitializeCommunication())) {
        return STATUS_ABANDONED;
    }

    // Spawn the message loop in its own system thread so DriverEntry can
    // return immediately (see KernelThreadRoutine comment above).
    HANDLE hThread = NULL;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    NTSTATUS st = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, &oa, NULL, NULL,
        (PKSTART_ROUTINE)KernelThreadRoutine, NULL);
    if (!NT_SUCCESS(st)) {
        silence(E("[vmm] PsCreateSystemThread failed: 0x%08X\n", st));
        usermode::exit();
        return st;
    }
    if (hThread) ZwClose(hThread);
    return STATUS_SUCCESS;
}

void UnloadDriver(PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);
    silence(E("Driver unloaded successfully\n"));
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    if (DriverObject && MmIsAddressValid(DriverObject)) {
        DriverObject->DriverUnload = UnloadDriver;
    }

    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    printfx("Driver loaded (Mapped or Normal)\n");

    return EP();
}
