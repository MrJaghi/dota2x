// Базовый набор оффсетов для ESP по игрокам (Dota 2, Source 2)
// Собрано из https://github.com/ikhsanprasetyo/dota2dumped (актуально на 2026-08-16)
// Проверяй актуальность после каждого патча Dota 2 - структуры/оффсеты могут сместиться

#pragma once
#include <cstdint>

namespace offsets
{
	// client.dll модуль
	namespace client_dll
	{
		constexpr std::ptrdiff_t dwEntityList          = 0x652EA80; // он же dwGameEntitySystem
		constexpr std::ptrdiff_t dwGameEntitySystem_highestEntityIndex = 0x2090;
		constexpr std::ptrdiff_t dwLocalPlayerPawn      = 0x5A86188;
		constexpr std::ptrdiff_t dwGlobalVars           = 0x5A7D200;
		constexpr std::ptrdiff_t dwViewMatrix           = 0x61B5EA0;
	}

	// C_BaseEntity (общий для всех сущностей, включая героев/крипов)
	namespace C_BaseEntity
	{
		constexpr std::ptrdiff_t m_CBodyComponent = 0x30;  // CBodyComponent*
		constexpr std::ptrdiff_t m_iMaxHealth     = 0x348; // int32
		constexpr std::ptrdiff_t m_iHealth        = 0x34C; // int32
		constexpr std::ptrdiff_t m_lifeState      = 0x354; // uint8 (0 = жив)
		constexpr std::ptrdiff_t m_iTeamNum       = 0x3E7; // uint8
	}

	// позиция достаётся через m_pGameSceneNode (C_BaseEntity+0x330) -> CGameSceneNode
	namespace CGameSceneNode
	{
		constexpr std::ptrdiff_t m_pGameSceneNode = 0x330; // CGameSceneNode* (из C_BaseEntity)
		constexpr std::ptrdiff_t m_vecAbsOrigin    = 0xD8;  // VectorWS - абсолютная мировая позиция (то, что нужно для ESP)
	}
}

/*
Базовая логика чтения позиции игрока для ESP (внешний чит, через ReadProcessMemory):

uintptr_t entityList = Read<uintptr_t>(clientBase + offsets::client_dll::dwEntityList);
uintptr_t localPawn  = Read<uintptr_t>(clientBase + offsets::client_dll::dwLocalPlayerPawn);

// перебор сущностей по entityList (структура списка сущностей Source2 - двухуровневая, см. dwGameEntitySystem)
uintptr_t entity = ...; // конкретная сущность (герой)

int health = Read<int>(entity + offsets::C_BaseEntity::m_iHealth);
int team   = Read<uint8_t>(entity + offsets::C_BaseEntity::m_iTeamNum);
uintptr_t sceneNode = Read<uintptr_t>(entity + offsets::CGameSceneNode::m_pGameSceneNode);
Vector origin = Read<Vector>(sceneNode + offsets::CGameSceneNode::m_vecAbsOrigin);

// далее world-to-screen через dwViewMatrix для отрисовки ESP-бокса/имени
*/
