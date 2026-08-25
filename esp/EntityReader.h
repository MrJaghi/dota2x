#pragma once
#include "Memory.h"
#include "VectorMath.h"
#include "Offsets.h"
#include "EntityLayout.h"
#include "Config.h"
#include <vector>
#include <string>
#include <cmath>
#include <cctype>
#include <unordered_map>
#include <utility>

class EntityReader {
public:
	static bool GetEntityOrigin(const Memory& mem, uintptr_t entity, Vector3& out) {
		uintptr_t sceneNode = mem.Read<uintptr_t>(entity + offsets::C_BaseEntity::m_pGameSceneNode);
		if (!sceneNode)
			return false;
		out = mem.Read<Vector3>(sceneNode + offsets::CGameSceneNode::m_vecAbsOrigin);
		return true;
	}

	static std::vector<uintptr_t> ReadChunkPointers(const Memory& mem, uintptr_t entitySystem, int chunkCount) {
		std::vector<uintptr_t> chunks(chunkCount);
		for (int c = 0; c < chunkCount; c++)
			chunks[c] = mem.Read<uintptr_t>(entitySystem + offsets::CGameEntitySystem::m_EntityPtrArray + 8 * c);
		return chunks;
	}

	static uintptr_t GetIdentityFromChunks(const std::vector<uintptr_t>& chunks, int index) {
		const auto& L = EntityLayout::Get();
		int adj = index - L.bias;
		if (adj < 0)
			return 0;
		int chunkIndex = adj / offsets::CGameEntitySystem::ChunkSize;
		int slotInChunk = adj % offsets::CGameEntitySystem::ChunkSize;

		if (chunkIndex < 0 || chunkIndex >= (int)chunks.size() || !chunks[chunkIndex])
			return 0;

		return chunks[chunkIndex] + L.stride * slotInChunk;
	}

	static std::string GetDesignerName(const Memory& mem, uintptr_t identity) {
		uintptr_t namePtr = mem.Read<uintptr_t>(identity + 0x20);
		if (!namePtr)
			return {};
		char buf[64] = {};
		if (!mem.ReadRaw(namePtr, buf, sizeof(buf) - 1))
			return {};
		return std::string(buf);
	}

	static std::string FormatHeroName(const std::string& designerName) {
		std::string raw = designerName.substr(14);
		if (!raw.empty())
			raw[0] = (char)toupper((unsigned char)raw[0]);
		for (auto& c : raw)
			if (c == '_') c = ' ';
		return raw;
	}

	static bool IsValidHeroEntity(const Memory& mem, uintptr_t candidate) {
		if (!candidate)
			return false;
		uint8_t lifeState = mem.Read<uint8_t>(candidate + offsets::C_BaseEntity::m_lifeState);
		uint8_t team = mem.Read<uint8_t>(candidate + offsets::C_BaseEntity::m_iTeamNum);
		int health = mem.Read<int>(candidate + offsets::C_BaseEntity::m_iHealth);
		int maxHealth = mem.Read<int>(candidate + offsets::C_BaseEntity::m_iMaxHealth);
		return lifeState <= 2 && (team == 2 || team == 3) &&
			health > 0 && health <= maxHealth && maxHealth > 0 && maxHealth < 20000;
	}

