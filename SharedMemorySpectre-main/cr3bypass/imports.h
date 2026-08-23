#pragma once
#include <cstdint>
#include <windef.h>
 
#ifdef _MSC_VER
#define _KLI_FORCEINLINE __forceinline
#else
#define _KLI_FORCEINLINE __attribute__((always_inline))
#endif

#ifndef KLI_DONT_INLINE
#define KLI_FORCEINLINE _KLI_FORCEINLINE
#else
#define KLI_FORCEINLINE inline
#endif

#define driver_api inline
#include "structs.h"
#include <ntimage.h>
#include <xtr1common>
 
namespace struc
{
	extern std::uintptr_t ntos_image_base;
	extern std::uintptr_t kernel_base;
	extern ULONGLONG SavedCr3;
}

 
#define udman_spoof(name) ((decltype(&##name))(::kli::find_kernel_export_globald<KLI_HASH_STR(#name)>()))