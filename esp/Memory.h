#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <cstdint>

class Memory {
public:
	HANDLE processHandle = nullptr;
	DWORD processId = 0;
	uintptr_t clientDllBase = 0;

	~Memory() { Close(); }

	bool Attach(const wchar_t* processName, const wchar_t* moduleName) {
		Close();
		processId = GetProcessId(processName);
		if (!processId)
			return false;

		// Read-only ESP: VIRTUAL_MEMORY_READ is enough and is less
		// likely to be flagged by AV/EDR than PROCESS_ALL_ACCESS.
		processHandle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, processId);
		if (!processHandle || processHandle == INVALID_HANDLE_VALUE) {
			processHandle = nullptr;
			return false;
		}

		clientDllBase = GetModuleBase(moduleName);
		if (!clientDllBase) {
			Close();
			return false;
		}
		return true;
	}

	void Close() {
		if (processHandle && processHandle != INVALID_HANDLE_VALUE) {
			CloseHandle(processHandle);
		}
		processHandle = nullptr;
		processId = 0;
		clientDllBase = 0;
	}

	template <typename T>
	T Read(uintptr_t address) const {
		T value{};
		if (!processHandle || !address) return value;
		SIZE_T n = 0;
		if (!ReadProcessMemory(processHandle, (LPCVOID)address, &value, sizeof(T), &n) || n != sizeof(T)) {
			// Short or failed read: zero out so garbage isn't consumed.
			value = T{};
		}
		return value;
	}

	bool ReadRaw(uintptr_t address, void* buffer, size_t size) const {
		if (!processHandle || !address || !buffer || !size) return false;
		SIZE_T bytesRead = 0;
		return ReadProcessMemory(processHandle, (LPCVOID)address, buffer, size, &bytesRead) && bytesRead == size;
	}

	bool IsProcessAlive() const {
		if (!processHandle) return false;
		DWORD exitCode = 0;
		return GetExitCodeProcess(processHandle, &exitCode) && exitCode == STILL_ACTIVE;
	}

private:
	static DWORD GetProcessId(const wchar_t* processName) {
		DWORD pid = 0;
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snap == INVALID_HANDLE_VALUE) return 0;

		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		if (Process32FirstW(snap, &entry)) {
			do {
				if (_wcsicmp(entry.szExeFile, processName) == 0) {
					pid = entry.th32ProcessID;
					break;
				}
			} while (Process32NextW(snap, &entry));
		}
		CloseHandle(snap);
		return pid;
	}

	uintptr_t GetModuleBase(const wchar_t* moduleName) const {
		uintptr_t base = 0;
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
		if (snap == INVALID_HANDLE_VALUE) return 0;

		MODULEENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		if (Module32FirstW(snap, &entry)) {
			do {
				if (_wcsicmp(entry.szModule, moduleName) == 0) {
					base = (uintptr_t)entry.modBaseAddr;
					break;
				}
			} while (Module32NextW(snap, &entry));
		}
		CloseHandle(snap);
		return base;
	}
};
