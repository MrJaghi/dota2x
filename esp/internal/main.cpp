
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <vector>
#include <string>
#include <cmath>
#include <unordered_map>
#include <functional>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "../Offsets.h"
#include "../Math.h"
#include "MemoryInternal.h"
#include "Hooks.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static HWND g_gameWnd = nullptr;
static WNDPROC g_originalWndProc = nullptr;
static bool g_imguiInited = false;
static bool g_menuOpen = false;
static HMODULE g_hModule = nullptr;

static MemoryInternal g_mem;

static ImFont* g_fontRegular = nullptr;
static ImFont* g_fontHeading = nullptr;

namespace Icon
{
	constexpr const char* Eye = "\xEF\x81\xAE";
	constexpr const char* Crosshair = "\xEF\x81\x9B";
	constexpr const char* Gear = "\xEF\x80\x93";
	constexpr const char* Bolt = "\xEF\x83\xA7";
	constexpr const char* Shield = "\xEF\x84\xB2";
}

static std::string GetModuleDirectory()
{
	char path[MAX_PATH]{};
	GetModuleFileNameA(g_hModule, path, MAX_PATH);
	std::string p(path);
	return p.substr(0, p.find_last_of("\\/") + 1);
}

static void LoadFonts()
{
	ImGuiIO& io = ImGui::GetIO();
	std::string dir = GetModuleDirectory();
	std::string regularPath = dir + "assets\\fonts\\Inter.ttf";
	std::string iconPath = dir + "assets\\fonts\\fa-solid.ttf";

	ImFontConfig cfg;
	cfg.OversampleH = 3;
	cfg.OversampleV = 3;

	g_fontRegular = io.Fonts->AddFontFromFileTTF(regularPath.c_str(), 16.0f, &cfg);
	if (g_fontRegular)
	{
		static const ImWchar iconRanges[] = { 0xE000, 0xF8FF, 0 };
		ImFontConfig iconCfg;
		iconCfg.MergeMode = true;
		iconCfg.PixelSnapH = true;
		iconCfg.GlyphMinAdvanceX = 16.0f;
		io.Fonts->AddFontFromFileTTF(iconPath.c_str(), 15.0f, &iconCfg, iconRanges);
	}

	g_fontHeading = io.Fonts->AddFontFromFileTTF(regularPath.c_str(), 21.0f, &cfg);

	if (!g_fontRegular) g_fontRegular = io.FontDefault;
	if (!g_fontHeading) g_fontHeading = io.FontDefault;
}

struct EspSettings
{
	bool enabled = true;
	bool showEnemies = true;
	bool showAllies = true;
	bool showHealthBar = true;
	bool showNames = true;
	bool showDistance = true;
	bool cornerStyle = true;
	bool dotaStylePanel = true;
	float boxThickness = 2.2f;
	float smoothing = 0.35f;
};

struct AbilityInfo
{
	std::string shortName;
	std::string fullName;
	int level = 0;
	float cooldown = 0;
	float cooldownMax = 0;
	bool inAbilityPhase = false;
};

struct EspTarget
{
	uintptr_t id = 0;
	float x = 0, y = 0, w = 0, h = 0;
	int health = 0, maxHealth = 0;
	int level = 0;
	float mana = 0, maxMana = 0;
	float distance = 0;
	std::string name;
	bool enemy = false;
	std::vector<AbilityInfo> abilities;
};

static EspSettings g_esp;
static std::unordered_map<uintptr_t, Vector3> g_smoothedOrigins;
static bool g_debugNetWorthScan = false;

namespace offsets { namespace CBasePlayerController {
	constexpr std::ptrdiff_t m_hPawn = 0x6A4;
	constexpr std::ptrdiff_t m_bIsLocalPlayerController = 0x770;
} }

static bool GetEntityOrigin(const MemoryInternal& mem, uintptr_t entity, Vector3& out)
{
	uintptr_t sceneNode = mem.Read<uintptr_t>(entity + offsets::C_BaseEntity::m_pGameSceneNode);
	if (!sceneNode)
		return false;
	out = mem.Read<Vector3>(sceneNode + offsets::CGameSceneNode::m_vecAbsOrigin);
	return true;
}

static std::vector<uintptr_t> ReadChunkPointers(const MemoryInternal& mem, uintptr_t entitySystem, int chunkCount)
{
	std::vector<uintptr_t> chunks(chunkCount);
	for (int c = 0; c < chunkCount; c++)
		chunks[c] = mem.Read<uintptr_t>(entitySystem + offsets::CGameEntitySystem::m_EntityPtrArray + 8 * c);
	return chunks;
}

static uintptr_t GetIdentityFromChunks(const std::vector<uintptr_t>& chunks, int index)
{
	int zeroBased = index - 1;
	int chunkIndex = zeroBased / offsets::CGameEntitySystem::ChunkSize;
	int slotInChunk = zeroBased % offsets::CGameEntitySystem::ChunkSize;
	if (chunkIndex < 0 || chunkIndex >= (int)chunks.size() || !chunks[chunkIndex])
		return 0;
	return chunks[chunkIndex] + offsets::CGameEntitySystem::IdentityStride * slotInChunk;
}

static std::string ReadSymbolString(const MemoryInternal& mem, uintptr_t symbolAddr)
{
	uintptr_t strPtr = mem.Read<uintptr_t>(symbolAddr);
	if (!strPtr)
		return {};
	char buf[64] = {};
	if (!mem.ReadRaw(strPtr, buf, sizeof(buf) - 1))
		return {};
	return std::string(buf);
}

static std::string GetDesignerName(const MemoryInternal& mem, uintptr_t identity)
{
	std::string name = ReadSymbolString(mem, identity + 0x20);
	if (!name.empty())
		return name;
	return ReadSymbolString(mem, identity + 0x18);
}

static std::string FormatHeroName(const std::string& designerName)
{
	std::string raw = designerName.substr(14);
	if (!raw.empty())
		raw[0] = (char)toupper((unsigned char)raw[0]);
	for (auto& c : raw)
		if (c == '_') c = ' ';
	return raw;
}

static bool LooksLikeValidHeroEntity(const MemoryInternal& mem, uintptr_t candidate)
{
	if (!candidate)
		return false;
	uint8_t lifeState = mem.Read<uint8_t>(candidate + offsets::C_BaseEntity::m_lifeState);
	uint8_t team = mem.Read<uint8_t>(candidate + offsets::C_BaseEntity::m_iTeamNum);
	int health = mem.Read<int>(candidate + offsets::C_BaseEntity::m_iHealth);
	int maxHealth = mem.Read<int>(candidate + offsets::C_BaseEntity::m_iMaxHealth);
	return lifeState <= 2 && (team == 2 || team == 3) &&
		health > 0 && health <= maxHealth && maxHealth > 0 && maxHealth < 20000;
}

static std::string FormatAbilityShortName(const std::string& designerName)
{
	size_t lastUnderscore = designerName.find_last_of('_');
	std::string raw = (lastUnderscore == std::string::npos) ? designerName : designerName.substr(lastUnderscore + 1);
	if (raw.size() > 5)
		raw = raw.substr(0, 5);
	for (auto& c : raw)
		c = (char)toupper((unsigned char)c);
	return raw;
}

