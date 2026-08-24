// Sample schema override for DragonBurn ExternalESP.
//
// This file mirrors the per-module SCHEMA section of Anroshka/dota2-dumper
// output (output/client_dll.hpp). It lists the netvar / member offsets the
// ESP actually reads from client.dll.
//
// After a game update, overwrite this file with the real dumper output (or
// drop the entire dumper output\ folder verbatim into .\output\). The loader
// reads every *.hpp in output\ at startup so the full 2 MB client_dll.hpp
// with all 7000+ classes will just work -- we silently ignore classes the
// ESP doesn't touch.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dota2_dumper {
    namespace schemas {
        // Module: client.dll
        namespace client_dll {

            // Parent: None
            // Field count: 7
            namespace C_BaseEntity {
                constexpr std::ptrdiff_t m_CBodyComponent  = 0x030; // CBodyComponent*
                constexpr std::ptrdiff_t m_iMaxHealth      = 0x348; // int32
                constexpr std::ptrdiff_t m_iHealth         = 0x34C; // int32
                constexpr std::ptrdiff_t m_lifeState       = 0x354; // uint8
                constexpr std::ptrdiff_t m_iTeamNum        = 0x3E7; // uint8
                constexpr std::ptrdiff_t m_pGameSceneNode  = 0x330; // CGameSceneNode*
                constexpr std::ptrdiff_t m_hOwnerEntity    = 0x514; // CHandle<C_BaseEntity>
            }

            // Parent: C_BaseModelEntity
            namespace CGameSceneNode {
                constexpr std::ptrdiff_t m_vecAbsOrigin    = 0xD8;  // Vector
            }

            // Parent: C_BaseCombatCharacter
            namespace C_DOTA_BaseNPC {
                constexpr std::ptrdiff_t m_iCurrentLevel   = 0xBAC; // int32
                constexpr std::ptrdiff_t m_flMana          = 0xC04; // float32
                constexpr std::ptrdiff_t m_flMaxMana       = 0xC08; // float32
                constexpr std::ptrdiff_t m_bIsIllusion     = 0xC2C; // bool
                constexpr std::ptrdiff_t m_vecAbilities    = 0xC30; // CUtlVector<...>
                constexpr std::ptrdiff_t m_iDamageMin      = 0xD20; // int32
                constexpr std::ptrdiff_t m_iDamageMax      = 0xD24; // int32
            }

            // Parent: CBaseController
            namespace CBasePlayerController {
                constexpr std::ptrdiff_t m_hPawn                    = 0x6A4; // CHandle<C_BaseEntity>
                constexpr std::ptrdiff_t m_bIsLocalPlayerController = 0x770; // bool
            }

        }
    }
}
