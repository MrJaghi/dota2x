#include "read.h"
#include "../callstack.h"

NTSTATUS Read::read(PVOID target_address, PVOID buffer, SIZE_T size, SIZE_T* bytes_read) {
	SPOOF_FUNC;

	if (!target_address || !buffer || !bytes_read) {
		return STATUS_INVALID_PARAMETER;
	}

	if (size == 0) {
		*bytes_read = 0;
		return STATUS_SUCCESS;
	}

	// Initialize bytes_read to avoid undefined behavior
	*bytes_read = 0;

	// Basic validation (example; enhance based on your context)
	PHYSICAL_ADDRESS phys_addr = { .QuadPart = (LONGLONG)target_address };
	MM_COPY_ADDRESS to_read = { .PhysicalAddress = phys_addr };

	NTSTATUS status =  (MmCopyMemory)(buffer, to_read, size, MM_COPY_MEMORY_PHYSICAL, bytes_read);
	if (!NT_SUCCESS(status)) {
		DbgPrint("MmCopyMemory failed with status: 0x%X\n", status);
		return status;
	}

	if (*bytes_read > size) {
		*bytes_read = size;
	}

	return STATUS_SUCCESS;
}


NTSTATUS Read2(PVOID target_address, PVOID buffer, SIZE_T size, SIZE_T* bytes_read) {
	SPOOF_FUNC;

	if (!target_address || !buffer || !bytes_read) {
		return STATUS_INVALID_PARAMETER;
	}

	*bytes_read = 0;
	PHYSICAL_ADDRESS addr = { .QuadPart = (LONGLONG)target_address };

	// Ensure size is reasonable
	if (size > PAGE_SIZE) {
		size = PAGE_SIZE;  // Cap at page size
	}

	PVOID pmapped_mem =  (MmMapIoSpaceEx)(addr, size, PAGE_READWRITE);
	if (!pmapped_mem) {
		DbgPrint("MmMapIoSpaceEx failed for address: %p, size: %zu\n", target_address, size);
		return STATUS_UNSUCCESSFUL;
	}

	Helper::custom_Memcpy(buffer, pmapped_mem, size);
	*bytes_read = size;

	 (MmUnmapIoSpace)(pmapped_mem, size);
	return STATUS_SUCCESS;
}
NTSTATUS ProbeAndReadMemory(ReadStruct x) {
	__try {
		ProbeForWrite((PVOID)((ULONG64)x->buffer), x->size, 1);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return STATUS_ACCESS_VIOLATION;
	}

	return STATUS_SUCCESS;
}
NTSTATUS Read::ReadMemory(ReadStruct x) {
	SPOOF_FUNC;
 
		// Check for null or invalid process ID
		if (!x || !x->process_id || !struc::SavedCr3) {
			return STATUS_INVALID_PARAMETER;
		}

		SIZE_T this_offset = 0;

		SIZE_T total_size = x->size;
		if (total_size == 0) {
			return STATUS_SUCCESS;
		}
		// Translate the linear address and check validity
		INT64 physical_address = Helper::translate_linear(struc::SavedCr3, (ULONG64)x->address + this_offset);
		if (physical_address <= 0) {
			return STATUS_UNSUCCESSFUL;  // Handle invalid address
		}

		// Calculate the final size to read
		ULONG64 final_size = Helper::find_min(PAGE_SIZE - (physical_address & 0xFFF), total_size);
		if (final_size == 0) {
			return STATUS_UNSUCCESSFUL;  // No valid size to read
		}
	 
		NTSTATUS status = ProbeAndReadMemory(x);
		if (!NT_SUCCESS(status)) {
			return status;
		}
	 
		// Call Read2 and check for success
		SIZE_T bytes_trough = 0;
		  status = Read2(PVOID(physical_address), (PVOID)((ULONG64)x->buffer + this_offset), final_size, &bytes_trough);
		if (!NT_SUCCESS(status)) {
			return status;  // Propagate error status
		}

		return STATUS_SUCCESS;
	}
     
 
