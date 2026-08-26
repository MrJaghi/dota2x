#pragma once
#include <windows.h>
#include <stdint.h>
#include <string>

namespace service
{
	// Direct NtLoadDriver-based registration/start (no SCM), matching DragonBurn's
	// stable loader. Returns an NTSTATUS code (0 == STATUS_SUCCESS).
	long RegisterAndStart(const std::wstring& driver_path, const std::wstring& service_name);
	long StopAndRemove(const std::wstring& service_name);
}
