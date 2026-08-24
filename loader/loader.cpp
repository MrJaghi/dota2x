#include <windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <string>
#include <fstream>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <chrono>
#include "mapper_resource.hpp"
#include "esp_resource.hpp"

// ============================================================
// Shared-memory protocol with the kernel driver.
// Mirrors Requests / req_op from SharedMemorySpectre-main/Ustruct.h
// ============================================================
enum class req_op : int {
    op_r     = 1,
    op_w     = 2,
    op_a     = 3,
    op_idle  = 0,
    op_cr3   = 4,
    op_base  = 51,
    op_peb   = 31,
    op_exit  = 420,
    op_unmap = 421
};

struct Requests {
    volatile LONG Ready;
    volatile LONG uready;
    volatile LONG InUse;
    int          g_pid;
    req_op       Op;
    uint64_t     baseaddress;
    void*        TargetAddress;
    ULONG        Size;
    void*        buffer;
    LONG         Status;
};

// ---- Globals so the console-control handler can reach them ----
static Requests*  g_req         = nullptr;
static volatile LONG g_cleanupDone = 0;
static HANDLE     g_hEspProcess = nullptr;  // handle to running ExternalESP child
static char       g_tempEspPath[MAX_PATH] = {};
static char       g_tempMapperPath[MAX_PATH] = {};

// ============================================================
// Console color helpers
// ============================================================
static void SetColor(WORD color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}
static void PrintOk  (const std::string& m) { SetColor(10); std::cout << "[+] " << m << std::endl; SetColor(7); }
static void PrintInfo(const std::string& m) { SetColor(11); std::cout << "[*] " << m << std::endl; SetColor(7); }
static void PrintWarn(const std::string& m) { SetColor(14); std::cout << "[!] " << m << std::endl; SetColor(7); }
static void PrintErr (const std::string& m) { SetColor(12); std::cout << "[-] " << m << std::endl; SetColor(7); }

static void PrintBanner() {
    SetColor(11);
    std::cout <<
        "  ____                                     ____                  \n"
        " |  _ \\ _ __ __ _  __ _  ___  _ __       | __ ) _   _ _ __ _ __  \n"
        " | | | | '__/ _` |/ _` |/ _ \\| '_ \\      |  _ \\| | | | '__| '_ \\ \n"
        " | |_| | | | (_| | (_| | (_) | | | |     | |_) | |_| | |  | | | |\n"
        " |____/|_|  \\__,_|\\__, |\\___/|_| |_|_____|____/ \\__,_|_|  |_| |_|\n"
        "                  |___/           |_____|                       \n";
    SetColor(14);
    std::cout << "=================================================================================\n";
    std::cout << "                       DragonBurn Kernel Cheat Loader                            \n";
    std::cout << "=================================================================================\n\n";
    SetColor(7);
}

static bool IsRunAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