static std::vector<AbilityInfo> GetHeroAbilities(const MemoryInternal& mem, uintptr_t heroEntity,
	const std::vector<uintptr_t>& chunks, int maxIndex)
{
	std::vector<AbilityInfo> result;

	int32_t count = mem.Read<int32_t>(heroEntity + offsets::C_DOTA_BaseNPC::m_vecAbilities);
	uintptr_t dataPtr = mem.Read<uintptr_t>(heroEntity + offsets::C_DOTA_BaseNPC::m_vecAbilities + 0x8);
	if (!dataPtr || count <= 0 || count > 64)
		return result;

	for (int i = 0; i < count && (int)result.size() < 6; i++)
	{
		uint32_t handle = mem.Read<uint32_t>(dataPtr + i * 4);
		if (handle == 0 || handle == 0xFFFFFFFF)
			continue;

		int abilityIndex0 = (int)(handle & 0x7FFF);
		if (abilityIndex0 <= 0)
			continue;

		uintptr_t identity = GetIdentityFromChunks(chunks, abilityIndex0 + 1);
		if (!identity)
			continue;
		uintptr_t ability = mem.Read<uintptr_t>(identity + offsets::CGameEntitySystem::m_pInstance);
		if (!ability)
			continue;

		bool hidden = mem.Read<bool>(ability + offsets::C_DOTABaseAbility::m_bHidden);
		int level = mem.Read<int>(ability + offsets::C_DOTABaseAbility::m_iLevel);
		if (hidden || level <= 0)
			continue;

		std::string designerName = GetDesignerName(mem, identity);
		if (designerName.empty())
			continue;

		AbilityInfo info;
		info.shortName = FormatAbilityShortName(designerName);
		info.fullName = designerName;
		info.level = level;
		info.cooldown = mem.Read<float>(ability + offsets::C_DOTABaseAbility::m_fCooldown);
		info.cooldownMax = mem.Read<float>(ability + offsets::C_DOTABaseAbility::m_flCooldownLength);
		info.inAbilityPhase = mem.Read<bool>(ability + offsets::C_DOTABaseAbility::m_bInAbilityPhase);

		result.push_back(info);
	}

	return result;
}

static float FindLocalMantaCooldown(const MemoryInternal& mem, const std::vector<uintptr_t>& chunks, int maxIndex, uintptr_t localPawn)
{
	for (int i = 1; i <= maxIndex; i++)
	{
		uintptr_t identity = GetIdentityFromChunks(chunks, i);
		if (!identity)
			continue;
		uintptr_t entity = mem.Read<uintptr_t>(identity + offsets::CGameEntitySystem::m_pInstance);
		if (!entity)
			continue;

		std::string designerName = GetDesignerName(mem, identity);
		if (designerName != "item_manta")
			continue;

		uint32_t ownerHandle = mem.Read<uint32_t>(entity + offsets::C_BaseEntity::m_hOwnerEntity);
		int ownerIndex0 = (int)(ownerHandle & 0x7FFF);
		if (ownerIndex0 <= 0)
			continue;

		uintptr_t ownerIdentity = GetIdentityFromChunks(chunks, ownerIndex0 + 1);
		if (!ownerIdentity)
			continue;
		uintptr_t owner = mem.Read<uintptr_t>(ownerIdentity + offsets::CGameEntitySystem::m_pInstance);
		if (owner != localPawn)
			continue;

		return mem.Read<float>(entity + offsets::C_DOTABaseAbility::m_fCooldown);
	}
	return -1.0f;
}

constexpr float kLagunaBladeCastPoint = 0.4f;

struct DodgeHelperState
{
	bool enabled = false;

	bool tracking = false;
	float castStartTime = 0.0f;
	int healthAtCastStart = 0;

	bool resultShown = false;
	bool resultDodged = false;
	float resultUntil = 0.0f;

	float mantaCooldown = -1.0f;
};
static DodgeHelperState g_dodge;

static void UpdateDodgeHelper(const MemoryInternal& mem, const std::vector<EspTarget>& targets,
	const std::vector<uintptr_t>& chunks, int maxIndex, uintptr_t localPawn)
{
	float now = (float)ImGui::GetTime();

	g_dodge.mantaCooldown = FindLocalMantaCooldown(mem, chunks, maxIndex, localPawn);

	bool castInProgress = false;
	for (const auto& t : targets)
	{
		if (!t.enemy)
			continue;
		for (const auto& a : t.abilities)
		{
			if (a.fullName == "lina_laguna_blade" && a.inAbilityPhase)
			{
				castInProgress = true;
				break;
			}
		}
		if (castInProgress)
			break;
	}

	if (castInProgress && !g_dodge.tracking)
	{
		g_dodge.tracking = true;
		g_dodge.castStartTime = now;
		g_dodge.healthAtCastStart = mem.Read<int>(localPawn + offsets::C_BaseEntity::m_iHealth);
		g_dodge.resultShown = false;
	}
	else if (!castInProgress && g_dodge.tracking)
	{
		g_dodge.tracking = false;
		int healthNow = mem.Read<int>(localPawn + offsets::C_BaseEntity::m_iHealth);
		int drop = g_dodge.healthAtCastStart - healthNow;
		g_dodge.resultDodged = drop < 150;
		g_dodge.resultShown = true;
		g_dodge.resultUntil = now + 2.5f;
	}
	else if (g_dodge.tracking && now - g_dodge.castStartTime > 3.0f)
	{
		g_dodge.tracking = false;
	}
}

static void DrawDodgeIndicator(int width, int height)
{
	if (!g_dodge.enabled)
		return;

	float now = (float)ImGui::GetTime();
	ImDrawList* draw = ImGui::GetForegroundDrawList();

	if (g_dodge.tracking)
	{
		float impactIn = (g_dodge.castStartTime + kLagunaBladeCastPoint) - now;
		bool mantaReady = g_dodge.mantaCooldown >= 0.0f && g_dodge.mantaCooldown < 0.05f;

		char buf[64];
		if (impactIn > 0)
			snprintf(buf, sizeof(buf), "LAGUNA BLADE - %dms", (int)(impactIn * 1000.0f));
		else
			snprintf(buf, sizeof(buf), "LAGUNA BLADE - NOW");

		float pulse = 0.5f + 0.5f * sinf(now * 14.0f);
		ImU32 col = mantaReady
			? IM_COL32(255, (int)(60 + pulse * 100), 40, 255)
			: IM_COL32(150, 150, 150, 255);

		ImGui::PushFont(g_fontHeading);
		ImVec2 size = ImGui::CalcTextSize(buf);
		ImGui::PopFont();
		float cx = width / 2.0f - size.x * 1.3f / 2.0f;
		float cy = height * 0.28f;

		draw->AddRectFilled(ImVec2(cx - 14, cy - 8), ImVec2(cx + size.x * 1.3f + 14, cy + size.y * 1.3f + 8), IM_COL32(10, 10, 12, 190), 8.0f);
		draw->AddText(g_fontHeading, g_fontHeading ? 21.0f * 1.3f : 24.0f, ImVec2(cx, cy), col, buf);

		if (!mantaReady)
		{
			const char* warn = g_dodge.mantaCooldown < 0 ? "no Manta in inventory" : "Manta on cooldown";
			ImVec2 warnSize = ImGui::CalcTextSize(warn);
			draw->AddText(ImVec2(width / 2.0f - warnSize.x / 2.0f, cy + size.y * 1.3f + 12), IM_COL32(220, 90, 90, 255), warn);
		}
	}
	else if (g_dodge.resultShown && now < g_dodge.resultUntil)
	{
		float fade = (g_dodge.resultUntil - now) / 2.5f;
		if (fade > 1.0f) fade = 1.0f;
		const char* text = g_dodge.resultDodged ? "DODGED" : "HIT";
		ImU32 col = g_dodge.resultDodged
			? IM_COL32(120, 230, 140, (int)(255 * fade))
			: IM_COL32(230, 90, 80, (int)(255 * fade));

		ImGui::PushFont(g_fontHeading);
		ImVec2 size = ImGui::CalcTextSize(text);
		ImGui::PopFont();
		float cx = width / 2.0f - size.x * 1.6f / 2.0f;
		float cy = height * 0.28f;
		draw->AddText(g_fontHeading, g_fontHeading ? 21.0f * 1.6f : 30.0f, ImVec2(cx, cy), col, text);
	}
}

