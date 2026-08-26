#include "service.hpp"
#include <iostream>
#include <sstream>

//
// Minimal NT declarations needed to call ntdll!NtLoadDriver / NtUnloadDriver /
// RtlAdjustPrivilege without pulling in the WDK. These match the Windows
// Native API signatures used by the upstream DragonBurn mapper.
//
extern "C" {
	typedef LONG NTSTATUS, * PNTSTATUS;

	typedef struct _UNICODE_STRING_ {
		USHORT Length;
		USHORT MaximumLength;
		PWSTR  Buffer;
	} UNICODE_STRING_, * PUNICODE_STRING_;

	typedef VOID(NTAPI* PFN_RtlInitUnicodeString)(PUNICODE_STRING_, PCWSTR);
	typedef NTSTATUS(NTAPI* PFN_RtlAdjustPrivilege)(ULONG, BOOLEAN, BOOLEAN, PBOOLEAN);
	typedef NTSTATUS(NTAPI* PFN_NtLoadDriver)(PUNICODE_STRING_);
	typedef NTSTATUS(NTAPI* PFN_NtUnloadDriver)(PUNICODE_STRING_);

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_OBJECT_NAME_NOT_FOUND
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)
#endif
#ifndef STATUS_IMAGE_CERT_REVOKED
#define STATUS_IMAGE_CERT_REVOKED ((NTSTATUS)0xC0000425L)
#endif
#ifndef STATUS_ACCESS_DENIED
#define STATUS_ACCESS_DENIED ((NTSTATUS)0xC0000022L)
#endif
#ifndef STATUS_INSUFFICIENT_RESOURCES
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009AL)
#endif
#ifndef STATUS_REGISTRY_IO_FAILED
#define STATUS_REGISTRY_IO_FAILED ((NTSTATUS)0xC0000141L)
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif
}

static void PrintNtStatus(const char* prefix, NTSTATUS st)
{
	std::ostringstream ss;
	ss << prefix << " (NTSTATUS=0x" << std::hex << st << std::dec << ")";
	std::cout << "[-] " << ss.str() << std::endl;
}

static bool ResolveNt(PFN_RtlInitUnicodeString& rtlInit,
	PFN_RtlAdjustPrivilege& rtlAdjust,
	PFN_NtLoadDriver& ntLoad,
	PFN_NtUnloadDriver& ntUnload)
{
	HMODULE ntdll = GetModuleHandleA("ntdll.dll");
	if (!ntdll) return false;
	rtlInit = (PFN_RtlInitUnicodeString)GetProcAddress(ntdll, "RtlInitUnicodeString");
	rtlAdjust = (PFN_RtlAdjustPrivilege)GetProcAddress(ntdll, "RtlAdjustPrivilege");
	ntLoad = (PFN_NtLoadDriver)GetProcAddress(ntdll, "NtLoadDriver");
	ntUnload = (PFN_NtUnloadDriver)GetProcAddress(ntdll, "NtUnloadDriver");
	return rtlInit && rtlAdjust && ntLoad && ntUnload;
}