	static uintptr_t FindLocalPawn(const Memory& mem, const std::vector<uintptr_t>& chunks, int maxIndex) {
		// Walk entity IDs 1..maxIndex. Controllers appear early, pawns appear
		// right after them. We scan controllers first (via the 1-byte
		// m_bIsLocalPlayerController flag) and dereference their m_hPawn
		// handle to resolve the actual local pawn.
		// Index -> slot math (bias, stride, entity-pointer offset) comes from
		// EntityLayout, which is probed against live memory at startup.
		for (int i = 1; i <= maxIndex; i++) {
			uintptr_t identity = GetIdentityFromChunks(chunks, i);
			if (!identity)
				continue;
			uintptr_t entity = mem.Read<uintptr_t>(identity + EntityLayout::Get().pInstance);
			if (!entity)
				continue;

			bool isLocal = mem.Read<uint8_t>(entity + offsets::CBasePlayerController::m_bIsLocalPlayerController) != 0;
			if (!isLocal)
				continue;

			uint32_t handle = mem.Read<uint32_t>(entity + offsets::CBasePlayerController::m_hPawn);
			int pawnIndex0 = (int)(handle & 0x7FFF);
			if (pawnIndex0 <= 0)
				continue;

			uintptr_t pawnIdentity = GetIdentityFromChunks(chunks, pawnIndex0);
			if (!pawnIdentity)
				continue;
			uintptr_t pawn = mem.Read<uintptr_t>(pawnIdentity + EntityLayout::Get().pInstance);
			if (IsValidHeroEntity(mem, pawn))
				return pawn;
		}
		return 0;
	}

	static std::vector<EspTarget> CollectTargets(const Memory& mem, const std::vector<uintptr_t>& chunks,
		int maxScanIndex, uintptr_t localPawn, uint8_t localTeam, const Vector3& localOrigin,
		const ViewMatrix& viewMatrix, int screenWidth, int screenHeight, EspSettings& settings,
		std::unordered_map<uintptr_t, Vector3>& smoothedOrigins) {

		std::vector<EspTarget> targets;
		std::unordered_map<uintptr_t, Vector3> newSmoothed;

		for (int i = 1; i <= maxScanIndex; i++) {
			uintptr_t identity = GetIdentityFromChunks(chunks, i);
			if (!identity)
				continue;
			uintptr_t entity = mem.Read<uintptr_t>(identity + EntityLayout::Get().pInstance);
			if (!entity || entity == localPawn)
				continue;

			std::string designerName = GetDesignerName(mem, identity);
			if (designerName.compare(0, 14, "npc_dota_hero_") != 0)
				continue;

			uint8_t lifeState = mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_lifeState);
			if (lifeState != 0)
				continue;

			int health = mem.Read<int>(entity + offsets::C_BaseEntity::m_iHealth);
			int maxHealth = mem.Read<int>(entity + offsets::C_BaseEntity::m_iMaxHealth);
			if (health <= 0 || maxHealth <= 0)
				continue;

			uint8_t team = mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_iTeamNum);

			float mana = mem.Read<float>(entity + offsets::C_DOTA_BaseNPC::m_flMana);
			float maxMana = mem.Read<float>(entity + offsets::C_DOTA_BaseNPC::m_flMaxMana);
			int level = mem.Read<int>(entity + offsets::C_DOTA_BaseNPC::m_iCurrentLevel);
			bool isIllusion = mem.Read<uint8_t>(entity + offsets::C_DOTA_BaseNPC::m_bIsIllusion) != 0;

			Vector3 origin{};
			if (!GetEntityOrigin(mem, entity, origin))
				continue;

			auto it = smoothedOrigins.find(entity);
			Vector3 smooth = origin;
			if (it != smoothedOrigins.end()) {
				float jumpDx = origin.x - it->second.x;
				float jumpDy = origin.y - it->second.y;
				float jumpDist = sqrtf(jumpDx * jumpDx + jumpDy * jumpDy);
				constexpr float kTeleportThreshold = 250.0f;

				if (jumpDist < kTeleportThreshold) {
					float a = 1.0f - settings.smoothing;
					smooth.x = it->second.x + (origin.x - it->second.x) * a;
					smooth.y = it->second.y + (origin.y - it->second.y) * a;
					smooth.z = it->second.z + (origin.z - it->second.z) * a;
				}
			}
			newSmoothed[entity] = smooth;

			Vector3 feet = smooth;
			Vector3 head = smooth;
			head.z += 90.0f;

			Vector2 screenFeet{}, screenHead{};
			if (!WorldToScreen(feet, screenFeet, viewMatrix, screenWidth, screenHeight))
				continue;
			if (!WorldToScreen(head, screenHead, viewMatrix, screenWidth, screenHeight))
				continue;