struct RoshanState
{
	bool enabled = false;

	bool everSeenAlive = false;
	bool aliveLastFrame = false;
	int health = 0, maxHealth = 0;
	Vector3 origin{};
	bool hasOrigin = false;

	bool dead = false;
	float deathTime = 0.0f;
};
static RoshanState g_rosh;

constexpr float kRoshanMinRespawn = 8.0f * 60.0f;
constexpr float kRoshanMaxRespawn = 11.0f * 60.0f;

static void UpdateRoshanTracker(const MemoryInternal& mem, const std::vector<uintptr_t>& chunks, int maxIndex)
{
	float now = (float)ImGui::GetTime();
	bool foundAlive = false;

	for (int i = 1; i <= maxIndex; i++)
	{
		uintptr_t identity = GetIdentityFromChunks(chunks, i);
		if (!identity)
			continue;
		uintptr_t entity = mem.Read<uintptr_t>(identity + offsets::CGameEntitySystem::m_pInstance);
		if (!entity)
			continue;

		std::string name = GetDesignerName(mem, identity);
		if (name != "npc_dota_roshan")
			continue;

		uint8_t lifeState = mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_lifeState);
		if (lifeState != 0)
			continue;

		int health = mem.Read<int>(entity + offsets::C_BaseEntity::m_iHealth);
		if (health <= 0)
			continue;

		foundAlive = true;
		g_rosh.health = health;
		g_rosh.maxHealth = mem.Read<int>(entity + offsets::C_BaseEntity::m_iMaxHealth);
		g_rosh.hasOrigin = GetEntityOrigin(mem, entity, g_rosh.origin);
		break;
	}

	if (foundAlive)
	{
		g_rosh.everSeenAlive = true;
		g_rosh.dead = false;
	}
	else if (g_rosh.everSeenAlive && g_rosh.aliveLastFrame && !g_rosh.dead)
	{
		g_rosh.dead = true;
		g_rosh.deathTime = now;
	}

	g_rosh.aliveLastFrame = foundAlive;
}

static void DrawRoshanIndicator(int width, int height)
{
	if (!g_rosh.enabled || !g_rosh.everSeenAlive)
		return;

	ImDrawList* draw = ImGui::GetForegroundDrawList();
	float now = (float)ImGui::GetTime();

	char buf[96];
	ImU32 col;

	if (!g_rosh.dead)
	{
		float pct = g_rosh.maxHealth > 0 ? (float)g_rosh.health / g_rosh.maxHealth : 0.0f;
		snprintf(buf, sizeof(buf), "Roshan alive - %d%% hp", (int)(pct * 100));
		col = IM_COL32(120, 220, 140, 255);
	}
	else
	{
		float elapsed = now - g_rosh.deathTime;
		float toMin = kRoshanMinRespawn - elapsed;
		float toMax = kRoshanMaxRespawn - elapsed;

		if (toMin > 0)
		{
			snprintf(buf, sizeof(buf), "Roshan dead - min respawn in %02d:%02d", (int)toMin / 60, (int)toMin % 60);
			col = IM_COL32(200, 90, 90, 255);
		}
		else if (toMax > 0)
		{
			snprintf(buf, sizeof(buf), "Roshan CAN respawn - up to %02d:%02d left", (int)toMax / 60, (int)toMax % 60);
			col = IM_COL32(230, 200, 60, 255);
		}
		else
		{
			snprintf(buf, sizeof(buf), "Roshan should have respawned");
			col = IM_COL32(150, 150, 150, 255);
		}
	}

	ImGui::PushFont(g_fontRegular);
	ImVec2 size = ImGui::CalcTextSize(buf);
	ImGui::PopFont();
	float x = width - size.x - 20.0f;
	float y = 80.0f;
	draw->AddRectFilled(ImVec2(x - 10, y - 6), ImVec2(x + size.x + 10, y + size.y + 6), IM_COL32(15, 15, 18, 200), 6.0f);
	draw->AddText(ImVec2(x, y), col, buf);
}

struct LastHitSettings { bool enabled = false; };
static LastHitSettings g_lastHit;

struct CreepHint
{
	float x = 0, y = 0;
	int health = 0;
	bool killableNow = false;
	bool closeSoon = false;
};
static std::vector<CreepHint> g_creepHints;
static std::vector<std::string> g_lastHitDebugLines;