namespace service
{
	long RegisterAndStart(const std::wstring& driver_path, const std::wstring& service_name)
	{
		PFN_RtlInitUnicodeString RtlInitUnicodeString = nullptr;
		PFN_RtlAdjustPrivilege RtlAdjustPrivilege = nullptr;
		PFN_NtLoadDriver NtLoadDriver = nullptr;
		PFN_NtUnloadDriver NtUnloadDriver = nullptr;
		if (!ResolveNt(RtlInitUnicodeString, RtlAdjustPrivilege, NtLoadDriver, NtUnloadDriver))
		{
			std::cout << "[-] Failed to resolve ntdll entrypoints" << std::endl;
			return STATUS_UNSUCCESSFUL;
		}

		const std::wstring services_path = L"SYSTEM\\CurrentControlSet\\Services\\" + service_name;
		const std::wstring nt_image_path = L"\\??\\" + driver_path;
		const DWORD service_type_kernel = 1; // SERVICE_KERNEL_DRIVER

		HKEY service_key = nullptr;
		LSTATUS st = RegCreateKeyExW(HKEY_LOCAL_MACHINE, services_path.c_str(),
			0, nullptr, 0, KEY_SET_VALUE, nullptr, &service_key, nullptr);
		if (st != ERROR_SUCCESS)
		{
			std::cout << "[-] Can't create service registry key (Win32=" << st << ")" << std::endl;
			return STATUS_REGISTRY_IO_FAILED;
		}

		st = RegSetValueExW(service_key, L"ImagePath", 0, REG_EXPAND_SZ,
			(const BYTE*)nt_image_path.c_str(),
			(DWORD)((nt_image_path.size() + 1) * sizeof(wchar_t)));
		if (st != ERROR_SUCCESS)
		{
			std::cout << "[-] Can't write 'ImagePath' (Win32=" << st << ")" << std::endl;
			RegCloseKey(service_key);
			RegDeleteTreeW(HKEY_LOCAL_MACHINE, services_path.c_str());
			return STATUS_REGISTRY_IO_FAILED;
		}

		st = RegSetValueExW(service_key, L"Type", 0, REG_DWORD,
			(const BYTE*)&service_type_kernel, sizeof(DWORD));
		if (st != ERROR_SUCCESS)
		{
			std::cout << "[-] Can't write 'Type' (Win32=" << st << ")" << std::endl;
			RegCloseKey(service_key);
			RegDeleteTreeW(HKEY_LOCAL_MACHINE, services_path.c_str());
			return STATUS_REGISTRY_IO_FAILED;
		}

		RegCloseKey(service_key);

		constexpr ULONG SE_LOAD_DRIVER_PRIVILEGE = 10UL;
		BOOLEAN was_enabled = FALSE;
		NTSTATUS nt_status = RtlAdjustPrivilege(SE_LOAD_DRIVER_PRIVILEGE, TRUE, FALSE, &was_enabled);
		if (!NT_SUCCESS(nt_status))
		{
			std::cout << "[-] Failed to acquire SeLoadDriverPrivilege. Are you running as administrator?" << std::endl;
			RegDeleteTreeW(HKEY_LOCAL_MACHINE, services_path.c_str());
			return nt_status;
		}

		const std::wstring reg_path = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\" + service_name;
		UNICODE_STRING_ service_str;
		RtlInitUnicodeString(&service_str, reg_path.c_str());

		nt_status = NtLoadDriver(&service_str);

		if (nt_status == STATUS_IMAGE_CERT_REVOKED)
		{
			std::cout << "[-] Microsoft Vulnerable Driver Blocklist is enabled and has blocked iqvw64e.sys." << std::endl;
			std::cout << "    Disable it in Windows Security > Device Security > Core Isolation >" << std::endl;
			std::cout << "    'Microsoft Vulnerable Driver Blocklist', then reboot. (Reg:" << std::endl;
			std::cout << "    HKLM\\SYSTEM\\CurrentControlSet\\Control\\CI\\Config\\VulnerableDriverBlocklistEnable = 0)" << std::endl;
		}
		else if (nt_status == STATUS_ACCESS_DENIED || nt_status == STATUS_INSUFFICIENT_RESOURCES)
		{
			std::cout << "[-] NtLoadDriver returned "
				<< (nt_status == STATUS_ACCESS_DENIED ? "STATUS_ACCESS_DENIED" : "STATUS_INSUFFICIENT_RESOURCES")
				<< " (0x" << std::hex << nt_status << std::dec << ")." << std::endl;
			std::cout << "    HVCI/Memory Integrity or an antivirus/anticheat is blocking the vulnerable" << std::endl;
			std::cout << "    Intel driver from loading. Try: Disable Core Isolation / Memory Integrity" << std::endl;
			std::cout << "    in Windows Security, exit any AV/AC (EAC, BE, Vanguard, ...), and retry." << std::endl;
		}
		else if (!NT_SUCCESS(nt_status))
		{
			PrintNtStatus("NtLoadDriver failed", nt_status);
		}

		if (!NT_SUCCESS(nt_status))
		{
			// Best-effort cleanup of the service key.
			LSTATUS cl = RegDeleteTreeW(HKEY_LOCAL_MACHINE, services_path.c_str());
			if (cl != ERROR_SUCCESS && cl != ERROR_FILE_NOT_FOUND)
			{
				std::cout << "[-] Warning: failed to clean up service registry key (Win32=" << cl << ")" << std::endl;
			}
		}

		return nt_status;
	}

	long StopAndRemove(const std::wstring& service_name)
	{
		PFN_RtlInitUnicodeString RtlInitUnicodeString = nullptr;
		PFN_RtlAdjustPrivilege RtlAdjustPrivilege = nullptr;
		PFN_NtLoadDriver NtLoadDriver = nullptr;
		PFN_NtUnloadDriver NtUnloadDriver = nullptr;
		if (!ResolveNt(RtlInitUnicodeString, RtlAdjustPrivilege, NtLoadDriver, NtUnloadDriver))
			return STATUS_UNSUCCESSFUL;

		const std::wstring services_path = L"SYSTEM\\CurrentControlSet\\Services\\" + service_name;

		// If the registry key is already gone there's nothing to unload.
		HKEY k = nullptr;
		LSTATUS st = RegOpenKeyW(HKEY_LOCAL_MACHINE, services_path.c_str(), &k);
		if (st == ERROR_FILE_NOT_FOUND) return STATUS_SUCCESS;
		if (st == ERROR_SUCCESS) RegCloseKey(k);

		const std::wstring reg_path = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\" + service_name;
		UNICODE_STRING_ service_str;
		RtlInitUnicodeString(&service_str, reg_path.c_str());

		// Best-effort SeLoadDriverPrivilege re-enable for NtUnloadDriver.
		BOOLEAN was_enabled = FALSE;
		RtlAdjustPrivilege(10UL, TRUE, FALSE, &was_enabled);

		NTSTATUS nt = NtUnloadDriver(&service_str);
		if (!NT_SUCCESS(nt) && nt != STATUS_OBJECT_NAME_NOT_FOUND)
		{
			PrintNtStatus("NtUnloadDriver failed (driver may already be stopped)", nt);
		}

		st = RegDeleteTreeW(HKEY_LOCAL_MACHINE, services_path.c_str());
		if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND)
		{
			std::cout << "[-] Warning: failed to delete service key (Win32=" << st << ")" << std::endl;
			return STATUS_REGISTRY_IO_FAILED;
		}

		return nt;
	}
}
