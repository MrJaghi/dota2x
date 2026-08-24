#include "utils.hpp"
#include <fstream>
#include <TlHelp32.h>
#include <cwchar>
#include <cstdlib>
#include <ctime>
#include <random>
#include <vector>
#include <string>

//
// RTL_PROCESS_MODULES / SYSTEM_HANDLE types for NtQuerySystemInformation.
// (We don't include <ntstatus.h> to avoid macro collisions; NT_SUCCESS is defined
// by <winternl.h> which is already in utils.hpp.)
//
typedef struct _SYSTEM_HANDLE_
{
	ULONG ProcessId;
	UCHAR ObjectTypeNumber;
	UCHAR Flags;
	USHORT Handle;
	PVOID Object;
	ACCESS_MASK GrantedAccess;
} SYSTEM_HANDLE_, * PSYSTEM_HANDLE_;

typedef struct _SYSTEM_HANDLE_INFORMATION_
{
	ULONG HandleCount;
	SYSTEM_HANDLE_ Handles[1];
} SYSTEM_HANDLE_INFORMATION_, * PSYSTEM_HANDLE_INFORMATION_;

typedef struct _RTL_PROCESS_MODULE_INFORMATION_
{
	HANDLE Section;
	PVOID MappedBase;
	PVOID ImageBase;
	ULONG ImageSize;
	ULONG Flags;
	USHORT LoadOrderIndex;
	USHORT InitOrderIndex;
	USHORT LoadCount;
	USHORT OffsetToFileName;
	UCHAR FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION_, * PRTL_PROCESS_MODULE_INFORMATION_;

typedef struct _RTL_PROCESS_MODULES_
{
	ULONG NumberOfModules;
	RTL_PROCESS_MODULE_INFORMATION_ Modules[1];
} RTL_PROCESS_MODULES_, * PRTL_PROCESS_MODULES_;

namespace utils
{
	bool ReadFileToMemory(const std::wstring& file_path, std::vector<uint8_t>* out_buffer)
	{
		std::ifstream file_ifstream(file_path, std::ios::binary | std::ios::ate);
		if (!file_ifstream.is_open())
			return false;

		std::streamsize file_size = file_ifstream.tellg();
		out_buffer->resize((size_t)file_size);

		file_ifstream.seekg(0, std::ios::beg);
		if (file_size > 0)
			file_ifstream.read((char*)out_buffer->data(), file_size);
		file_ifstream.close();
		return true;
	}

	bool CreateFileFromMemory(const std::wstring& desired_file_path, const char* address, size_t size)
	{
		HANDLE hFile = CreateFileW(desired_file_path.c_str(), GENERIC_WRITE, 0, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
			return false;

		DWORD written = 0;
		BOOL ok = WriteFile(hFile, address, (DWORD)size, &written, nullptr);
		CloseHandle(hFile);
		return ok && written == size;
	}

	std::wstring GetTempPathW()
	{
		wchar_t buf[MAX_PATH] = {};
		DWORD len = ::GetTempPathW(MAX_PATH, buf);
		if (len == 0 || len > MAX_PATH) return L"";
		std::wstring p(buf);
		while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
		return p;
	}

	std::wstring GetFullTempPath()
	{
		return GetTempPathW();
	}

	std::wstring GetExeDirW()
	{
		wchar_t buf[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, buf, MAX_PATH);
		std::wstring p(buf);
		auto slash = p.find_last_of(L"\\/");
		if (slash != std::wstring::npos) p = p.substr(0, slash);
		return p;
	}

	std::wstring RandomAlphaNumW(size_t len)
	{
		static const wchar_t alphabet[] =
			L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
		static bool seeded = false;
		if (!seeded) { srand((unsigned)time(nullptr) ^ GetCurrentThreadId()); seeded = true; }
		std::wstring out;
		out.resize(len);
		for (size_t i = 0; i < len; ++i)
			out[i] = alphabet[rand() % (_countof(alphabet) - 1)];
		return out;
	}

	uint64_t GetKernelModuleAddress(const std::string& module_name)
	{
		ULONG size = 0;
		NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)11, nullptr, 0, &size);
		if (size == 0) return 0;

		PRTL_PROCESS_MODULES_ modules = (PRTL_PROCESS_MODULES_)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!modules) return 0;

		if (!NT_SUCCESS(NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)11, modules, size, &size)))
		{
			VirtualFree(modules, 0, MEM_RELEASE);
			return 0;
		}

		uint64_t module_base = 0;
		for (ULONG i = 0; i < modules->NumberOfModules; i++)
		{
			std::string current((char*)modules->Modules[i].FullPathName + modules->Modules[i].OffsetToFileName);
			if (_stricmp(current.c_str(), module_name.c_str()) == 0)
			{
				module_base = (uint64_t)modules->Modules[i].ImageBase;
				break;
			}
		}

		VirtualFree(modules, 0, MEM_RELEASE);
		return module_base;
	}

	// NOTE: kernel-side export resolution now lives in intel_driver::GetKernelModuleExport
	// (it reads the PE export directory directly from kernel memory via ReadMemory,
	//  which is the only correct way to get function addresses that work with
	//  the NtAddAtom-trampoline CallKernelFunction).

	bool SetSystemPrivilege(const std::wstring& privilege_name, bool enable)
	{
		HANDLE token_handle = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token_handle))
			return false;

		TOKEN_PRIVILEGES tp;
		LUID luid;

		if (!LookupPrivilegeValueW(nullptr, privilege_name.c_str(), &luid))
		{
			CloseHandle(token_handle);
			return false;
		}

		tp.PrivilegeCount = 1;
		tp.Privileges[0].Luid = luid;
		tp.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;

		BOOL ok = AdjustTokenPrivileges(token_handle, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr);
		CloseHandle(token_handle);
		return ok && GetLastError() == ERROR_SUCCESS;
	}

	// -------------------------------------------------------------------
	// Environment pre-flight check
	// -------------------------------------------------------------------
	namespace envcheck
	{
		struct Finding
		{
			enum Severity { INFO, WARN, BLOCK } severity;
			std::string message;
			bool can_auto_fix = false;
			std::string fix_cmd;
			std::string fix_help;
		};

		static const wchar_t* const kBlockedProcs[][2] = {
			{L"vgc.exe",                 L"Riot Vanguard (Valorant)"},
			{L"vgtray.exe",              L"Riot Vanguard tray"},
			{L"easyanticheat.exe",       L"Easy Anti-Cheat"},
			{L"eac_launcher.exe",        L"Easy Anti-Cheat launcher"},
			{L"EasyAntiCheat.sys",       L"EAC kernel driver"},
			{L"BEService.exe",           L"BattlEye Service"},
			{L"BEDaisy.sys",             L"BattlEye kernel driver"},
			{L"faceitclient.exe",        L"FACEIT AC"},
			{L"faceitservice.exe",       L"FACEIT AC service"},
			{L"acs.exe",                 L"Riot Vanguard user-mode"},
			{L"MsMpEng.exe",             L"Windows Defender core"},
			{L"mpdefendercoreservice.exe", L"Windows Defender Core Service"},
			{L"NisSrv.exe",              L"Windows Defender NIS"},
			{L"mbamservice.exe",         L"Malwarebytes"},
			{L"avp.exe",                 L"Kaspersky"},
			{L"ekrn.exe",                L"ESET NOD32"},
			{L"bdagent.exe",             L"Bitdefender"},
			{L"avguard.exe",             L"Avira"},
			{L"ashDisp.exe",             L"Avast"},
			{L"aswidsagent.exe",         L"Avast IDS"},
		};

		static bool RegQueryDword(HKEY root, const wchar_t* subkey, const wchar_t* name, DWORD* out)
		{
			HKEY k = nullptr;
			if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS)
				return false;
			DWORD type = 0;
			DWORD val = 0;
			DWORD sz = sizeof(val);
			LSTATUS st = RegQueryValueExW(k, name, nullptr, &type, (BYTE*)&val, &sz);
			RegCloseKey(k);
			if (st == ERROR_SUCCESS && type == REG_DWORD) { *out = val; return true; }
			return false;
		}

		static bool IsHVCIEnabled()
		{
			DWORD v = 0;
			if (RegQueryDword(HKEY_LOCAL_MACHINE,
				L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",
				L"Enabled", &v) && v == 1)
				return true;
			if (RegQueryDword(HKEY_LOCAL_MACHINE,
				L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",
				L"EnableVirtualizationBasedSecurity", &v) && v == 1)
			{
				DWORD req = 0;
				RegQueryDword(HKEY_LOCAL_MACHINE,
					L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",
					L"RequiredSecurityProperties", &req);
				if (req & 1) return true;
			}
			return false;
		}

		static bool IsVulnerableDriverBlocklistEnabled()
		{
			DWORD v = 0;
			if (RegQueryDword(HKEY_LOCAL_MACHINE,
				L"SYSTEM\\CurrentControlSet\\Control\\CI\\Config",
				L"VulnerableDriverBlocklistEnable", &v) && v == 1)
				return true;
			DWORD v2 = 0;
			if (RegQueryDword(HKEY_LOCAL_MACHINE,
				L"SOFTWARE\\Microsoft\\Windows Defender\\Windows Defender Exploit Guard",
				L"VulnerableDriverBlockListEnabled", &v2) && v2 == 1)
				return true;
			return false;
		}

		static bool IsTamperProtectionEnabled()
		{
			DWORD v = 0;
			return RegQueryDword(HKEY_LOCAL_MACHINE,
				L"SOFTWARE\\Microsoft\\Windows Defender\\Features",
				L"TamperProtection", &v) && (v & 1) == 1;
		}

		static bool IsDefenderRealtimeEnabled()
		{
			DWORD v = 0;
			if (RegQueryDword(HKEY_LOCAL_MACHINE,
				L"SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection",
				L"DisableRealtimeMonitoring", &v))
				return v == 0;
			return true; // assume on if missing
		}

		static std::vector<std::wstring> SnapshotProcessNames()
		{
			std::vector<std::wstring> out;
			HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
			if (snap == INVALID_HANDLE_VALUE) return out;
			PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
			if (Process32FirstW(snap, &pe))
			{
				do { out.push_back(pe.szExeFile); } while (Process32NextW(snap, &pe));
			}
			CloseHandle(snap);
			for (auto& s : out) { for (auto& c : s) c = towlower(c); }
			return out;
		}

		static std::string WideToUtf8(const std::wstring& w)
		{
			int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
			std::string s(sz, 0);
			WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], sz, nullptr, nullptr);
			return s;
		}

		static bool PrintDiagnosis()
		{
			std::vector<Finding> findings;
			auto procs = SnapshotProcessNames();
			auto running = [&](const wchar_t* name) -> bool {
				std::wstring n(name);
				for (auto& c : n) c = towlower(c);
				for (const auto& p : procs) if (p == n) return true;
				return false;
			};

			for (size_t i = 0; i < _countof(kBlockedProcs); ++i)
			{
				if (running(kBlockedProcs[i][0]))
				{
					findings.push_back({ Finding::BLOCK,
						"Running security/anti-cheat: " + WideToUtf8(kBlockedProcs[i][1]),
						false, {}, {}});
				}
			}

			if (IsHVCIEnabled())
			{
				findings.push_back({ Finding::BLOCK,
					"HVCI / Memory Integrity (Core Isolation) is ON. It blocks iqvw64e.sys.",
					true,
					"reg add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity\" /v Enabled /t REG_DWORD /d 0 /f",
					"Reboot the PC after applying for HVCI to turn off."});
			}

			if (IsVulnerableDriverBlocklistEnabled())
			{
				findings.push_back({ Finding::BLOCK,
					"Microsoft Vulnerable Driver Blocklist is ON. It blocks iqvw64e.sys by hash.",
					true,
					"reg add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\CI\\Config\" /v VulnerableDriverBlocklistEnable /t REG_DWORD /d 0 /f",
					"Reboot the PC after applying."});
			}

			if (IsDefenderRealtimeEnabled())
			{
				findings.push_back({ Finding::WARN,
					"Windows Defender real-time protection is ON. It may quarantine the mapper / temp Intel driver.",
					true,
					"powershell -NoProfile -Command \"Set-MpPreference -DisableRealtimeMonitoring $true -DisableBehaviorMonitoring $true -DisableIOAVProtection $true\"",
					"Re-enable later via Windows Security."});
			}

			if (IsTamperProtectionEnabled())
			{
				findings.push_back({ Finding::WARN,
					"Windows Defender Tamper Protection is ON. It blocks disabling Defender via registry/PowerShell; turn it off manually in Windows Security -> Virus & threat protection settings.",
					false, {}, {}});
			}

			if (findings.empty())
			{
				std::cout << "[+] Environment check: no known blockers detected." << std::endl;
				return false;
			}

			std::cout << std::endl;
			std::cout << "================================================================" << std::endl;
			std::cout << "  ENVIRONMENT CHECK - " << findings.size() << " issue(s) found" << std::endl;
			std::cout << "================================================================" << std::endl;
			bool need_reboot = false;
			for (const auto& f : findings)
			{
				const char* mark = "[i]";
				if (f.severity == Finding::WARN) mark = "[!]";
				if (f.severity == Finding::BLOCK) mark = "[-]";
				std::cout << "  " << mark << " " << f.message << std::endl;
				if (!f.fix_help.empty())
					std::cout << "       -> " << f.fix_help << std::endl;
			}
			std::cout << "================================================================" << std::endl;

			// Apply auto-fixable commands
			for (const auto& f : findings)
			{
				if (!f.can_auto_fix || f.fix_cmd.empty()) continue;
				std::cout << "[*] Auto-fix: " << f.fix_cmd << std::endl;
				std::string full = "cmd.exe /c \"" + f.fix_cmd + "\"";
				STARTUPINFOA si{sizeof(si)};
				PROCESS_INFORMATION pi{};
				si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
				if (CreateProcessA(nullptr, (LPSTR)full.c_str(), nullptr, nullptr, FALSE,
					CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
				{
					WaitForSingleObject(pi.hProcess, 20000);
					DWORD ec = 0; GetExitCodeProcess(pi.hProcess, &ec);
					if (ec == 0) {
						std::cout << "[+] Fix applied OK." << std::endl;
						if (f.severity == Finding::BLOCK) need_reboot = true;
					} else {
						std::cout << "[!] Fix command failed (exit " << ec << ") -- Tamper Protection or policy may have blocked it." << std::endl;
					}
					CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
				}
				else
				{
					std::cout << "[!] Could not launch fix command (GLE=" << GetLastError() << ")." << std::endl;
				}
			}
			if (need_reboot)
			{
				std::cout << std::endl;
				std::cout << "*** REBOOT REQUIRED ***" << std::endl;
				std::cout << "HVCI and/or the Vulnerable Driver Blocklist were just disabled via" << std::endl;
				std::cout << "registry writes. Those changes do NOT take effect until Windows" << std::endl;
				std::cout << "is restarted. Reboot now, then run the loader again." << std::endl;
			}
			return need_reboot;
		}
	} // namespace envcheck

	bool EnvironmentPreflight()
	{
		return envcheck::PrintDiagnosis();
	}
} // namespace utils
