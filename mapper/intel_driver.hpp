#pragma once
#include <windows.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <cstring>
#include <fstream>
#include <type_traits>
#include "utils.hpp"

extern "C" {
	typedef LONG NTSTATUS, * PNTSTATUS;
#ifndef STATUS_SUCCESS
#	define STATUS_SUCCESS       ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_UNSUCCESSFUL
#	define STATUS_UNSUCCESSFUL  ((NTSTATUS)0xC0000001L)
#endif

	// SEH helper implemented in intel_driver.cpp with no C++ dtors in scope so
	// __try/__except is permitted (avoids C2712 when used from templates that
	// have locals with non-trivial destructors, e.g. std::cout).
	uint64_t CKF_SEH_Call(
		uint64_t(__stdcall* fn)(uint64_t, uint64_t, uint64_t, uint64_t),
		uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, int* out_except);
}
#ifndef NT_SUCCESS
#	define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace intel_driver
{
	// Match kdmapper/DragonBurn API:
	//   hDevice  : handle to \\Device\\Nal (set by Load())
	//   ntoskrnlAddr : base of ntoskrnl.exe in kernel (set by Load())
	extern HANDLE g_hDevice;
	extern uint64_t g_ntoskrnlAddr;

	// IOCTL primitives
	bool MemCopy(uint64_t destination, uint64_t source, uint64_t size);
	bool SetMemory(uint64_t address, uint32_t value, uint64_t size);
	bool GetPhysicalAddress(uint64_t address, uint64_t* out_physical_address);
	uint64_t MapIoSpace(uint64_t physical_address, uint32_t size);
	bool UnmapIoSpace(uint64_t address, uint32_t size);
	bool ReadMemory(uint64_t address, void* buffer, uint64_t size);
	bool WriteMemory(uint64_t address, const void* buffer, uint64_t size);
	bool WriteToReadOnlyMemory(uint64_t address, const void* buffer, uint32_t size);

	// Allocate/free kernel pool (NonPagedPool executable)
	// pool_type must be 0 (NonPagedPool == NonPagedPoolExecute on Win8+)
	uint64_t AllocatePool(uint32_t pool_type, uint64_t size);
	bool FreePool(uint64_t address);

	// Kernel-side PE parser (resolves exports directly from kernel memory)
	uint64_t GetKernelModuleExport(uint64_t kernel_module_base, const std::string& function_name);

	// Loader/unloader
	HANDLE Load();
	bool Unload(HANDLE device_handle);

	// ----------------------------------------------------------------
	// DragonBurn/kdmapper-style NtAddAtom trampoline (header template).
	// 12-byte `mov rax, imm64; jmp rax` is patched over kernel!NtAddAtom;
	// then ntdll!NtAddAtom is called from user mode (syscall lands on the
	// trampoline, jumps to the target kernel function with the same
	// rcx/rdx/r8/r9 args); then original bytes are restored.
	//
	// Hook persistence across mapper restarts:
	//   If a previous mapper run crashed/was-killed mid-CKF, the 12-byte
	//   trampoline can be left in place on nt!NtAddAtom. To recover we
	//   save the known-good original bytes to a temp file on the FIRST
	//   successful read, and reuse them on future runs to restore a
	//   stuck trampoline.
	// ----------------------------------------------------------------
	namespace ckp {
		inline constexpr const char* kBackupFileName = "dragonburn_ntaddatom.bin";
		inline bool LooksLikeOurTrampoline(const uint8_t* b) {
			return b[0] == 0x48 && b[1] == 0xB8
				&& b[10] == 0xFF && b[11] == 0xE0;
		}
		inline std::string GetBackupPath() {
			char tmp[MAX_PATH] = {};
			DWORD len = GetTempPathA(MAX_PATH, tmp);
			if (!len) return {};
			return std::string(tmp) + kBackupFileName;
		}
		inline bool SaveOriginalToDisk(const uint8_t* orig) {
			std::string path = GetBackupPath();
			if (path.empty()) return false;
			std::ofstream f(path, std::ios::binary | std::ios::trunc);
			if (!f.is_open()) return false;
			f.write(reinterpret_cast<const char*>(orig), 12);
			return true;
		}
		inline bool LoadOriginalFromDisk(uint8_t* out_orig) {
			std::string path = GetBackupPath();
			if (path.empty()) return false;
			std::ifstream f(path, std::ios::binary);
			if (!f.is_open()) return false;
			f.read(reinterpret_cast<char*>(out_orig), 12);
			return (size_t)f.gcount() == 12;
		}
		// Sanity: on a clean exit, delete the backup so a future reboot
		// (where ntoskrnl might be at a different VA / have a different
		// NtAddAtom prologue after a Windows update) does not load
		// stale bytes.
		inline void DeleteBackup() {
			std::string path = GetBackupPath();
			if (!path.empty()) DeleteFileA(path.c_str());
		}
	}

	template<typename T, typename... A>
	inline bool CallKernelFunction(T* out_result, uint64_t kernel_function_address, const A... arguments)
	{
		constexpr bool call_void = std::is_same_v<T, void>;
		static_assert(sizeof...(A) <= 4, "CallKernelFunction supports up to 4 arguments");

		if constexpr (!call_void) {
			if (!out_result) return false;
		} else {
			UNREFERENCED_PARAMETER(out_result);
		}
		if (!kernel_function_address) return false;

		HMODULE ntdll = GetModuleHandleA("ntdll.dll");
		if (!ntdll) return false;
		FARPROC um_NtAddAtom_p = GetProcAddress(ntdll, "NtAddAtom");
		if (!um_NtAddAtom_p) return false;

		static uint64_t kernel_NtAddAtom = 0;
		static uint8_t  saved_original[12] = {};
		static bool     have_original = false;
		if (!kernel_NtAddAtom) {
			kernel_NtAddAtom = GetKernelModuleExport(g_ntoskrnlAddr, "NtAddAtom");
		}
		if (!kernel_NtAddAtom) return false;

		uint8_t patch[12], current[12];
		const uint8_t trampoline[] = { 0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0 };
		memcpy(patch, trampoline, sizeof(patch));
		memcpy(patch + 2, &kernel_function_address, sizeof(kernel_function_address));

		if (!ReadMemory(kernel_NtAddAtom, current, sizeof(current))) {
			std::cout << "[-] CKF: failed to read NtAddAtom prologue" << std::endl;
			return false;
		}

		// If NtAddAtom is already patched with our 12-byte movabs/jmp
		// trampoline, try to recover:
		//   1. Use previously-cached saved_original from THIS process.
		//   2. Else try the on-disk backup from a previous mapper run.
		//   3. Else bail out (hooked by something else, can't safely overwrite).
		if (ckp::LooksLikeOurTrampoline(current)) {
			bool restored = false;
			if (have_original) {
				if (WriteToReadOnlyMemory(kernel_NtAddAtom, saved_original, 12)) {
					if (ReadMemory(kernel_NtAddAtom, current, 12)
						&& !ckp::LooksLikeOurTrampoline(current)) {
						restored = true;
					}
				}
			}
			if (!restored) {
				uint8_t backup[12] = {};
				if (ckp::LoadOriginalFromDisk(backup)) {
					if (WriteToReadOnlyMemory(kernel_NtAddAtom, backup, 12)) {
						if (ReadMemory(kernel_NtAddAtom, current, 12)
							&& !ckp::LooksLikeOurTrampoline(current)) {
							memcpy(saved_original, backup, 12);
							have_original = true;
							restored = true;
						}
					}
				}
			}
			if (restored) {
				std::cout << "[!] CKF: recovered stale NtAddAtom hook from previous run" << std::endl;
			} else {
				std::cout << "[-] CKF: NtAddAtom is already hooked and we have no backup to restore -- cannot safely patch" << std::endl;
				return false;
			}
		}

		// Remember the (now-known-good) prologue for later restores and
		// persist it to disk so a future mapper instance can recover if
		// we crash mid-call.
		memcpy(saved_original, current, 12);
		have_original = true;
		ckp::SaveOriginalToDisk(saved_original);

		if (!WriteToReadOnlyMemory(kernel_NtAddAtom, patch, sizeof(patch))) {
			std::cout << "[-] CKF: failed to patch NtAddAtom" << std::endl;
			return false;
		}

		// Convert up to 4 args to uint64_t; fill unused slots with 0.
		const uint64_t argv[] = { (uint64_t)arguments..., 0, 0, 0, 0 };
		int ex = 0;
		uint64_t rax = CKF_SEH_Call(
			(uint64_t(__stdcall*)(uint64_t,uint64_t,uint64_t,uint64_t))um_NtAddAtom_p,
			argv[0], argv[1], argv[2], argv[3], &ex);

		// ALWAYS restore the original bytes, even if the kernel call crashed.
		// Retry with verify-read because WriteToReadOnlyMemory can transiently
		// fail right after kernel calls that altered page-table state.
		bool restored = false;
		for (int attempt = 0; attempt < 10 && !restored; ++attempt) {
			if (WriteToReadOnlyMemory(kernel_NtAddAtom, saved_original, sizeof(saved_original))) {
				uint8_t check[12] = {};
				if (ReadMemory(kernel_NtAddAtom, check, 12)
					&& memcmp(check, saved_original, 12) == 0) {
					restored = true;
				}
			}
			if (!restored) Sleep(1);
		}
		if (!restored) {
			std::cout << "[-] CKF: WARNING -- failed to restore NtAddAtom prologue after 10 attempts; next CallKernelFunction will auto-recover." << std::endl;
		}

		if (ex != 0) {
			std::cout << "[-] CKF: exception 0x" << std::hex << ex << std::dec
				<< " calling kernel fn 0x" << std::hex << kernel_function_address
				<< std::dec << std::endl;
			return false;
		}

		if constexpr (!call_void) {
			*out_result = (T)rax;
		}
		return true;
	}
} // namespace intel_driver