static void UpdateLastHitHelper(const MemoryInternal& mem, const std::vector<uintptr_t>& chunks, int maxIndex,
	uintptr_t localPawn, const Vector3& localOrigin, const ViewMatrix& viewMatrix, int width, int height)
{
	g_creepHints.clear();
	g_lastHitDebugLines.clear();

	int dmgMin = mem.Read<int>(localPawn + offsets::C_DOTA_BaseNPC::m_iDamageMin);
	int dmgMax = mem.Read<int>(localPawn + offsets::C_DOTA_BaseNPC::m_iDamageMax);
	int avgDamage = (dmgMin + dmgMax) / 2;
	int effectiveDamage = avgDamage > 0 ? avgDamage : 40;

	{
		uintptr_t sceneNode = mem.Read<uintptr_t>(localPawn + offsets::C_BaseEntity::m_pGameSceneNode);
		char l[160];
		snprintf(l, sizeof(l), "localPawn=0x%llX sceneNode=0x%llX dmgMin=%d dmgMax=%d avgDamage=%d localOrigin=(%.0f,%.0f,%.0f)",
			(unsigned long long)localPawn, (unsigned long long)sceneNode, dmgMin, dmgMax, avgDamage, localOrigin.x, localOrigin.y, localOrigin.z);
		g_lastHitDebugLines.push_back(l);

		bool matchFound = false;
		for (int j = 1; j <= maxIndex; j++)
		{
			uintptr_t id2 = GetIdentityFromChunks(chunks, j);
			if (!id2) continue;
			uintptr_t e2 = mem.Read<uintptr_t>(id2 + offsets::CGameEntitySystem::m_pInstance);
			if (e2 != localPawn) continue;

			std::string n2 = GetDesignerName(mem, id2);
			Vector3 o2{};
			bool ok2 = GetEntityOrigin(mem, e2, o2);
			char l2[160];
			snprintf(l2, sizeof(l2), "  found at idx=%d name=%s origin_ok=%d origin=(%.0f,%.0f,%.0f)",
				j, n2.empty() ? "<empty>" : n2.c_str(), ok2, o2.x, o2.y, o2.z);
			g_lastHitDebugLines.push_back(l2);
			matchFound = true;
			break;
		}
		if (!matchFound)
			g_lastHitDebugLines.push_back("  localPawn NOT found anywhere in full entity scan!");
	}

	for (int i = 1; i <= maxIndex; i++)
	{
		uintptr_t identity = GetIdentityFromChunks(chunks, i);
		if (!identity)
			continue;
		uintptr_t entity = mem.Read<uintptr_t>(identity + offsets::CGameEntitySystem::m_pInstance);
		if (!entity)
			continue;

		std::string name = GetDesignerName(mem, identity);

		Vector3 origin{};
		bool hasOrigin = GetEntityOrigin(mem, entity, origin);
		float dist = 999999.0f;
		if (hasOrigin)
		{
			float dx0 = origin.x - localOrigin.x, dy0 = origin.y - localOrigin.y;
			dist = sqrtf(dx0 * dx0 + dy0 * dy0);
		}

		if (hasOrigin && dist <= 700.0f && g_lastHitDebugLines.size() < 20)
		{
			char l[128];
			snprintf(l, sizeof(l), "idx=%d name=%s dist=%.0f", i, name.empty() ? "<empty>" : name.c_str(), dist);
			g_lastHitDebugLines.push_back(l);
		}

		bool isCreep = name.compare(0, 15, "npc_dota_creep_") == 0 || name.compare(0, 17, "npc_dota_neutral_") == 0;
		if (!isCreep)
			continue;

		uint8_t lifeState = mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_lifeState);
		if (lifeState != 0)
			continue;

		int health = mem.Read<int>(entity + offsets::C_BaseEntity::m_iHealth);
		if (health <= 0)
			continue;

		if (!hasOrigin || dist > 700.0f)
			continue;

		Vector3 head = origin;
		head.z += 60.0f;
		Vector2 screen{};
		if (!WorldToScreen(head, screen, viewMatrix, width, height))
			continue;

		CreepHint hint;
		hint.x = screen.x;
		hint.y = screen.y;
		hint.health = health;
		hint.killableNow = health <= effectiveDamage;
		hint.closeSoon = !hint.killableNow && health <= (int)(effectiveDamage * 1.6f);
		g_creepHints.push_back(hint);
	}
}

static void DrawLastHitHelper()
{
	if (!g_lastHit.enabled)
		return;

	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	float time = (float)ImGui::GetTime();

	for (const auto& c : g_creepHints)
	{
		if (!c.killableNow && !c.closeSoon)
			continue;

		ImU32 col = c.killableNow
			? IM_COL32(90, 255, 130, (int)(200 + 55 * sinf(time * 8.0f)))
			: IM_COL32(255, 205, 80, 220);

		draw->AddCircle(ImVec2(c.x, c.y), 22.0f, col, 24, 2.5f);

		const char* label = c.killableNow ? "LAST HIT" : "soon";
		ImGui::PushFont(g_fontHeading);
		ImVec2 sz = ImGui::CalcTextSize(label);
		ImGui::PopFont();
		draw->AddText(g_fontHeading, 15.0f, ImVec2(c.x - sz.x * 0.7f / 2.0f, c.y - 22.0f - sz.y * 0.7f - 3.0f), col, label);
	}

	float dy = 260.0f;
	draw->AddText(ImVec2(300, dy), IM_COL32(255, 220, 80, 255), "[LastHit debug - entities near hero]");
	dy += 18;
	for (const auto& line : g_lastHitDebugLines)
	{
		draw->AddText(ImVec2(300, dy), IM_COL32(200, 220, 255, 255), line.c_str());
		dy += 15;
	}
}

static uintptr_t FindLocalPawnViaController(const MemoryInternal& mem, const std::vector<uintptr_t>& chunks, int maxIndex)
{
	for (int i = 1; i <= maxIndex; i++)
	{
		uintptr_t identity = GetIdentityFromChunks(chunks, i);
		if (!identity)
			continue;
		uintptr_t entity = mem.Read<uintptr_t>(identity + offsets::CGameEntitySystem::m_pInstance);
		if (!entity)
			continue;

		bool isLocal = mem.Read<uint8_t>(entity + offsets::CBasePlayerController::m_bIsLocalPlayerController) != 0;
		if (!isLocal)
			continue;

		uint32_t handle = mem.Read<uint32_t>(entity + offsets::CBasePlayerController::m_hPawn);
		int pawnIndex0 = (int)(handle & 0x7FFF);
		if (pawnIndex0 <= 0)
			continue;

		uintptr_t pawnIdentity = GetIdentityFromChunks(chunks, pawnIndex0 + 1);
		if (!pawnIdentity)
			continue;
		uintptr_t pawn = mem.Read<uintptr_t>(pawnIdentity + offsets::CGameEntitySystem::m_pInstance);
		if (LooksLikeValidHeroEntity(mem, pawn))
			return pawn;
	}
	return 0;
}

struct Module
{
	std::string name;
	std::string icon;
	bool* enabled;
	std::function<void()> drawSettings;
};

struct Category
{
	std::string name;
	std::string icon;
	std::vector<Module> modules;
};

static std::vector<Category> g_categories;
static int g_selectedCategory = 0;

static void ApplyTheme()
{
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 12.0f;
	style.ChildRounding = 10.0f;
	style.FrameRounding = 6.0f;
	style.GrabRounding = 8.0f;
	style.PopupRounding = 8.0f;
	style.ScrollbarRounding = 8.0f;
	style.WindowBorderSize = 1.0f;
	style.FramePadding = ImVec2(10, 6);
	style.ItemSpacing = ImVec2(10, 8);
	style.WindowPadding = ImVec2(0, 0);

	ImVec4 accent(0.60f, 0.77f, 0.15f, 1.0f);
	ImVec4 accentHover(0.69f, 0.85f, 0.22f, 1.0f);
	ImVec4 accentActive(0.49f, 0.62f, 0.13f, 1.0f);
	ImVec4* colors = style.Colors;

	colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.09f, 0.55f);
	colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);
	colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
	colors[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.10f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.14f);
	colors[ImGuiCol_CheckMark] = accent;
	colors[ImGuiCol_SliderGrab] = accent;
	colors[ImGuiCol_SliderGrabActive] = accentActive;
	colors[ImGuiCol_Button] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.60f, 0.77f, 0.15f, 0.25f);
	colors[ImGuiCol_ButtonActive] = accentActive;
	colors[ImGuiCol_Header] = ImVec4(0.60f, 0.77f, 0.15f, 0.16f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.60f, 0.77f, 0.15f, 0.24f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.60f, 0.77f, 0.15f, 0.32f);
	colors[ImGuiCol_Separator] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
	colors[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.94f, 1.0f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.57f, 0.58f, 1.0f);
}