// ---------------------------------------------------------------------------
// Simple anti-cheat / AV presences detection. We only print a warning — we
// do not block execution; the user is running a kernel mapper against an
// always-online game and should know what's running on their system.
// ---------------------------------------------------------------------------
static void WarnIfAntiCheatsPresent() {
    static const wchar_t* kBadProcesses[] = {
        L"vgc.exe",              // Riot Vanguard
        L"vgtray.exe",
        L"easyanticheat.exe",    // Easy Anti-Cheat
        L"easyanticheat.sys",
        L"BEService.exe",        // BattlEye
        L"BEDaisy.sys",
        L"EasyAntiCheat_Setup.exe",
    };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    std::wstring found;
    if (Process32FirstW(snap, &pe)) {
        do {
            for (auto bad : kBadProcesses) {
                if (_wcsicmp(pe.szExeFile, bad) == 0) {
                    if (!found.empty()) found += L", ";
                    found += bad;
                    break;
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (!found.empty()) {
        PrintWarn("Anti-cheat/EDR process(es) detected:");
        SetColor(12); std::wcerr << L"    " << found << L"\n"; SetColor(7);
        PrintWarn("Running a kernel mapper with anti-cheat software running can cause BSODs,");
        PrintWarn("bans, or system instability. If you play other online games (Valorant/EAC/BE),");
        PrintWarn("ensure their anti-cheat services are fully stopped before proceeding.");
        std::cout << "    Press Y to continue, any other key to abort: ";
        std::string line;
        if (!std::getline(std::cin, line) || (line.empty() || (line[0] != 'Y' && line[0] != 'y'))) {
            PrintInfo("Aborted by user.");
            ExitProcess(0);
        }
    }
}

static bool WriteRegDword(HKEY root, const wchar_t* subkey, const wchar_t* name, DWORD value) {
    HKEY k;
    DWORD disp = 0;
    LONG st = RegCreateKeyExW(root, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, &disp);
    if (st != ERROR_SUCCESS) return false;
    st = RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
    RegCloseKey(k);
    return st == ERROR_SUCCESS;
}
static bool WriteRegQword(HKEY root, const wchar_t* subkey, const wchar_t* name, uint64_t value) {
    HKEY k;
    DWORD disp = 0;
    LONG st = RegCreateKeyExW(root, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, &disp);
    if (st != ERROR_SUCCESS) return false;
    st = RegSetValueExW(k, name, 0, REG_QWORD, (const BYTE*)&value, sizeof(value));
    RegCloseKey(k);
    return st == ERROR_SUCCESS;
}

static bool ExtractFile(const std::string& path, const uint8_t* data, size_t size) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    file.write((const char*)data, size);
    file.close();
    return true;
}

// ------------------------------------------------------------
// Ask the kernel driver to terminate / unmap. Safe to call
// from any thread (including the console control handler) and
// safe to call repeatedly; returns immediately if no driver
// connection is active.
// ------------------------------------------------------------
static void CleanupDriverAndExit(int exitCode) {
    // Make sure we only run cleanup once even if Ctrl+C / close / menu
    // option 3 fire concurrently.
    if (InterlockedExchange(&g_cleanupDone, 1) != 0) {
        // Another thread is already cleaning up; just sleep long enough
        // for it to finish then exit. Don't block forever in case of a
        // wedged driver handhake (the Sleep below is bounded).
        Sleep(6000);
        ExitProcess((UINT)exitCode);
        return;
    }

    // 1) Kill the ESP child so it doesn't keep reading from dead driver.
    if (g_hEspProcess) {
        TerminateProcess(g_hEspProcess, 1);
        WaitForSingleObject(g_hEspProcess, 2000);
        CloseHandle(g_hEspProcess);
        g_hEspProcess = nullptr;
    }

    // 2) Ask the kernel to unmap.
    if (g_req) {
        PrintInfo("Sending op_unmap to driver...");
        g_req->Op = req_op::op_unmap;
        g_req->TargetAddress = nullptr;
        g_req->Size = 0;
        InterlockedExchange(&g_req->Ready, 1);
        using namespace std::chrono;
        auto deadline = steady_clock::now() + seconds(4);
        bool acked = false;
        while (steady_clock::now() < deadline) {
            if (InterlockedCompareExchange(&g_req->uready, 0, 1) == 1) {
                acked = true;
                break;
            }
            Sleep(50);
        }
        if (!acked) {
            // Hard fallback: at least ask the kernel thread to exit.
            PrintWarn("Driver did not ack unmap in time -- sending op_exit.");
            g_req->Op = req_op::op_exit;
            InterlockedExchange(&g_req->Ready, 1);
            Sleep(800);
        } else {
            PrintOk("Driver acknowledged unload.");
            // Give kernel thread time to tear down before we free shared mem.
            Sleep(800);
        }
        VirtualFree(g_req, 0, MEM_RELEASE);
        g_req = nullptr;
    }

    // 3) Delete temp files.
    if (g_tempEspPath[0])  { DeleteFileA(g_tempEspPath);  g_tempEspPath[0]  = 0; }
    if (g_tempMapperPath[0]) { DeleteFileA(g_tempMapperPath); g_tempMapperPath[0] = 0; }

    // 4) Clear registry breadcrumbs so a stale driver can't re-handshake
    //    after we're gone. (Best-effort; ignore failures.)
    RegDeleteValueW(HKEY_LOCAL_MACHINE, L"oPid");
    RegDeleteValueW(HKEY_LOCAL_MACHINE, L"oAddr");

    PrintOk("Cleanup done. Goodbye.");
    ExitProcess((UINT)exitCode);
}

// ------------------------------------------------------------
// Console control handler -- fires on Ctrl+C, Ctrl+Break,
// console window close, logoff, shutdown. MUST NOT call CRT
// stream I/O that can deadlock (but printf/cout on the local
// console is generally safe). We do the minimum: signal the
// same cleanup path the menu uses.
// ------------------------------------------------------------
static BOOL WINAPI ConsoleHandlerRoutine(DWORD type) {
    (void)type;
    // Don't return from this function -- after cleanup we ExitProcess so
    // Windows doesn't kill us mid-cleanup.
    SetColor(14);
    std::cout << "\n[!] Caught console close signal -- cleaning up...\n";
    SetColor(7);
    std::cout.flush();
    CleanupDriverAndExit(1);
    return TRUE; // unreachable
}

// ------------------------------------------------------------
// Run a child process, inheriting our console so stdout prints
// inline. Returns true if CreateProcess succeeded. If timeoutMs
// elapses before the child exits, the child is terminated so
// a hung mapper can't wedge the loader.
// ------------------------------------------------------------
static bool RunProcessInheritConsole(const std::string& cmd, DWORD* exitCodeOut,
                                     DWORD timeoutMs, HANDLE* outProcHandle = nullptr) {
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    BOOL ok = CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);

    DWORD waitRc = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (waitRc == WAIT_TIMEOUT) {
        PrintWarn("Child process timed out -- terminating.");
        TerminateProcess(pi.hProcess, 259);
        WaitForSingleObject(pi.hProcess, 2000);
    }

    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    if (exitCodeOut) *exitCodeOut = ec;

    if (outProcHandle) {
        *outProcHandle = pi.hProcess; // caller owns
    } else {
        CloseHandle(pi.hProcess);
    }
    return true;
}

// ------------------------------------------------------------
// Extract + run the embedded mapper; wait for it to finish
// (with a hard timeout so it can't wedge us).
// ------------------------------------------------------------
static int RunEmbeddedMapper() {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string mapperPath = std::string(tempPath) + "\\dragonburn_mapper.exe";
    strncpy_s(g_tempMapperPath, mapperPath.c_str(), sizeof(g_tempMapperPath) - 1);
    if (!ExtractFile(mapperPath, mapper_resource::mapper_bytes, mapper_resource::mapper_bytes_size)) {
        PrintErr("Failed to extract mapper to temp.");
        return -1;
    }
    PrintInfo("Launching embedded mapper...");
    std::cout << "--------- [mapper output start] ---------" << std::endl;
    DWORD ec = 1;
    BOOL created = RunProcessInheritConsole(mapperPath, &ec, 30000);
    std::cout << "---------  [mapper output end]  ---------" << std::endl;
    DeleteFileA(mapperPath.c_str());
    g_tempMapperPath[0] = 0;
    if (!created) {
        PrintErr("Failed to start mapper process (CreateProcessA failed).");
        return -1;
    }
    return (int)ec;
}

// ------------------------------------------------------------
// Allocate shared-memory Requests struct, publish PID/addr to
// registry, launch mapper, wait for kernel handshake.
// ------------------------------------------------------------
static bool LoadDriver() {
    if (g_req) {
        PrintWarn("Already have a Requests struct -- freeing stale one.");
        VirtualFree(g_req, 0, MEM_RELEASE);
        g_req = nullptr;
    }
    Requests* req = (Requests*)VirtualAlloc(NULL, sizeof(Requests),
                                            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!req) { PrintErr("VirtualAlloc for Requests failed."); return false; }
    memset(req, 0, sizeof(*req));
    req->g_pid = GetCurrentProcessId();
    req->Op    = req_op::op_a;

    if (!WriteRegDword(HKEY_LOCAL_MACHINE, L"", L"oPid", (DWORD)req->g_pid)) {
        PrintErr("Failed to write oPid to HKLM. (Run as admin?)");
        VirtualFree(req, 0, MEM_RELEASE);
        return false;
    }
    if (!WriteRegQword(HKEY_LOCAL_MACHINE, L"", L"oAddr", (uint64_t)req)) {
        PrintErr("Failed to write oAddr to HKLM.");
        VirtualFree(req, 0, MEM_RELEASE);
        return false;
    }

    InterlockedExchange(&req->Ready, 1);

    int ec = RunEmbeddedMapper();
    if (ec != 0) {
        PrintWarn("Mapper exited with non-zero code " + std::to_string(ec) + ".");
    }

    using namespace std::chrono;
    auto deadline = steady_clock::now() + seconds(15);
    while (steady_clock::now() < deadline) {
        if (InterlockedCompareExchange(&req->uready, 0, 1) == 1) {
            InterlockedExchange(&req->uready, 0);
            PrintOk("Kernel handshake OK (driver is online).");
            g_req = req;
            return true;
        }
        Sleep(50);
    }
    PrintErr("Kernel handshake timed out - driver did not respond.");
    VirtualFree(req, 0, MEM_RELEASE);
    g_req = nullptr;
    return false;
}

// ------------------------------------------------------------
// External ESP is launched *next to* Loader.exe (not in %TEMP%)
// so that when ESP calls GetModuleFileNameW to find its exe dir,
// it lands in the same folder as output\*.hpp. Without this, the
// ESP runs from a temp dir, sees no output folder, and falls back
// to compiled-in (stale) defaults.
// We also launch it in its own console (CREATE_NEW_CONSOLE) so
// closing the ESP window doesn't kill the loader, and vice versa.
// We stash the process handle so the cleanup path can Terminate it.
// ------------------------------------------------------------
static bool LaunchEsp() {
    // Always prefer the on-disk ExternalESP.exe sitting next to us --
    // that's the one build_all just produced, and it lives in the same
    // directory as output\*.hpp. Only fall back to extracting the
    // embedded payload if no on-disk binary is found (portable/zip use).
    char ourDir[MAX_PATH] = {};
    GetModuleFileNameA(NULL, ourDir, MAX_PATH);
    char* lastSlash = strrchr(ourDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    std::string nextToUs = std::string(ourDir) + "\\ExternalESP.exe";
    std::string espPath;

    if (GetFileAttributesA(nextToUs.c_str()) != INVALID_FILE_ATTRIBUTES) {
        espPath = nextToUs;
        g_tempEspPath[0] = 0; // not a temp file -- don't delete on exit
        PrintInfo("Using on-disk " + espPath);
    } else {
        // No on-disk binary -- extract embedded payload next to us too
        // (so it inherits our output\ directory), not to %TEMP%.
        espPath = nextToUs;
        strncpy_s(g_tempEspPath, espPath.c_str(), sizeof(g_tempEspPath) - 1);
        bool extracted = ExtractFile(espPath, esp_resource::esp_bytes, esp_resource::esp_bytes_size);
        if (!extracted || esp_resource::esp_bytes_size < 1024) {
            // Embedded payload is a placeholder stub; give up.
            if (extracted) { DeleteFileA(espPath.c_str()); g_tempEspPath[0] = 0; }
            PrintErr("Failed to extract ESP and no ExternalESP.exe next to loader.");
            return false;
        }
        PrintInfo("Extracted embedded ESP to " + espPath);
    }

    // Set the CWD of the child to our directory so relative paths resolve.
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(NULL, (LPSTR)espPath.c_str(), NULL, NULL, FALSE,
                        CREATE_NEW_CONSOLE, NULL, ourDir, &si, &pi)) {
        PrintErr("Failed to launch ESP (CreateProcessA failed, GetLastError="
                 + std::to_string(GetLastError()) + ").");
        if (g_tempEspPath[0]) { DeleteFileA(g_tempEspPath); g_tempEspPath[0] = 0; }
        return false;
    }
    CloseHandle(pi.hThread);
    if (g_hEspProcess) { CloseHandle(g_hEspProcess); g_hEspProcess = nullptr; }
    g_hEspProcess = pi.hProcess;
    PrintOk("ExternalESP launched.");
    return true;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleA("DragonBurn Loader");
    PrintBanner();

    // Register console control handler BEFORE anything else so Ctrl+C /
    // window close can't leave the driver mapped.
    SetConsoleCtrlHandler(ConsoleHandlerRoutine, TRUE);

    if (!IsRunAsAdmin()) {
        PrintErr("Please run this loader as Administrator (right-click -> Run as administrator).");
        std::cout << "Press Enter to exit...";
        std::cin.ignore();
        return 1;
    }
    PrintOk("Administrator privileges verified.");

    WarnIfAntiCheatsPresent();

    bool driverOnline = false;

    std::cout << "\n";
    PrintInfo("Loading kernel driver...");
    driverOnline = LoadDriver();
    if (!driverOnline) {
        PrintErr("Driver is NOT online. External ESP will NOT be started automatically.");
        PrintWarn("You can retry from the menu below.");
    }

    bool espRunning = false;
    if (driverOnline) {
        PrintInfo("Launching External ESP...");
        espRunning = LaunchEsp();
    }

    auto printMenu = [&]() {
        std::cout << "\n";
        SetColor(11);
        std::cout << "=====================================================\n";
        std::cout << "  Driver: " << (driverOnline ? "ONLINE" : "OFFLINE")
                  << "    |    ESP: " << (espRunning ? "running" : "stopped") << "\n";
        std::cout << "-----------------------------------------------------\n";
        std::cout << "  [1] Reload / map driver\n";
        std::cout << "  [2] Launch External ESP\n";
        std::cout << "  [3] Unload driver & exit\n";
        std::cout << "  [4] Exit without unloading (keep driver)\n";
        std::cout << "=====================================================\n";
        std::cout << "Choice: ";
        SetColor(7);
        std::cout.flush();
    };

    std::string line;
    while (true) {
        printMenu();
        if (!std::getline(std::cin, line)) {
            // cin is broken (EOF / pipe closed / Ctrl+Z on Windows) --
            // don't spin at 100% CPU; exit cleanly.
            PrintWarn("Standard input closed -- exiting.");
            if (driverOnline) {
                PrintInfo("Driver was online; performing clean unload.");
                CleanupDriverAndExit(0);
            }
            return 0;
        }
        if (line.empty()) continue;
        char choice = line[0];
        switch (choice) {
            case '1': {
                if (driverOnline) {
                    PrintWarn("Driver appears already online. Unloading ESP + driver first...");

                    // Kill the ESP child first -- it is still reading through
                    // the shared-memory Requests struct we are about to free,
                    // so if we unmap the driver while ESP is alive the ESP
                    // will either crash or keep poking freed memory.
                    if (g_hEspProcess) {
                        TerminateProcess(g_hEspProcess, 1);
                        WaitForSingleObject(g_hEspProcess, 2000);
                        CloseHandle(g_hEspProcess);
                        g_hEspProcess = nullptr;
                        espRunning = false;
                    }

                    // Send unmap via the existing req.
                    if (g_req) {
                        g_req->Op = req_op::op_unmap;
                        g_req->TargetAddress = nullptr;
                        g_req->Size = 0;
                        InterlockedExchange(&g_req->Ready, 1);
                        using namespace std::chrono;
                        auto dl = steady_clock::now() + seconds(5);
                        bool acked = false;
                        while (steady_clock::now() < dl) {
                            if (InterlockedCompareExchange(&g_req->uready, 0, 1) == 1) {
                                acked = true; break;
                            }
                            Sleep(50);
                        }
                        if (!acked) {
                            PrintWarn("Unload not acked; sending op_exit and proceeding anyway.");
                            g_req->Op = req_op::op_exit;
                            InterlockedExchange(&g_req->Ready, 1);
                            Sleep(500);
                        } else {
                            PrintOk("Driver acknowledged unload.");
                        }
                        // Give the old kernel thread time to finish
                        // usermode::exit() (frees MDL, returns from EP) so
                        // the new mapper's CKF doesn't race with it on
                        // NtAddAtom.
                        Sleep(1000);
                        VirtualFree(g_req, 0, MEM_RELEASE);
                        g_req = nullptr;
                        driverOnline = false;
                        RegDeleteValueW(HKEY_LOCAL_MACHINE, L"oPid");
                        RegDeleteValueW(HKEY_LOCAL_MACHINE, L"oAddr");
                    }
                }
                driverOnline = LoadDriver();
                if (driverOnline) {
                    PrintInfo("Driver reloaded. Press [2] to (re)launch External ESP.");
                }
                break;
            }
            case '2': {
                if (!driverOnline) {
                    PrintErr("Driver is not online - ESP will not run until the driver is mapped.");
                } else {
                    espRunning = LaunchEsp();
                }
                break;
            }
            case '3': {
                PrintInfo("Unloading driver and exiting...");
                CleanupDriverAndExit(0);
                break; // unreachable
            }
            case '4': {
                PrintWarn("Exiting but leaving driver/ESP running (no unmap performed).");
                // Don't clean up -- leak g_req intentionally so kernel can
                // still reach it; just detach.
                InterlockedExchange(&g_cleanupDone, 1); // prevent handler double-unmap
                if (g_hEspProcess) { CloseHandle(g_hEspProcess); g_hEspProcess = nullptr; }
                if (g_tempEspPath[0])  { DeleteFileA(g_tempEspPath);  g_tempEspPath[0]  = 0; }
                if (g_tempMapperPath[0]) { DeleteFileA(g_tempMapperPath); g_tempMapperPath[0] = 0; }
                return 0;
            }
            default:
                PrintWarn("Unknown option.");
                break;
        }
    }
    return 0;
}
