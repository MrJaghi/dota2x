#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>
#include <cstring>

class MemoryInternal
{
public:
	uintptr_t clientDllBase = 0;

	bool Init()
	{
		clientDllBase = (uintptr_t)GetModuleHandleA("client.dll");
		return clientDllBase != 0;
	}

	template <typename T>
	T Read(uintptr_t address) const
	{
		T value{};
		__try
		{
			value = *reinterpret_cast<T*>(address);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			value = T{};
		}
		return value;
	}

	bool ReadRaw(uintptr_t address, void* buffer, size_t size) const
	{
		__try
		{
			memcpy(buffer, reinterpret_cast<void*>(address), size);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool IsProcessAlive() const { return true; }
};