static void ToggleSwitch(const char* strId, bool* v)
{
	ImGui::PushID(strId);
	ImVec2 p = ImGui::GetCursorScreenPos();
	float w = 36.0f, h = 20.0f;
	ImDrawList* draw = ImGui::GetWindowDrawList();

	ImGui::InvisibleButton("toggle", ImVec2(w, h));
	if (ImGui::IsItemClicked())
		*v = !*v;

	float styleAlpha = ImGui::GetStyle().Alpha;
	auto withAlpha = [styleAlpha](ImU32 col) -> ImU32
	{
		ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
		c.w *= styleAlpha;
		return ImGui::ColorConvertFloat4ToU32(c);
	};

	ImU32 bg = withAlpha(*v ? IM_COL32(153, 196, 39, 255) : IM_COL32(255, 255, 255, 30));
	draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h * 0.5f);
	float knobX = p.x + (*v ? w - h * 0.5f - 2.0f : h * 0.5f + 2.0f);
	draw->AddCircleFilled(ImVec2(knobX, p.y + h * 0.5f), h * 0.5f - 3.0f, withAlpha(IM_COL32(255, 255, 255, 255)));

	ImGui::PopID();
}

static void DrawModule(Module& m)
{
	ImGui::PushID(m.name.c_str());

	ImGui::BeginGroup();

	ToggleSwitch(m.name.c_str(), m.enabled);
	ImGui::SameLine();
	if (!m.icon.empty())
	{
		ImGui::TextColored(ImVec4(0.60f, 0.77f, 0.15f, 1.0f), "%s", m.icon.c_str());
		ImGui::SameLine();
	}
	ImGui::Text("%s", m.name.c_str());
	ImGui::EndGroup();

	if (*m.enabled && m.drawSettings)
	{
		ImGui::Dummy(ImVec2(0, 3));
		ImGui::Indent(4.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 3));
		m.drawSettings();
		ImGui::PopStyleVar(2);
		ImGui::Unindent(4.0f);
	}

	ImGui::Dummy(ImVec2(0, 4));
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0, 4));

	ImGui::PopID();
}

static float g_menuAnim = 0.0f;

static void DrawClickGui()
{
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 winSize(480, 340);

	float target = g_menuOpen ? 1.0f : 0.0f;
	float animSpeed = io.DeltaTime * 12.0f;
	if (animSpeed > 1.0f) animSpeed = 1.0f;
	g_menuAnim += (target - g_menuAnim) * animSpeed;
	if (fabsf(g_menuAnim - target) < 0.002f)
		g_menuAnim = target;

	float eased = 1.0f - (1.0f - g_menuAnim) * (1.0f - g_menuAnim);
	float slideOffset = (1.0f - eased) * 18.0f;

	ImGui::SetNextWindowSize(winSize, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - winSize.x) * 0.5f, (io.DisplaySize.y - winSize.y) * 0.5f - slideOffset), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowBgAlpha(0.55f * eased);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, eased);

	ImGui::PushFont(g_fontRegular);
	ImGui::Begin("##azheng", &g_menuOpen, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);

	ImVec2 winP = ImGui::GetWindowPos();
	ImVec2 winSz = ImGui::GetWindowSize();
	ImDrawList* draw = ImGui::GetWindowDrawList();

	float headerH = 46.0f;
	draw->AddRectFilled(winP, ImVec2(winP.x + winSz.x, winP.y + headerH), IM_COL32(255, 255, 255, 8), 12.0f, ImDrawFlags_RoundCornersTop);
	ImGui::PushFont(g_fontHeading);
	ImGui::SetCursorPos(ImVec2(18, 11));
	ImGui::TextColored(ImVec4(0.65f, 0.85f, 0.30f, 1.0f), "AZHENG");
	ImGui::PopFont();
	ImGui::SameLine();
	ImGui::SetCursorPosY(13);
	ImGui::TextDisabled("Right Shift to close");

	ImGui::SetCursorPos(ImVec2(0, headerH));
	draw->AddLine(ImVec2(winP.x, winP.y + headerH), ImVec2(winP.x + winSz.x, winP.y + headerH), IM_COL32(255, 255, 255, 15));

	float sidebarW = 140.0f;
	constexpr float kHeaderIndentX = 18.0f;
	ImDrawList* sidebarDraw = ImGui::GetWindowDrawList();
	ImGui::BeginChild("sidebar", ImVec2(sidebarW, winSz.y - headerH), false, ImGuiWindowFlags_NoScrollbar);
	ImGui::Dummy(ImVec2(0, 6));
	for (int i = 0; i < (int)g_categories.size(); i++)
	{
		bool selected = (g_selectedCategory == i);

		ImGui::SetCursorPosX(0);
		ImVec2 itemScreenPos = ImGui::GetCursorScreenPos();
		std::string id = "##cat" + std::to_string(i);
		if (ImGui::Selectable(id.c_str(), selected, 0, ImVec2(sidebarW, 34)))
			g_selectedCategory = i;

		std::string label = g_categories[i].icon + "  " + g_categories[i].name;
		ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
		float textY = itemScreenPos.y + (34.0f - labelSize.y) / 2.0f;
		ImVec4 baseCol = selected ? ImVec4(0.86f, 0.94f, 0.75f, 1.0f) : ImVec4(0.78f, 0.78f, 0.80f, 1.0f);
		baseCol.w *= ImGui::GetStyle().Alpha;
		ImU32 textCol = ImGui::ColorConvertFloat4ToU32(baseCol);
		sidebarDraw->AddText(ImVec2(itemScreenPos.x + kHeaderIndentX, textY), textCol, label.c_str());
	}
	ImGui::EndChild();

	ImGui::SameLine();
	draw->AddLine(ImVec2(winP.x + sidebarW, winP.y + headerH), ImVec2(winP.x + sidebarW, winP.y + winSz.y), IM_COL32(255, 255, 255, 12));

	ImGui::BeginChild("content", ImVec2(winSz.x - sidebarW - 8, winSz.y - headerH), false);
	ImGui::Dummy(ImVec2(0, 4));
	if (g_selectedCategory >= 0 && g_selectedCategory < (int)g_categories.size())
		for (auto& mod : g_categories[g_selectedCategory].modules)
			DrawModule(mod);
	ImGui::EndChild();

	ImGui::End();
	ImGui::PopFont();
	ImGui::PopStyleVar();
}

static ImU32 HealthColor(float pct)
{
	pct = pct < 0 ? 0 : (pct > 1 ? 1 : pct);
	int r, g;
	if (pct > 0.5f) { r = (int)((1.0f - pct) * 2 * 255); g = 255; }
	else { r = 255; g = (int)(pct * 2 * 255); }
	return IM_COL32(r, g, 60, 255);
}

