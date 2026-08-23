#pragma once
#include <cstdint>

namespace offsets
{
	namespace client_dll {
		constexpr std::ptrdiff_t dwEntityList = 0x652EA80;
		constexpr std::ptrdiff_t dwLocalPlayerPawnBase = 0x5A86188;
		constexpr std::ptrdiff_t dwViewMatrix = 0x61B5EA0;
	}

	namespace CGameEntitySystem {
		constexpr std::ptrdiff_t m_EntityPtrArray = 0x10;
		constexpr int ChunkSize = 512;
		constexpr int IdentityStride = 0x70;
		constexpr std::ptrdiff_t m_pInstance = 0x0;
	}

	namespace C_BaseEntity {
		constexpr std::ptrdiff_t m_CBodyComponent = 0x30;
		constexpr std::ptrdiff_t m_iMaxHealth = 0x348;
		constexpr std::ptrdiff_t m_iHealth = 0x34C;
		constexpr std::ptrdiff_t m_lifeState = 0x354;
		constexpr std::ptrdiff_t m_iTeamNum = 0x3E7;
		constexpr std::ptrdiff_t m_pGameSceneNode = 0x330;
		constexpr std::ptrdiff_t m_hOwnerEntity = 0x514;
	}

	namespace CGameSceneNode {
		constexpr std::ptrdiff_t m_vecAbsOrigin = 0xD8;
	}

	namespace C_DOTA_BaseNPC {
		constexpr std::ptrdiff_t m_iCurrentLevel = 0xBAC;
		constexpr std::ptrdiff_t m_flMana = 0xC04;
		constexpr std::ptrdiff_t m_flMaxMana = 0xC08;
		constexpr std::ptrdiff_t m_bIsIllusion = 0xC2C;
		constexpr std::ptrdiff_t m_vecAbilities = 0xC30;
		constexpr std::ptrdiff_t m_iDamageMin = 0xD20;
		constexpr std::ptrdiff_t m_iDamageMax = 0xD24;
	}

	namespace C_DOTABaseAbility {
		constexpr std::ptrdiff_t m_bHidden = 0x617;
		constexpr std::ptrdiff_t m_iLevel = 0x628;
		constexpr std::ptrdiff_t m_fCooldown = 0x638;
		constexpr std::ptrdiff_t m_flCooldownLength = 0x63C;
		constexpr std::ptrdiff_t m_iManaCost = 0x640;
		constexpr std::ptrdiff_t m_bInAbilityPhase = 0x634;
	}
}
