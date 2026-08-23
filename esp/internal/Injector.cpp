#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>
#include <cstdio>
#include <string>

static DWORD FindProcessId(const wchar_t* processName)
{
	DWORD pid = 0;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return 0;

	PROCESSENTRY32W entry{};
	entry.dwSize = sizeof(entry);
	if (Process32FirstW(snap, &entry))
	{
		do
		{
			if (_wcsicmp(entry.szExeFile, processName) == 0)
			{
				pid = entry.th32ProcessID;
				break;
			}
		} while (Process32NextW(snap, &entry));
	}
	CloseHandle(snap);
	return pid;
}

static bool InjectDll(DWORD pid, const std::string& dllPath)
{
	HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (!process)
	{
		printf("[-] OpenProcess failed (%lu). Run as administrator.\n", GetLastError());
		return false;
	}

	LPVOID remoteMem = VirtualAllocEx(process, nullptr, dllPath.size() + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remoteMem)
	{
		printf("[-] VirtualAllocEx failed (%lu)\n", GetLastError());
		CloseHandle(process);
		return false;
	}

	if (!WriteProcessMemory(process, remoteMem, dllPath.c_str(), dllPath.size() + 1, nullptr))
	{
		printf("[-] WriteProcessMemory failed (%lu)\n", GetLastError());
		VirtualFreeEx(process, remoteMem, 0, MEM_RELEASE);
		CloseHandle(process);
		return false;
	}

	LPVOID loadLibraryAddr = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
	HANDLE remoteThread = CreateRemoteThread(process, nullptr, 0, (LPTHREAD_START_ROUTINE)loadLibraryAddr, remoteMem, 0, nullptr);
	if (!remoteThread)
	{
		printf("[-] CreateRemoteThread failed (%lu)\n", GetLastError());
		VirtualFreeEx(process, remoteMem, 0, MEM_RELEASE);
		CloseHandle(process);
		return false;
	}

	WaitForSingleObject(remoteThread, INFINITE);

	VirtualFreeEx(process, remoteMem, 0, MEM_RELEASE);
	CloseHandle(remoteThread);
	CloseHandle(process);
	return true;
}

int main(int argc, char** argv)
{
	char fullPath[MAX_PATH];

	if (argc > 1)
	{
		GetFullPathNameA(argv[1], MAX_PATH, fullPath, nullptr);
	}
	else
	{
		char exeDir[MAX_PATH];
		GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
		std::string dir(exeDir);
		dir = dir.substr(0, dir.find_last_of("\\/") + 1);
		std::string dllPath = dir + "AzhengInternal.dll";
		strncpy_s(fullPath, dllPath.c_str(), MAX_PATH - 1);
	}

	printf("[*] DLL path: %s\n", fullPath);
	if (GetFileAttributesA(fullPath) == INVALID_FILE_ATTRIBUTES)
	{
		printf("[-] File not found. Pass the path as an argument if it isn't next to this exe.\n");
		return 1;
	}

	printf("[*] Waiting for dota2.exe...\n");
	DWORD pid = 0;
	while (!(pid = FindProcessId(L"dota2.exe")))
		Sleep(500);

	printf("[+] Found dota2.exe, pid=%lu\n", pid);
	printf("[*] Injecting...\n");

	if (InjectDll(pid, fullPath))
		printf("[+] Injected successfully. Press Right Shift in-game to open the menu.\n");
	else
		printf("[-] Injection failed.\n");

	printf("Press Enter to exit...\n");
	getchar();
	return 0;
}