static ImU32 AbilityAccentColor(const std::string& name)
{
	uint32_t hash = 2166136261u;
	for (char c : name) { hash ^= (uint8_t)c; hash *= 16777619u; }
	float hue = (hash % 360) / 360.0f;

	float h = hue * 6.0f;
	int i = (int)h;
	float f = h - i;
	float s = 0.55f, v = 0.75f;
	float p = v * (1 - s), q = v * (1 - s * f), tt = v * (1 - s * (1 - f));
	float r, g, b;
	switch (i % 6)
	{
		case 0: r = v; g = tt; b = p; break;
		case 1: r = q; g = v; b = p; break;
		case 2: r = p; g = v; b = tt; break;
		case 3: r = p; g = q; b = v; break;
		case 4: r = tt; g = p; b = v; break;
		default: r = v; g = p; b = q; break;
	}
	return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
}

static void DrawAbilitySlot(ImDrawList* draw, ImVec2 pos, float size, const AbilityInfo& a, float time)
{
	bool onCooldown = a.cooldown > 0.35f;
	ImVec2 center(pos.x + size / 2.0f, pos.y + size / 2.0f);
	float radius = size / 2.0f;

	ImU32 accent = AbilityAccentColor(a.shortName);

	draw->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(24, 24, 28, 235), 5.0f);

	ImU32 iconColor = onCooldown ? (accent & 0x66FFFFFF) : accent;
	draw->AddRectFilled(ImVec2(pos.x + 2, pos.y + 2), ImVec2(pos.x + size - 2, pos.y + size - 2), iconColor, 4.0f);

	constexpr float kHeadingBaseSize = 21.0f;
	float scale0 = size / 34.0f;

	ImGui::PushFont(g_fontHeading);
	ImVec2 codeSize = ImGui::CalcTextSize(a.shortName.substr(0, 2).c_str());
	ImGui::PopFont();
	std::string code = a.shortName.substr(0, 2);
	draw->AddText(g_fontHeading, kHeadingBaseSize * scale0,
		ImVec2(center.x - codeSize.x * scale0 / 2.0f, center.y - codeSize.y * scale0 / 2.0f),
		IM_COL32(255, 255, 255, onCooldown ? 90 : 235), code.c_str());

	if (onCooldown && a.cooldownMax > 0.01f)
	{
		float frac = a.cooldown / a.cooldownMax;
		if (frac > 1.0f) frac = 1.0f;

		constexpr float kPi = 3.14159265358979323846f;
		float startAngle = -kPi / 2.0f;
		float endAngle = startAngle + frac * 2.0f * kPi;

		draw->PushClipRect(pos, ImVec2(pos.x + size, pos.y + size), true);
		draw->PathArcTo(center, radius * 1.42f, startAngle, endAngle, 24);
		draw->PathLineTo(center);
		draw->PathFillConvex(IM_COL32(0, 0, 0, 165));
		draw->PopClipRect();

		char cdBuf[8];
		snprintf(cdBuf, sizeof(cdBuf), "%d", (int)ceilf(a.cooldown));
		ImGui::PushFont(g_fontHeading);
		ImVec2 cdSize = ImGui::CalcTextSize(cdBuf);
		ImGui::PopFont();
		float scale = size / 34.0f * 1.05f;
		draw->AddText(g_fontHeading, kHeadingBaseSize * scale,
			ImVec2(center.x - cdSize.x * scale / 2.0f, center.y - cdSize.y * scale / 2.0f),
			IM_COL32(255, 130, 120, 255), cdBuf);
	}
	else
	{
		float pulse = 0.5f + 0.5f * sinf(time * 3.0f);
		ImU32 glow = IM_COL32(255, 255, 255, (int)(60 + pulse * 90));
		draw->AddRect(pos, ImVec2(pos.x + size, pos.y + size), glow, 5.0f, 0, 1.5f);
	}

	draw->AddRect(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(0, 0, 0, 200), 5.0f, 0, 1.0f);
}

static void DrawDotaPanel(ImDrawList* draw, const EspTarget& t)
{
	ImU32 accent = t.enemy ? IM_COL32(235, 90, 80, 255) : IM_COL32(100, 220, 140, 255);
	float time = (float)ImGui::GetTime();

	char statBuf[48];
	if (t.maxMana > 0)
		snprintf(statBuf, sizeof(statBuf), "%d hp   %d mp", t.health, (int)t.mana);
	else
		snprintf(statBuf, sizeof(statBuf), "%d hp", t.health);

	ImVec2 statSize = ImGui::CalcTextSize(statBuf);
	float statY = t.y - statSize.y - 4;
	draw->AddText(ImVec2(t.x + t.w / 2.0f - statSize.x / 2.0f, statY), accent, statBuf);

	if (!t.abilities.empty())
	{
		float slotSize = 30.0f, slotGap = 4.0f;
		float totalW = t.abilities.size() * slotSize + (t.abilities.size() - 1) * slotGap;
		float startX = t.x + t.w / 2.0f - totalW / 2.0f;
		float slotY = statY - slotSize - 6;

		for (size_t i = 0; i < t.abilities.size(); i++)
		{
			float sx = startX + i * (slotSize + slotGap);
			DrawAbilitySlot(draw, ImVec2(sx, slotY), slotSize, t.abilities[i], time);
		}
	}
}

