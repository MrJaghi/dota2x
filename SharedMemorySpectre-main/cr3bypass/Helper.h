#pragma once
#include <ntifs.h>
#include <windef.h>
#include "read.h"

namespace Helper
{
	UINT64 translate_linear(UINT64 directoryTableBase, UINT64 virtualAddress);
	ULONG64 find_min(INT32 g, SIZE_T f);

    static inline void custom_Memcpy(void* dstp, const void* srcp, UINT len)
    {
        ULONG* dst = (ULONG*)dstp;
        ULONG* src = (ULONG*)srcp;
        UINT i, tail;

        // Main copy in ULONG chunks
        for (i = 0; i < (len / sizeof(ULONG)); i++)
            *dst++ = *src++;

        // Handle remaining bytes
        tail = len & (sizeof(ULONG) - 1);
        if (tail) {
            UCHAR* dstb = (UCHAR*)dst;  // Continue from where dst left off
            UCHAR* srcb = (UCHAR*)src;  // Continue from where src left off
            for (i = 0; i < tail; i++)
                *dstb++ = *srcb++;
        }
    }
}