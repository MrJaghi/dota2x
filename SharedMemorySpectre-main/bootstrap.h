#pragma once
#include "includes.h"
enum piddb_type {
	UNLINK = 0x55,
	MODIFY = 0x43,
	REPLACE = 0x22
};
namespace bootstrap {
	NTSTATUS UnicodeStringToAnsiString(
		PUNICODE_STRING UnicodeString,
		PANSI_STRING AnsiString,
		BOOLEAN AllocateDestination
	);

	NTSTATUS mm_piddb(UNICODE_STRING DriverName, ULONG timeDateStamp, piddb_type type = piddb_type::UNLINK, PiDDBCacheEntry* replace_link = nullptr);
	NTSTATUS mm_mmu_mml(UNICODE_STRING DriverName);
	NTSTATUS mm_hashbucket(UNICODE_STRING DriverName);
	NTSTATUS kernelbootstrap();
}