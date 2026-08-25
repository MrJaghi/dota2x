#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <cmath>
#include <initializer_list>
#include "Memory.h"
#include "Offsets.h"
#include "VectorMath.h"
#include "EntityReader.h"

namespace OffsetValidator
{
	// Single-byte pattern mask for a "sane" pointer at an offset: must look
	// like a virtual address inside the module (i.e. top bytes nonzero if we
	// are reading a pointer inside a 64-bit DLL image).
	static bool IsPlausibleRelativePointer(const Memory& mem, uintptr_t base, std::ptrdiff_t off, const char* name)
	{
		uintptr_t p = mem.Read<uintptr_t>(base + off);
		// Valid heap / image pointers are user-mode (>0x10000) and <128TB (canonical).
		bool ok = (p > 0x10000ull) && (p < 0x00007FFFFFFFFFFFull);
		if (!ok) {
			printf("[!] OFFSET WARNING: %s (0x%IX) -> read 0x%llX -- looks wrong, offsets may be out of date.\n",
				name, (uintptr_t)off, (unsigned long long)p);
		}
		return ok;
	}

	static bool IsPlausibleU8(const Memory& mem, uintptr_t base, std::ptrdiff_t off,
		uint8_t lo, uint8_t hi, const char* name)
	{
		uint8_t v = mem.Read<uint8_t>(base + off);
		bool ok = (v >= lo && v <= hi);
		if (!ok) {
			printf("[!] OFFSET WARNING: %s (0x%IX) -> read u8=0x%02X -- out of expected range 0x%02X..0x%02X, offset may be stale.\n",
				name, (uintptr_t)off, v, lo, hi);
		}
		return ok;
	}

	static bool IsPlausibleI32(const Memory& mem, uintptr_t base, std::ptrdiff_t off,
		int lo, int hi, const char* name)
	{
		int v = mem.Read<int>(base + off);
		bool ok = (v >= lo && v <= hi);
		if (!ok) {
			printf("[!] OFFSET WARNING: %s (0x%IX) -> read int=%d -- out of expected range %d..%d, offset may be stale.\n",
				name, (uintptr_t)off, v, lo, hi);
		}
		return ok;
	}

