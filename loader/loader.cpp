#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

void SetColor(WORD color)
{
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

void PrintBanner()
{
    SetColor(11); // Bright Cyan
    std::cout << R"(
  ██████╗ ██████╗  █████╗  ██████╗  ██████╗ ███╗   ██╗██████╗ ██╗   ██╗██████╗ ███╗   ██╗
  ██╔══██╗██╔══██╗██╔══██╗██╔════╝ ██╔═══██╗████╗  ██║██╔══██╗██║   ██║██╔══██╗████╗  ██║
  ██║  ██║██████╔╝███████║██║  ███╗██║   ██║██╔██╗ ██║██████╔╝██║   ██║██████╔╝██╔██╗ ██║
  ██║  ██║██╔══██╗██╔══██║██║   ██║██║   ██║██║╚██╗██║██╔══██╗██║   ██║██╔══██╗██║╚██╗██║
  ██████╔╝██║  ██║██║  ██║╚██████╔╝╚██████╔╝██║ ╚████║██████╔╝╚██████╔╝██║  ██║██║ ╚████║
  ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝╚═════╝  ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═══╝
)" << std::endl;

    SetColor(14); // Yellow
    std::cout << "=================================================================================\n";
    std::cout << "                       DragonBurn Kernel Cheat Loader                            \n";
    std::cout << "=================================================================================\n\n";
    SetColor(7); // Reset
}

bool IsRunAsAdmin()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup))
    {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

bool RunProcess(const std::string& cmd)
{
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (!CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exitCode == 0;
}

int main()
{
    SetConsoleTitleA("DragonBurn Kernel Cheat Loader");
    PrintBanner();

    if (!IsRunAsAdmin())
    {
        SetColor(12); // Red
        std::cout << "[-] Error: Please run this loader as Administrator!" << std::endl;
        SetColor(7);
        std::cout << "Press Enter to exit...";
        std::cin.get();
        return 1;
    }

    SetColor(10); // Green
    std::cout << "[+] Administrator privileges verified." << std::endl;

    SetColor(14); // Yellow
    std::cout << "\n[*] Step 1: Mapping DragonBurn Kernel Driver..." << std::endl;
    SetColor(7);

    // Run driver mapper
    std::string mapperCmd = "mapper.exe driver.sys";
    if (!RunProcess(mapperCmd))
    {
        SetColor(12);
        std::cout << "[-] Warning: Mapper execution returned non-zero. Ensure mapper.exe and driver.sys are present." << std::endl;
    }
    else
    {
        SetColor(10);
        std::cout << "[+] Kernel Driver successfully mapped!" << std::endl;
    }

    SetColor(14);
    std::cout << "\n[*] Step 2: Launching External ESP Overlay..." << std::endl;
    SetColor(7);

    // Launch external ESP application
    std::string espCmd = "AzhengESP.exe";
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (CreateProcessA(NULL, (LPSTR)espCmd.c_str(), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
    {
        SetColor(10);
        std::cout << "[+] External ESP launched successfully!" << std::endl;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else
    {
        SetColor(12);
        std::cout << "[-] Failed to start AzhengESP.exe. Please verify executable path." << std::endl;
    }

    SetColor(11);
    std::cout << "\n=========================================================================" << std::endl;
    std::cout << "[+] All systems initialized. Press END in game to toggle menu." << std::endl;
    std::cout << "=========================================================================\n" << std::endl;

    SetColor(7);
    std::cout << "Press Enter to close loader...";
    std::cin.get();

    return 0;
}
