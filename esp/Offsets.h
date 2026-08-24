#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// Runtime-loaded offsets (mutable — overwritten at startup from .hpp files).
//
// Defaults below are hardcoded fall-backs (last known good). At startup
// OffsetLoader::LoadFromFile() reads every *.hpp file in <exe_dir>\output\
// (offsets.hpp, client_dll.hpp, …) in the format emitted by Anroshka/dota2-dumper
//   https://github.com/Anroshka/dota2-dumper
// and overwrites any matching key. Drop the dumper's output\ folder contents
// verbatim into .\output\ after each game update — no recompilation needed.
// ---------------------------------------------------------------------------

namespace offsets
{
	// client.dll globals (offsets.hpp)
	namespace client_dll {
		inline std::ptrdiff_t dwEntityList          = 0x652E8F0;
		inline std::ptrdiff_t dwLocalPlayerPawnBase = 0x0;      // not in Anroshka's dump; derived via controller
		inline std::ptrdiff_t dwViewMatrix          = 0x61B5D20;
	}

	// Entity system layout (stable engine constants — Source2 always uses 512
	// slots per chunk, identity stride is a struct size that rarely changes).
	// These are inline mutable so future dumper fields can override them at runtime.
	namespace CGameEntitySystem {
		inline std::ptrdiff_t m_EntityPtrArray = 0x10;
		inline int           ChunkSize         = 512;
		inline std::ptrdiff_t IdentityStride   = 0x70;
		inline std::ptrdiff_t m_pInstance      = 0x0;
	}

	// Schemas (client_dll.hpp)
	namespace C_BaseEntity {
		inline std::ptrdiff_t m_CBodyComponent  = 0x30;
		inline std::ptrdiff_t m_iMaxHealth      = 0x348;
		inline std::ptrdiff_t m_iHealth         = 0x34C;
		inline std::ptrdiff_t m_lifeState       = 0x354;
		inline std::ptrdiff_t m_iTeamNum        = 0x3E7;
		inline std::ptrdiff_t m_pGameSceneNode  = 0x330;
		inline std::ptrdiff_t m_hOwnerEntity    = 0x514;
	}

	namespace CGameSceneNode {
		inline std::ptrdiff_t m_vecAbsOrigin = 0xD8;
	}

	namespace C_DOTA_BaseNPC {
		inline std::ptrdiff_t m_iCurrentLevel = 0xBAC;
		inline std::ptrdiff_t m_flMana        = 0xC04;
		inline std::ptrdiff_t m_flMaxMana     = 0xC08;
		inline std::ptrdiff_t m_bIsIllusion   = 0xC2C;
		inline std::ptrdiff_t m_vecAbilities  = 0xC30;
		inline std::ptrdiff_t m_iDamageMin    = 0xD20;
		inline std::ptrdiff_t m_iDamageMax    = 0xD24;
	}

	namespace CBasePlayerController {
		inline std::ptrdiff_t m_hPawn                    = 0x6A4;
		inline std::ptrdiff_t m_bIsLocalPlayerController = 0x770;
	}
}
