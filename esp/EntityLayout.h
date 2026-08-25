#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>
#include "Offsets.h"
#include "Memory.h"

// ---------------------------------------------------------------------------
// EntityLayout -- READ-ONLY auto-detection of the Source2 entity-list layout.
//
// The schema offsets come from the dumper and are hardcoded via Offsets.h,
// but the engine-side CGameEntitySystem / CEntityIdentity layout is NOT part
// of any dump and has changed over time:
//
//   * CEntityIdentity stride shrank from 0x78 to 0x70 in a Source2 update,
//   * the entity pointer inside CEntityIdentity has lived at both offset 0x0
//     and offset 0x78 depending on the build,
//   * the index->slot convention differs by one between builds
//     (slot = index & 511 vs slot = (index-1) & 511).
//
// Guessing once and silently failing is exactly what produced the
// "Could not locate local player pawn" error while all dumper offsets were
// actually correct. So at startup we probe a small set of known layout
// variants against live memory and lock in the first one that resolves the
// local player controller -> pawn chain.
//
// Everything here is pure memory READS (Memory::Read == ReadProcessMemory).
// Nothing is written to the game process, and the kernel driver is not
// involved at all (it stays read-only / unused by the ESP).
// ---------------------------------------------------------------------------

namespace EntityLayout
{
	struct Params {
		std::ptrdiff_t stride;     // CEntityIdentity slot stride inside a chunk
		std::ptrdiff_t pInstance;  // offset of the entity pointer inside CEntityIdentity
		int bias;                  // slot = (entityIndex - bias) & (ChunkSize-1)
	};

	// Default = the modern Source2 layout (stride 0x70, entity ptr at
	// identity+0, slot = index & 511) as used by current Dota 2 / CS2
	// external readers. Values come from Offsets.h so there is a single
	// source of truth for the defaults.
	inline Params g_params{
		offsets::CGameEntitySystem::IdentityStride,
		offsets::CGameEntitySystem::m_pInstance,
		0
	};
	inline bool g_detected = false;

	inline const Params& Get() { return g_params; }
	inline bool Detected() { return g_detected; }

	namespace detail {
		inline bool PlausiblePtr(uintptr_t p) {
			return p > 0x10000ull && p < 0x00007FFFFFFFFFFFull;
		}

		struct ProbeResult {
			int validPtrs = 0;      // slots whose entity pointer looked valid
			int controllers = 0;    // entities with m_bIsLocalPlayerController == 1
			bool localPawnResolved = false; // controller found AND m_hPawn resolved to a live entity
		};

		// Walk the entity list with one candidate layout. Cheap: one pointer
		// read per slot, plus one byte read only when the pointer is valid,
		// and a handful of reads when a local controller candidate appears.
		inline ProbeResult Probe(const Memory& mem, const std::vector<uintptr_t>& chunks, const Params& p) {
			using namespace offsets;
			ProbeResult r;

			const int chunkSize = CGameEntitySystem::ChunkSize;
			const int maxIndex = (int)chunks.size() * chunkSize;

			for (int i = 1; i <= maxIndex; i++) {
				int adj = i - p.bias;
				if (adj < 0) continue;
				int ci = adj / chunkSize;
				int slot = adj % chunkSize;
				if (ci >= (int)chunks.size() || !chunks[ci]) continue;

				uintptr_t identity = chunks[ci] + p.stride * slot;
				uintptr_t entity = mem.Read<uintptr_t>(identity + p.pInstance);
				if (!PlausiblePtr(entity)) continue;
				r.validPtrs++;

				uint8_t isLocal = mem.Read<uint8_t>(entity + CBasePlayerController::m_bIsLocalPlayerController);
				if (isLocal != 1) continue;
				r.controllers++;

				// Try to resolve the local pawn through m_hPawn with THIS
				// candidate layout. If it lands on a live entity, this layout
				// is consistent end-to-end.
				uint32_t handle = mem.Read<uint32_t>(entity + CBasePlayerController::m_hPawn);
				int pIdx = (int)(handle & 0x7FFF);
				int pAdj = pIdx - p.bias;
				if (pAdj < 0) continue;
				int pci = pAdj / chunkSize;
				int pslot = pAdj % chunkSize;
				if (pci >= (int)chunks.size() || !chunks[pci]) continue;

				uintptr_t pIdentity = chunks[pci] + p.stride * pslot;
				uintptr_t pawn = mem.Read<uintptr_t>(pIdentity + p.pInstance);
				if (!PlausiblePtr(pawn)) continue;

				int pHealth = mem.Read<int>(pawn + C_BaseEntity::m_iHealth);
				uint8_t pTeam = mem.Read<uint8_t>(pawn + C_BaseEntity::m_iTeamNum);
				uint8_t pLife = mem.Read<uint8_t>(pawn + C_BaseEntity::m_lifeState);
				if (pHealth > 0 && (pTeam == 2 || pTeam == 3) && pLife <= 2) {
					r.localPawnResolved = true;
					return r; // full success -- no need to keep scanning
				}
			}
			return r;
		}
	}