static void DrawEsp(const std::vector<EspTarget>& targets)
{
	if (!g_esp.enabled)
		return;

	ImDrawList* draw = ImGui::GetBackgroundDrawList();

	for (const auto& t : targets)
	{
		if (t.enemy && !g_esp.showEnemies) continue;
		if (!t.enemy && !g_esp.showAllies) continue;

		ImU32 accent = t.enemy ? IM_COL32(235, 64, 52, 255) : IM_COL32(60, 200, 120, 255);
		float x = t.x, y = t.y, w = t.w, h = t.h;

		if (g_esp.cornerStyle)
		{
			float corner = w * 0.22f;
			if (corner < 6.0f) corner = 6.0f;
			auto DrawCorner = [&](float cx, float cy, float dx, float dy)
			{
				draw->AddLine(ImVec2(cx, cy), ImVec2(cx + dx, cy), accent, g_esp.boxThickness);
				draw->AddLine(ImVec2(cx, cy), ImVec2(cx, cy + dy), accent, g_esp.boxThickness);
			};
			DrawCorner(x, y, corner, corner);
			DrawCorner(x + w, y, -corner, corner);
			DrawCorner(x, y + h, corner, -corner);
			DrawCorner(x + w, y + h, -corner, -corner);
		}
		else
		{
			draw->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), accent, 3.0f, 0, g_esp.boxThickness);
		}

		if (g_esp.dotaStylePanel)
		{
			DrawDotaPanel(draw, t);

			if (g_esp.showDistance && t.distance > 0)
			{
				char buf[32];
				snprintf(buf, sizeof(buf), "%.0fm", t.distance / 60.0f);
				ImVec2 distSize = ImGui::CalcTextSize(buf);
				draw->AddText(ImVec2(x + w / 2.0f - distSize.x / 2.0f, y + h + 4), IM_COL32(220, 220, 225, 255), buf);
			}

			continue;
		}

		if (g_esp.showHealthBar && t.maxHealth > 0)
		{
			float pct = (float)t.health / (float)t.maxHealth;
			if (pct < 0) pct = 0; if (pct > 1) pct = 1;

			float barX = x - 10, barW = 5.0f;
			draw->AddRectFilled(ImVec2(barX, y), ImVec2(barX + barW, y + h), IM_COL32(30, 30, 34, 160), 2.5f);
			float filledH = h * pct;
			draw->AddRectFilled(ImVec2(barX, y + (h - filledH)), ImVec2(barX + barW, y + h), HealthColor(pct), 2.5f);
		}

		std::string label = g_esp.showNames ? t.name : "";
		if (g_esp.showDistance && t.distance > 0)
		{
			char buf[32];
			snprintf(buf, sizeof(buf), "  %.0fm", t.distance / 60.0f);
			label += buf;
		}

		if (!label.empty())
		{
			ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
			float labelX = x + w / 2.0f - textSize.x / 2.0f;
			float labelY = y - textSize.y - 6;
			draw->AddRectFilled(ImVec2(labelX - 4, labelY - 2), ImVec2(labelX + textSize.x + 4, labelY + textSize.y + 2), IM_COL32(18, 18, 22, 160), 4.0f);
			draw->AddText(ImVec2(labelX, labelY), IM_COL32(255, 255, 255, 255), label.c_str());
		}

		if (g_esp.showHealthBar && t.maxHealth > 0)
		{
			char hpBuf[32];
			snprintf(hpBuf, sizeof(hpBuf), "%d / %d", t.health, t.maxHealth);
			ImVec2 hpSize = ImGui::CalcTextSize(hpBuf);
			draw->AddText(ImVec2(x + w / 2.0f - hpSize.x / 2.0f, y + h + 4), IM_COL32(220, 220, 225, 255), hpBuf);
		}
	}
}

static void RunEspFrame(int width, int height)
{
	std::vector<EspTarget> targets;

	uintptr_t entitySystem = g_mem.Read<uintptr_t>(g_mem.clientDllBase + offsets::client_dll::dwEntityList);

	constexpr int kChunkCount = 16;
	constexpr int kMaxScanIndex = kChunkCount * offsets::CGameEntitySystem::ChunkSize;
	std::vector<uintptr_t> chunks;
	uintptr_t localPawn = 0;

	if (entitySystem)
	{
		chunks = ReadChunkPointers(g_mem, entitySystem, kChunkCount);
		localPawn = FindLocalPawnViaController(g_mem, chunks, kMaxScanIndex);
	}

	if (entitySystem && localPawn)
	{
		int localTeam = g_mem.Read<uint8_t>(localPawn + offsets::C_BaseEntity::m_iTeamNum);

		Vector3 localOrigin{};
		GetEntityOrigin(g_mem, localPawn, localOrigin);

		ViewMatrix viewMatrix = g_mem.Read<ViewMatrix>(g_mem.clientDllBase + offsets::client_dll::dwViewMatrix);

		std::unordered_map<uintptr_t, Vector3> newSmoothed;

		for (int i = 1; i <= kMaxScanIndex; i++)
		{
			uintptr_t identity = GetIdentityFromChunks(chunks, i);
			if (!identity)
				continue;
			uintptr_t entity = g_mem.Read<uintptr_t>(identity + offsets::CGameEntitySystem::m_pInstance);
			if (!entity || entity == localPawn)
				continue;

			std::string designerName = GetDesignerName(g_mem, identity);
			if (designerName.compare(0, 14, "npc_dota_hero_") != 0)
				continue;

			bool isIllusion = g_mem.Read<bool>(entity + offsets::C_DOTA_BaseNPC::m_bIsIllusion);
			if (isIllusion)
				continue;

			uint8_t lifeState = g_mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_lifeState);
			if (lifeState != 0)
				continue;

			int health = g_mem.Read<int>(entity + offsets::C_BaseEntity::m_iHealth);
			int maxHealth = g_mem.Read<int>(entity + offsets::C_BaseEntity::m_iMaxHealth);
			if (health <= 0 || maxHealth <= 0)
				continue;

			uint8_t team = g_mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_iTeamNum);

			Vector3 origin{};
			if (!GetEntityOrigin(g_mem, entity, origin))
				continue;

			float distFromWorldOrigin = sqrtf(origin.x * origin.x + origin.y * origin.y);
			if (distFromWorldOrigin < 50.0f || distFromWorldOrigin > 20000.0f)
				continue;

			auto it = g_smoothedOrigins.find(entity);
			Vector3 smooth = origin;
			if (it != g_smoothedOrigins.end())
			{
				float jumpDx = origin.x - it->second.x;
				float jumpDy = origin.y - it->second.y;
				float jumpDist = sqrtf(jumpDx * jumpDx + jumpDy * jumpDy);
				constexpr float kTeleportThreshold = 250.0f;
				if (jumpDist < kTeleportThreshold)
				{
					float a = 1.0f - g_esp.smoothing;
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
			if (!WorldToScreen(feet, screenFeet, viewMatrix, width, height))
				continue;
			if (!WorldToScreen(head, screenHead, viewMatrix, width, height))
				continue;

			float boxHeight = screenFeet.y - screenHead.y;
			if (boxHeight <= 0)
				continue;
			float boxWidth = boxHeight * 0.55f;

			bool inBottomLeftHud = (screenHead.x < width * 0.24f) && (screenHead.y > height * 0.60f);
			if (inBottomLeftHud)
				continue;

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
			t.level = g_mem.Read<int>(entity + offsets::C_DOTA_BaseNPC::m_iCurrentLevel);
			t.mana = g_mem.Read<float>(entity + offsets::C_DOTA_BaseNPC::m_flMana);
			t.maxMana = g_mem.Read<float>(entity + offsets::C_DOTA_BaseNPC::m_flMaxMana);
			t.distance = sqrtf(dx * dx + dy * dy);
			t.enemy = (team != localTeam);
			t.name = FormatHeroName(designerName);
			t.abilities = GetHeroAbilities(g_mem, entity, chunks, kMaxScanIndex);

			targets.push_back(t);
		}

		g_smoothedOrigins = std::move(newSmoothed);

		if (g_dodge.enabled)
			UpdateDodgeHelper(g_mem, targets, chunks, kMaxScanIndex, localPawn);

		if (g_lastHit.enabled)
			UpdateLastHitHelper(g_mem, chunks, kMaxScanIndex, localPawn, localOrigin, viewMatrix, width, height);

		if (g_rosh.enabled)
			UpdateRoshanTracker(g_mem, chunks, kMaxScanIndex);
	}

	DrawEsp(targets);
	DrawDodgeIndicator(width, height);
	DrawLastHitHelper();
	DrawRoshanIndicator(width, height);

	if (g_debugNetWorthScan)
	{
		ImDrawList* dbgDraw = ImGui::GetForegroundDrawList();
		float y = 60.0f;
		char status[128];
		snprintf(status, sizeof(status), "[NetWorth debug] entitySystem=0x%llX chunks=%zu localPawn=0x%llX",
			(unsigned long long)entitySystem, chunks.size(), (unsigned long long)localPawn);
		dbgDraw->AddText(ImVec2(300, y), IM_COL32(255, 220, 80, 255), status);
		y += 20;

		if (entitySystem)
		{
			int shownEmpty = 0, listed = 0;
			for (int i = 1; i <= 80 && listed < 25; i++)
			{
				uintptr_t identity = GetIdentityFromChunks(chunks, i);
				if (!identity) continue;
				uintptr_t entity = g_mem.Read<uintptr_t>(identity + offsets::CGameEntitySystem::m_pInstance);
				if (!entity) continue;

				std::string name = GetDesignerName(g_mem, identity);

				char line[96];
				snprintf(line, sizeof(line), "idx=%d ent=0x%llX name=%s", i, (unsigned long long)entity, name.empty() ? "<empty>" : name.c_str());
				dbgDraw->AddText(ImVec2(300, y), name.empty() ? IM_COL32(255, 150, 150, 255) : IM_COL32(200, 200, 200, 255), line);
				y += 15;
				listed++;

				uint32_t candCount = g_mem.Read<uint32_t>(entity + 0x5F0);
				uintptr_t candData = g_mem.Read<uintptr_t>(entity + 0x5F8);
				if (name.empty() && candCount == 5 && candData > 0x10000)
				{
					shownEmpty++;
					dbgDraw->AddText(ImVec2(300, y), IM_COL32(255, 255, 120, 255), "  candidate vector! trying strides for m_iNetWorth @ +0x90:");
					y += 15;

					for (int stride : { 0x2E0, 0x300, 0x320, 0x340, 0x360, 0x380, 0x3A0, 0x3C0, 0x400, 0x440, 0x480 })
					{
						char line[128];
						int p0 = g_mem.Read<int32_t>(candData + 0 * stride + 0x90);
						int p1 = g_mem.Read<int32_t>(candData + 1 * stride + 0x90);
						int p2 = g_mem.Read<int32_t>(candData + 2 * stride + 0x90);
						int p3 = g_mem.Read<int32_t>(candData + 3 * stride + 0x90);
						int p4 = g_mem.Read<int32_t>(candData + 4 * stride + 0x90);
						snprintf(line, sizeof(line), "    stride=0x%X: %d, %d, %d, %d, %d", stride, p0, p1, p2, p3, p4);
						dbgDraw->AddText(ImVec2(300, y), IM_COL32(150, 255, 180, 255), line);
						y += 14;
					}
				}
			}
		}
	}

	if (g_menuOpen || g_menuAnim > 0.001f)
		DrawClickGui();
}

