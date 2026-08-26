#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <cmath>
#include "Memory.h"
#include "Offsets.h"
#include "VectorMath.h"
#include "EntityReader.h"

// ---------------------------------------------------------------------------
// OffsetValidator.
//
// Validates the core schema offsets against live client.dll after attach.
// m_iCurrentLevel is intentionally NOT validated or auto-tuned anymore --
// a stale level offset must be fully ignored by the ESP (no console spam,
// no wrong badge; EntityReader hides the badge when the read is implausible).
// Pure ReadProcessMemory -- nothing is ever written (read-only contract).
// ---------------------------------------------------------------------------
namespace OffsetValidator {

	static bool PlausiblePtr(uintptr_t p) {
		return p > 0x10000ull && p < 0x00007FFFFFFFFFFFull;
	}

	static bool CheckI32(const Memory& mem, uintptr_t base, std::ptrdiff_t off,
		int lo, int hi, const char* name) {
		int v = mem.Read<int>(base + off);
		if (v < lo || v > hi) {
			printf("[!] %s (0x%IX) -> %d (expected %d..%d)\n", name, (uintptr_t)off, v, lo, hi);
			return false;
		}
		return true;
	}

	static bool CheckU8(const Memory& mem, uintptr_t base, std::ptrdiff_t off,
		uint8_t lo, uint8_t hi, const char* name) {
		uint8_t v = mem.Read<uint8_t>(base + off);
		if (v < lo || v > hi) {
			printf("[!] %s (0x%IX) -> %u (expected %u..%u)\n", name, (uintptr_t)off, v, lo, hi);
			return false;
		}
		return true;
	}

	// ------------------------------------------------------------------
	// Global offsets (entity-list pointer, view matrix). Safe to run at
	// attach time, even in the main menu.
	// ------------------------------------------------------------------
	inline bool ValidateGlobals(const Memory& mem) {
		uintptr_t base = mem.clientDllBase;
		if (!base) {
			printf("[!] client.dll base is NULL.\n");
			return false;
		}
		printf("[*] Validating global offsets against live client.dll...\n");
		bool ok = true;

		using namespace offsets;

		if (!PlausiblePtr(mem.Read<uintptr_t>(base + client_dll::dwEntityList))) {
			printf("[!] dwEntityList does not look valid -- offsets may be stale.\n");
			ok = false;
		}

		// dwViewMatrix points DIRECTLY at a 16-float matrix (not a pointer).
		{
			ViewMatrix vm{};
			if (mem.ReadRaw(base + client_dll::dwViewMatrix, &vm, sizeof(vm))) {
				bool sane = std::isfinite(vm.m[0][0]) && std::isfinite(vm.m[1][1])
					&& std::isfinite(vm.m[3][3]) && fabsf(vm.m[3][3]) > 0.001f
					&& fabsf(vm.m[0][0]) < 4.0f && fabsf(vm.m[1][1]) < 8.0f;
				if (!sane) {
					printf("[!] dwViewMatrix does not look like a view matrix -- offsets may be stale.\n");
					ok = false;
				}
			} else {
				printf("[!] Failed to read the view matrix -- offsets may be stale.\n");
				ok = false;
			}
		}
		if (ok) printf("[+] Global offsets look valid.\n");
		return ok;
	}

	// ------------------------------------------------------------------
	// Per-entity checks against the LOCAL HERO. Called lazily -- once, the
	// first time the scan thread actually resolves a local hero (which can
	// be long after attach if the ESP started in the main menu or while a
	// demo was still loading). The old build ran this exactly once at
	// startup and complained "Could not locate the local hero" forever,
	// even though the hero appeared seconds later.
	// ------------------------------------------------------------------
	inline void ValidateLocalEntity(const Memory& mem, uintptr_t pawn) {
		if (!pawn) return;
		printf("[+] Local hero @ 0x%llX -- running per-entity offset checks...\n",
			(unsigned long long)pawn);
		bool ok = true;

		using namespace offsets;

		ok &= CheckI32(mem, pawn, C_BaseEntity::m_iHealth, 1, 5000, "C_BaseEntity::m_iHealth");
		ok &= CheckI32(mem, pawn, C_BaseEntity::m_iMaxHealth, 1, 5000, "C_BaseEntity::m_iMaxHealth");
		ok &= CheckU8(mem, pawn, C_BaseEntity::m_iTeamNum, 2, 3, "C_BaseEntity::m_iTeamNum");
		ok &= CheckU8(mem, pawn, C_BaseEntity::m_lifeState, 0, 2, "C_BaseEntity::m_lifeState");

		// m_vecAbsOrigin via the scene node
		uintptr_t scene = mem.Read<uintptr_t>(pawn + C_BaseEntity::m_pGameSceneNode);
		if (PlausiblePtr(scene)) {
			Vector3 v = mem.Read<Vector3>(scene + CGameSceneNode::m_vecAbsOrigin);
			bool coordsOk = fabsf(v.x) <= 16384.0f && fabsf(v.y) <= 16384.0f && fabsf(v.z) <= 16384.0f;
			if (!coordsOk) {
				printf("[!] CGameSceneNode::m_vecAbsOrigin -> (%.1f, %.1f, %.1f) is out of map bounds.\n", v.x, v.y, v.z);
				ok = false;
			}
		} else {
			printf("[!] m_pGameSceneNode is not a valid pointer.\n");
			ok = false;
		}

		// mana fields (warn only)
		float mana = mem.Read<float>(pawn + C_DOTA_BaseNPC::m_flMana);
		float maxMana = mem.Read<float>(pawn + C_DOTA_BaseNPC::m_flMaxMana);
		if (mana < 0.0f || mana > 5000.0f || maxMana < 0.0f || maxMana > 5000.0f)
			printf("[!] m_flMana / m_flMaxMana read (%.0f / %.0f) -- mana bar may be wrong.\n", mana, maxMana);

		// NOTE: m_iCurrentLevel is deliberately not validated here (ignored).

		if (ok) printf("[+] All per-entity offsets look valid.\n");
		else   printf("[!] Some offsets failed validation -- drop fresh dumper files into output\\ and REBUILD.\n");
	}
}