	// Probe known layout variants and pick the best one. Returns true when a
	// variant fully resolved the local controller -> pawn chain.
	inline bool Detect(const Memory& mem, uintptr_t entitySystem) {
		using namespace offsets;
		using namespace detail;

		g_detected = false;

		printf("[*] Probing Source2 entity-list layout (read-only)...\n");

		if (!PlausiblePtr(entitySystem)) {
			printf("[!] EntityLayout: dwEntityList did not resolve to a valid CGameEntitySystem pointer -- nothing to probe.\n");
			return false;
		}

		// Read the chunk-pointer array (m_EntityPtrArray at +0x10).
		std::vector<uintptr_t> chunks(16);
		bool anyChunk = false;
		for (int c = 0; c < 16; c++) {
			chunks[c] = mem.Read<uintptr_t>(entitySystem + CGameEntitySystem::m_EntityPtrArray + 8ull * c);
			if (PlausiblePtr(chunks[c])) anyChunk = true;
		}
		if (!anyChunk) {
			printf("[!] EntityLayout: no valid entity-list chunks at CGameEntitySystem+0x%llX -- dwEntityList may be stale.\n",
				(unsigned long long)CGameEntitySystem::m_EntityPtrArray);
			return false;
		}

		// Known variants, most likely first.
		const Params candidates[] = {
			{ 0x70, 0x00, 0 }, // modern Source2 (current Dota 2 / CS2 default)
			{ 0x70, 0x00, 1 },
			{ 0x78, 0x78, 0 }, // pre-0x70-shrink layout
			{ 0x78, 0x78, 1 },
			{ 0x70, 0x78, 0 },
			{ 0x70, 0x78, 1 },
			{ 0x78, 0x00, 0 },
			{ 0x78, 0x00, 1 },
		};

		Params best = candidates[0];
		ProbeResult bestR{};

		for (const auto& c : candidates) {
			ProbeResult r = Probe(mem, chunks, c);
			printf("    stride=0x%llX pInstance=0x%llX bias=%d -> validPtrs=%d controllers=%d localPawn=%s\n",
				(unsigned long long)c.stride, (unsigned long long)c.pInstance, c.bias,
				r.validPtrs, r.controllers, r.localPawnResolved ? "YES" : "no");

			if (r.localPawnResolved) {
				g_params = c;
				g_detected = true;
				printf("[+] Entity layout locked: stride=0x%llX pInstance=0x%llX bias=%d\n",
					(unsigned long long)c.stride, (unsigned long long)c.pInstance, c.bias);
				return true;
			}
			if (r.controllers > bestR.controllers ||
				(r.controllers == bestR.controllers && r.validPtrs > bestR.validPtrs)) {
				best = c;
				bestR = r;
			}
		}

		g_params = best;
		printf("[!] EntityLayout: no variant resolved the local pawn. Using best-effort layout\n");
		printf("    stride=0x%llX pInstance=0x%llX bias=%d (validPtrs=%d controllers=%d).\n",
			(unsigned long long)best.stride, (unsigned long long)best.pInstance, best.bias,
			bestR.validPtrs, bestR.controllers);
		printf("    If this persists, verify you are in a match (not in the main menu) and that\n");
		printf("    the dumper files in output\\ match the running game version.\n");
		return false;
	}
}