static void CreateRenderTarget(IDXGISwapChain* swapChain)
{
	ID3D11Texture2D* backBuffer = nullptr;
	swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
	if (backBuffer)
	{
		g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
		backBuffer->Release();
	}
}

static LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	bool menuActive = g_menuOpen || g_menuAnim > 0.001f;

	if (menuActive)
		ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

	bool isMouseOrKeyboard =
		(msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
		msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR ||
		msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP;

	if (menuActive && isMouseOrKeyboard)
		return TRUE;

	return CallWindowProcA((WNDPROC)g_originalWndProc, hwnd, msg, wParam, lParam);
}

void OnResizeBuffers()
{
	if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

void OnPresent(IDXGISwapChain* swapChain)
{
	if (!g_imguiInited)
	{
		if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_device)))
			return;
		g_device->GetImmediateContext(&g_context);

		DXGI_SWAP_CHAIN_DESC desc;
		swapChain->GetDesc(&desc);
		g_gameWnd = desc.OutputWindow;

		g_originalWndProc = (WNDPROC)SetWindowLongPtrA(g_gameWnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();
		ApplyTheme();
		LoadFonts();
		ImGui_ImplWin32_Init(g_gameWnd);
		ImGui_ImplDX11_Init(g_device, g_context);

		g_mem.Init();

		Category visuals;
		visuals.name = "Visuals";
		visuals.icon = Icon::Eye;
		visuals.modules.push_back({
			"Player ESP", Icon::Crosshair, &g_esp.enabled,
			[]()
			{
				ImGui::Checkbox("Show enemies", &g_esp.showEnemies);
				ImGui::Checkbox("Show allies", &g_esp.showAllies);
				ImGui::Checkbox("Health bar", &g_esp.showHealthBar);
				ImGui::Checkbox("Names", &g_esp.showNames);
				ImGui::Checkbox("Distance", &g_esp.showDistance);
				ImGui::Checkbox("Corner style box", &g_esp.cornerStyle);
				ImGui::Checkbox("Dota-style HUD panel (HP/mana/level/abilities)", &g_esp.dotaStylePanel);
				ImGui::SliderFloat("Box thickness", &g_esp.boxThickness, 1.0f, 4.0f, "%.1f");
				ImGui::SliderFloat("Smoothing", &g_esp.smoothing, 0.0f, 0.9f, "%.2f");
			}
			});
		g_categories.push_back(visuals);

		Category combat;
		combat.name = "Combat";
		combat.icon = Icon::Bolt;
		combat.modules.push_back({
			"Lina Dodge Helper", Icon::Shield, &g_dodge.enabled,
			[]()
			{
				ImGui::TextWrapped("Watches for enemy Lina casting Laguna Blade and shows a countdown "
					"to manually pop Manta Style in time. Does NOT press any keys for you.");
			}
			});
		combat.modules.push_back({
			"Last Hit Helper", Icon::Crosshair, &g_lastHit.enabled,
			[]()
			{
				ImGui::TextWrapped("Highlights nearby creeps that die from your hero's next basic attack. "
					"Pure information - you still click/attack yourself.");
			}
			});
		g_categories.push_back(combat);

		Category awareness;
		awareness.name = "Awareness";
		awareness.icon = Icon::Eye;
		awareness.modules.push_back({
			"Roshan Tracker", Icon::Shield, &g_rosh.enabled,
			[]()
			{
				ImGui::TextWrapped("Shows Roshan's HP while alive, and a min/max respawn window countdown after he dies. "
					"Death time is tracked locally (wall clock) from when Roshan first disappears from the entity list.");
			}
			});
		g_categories.push_back(awareness);

		g_imguiInited = true;
	}

	if (!g_rtv)
		CreateRenderTarget(swapChain);

	static bool toggleKeyWasDown = false;
	bool toggleKeyDown = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
	if (toggleKeyDown && !toggleKeyWasDown)
		g_menuOpen = !g_menuOpen;
	toggleKeyWasDown = toggleKeyDown;

	RECT rect;
	GetClientRect(g_gameWnd, &rect);
	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	RunEspFrame(width, height);

	ImGui::Render();
	g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

static DWORD WINAPI MainThread(LPVOID)
{
	InstallPresentHook();
	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_hModule = hModule;
		DisableThreadLibraryCalls(hModule);
		CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
	}
	return TRUE;
}
