#pragma once
#include <ntifs.h>
#include <windef.h>
#include "structs.h"
#include "Helper.h"
extern uintptr_t dirbase_from_base_address(void* base);

namespace EacBypass
{
	NTSTATUS DecryptCr3Packet(cr3 ControlRegister);
}