	// Returns true if every checked offset looked valid, false otherwise.
	inline bool ValidateAll(const Memory& mem)
	{
		uintptr_t base = mem.clientDllBase;
		if (!base) {
			printf("[!] OFFSET WARNING: client.dll base is NULL.\n");
			return false;
		}
		printf("[*] Validating offsets against live client.dll...\n");
		bool ok = true;

		using namespace offsets;

		// Top-level client.dll pointers -- must point to valid live memory.
		ok &= IsPlausibleRelativePointer(mem, base, client_dll::dwEntityList,        "client_dll::dwEntityList");
		// NOTE: the local pawn is found via CBasePlayerController::m_bIsLocalPlayerController,
		// so no dwLocalPlayerPawn global is needed (the a2x dump doesn't provide one).
		// dwViewMatrix points DIRECTLY at a ViewMatrix struct (16 floats), NOT at a pointer.
		// Reading a uintptr_t from it would interpret matrix floats as an address and
		// always look bogus (e.g. 0x3F8000003F800000). Read the struct directly and
		// sanity-check that key entries look like a sane view-projection matrix.
		{
			ViewMatrix vm{};
			if (mem.ReadRaw(base + client_dll::dwViewMatrix, &vm, sizeof(vm))) {
				// A reasonable Dota 2 view/projection matrix:
				//  - m[3][3] is the homogeneous W scale and is nonzero; after the
				//    perspective divide it's effectively 1, but pre-divide it's the
				//    projection far-plane term -- just check it's not NaN/zero.
				//  - m[0][0] / m[1][1] are X/Y scaling terms in [-4..8] range for
				//    a typical Dota 2 camera FOV.
				bool sane = std::isfinite(vm.m[0][0]) && std::isfinite(vm.m[1][1])
					&& std::isfinite(vm.m[3][2]) && std::isfinite(vm.m[3][3])
					&& fabsf(vm.m[3][3]) > 0.001f
					&& fabsf(vm.m[0][0]) < 4.0f && fabsf(vm.m[1][1]) < 8.0f;
				if (!sane) {
					printf("[!] OFFSET WARNING: client_dll::dwViewMatrix (0x%llX) -> first floats don't look like a view matrix -- offset may be stale.\n",
						(unsigned long long)client_dll::dwViewMatrix);
					ok = false;
				}
			} else {
				printf("[!] OFFSET WARNING: client_dll::dwViewMatrix (0x%llX) -> failed to read view matrix bytes.\n",
					(unsigned long long)client_dll::dwViewMatrix);
				ok = false;
			}
		}

		// Entity list chunks are pointer arrays; we'll test one extra level in.
		uintptr_t entitySystem = mem.Read<uintptr_t>(base + client_dll::dwEntityList);
		if (entitySystem > 0x10000ull) {
			ok &= IsPlausibleRelativePointer(mem, entitySystem,
				CGameEntitySystem::m_EntityPtrArray, "CGameEntitySystem::m_EntityPtrArray[0]");
		} else {
			printf("[!] OFFSET WARNING: cannot follow entitySystem -> chunk[0] (bad dwEntityList).\n");
			ok = false;
		}

		// Now find the local pawn and sanity-check the per-entity field offsets.
		// This goes through EntityReader, which walks the list using the
		// EntityLayout detected at startup (stride / pInstance / index bias).
		uintptr_t localPawn = 0;
		{
			std::vector<uintptr_t> chunks = EntityReader::ReadChunkPointers(mem, entitySystem, 16);
			localPawn = EntityReader::FindLocalPawn(mem, chunks, 16 * CGameEntitySystem::ChunkSize);
		}

		if (!localPawn) {
			printf("[!] OFFSET WARNING: Could not locate local player pawn -- offsets for entities may be out of date.\n");
			return false;
		}
		printf("[+] Local pawn @ 0x%llX, running per-entity offset checks...\n", (unsigned long long)localPawn);

		// m_iHealth / m_iMaxHealth should be small positive integers.
		ok &= IsPlausibleI32(mem, localPawn, C_BaseEntity::m_iHealth,    1, 5000, "C_BaseEntity::m_iHealth");
		ok &= IsPlausibleI32(mem, localPawn, C_BaseEntity::m_iMaxHealth, 1, 5000, "C_BaseEntity::m_iMaxHealth");
		// m_iTeamNum is 2 or 3 for Radiant/Dire.
		ok &= IsPlausibleU8(mem, localPawn, C_BaseEntity::m_iTeamNum, 2, 3, "C_BaseEntity::m_iTeamNum");
		// m_lifeState should be 0 when alive.
		ok &= IsPlausibleU8(mem, localPawn, C_BaseEntity::m_lifeState, 0, 2, "C_BaseEntity::m_lifeState");
		// m_pGameSceneNode must be a valid pointer.
		ok &= IsPlausibleRelativePointer(mem, localPawn, C_BaseEntity::m_pGameSceneNode, "C_BaseEntity::m_pGameSceneNode");

		// Check m_vecAbsOrigin via the scene node.
		uintptr_t scene = mem.Read<uintptr_t>(localPawn + C_BaseEntity::m_pGameSceneNode);
		if (scene > 0x10000ull) {
			float vx = mem.Read<float>(scene + CGameSceneNode::m_vecAbsOrigin);
			float vy = mem.Read<float>(scene + CGameSceneNode::m_vecAbsOrigin + 4);
			float vz = mem.Read<float>(scene + CGameSceneNode::m_vecAbsOrigin + 8);
			bool coordsOk = (-16384.0f <= vx && vx <= 16384.0f)
				&& (-16384.0f <= vy && vy <= 16384.0f)
				&& (-16384.0f <= vz && vz <= 16384.0f);
			if (!coordsOk) {
				printf("[!] OFFSET WARNING: CGameSceneNode::m_vecAbsOrigin (0x%IX) -> read (%f, %f, %f) -- out of map bounds, offset may be stale.\n",
					(uintptr_t)CGameSceneNode::m_vecAbsOrigin, vx, vy, vz);
				ok = false;
			}
		} else {
			printf("[!] OFFSET WARNING: scene node pointer was bad, skipping m_vecAbsOrigin check.\n");
			ok = false;
		}

		// Hero-specific fields (level/mana/illusion).
		int level = mem.Read<int>(localPawn + C_DOTA_BaseNPC::m_iCurrentLevel);
		if (level < 1 || level > 30) {
			printf("[!] OFFSET WARNING: C_DOTA_BaseNPC::m_iCurrentLevel (0x%IX) -> read %d (expected 1..30), offset may be stale.\n",
				(uintptr_t)C_DOTA_BaseNPC::m_iCurrentLevel, level);
			ok = false;
		}
		float mana = mem.Read<float>(localPawn + C_DOTA_BaseNPC::m_flMana);
		float maxMana = mem.Read<float>(localPawn + C_DOTA_BaseNPC::m_flMaxMana);
		if (mana < 0.0f || mana > 5000.0f) {
			printf("[!] OFFSET WARNING: C_DOTA_BaseNPC::m_flMana (0x%IX) -> read %f, offset may be stale.\n",
				(uintptr_t)C_DOTA_BaseNPC::m_flMana, mana);
			ok = false;
		}
		if (maxMana < 0.0f || maxMana > 5000.0f) {
			printf("[!] OFFSET WARNING: C_DOTA_BaseNPC::m_flMaxMana (0x%IX) -> read %f, offset may be stale.\n",
				(uintptr_t)C_DOTA_BaseNPC::m_flMaxMana, maxMana);
			ok = false;
		}

		if (ok) printf("[+] All offsets look valid.\n");
		else   printf("[!] One or more offsets failed validation -- drop fresh dumper files into output\\ and REBUILD the ESP!\n");
		return ok;
	}
}
