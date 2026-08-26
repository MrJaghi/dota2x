#pragma once
#include <ntifs.h>
#include <windef.h>
#include "structs.h"
#include "Helper.h"
extern ULONG_PTR dirbase_from_base_address(void* base);

namespace EacBypass
{
	NTSTATUS DecryptCr3Packet(cr3 ControlRegister);
}