			float boxHeight = screenFeet.y - screenHead.y;
			if (boxHeight <= 0)
				continue;
			float boxWidth = boxHeight * 0.55f;

			float dx = origin.x - localOrigin.x;
			float dy = origin.y - localOrigin.y;

			EspTarget t;
			t.id = entity;
			t.x = screenHead.x - boxWidth / 2.0f;
			t.y = screenHead.y;
			t.w = boxWidth;
			t.h = boxHeight;
			t.health = health;
			t.maxHealth = maxHealth;
			t.mana = (int)mana;
			t.maxMana = (int)(maxMana > 0 ? maxMana : 100.0f);
			t.level = (level > 0 && level <= 30) ? level : 1;
			t.distance = sqrtf(dx * dx + dy * dy);
			t.enemy = (team != localTeam);
			t.isIllusion = isIllusion;
			t.name = FormatHeroName(designerName);

			targets.push_back(t);
		}

		smoothedOrigins = std::move(newSmoothed);
		return targets;
	}

	static std::vector<CreepTarget> CollectCreepTargets(const Memory& mem, const std::vector<uintptr_t>& chunks,
		int maxScanIndex, uintptr_t localPawn, uint8_t localTeam, const Vector3& localOrigin,
		const ViewMatrix& viewMatrix, int screenWidth, int screenHeight, float heroDamage = 68.0f,
		float attackPoint = 0.3f, float projectileSpeed = 1100.0f) {

		std::vector<CreepTarget> creeps;

		for (int i = 1; i <= maxScanIndex; i++) {
			uintptr_t identity = GetIdentityFromChunks(chunks, i);
			if (!identity)
				continue;
			uintptr_t entity = mem.Read<uintptr_t>(identity + EntityLayout::Get().pInstance);
			if (!entity || entity == localPawn)
				continue;

			std::string designerName = GetDesignerName(mem, identity);
			if (designerName.find("creep") == std::string::npos && designerName.find("building") == std::string::npos)
				continue;

			uint8_t lifeState = mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_lifeState);
			if (lifeState != 0)
				continue;

			int health = mem.Read<int>(entity + offsets::C_BaseEntity::m_iHealth);
			int maxHealth = mem.Read<int>(entity + offsets::C_BaseEntity::m_iMaxHealth);
			if (health <= 0 || maxHealth <= 0)
				continue;

			uint8_t team = mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_iTeamNum);
			if (team == localTeam)
				continue;

			Vector3 origin{};
			if (!GetEntityOrigin(mem, entity, origin))
				continue;

			Vector3 feet = origin;
			Vector3 head = origin;
			head.z += 60.0f;

			Vector2 screenFeet{}, screenHead{};
			if (!WorldToScreen(feet, screenFeet, viewMatrix, screenWidth, screenHeight))
				continue;
			if (!WorldToScreen(head, screenHead, viewMatrix, screenWidth, screenHeight))
				continue;

			float boxHeight = screenFeet.y - screenHead.y;
			if (boxHeight <= 0)
				continue;
			float boxWidth = boxHeight * 0.6f;

			float dx = origin.x - localOrigin.x;
			float dy = origin.y - localOrigin.y;
			float dist = sqrtf(dx * dx + dy * dy);

			float flightTime = (projectileSpeed > 0.0f) ? (dist / projectileSpeed) : 0.0f;
			float totalDelay = attackPoint + flightTime;

			bool killableNow = (health <= heroDamage);
			bool killableSoon = (!killableNow && (health <= (heroDamage + 40.0f)));

			CreepTarget c;
			c.id = entity;
			c.x = screenHead.x - boxWidth / 2.0f;
			c.y = screenHead.y;
			c.w = boxWidth;
			c.h = boxHeight;
			c.health = health;
			c.maxHealth = maxHealth;
			c.distance = dist;
			c.isKillableNow = killableNow;
			c.isKillableSoon = killableSoon;
			c.timeToKillable = totalDelay;
			c.name = designerName;

			creeps.push_back(c);
		}

		return creeps;
	}
};
