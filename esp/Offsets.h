#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// Compile-time HARDCODED offsets.
//
// The two headers included below ARE the dumper output living in the repo's
// output\ folder. They are pulled in VERBATIM -- byte-for-byte unchanged,
// exactly as the dumper (https://github.com/a2x/dota2-dumper) emitted them.
// Nothing is parsed or patched at runtime anymore: the values are baked into
// ExternalESP.exe at compile time.
//
// To update offsets after a Dota 2 patch:
//   1. Re-run the dumper and drop its files into output\ (overwrite).
//   2. Rebuild (bat\build_all.bat) -- that's it.
//
// $(ProjectDir)output is on the include path (see ExternalESP.vcxproj and
// esp/CMakeLists.txt), so these plain includes resolve straight to
// output\offsets.hpp / output\client_dll.hpp without touching those files.
// ---------------------------------------------------------------------------

#include "offsets.hpp"      // -> output\offsets.hpp    (dota2_dumper::offsets::*)
#include "client_dll.hpp"   // -> output\client_dll.hpp (dota2_dumper::schemas::client_dll::*)

namespace offsets
{
	// client.dll globals (from output\offsets.hpp)
	namespace client_dll {
		constexpr std::ptrdiff_t dwEntityList = dota2_dumper::offsets::client_dll::dwEntityList;
		constexpr std::ptrdiff_t dwViewMatrix = dota2_dumper::offsets::client_dll::dwViewMatrix;
		constexpr std::ptrdiff_t dwGlobalVars = dota2_dumper::offsets::client_dll::dwGlobalVars;
	}

	// Entity-system layout. These are stable Source 2 engine constants that
	// no dumper emits, so they stay hardcoded here (they don't move between
	// patches). ChunkSize is always 512 slots per entity-list chunk.
	namespace CGameEntitySystem {
		constexpr std::ptrdiff_t m_EntityPtrArray = 0x10;
		constexpr int           ChunkSize         = 512;
		constexpr std::ptrdiff_t IdentityStride   = 0x70;
		constexpr std::ptrdiff_t m_pInstance      = 0x0;
	}

	// Schemas (from output\client_dll.hpp)
	namespace C_BaseEntity {
		constexpr std::ptrdiff_t m_CBodyComponent = dota2_dumper::schemas::client_dll::C_BaseEntity::m_CBodyComponent;
		constexpr std::ptrdiff_t m_iMaxHealth     = dota2_dumper::schemas::client_dll::C_BaseEntity::m_iMaxHealth;
		constexpr std::ptrdiff_t m_iHealth        = dota2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth;
		constexpr std::ptrdiff_t m_lifeState      = dota2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState;
		constexpr std::ptrdiff_t m_iTeamNum       = dota2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum;
		constexpr std::ptrdiff_t m_pGameSceneNode = dota2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode;
		constexpr std::ptrdiff_t m_hOwnerEntity   = dota2_dumper::schemas::client_dll::C_BaseEntity::m_hOwnerEntity;
		constexpr std::ptrdiff_t m_pEntity        = dota2_dumper::schemas::client_dll::CEntityInstance::m_pEntity;
	}

	namespace CGameSceneNode {
		constexpr std::ptrdiff_t m_vecAbsOrigin = dota2_dumper::schemas::client_dll::CGameSceneNode::m_vecAbsOrigin;
	}

	namespace C_DOTA_BaseNPC {
		constexpr std::ptrdiff_t m_iCurrentLevel     = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_iCurrentLevel;
		constexpr std::ptrdiff_t m_flMana            = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_flMana;
		constexpr std::ptrdiff_t m_flMaxMana         = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_flMaxMana;
		constexpr std::ptrdiff_t m_bIsIllusion       = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_bIsIllusion;
		constexpr std::ptrdiff_t m_vecAbilities      = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_vecAbilities;
		constexpr std::ptrdiff_t m_iDamageMin        = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_iDamageMin;
		constexpr std::ptrdiff_t m_iDamageMax        = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_iDamageMax;
		constexpr std::ptrdiff_t m_iAttackRange      = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_iAttackRange;
		constexpr std::ptrdiff_t m_flInvisibilityLevel = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_flInvisibilityLevel;
		constexpr std::ptrdiff_t m_Inventory         = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_Inventory;
		constexpr std::ptrdiff_t m_NetworkActivity   = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_NetworkActivity;
		constexpr std::ptrdiff_t m_bIsNeutralUnitType = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_bIsNeutralUnitType;

		// --- last-hit engine fields (all read-only) ---
		constexpr std::ptrdiff_t m_flPhysicalArmorValue = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_flPhysicalArmorValue;
		constexpr std::ptrdiff_t m_iAttackCapabilities  = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_iAttackCapabilities;
		constexpr std::ptrdiff_t m_flBaseAttackTime     = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_flBaseAttackTime;
		constexpr std::ptrdiff_t m_flHullRadius         = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_flHullRadius;
		constexpr std::ptrdiff_t m_flLastAttackTime     = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_flLastAttackTime;
		constexpr std::ptrdiff_t m_flLastDamageTime     = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_flLastDamageTime;
		constexpr std::ptrdiff_t m_flDeathTime          = dota2_dumper::schemas::client_dll::C_DOTA_BaseNPC::m_flDeathTime;
	}

	namespace C_DOTA_UnitInventory {
		constexpr std::ptrdiff_t m_hItems = dota2_dumper::schemas::client_dll::C_DOTA_UnitInventory::m_hItems;
	}

	namespace CBasePlayerController {
		constexpr std::ptrdiff_t m_hPawn                    = dota2_dumper::schemas::client_dll::CBasePlayerController::m_hPawn;
		constexpr std::ptrdiff_t m_bIsLocalPlayerController = dota2_dumper::schemas::client_dll::CBasePlayerController::m_bIsLocalPlayerController;
		constexpr std::ptrdiff_t m_iszPlayerName            = dota2_dumper::schemas::client_dll::CBasePlayerController::m_iszPlayerName;
	}

	// Runtime-adjustable offsets. The validator may auto-tune these against
	// live memory (READ-ONLY: it only scans nearby candidate offsets and picks
	// the one that reads plausible values -- nothing is ever written).
	namespace runtime {
		inline std::ptrdiff_t m_iCurrentLevel = C_DOTA_BaseNPC::m_iCurrentLevel;
	}
}
