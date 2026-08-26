#pragma once
#include "Memory.h"
#include "VectorMath.h"
#include "Offsets.h"
#include "EntityLayout.h"
#include "Config.h"
#include <vector>
#include <cstring>
#include <cmath>
#include <cctype>
#include <cstdarg>
#include <unordered_map>

// ---------------------------------------------------------------------------
// EntityReader -- 100% READ-ONLY entity scanning, optimized for FPS:
//   * entity-list chunks are read in BULK (one RPM call per chunk instead of
//     two RPM calls per entity slot), cutting tens of thousands of
//     ReadProcessMemory calls per frame down to ~20,
//   * one single pass classifies heroes / creeps / wards / roshan / runes,
//   * designer names + item names are cached by string pointer, so in
//     steady state almost no string reads happen,
//   * the local controller/pawn is cached and re-validated with 2 reads.
// Nothing here ever writes to the game process (read-only kernel contract).
// ---------------------------------------------------------------------------
class EntityReader {
public:
	// --- cached local-player state -----------------------------------------
	struct LocalState {
		uintptr_t controller = 0;
		uintptr_t pawn = 0;
		uint8_t team = 0;
		Vector3 origin = {};
		float damage = 0.0f;       // avg basic damage, for last-hit calc
		float attackRange = 0.0f;
		float invisLevel = 0.0f;
	};

	// --- activity trackers (cast / jungle / roshan) -------------------------
	enum { Ping_Cast = 0, Ping_Jungle = 1, Ping_Roshan = 2 };

	struct Ping {
		Vector3 origin = {};
		Vector2 screen = {};
		float started = 0.0f;
		float lastSeen = 0.0f;
		int kind = Ping_Cast;
		bool onScreen = false;
		FixedStr<28> name;
	};

	struct FeedEvent {
		float time = 0.0f;
		int kind = Ping_Cast;
		FixedStr<80> text;
	};

	// --- persistent scan state (buffers reused => no per-frame allocs) -----
	struct ScanState {
		std::vector<uintptr_t> chunks;
		std::vector<uint8_t> identityBuf;   // bulk CEntityIdentity chunk buffer
		std::unordered_map<uintptr_t, FixedStr<48>> nameCache; // key: string ptr
		std::unordered_map<uintptr_t, Vector3> smoothed;       // key: entity ptr
		std::unordered_map<uintptr_t, GhostTarget> ghosts;     // last-known heroes
		std::unordered_map<uintptr_t, float> itemRefresh;      // key: hero ptr
		std::vector<Vector3> neutralPts;                       // jungle camps this scan
		std::unordered_map<uintptr_t, int> prevActivity;       // key: hero ptr
		std::unordered_map<uintptr_t, Ping> pings;             // active pings, key: hero ptr
		std::unordered_map<uintptr_t, float> eventGate;        // feed throttle
		std::vector<FeedEvent> feed;
		Vector3 lastRoshanPos = {};
		float lastRoshanSeen = -9999.0f;
		LocalState local;
		float now = 0.0f;                      // seconds, set by the caller
		float lastScanTime = 0.0f;
		bool localValid = false;
	};

	static constexpr int ChunkCount = 16; // 16 * 512 = 8192 entity slots

	// ------------------------------------------------------------------
	static bool GetEntityOrigin(const Memory& mem, uintptr_t entity, Vector3& out) {
		uintptr_t sceneNode = mem.Read<uintptr_t>(entity + offsets::C_BaseEntity::m_pGameSceneNode);
		if (!sceneNode)
			return false;
		out = mem.Read<Vector3>(sceneNode + offsets::CGameSceneNode::m_vecAbsOrigin);
		return true;
	}

	// Bulk-read the entity pointer table. One RPM call per non-null chunk.
	static void ReadChunkPointers(const Memory& mem, uintptr_t entitySystem, ScanState& st) {
		const size_t stride = (size_t)EntityLayout::Get().stride;
		const size_t chunkBytes = stride * (size_t)offsets::CGameEntitySystem::ChunkSize;

		st.chunks.resize(ChunkCount);
		for (int c = 0; c < ChunkCount; c++)
			st.chunks[c] = mem.Read<uintptr_t>(entitySystem + offsets::CGameEntitySystem::m_EntityPtrArray + 8 * c);

		// +8: the {stride=0x78, pInstance=0x78} layout variant reads exactly at
		// the end of the last slot, so keep a small safety tail.
		st.identityBuf.resize(chunkBytes + 8);
		for (int c = 0; c < ChunkCount; c++) {
			if (!st.chunks[c])
				continue;
			mem.ReadRaw(st.chunks[c], st.identityBuf.data(), chunkBytes);
		}
	}

	// Entity pointer for a chunk/slot, straight from the bulk buffer (no RPM).
	static uintptr_t EntityFromBuffer(const ScanState& st, int ci, int slot) {
		const size_t off = (size_t)EntityLayout::Get().stride * (size_t)slot
			+ (size_t)EntityLayout::Get().pInstance;
		uintptr_t p;
		memcpy(&p, &st.identityBuf[off], sizeof(p));
		return p;
	}

	static uintptr_t IdentityAddr(const ScanState& st, int ci, int slot) {
		return st.chunks[ci] + (size_t)EntityLayout::Get().stride * (size_t)slot;
	}

	// Cached designer-name lookup. The name POINTER for entities inside the
	// scanned chunks comes straight from the bulk buffer (zero RPM); only the
	// string itself is read once and cached by pointer.
	static const char* CachedNamePtr(const Memory& mem, ScanState& st, uintptr_t namePtr) {
		if (namePtr < 0x10000ull)
			return nullptr;

		auto it = st.nameCache.find(namePtr);
		if (it != st.nameCache.end())
			return it->second.c_str();

		char buf[48] = {};
		mem.ReadRaw(namePtr, buf, sizeof(buf) - 1);
		if (!buf[0])
			return nullptr;

		FixedStr<48> fs;
		fs.set(buf);
		return st.nameCache.emplace(namePtr, fs).first->second.c_str();
	}

	// slow path for entities resolved by address (e.g. items)
	static const char* CachedName(const Memory& mem, ScanState& st, uintptr_t identityAddr) {
		uintptr_t namePtr = mem.Read<uintptr_t>(identityAddr + 0x20);
		return CachedNamePtr(mem, st, namePtr);
	}

	static void PrettyName(const char* raw, int skipChars, char* out, int cap) {
		raw += skipChars;
		int i = 0;
		if (*raw && cap > 0) out[i++] = (char)toupper((unsigned char)*raw), raw++;
		for (; *raw && i < cap - 1; raw++, i++)
			out[i] = (*raw == '_') ? ' ' : *raw;
		out[i] = 0;
	}

	static bool ContainsI(const char* s, const char* sub) {
		for (const char* p = s; *p; p++) {
			const char* a = p; const char* b = sub;
			while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
			if (!*b) return true;
		}
		return false;
	}

	static bool IsPlausibleHero(const Memory& mem, uintptr_t e) {
		uint8_t lifeState = mem.Read<uint8_t>(e + offsets::C_BaseEntity::m_lifeState);
		uint8_t team = mem.Read<uint8_t>(e + offsets::C_BaseEntity::m_iTeamNum);
		int health = mem.Read<int>(e + offsets::C_BaseEntity::m_iHealth);
		int maxHealth = mem.Read<int>(e + offsets::C_BaseEntity::m_iMaxHealth);
		return lifeState <= 2 && (team == 2 || team == 3) &&
			health > 0 && health <= maxHealth && maxHealth > 0 && maxHealth < 20000;
	}

	// ------------------------------------------------------------------
	// Local pawn: cached; re-validated with a couple of reads per scan.
	// ------------------------------------------------------------------
	static uintptr_t ResolveHandle(ScanState& st, int index0) {
		if (index0 <= 0)
			return 0;
		int adj = index0 - EntityLayout::Get().bias;
		if (adj < 0)
			return 0;
		int ci = adj / offsets::CGameEntitySystem::ChunkSize;
		int slot = adj % offsets::CGameEntitySystem::ChunkSize;
		if (ci < 0 || ci >= ChunkCount || !st.chunks[ci])
			return 0;
		return EntityFromBuffer(st, ci, slot);
	}

	static bool ValidateLocal(const Memory& mem, ScanState& st) {
		LocalState& L = st.local;
		if (!L.controller || !L.pawn)
			return false;
		if (mem.Read<uint8_t>(L.controller + offsets::CBasePlayerController::m_bIsLocalPlayerController) != 1)
			return false;
		uint32_t handle = mem.Read<uint32_t>(L.controller + offsets::CBasePlayerController::m_hPawn);
		if (ResolveHandle(st, (int)(handle & 0x7FFF)) != L.pawn)
			return false;
		return IsPlausibleHero(mem, L.pawn);
	}

	// Slow path -- only on the very first scan or when the cache goes stale.
	static uintptr_t FindLocalController(const Memory& mem, ScanState& st) {
		for (int ci = 0; ci < ChunkCount; ci++) {
			if (!st.chunks[ci])
				continue;
			for (int slot = 0; slot < offsets::CGameEntitySystem::ChunkSize; slot++) {
				uintptr_t e = EntityFromBuffer(st, ci, slot);
				if (e && mem.Read<uint8_t>(e + offsets::CBasePlayerController::m_bIsLocalPlayerController) == 1)
					return e;
			}
		}
		return 0;
	}

	static void RefreshLocal(const Memory& mem, ScanState& st) {
		LocalState& L = st.local;
		if (!ValidateLocal(mem, st)) {
			L.controller = FindLocalController(mem, st);
			if (!L.controller) { st.localValid = false; return; }
			uint32_t handle = mem.Read<uint32_t>(L.controller + offsets::CBasePlayerController::m_hPawn);
			L.pawn = ResolveHandle(st, (int)(handle & 0x7FFF));
			if (!L.pawn || !IsPlausibleHero(mem, L.pawn)) {
				L.controller = L.pawn = 0;
				st.localValid = false;
				return;
			}
		}
		L.team = mem.Read<uint8_t>(L.pawn + offsets::C_BaseEntity::m_iTeamNum);
		if (!GetEntityOrigin(mem, L.pawn, L.origin)) { st.localValid = false; return; }
		int dmin = mem.Read<int>(L.pawn + offsets::C_DOTA_BaseNPC::m_iDamageMin);
		int dmax = mem.Read<int>(L.pawn + offsets::C_DOTA_BaseNPC::m_iDamageMax);
		if (dmin > 0 && dmin < 1000 && dmax >= dmin && dmax < 1000)
			L.damage = (dmin + dmax) * 0.5f;
		L.attackRange = (float)mem.Read<int>(L.pawn + offsets::C_DOTA_BaseNPC::m_iAttackRange);
		if (L.attackRange <= 0 || L.attackRange > 2000) L.attackRange = 0;
		L.invisLevel = mem.Read<float>(L.pawn + offsets::C_DOTA_BaseNPC::m_flInvisibilityLevel);
		st.localValid = true;
	}

	// ------------------------------------------------------------------
	// Items ("Heroes Information", read-only), refreshed ~1x/sec per hero.
	// ------------------------------------------------------------------
	static void ReadItems(const Memory& mem, ScanState& st, uintptr_t hero, EspTarget& t) {
		uintptr_t inv = mem.Read<uintptr_t>(hero + offsets::C_DOTA_BaseNPC::m_Inventory);
		if (inv < 0x10000ull)
			return;

		// C_NetworkUtlVectorBase<CHandle>: { u32 count; pad; T* data; }
		uint8_t hdr[16];
		if (!mem.ReadRaw(inv + offsets::C_DOTA_UnitInventory::m_hItems, hdr, sizeof(hdr)))
			return;
		uint32_t count;
		uintptr_t data;
		memcpy(&count, hdr, 4);
		memcpy(&data, hdr + 8, 8);
		if (!data || count == 0)
			return;
		if (count > 6) count = 6; // HUD shows six inventory slots

		uint32_t handles[6];
		if (!mem.ReadRaw(data, handles, count * sizeof(uint32_t)))
			return;

		t.itemCount = 0;
		for (uint32_t k = 0; k < count && t.itemCount < 6; k++) {
			uintptr_t item = ResolveHandle(st, (int)(handles[k] & 0x7FFF));
			if (!item)
				continue;
			// entity -> CEntityIdentity (CEntityInstance::m_pEntity) -> name
			uintptr_t identity = mem.Read<uintptr_t>(item + offsets::C_BaseEntity::m_pEntity);
			if (identity < 0x10000ull)
				continue;
			const char* nm = CachedName(mem, st, identity);
			if (!nm || strncmp(nm, "item", 4) != 0)
				continue;
			char pretty[24];
			PrettyName(nm, 5, pretty, 24); // strip "item_"
			t.items[t.itemCount++].set(pretty);
		}
	}

	// ------------------------------------------------------------------
	// Activity trackers. m_NetworkActivity is networked for enemy heroes:
	//   1503/1504 = attack, 1505..1510 = cast, 1515 = ultimate,
	//   1516..1521 = channel. Read-only -- we only observe state changes.
	// ------------------------------------------------------------------
	static void PushFeed(ScanState& st, int kind, const char* fmt, ...) {
		FeedEvent e;
		e.time = st.now;
		e.kind = kind;
		char buf[80];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		e.text.set(buf);
		if (st.feed.size() > 9)
			st.feed.erase(st.feed.begin());
		st.feed.push_back(e);
	}

	static void UpsertPing(ScanState& st, uintptr_t hero, const Vector3& o, int kind,
		const char* heroName, const ViewMatrix& vm, int screenW, int screenH) {
		Ping& p = st.pings[hero];
		if (p.name.buf[0] == 0) {
			p.started = st.now;
			p.name.set(heroName);
		}
		if (kind == Ping_Roshan || p.kind != Ping_Roshan)
			p.kind = kind;
		p.origin = o;
		p.lastSeen = st.now;
		Vector2 sp{};
		p.onScreen = WorldToScreen(o, sp, vm, screenW, screenH);
		p.screen = sp;
	}

	static void TrackActivity(const Memory& mem, ScanState& st, const EspSettings& s,
		uintptr_t entity, const Vector3& origin, const char* name,
		const ViewMatrix& vm, int screenW, int screenH) {
		const int act = mem.Read<int>(entity + offsets::C_DOTA_BaseNPC::m_NetworkActivity);
		const bool isCast = (act >= 1505 && act <= 1510) || act == 1515 || (act >= 1516 && act <= 1521);
		const bool isAttack = (act == 1503 || act == 1504);

		int prev = 0;
		auto it = st.prevActivity.find(entity);
		if (it != st.prevActivity.end()) prev = it->second;
		const bool wasCast = (prev >= 1505 && prev <= 1510) || prev == 1515 || (prev >= 1516 && prev <= 1521);

		char pretty[28];
		PrettyName(name, 14, pretty, 28);

		// cast / ult / channel -> location ping + feed line
		if (s.showCastTracker && isCast && !wasCast) {
			UpsertPing(st, entity, origin, Ping_Cast, pretty, vm, screenW, screenH);
			float& gate = st.eventGate[entity * 4 + Ping_Cast];
			if (st.now - gate > 2.5f) {
				gate = st.now;
				PushFeed(st, Ping_Cast, "%s %s", pretty,
					act == 1515 ? "ULTIMATE!" : (act >= 1516 ? "CHANNELING" : "CASTING"));
			}
		}

		if (isAttack) {
			// attacking a neutral camp -> jungling indicator
			if (s.showJungleTracker) {
				for (const Vector3& n : st.neutralPts) {
					float dx = origin.x - n.x, dy = origin.y - n.y;
					if (dx * dx + dy * dy < 360000.0f) { // within 600 units of a camp
						UpsertPing(st, entity, origin, Ping_Jungle, pretty, vm, screenW, screenH);
						float& gate = st.eventGate[entity * 4 + Ping_Jungle];
						if (st.now - gate > 4.0f) {
							gate = st.now;
							PushFeed(st, Ping_Jungle, "%s is JUNGLING", pretty);
						}
						break;
					}
				}
			}
			// attacking near Roshan -> log it
			if (s.showRoshanLog && st.now - st.lastRoshanSeen < 6.0f) {
				float dx = origin.x - st.lastRoshanPos.x, dy = origin.y - st.lastRoshanPos.y;
				if (dx * dx + dy * dy < 490000.0f) { // within 700 units of Roshan
					UpsertPing(st, entity, origin, Ping_Roshan, pretty, vm, screenW, screenH);
					float& gate = st.eventGate[entity * 4 + Ping_Roshan];
					if (st.now - gate > 5.0f) {
						gate = st.now;
						PushFeed(st, Ping_Roshan, "%s is attacking ROSHAN!", pretty);
						printf("[+] ROSHAN is being attacked by %s\n", pretty);
					}
				}
			}
		}

		st.prevActivity[entity] = act;
	}

	struct ScanOutput {
		std::vector<EspTarget> heroes;
		std::vector<CreepTarget> creeps;
		std::vector<MarkerTarget> markers;
		std::vector<GhostTarget> ghostList;
		std::vector<Ping> pingList;
		std::vector<FeedEvent> feed;
		float now = 0.0f;
	};

	// ------------------------------------------------------------------
	// THE single-pass scan.
	// ------------------------------------------------------------------
	static void ScanAll(const Memory& mem, ScanState& st, const EspSettings& s,
		const ViewMatrix& vm, int screenW, int screenH,
		ScanOutput& out) {

		out.heroes.clear();
		out.creeps.clear();
		out.markers.clear();
		out.ghostList.clear();
		out.pingList.clear();
		out.feed = st.feed;
		out.now = st.now;
		st.neutralPts.clear();

		const LocalState& L = st.local;
		const float maxDistSq = s.maxDrawDistance * s.maxDrawDistance;
		const int chunkSize = offsets::CGameEntitySystem::ChunkSize;

		for (int ci = 0; ci < ChunkCount; ci++) {
			if (!st.chunks[ci])
				continue;
			for (int slot = 0; slot < chunkSize; slot++) {
				uintptr_t entity = EntityFromBuffer(st, ci, slot);
				if (!entity || entity == L.pawn)
					continue;

				// name pointer straight from the bulk identity buffer -- zero
				// RPM per entity (this used to be thousands of calls/scan).
				// Buffer offset == chunkBase-relative identity addr: stride*slot + 0x20
				const size_t nameOff = (size_t)EntityLayout::Get().stride * (size_t)slot + 0x20;
				uintptr_t namePtr;
				memcpy(&namePtr, &st.identityBuf[nameOff], sizeof(namePtr));
				const char* name = CachedNamePtr(mem, st, namePtr);
				if (!name)
					continue;

				// announcer / killing-spree banner entities are not heroes
				if (ContainsI(name, "announcer"))
					continue;

				// ---------------- NEUTRAL CAMPS (jungle-tracker reference) ----
				if (strncmp(name, "npc_dota_neutral", 16) == 0) {
					if (mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_lifeState) == 0
						&& st.neutralPts.size() < 64) {
						Vector3 o{};
						if (GetEntityOrigin(mem, entity, o))
							st.neutralPts.push_back(o);
					}
					continue;
				}

				// ---------------- HERO ----------------
				if (strncmp(name, "npc_dota_hero_", 14) == 0) {
					if (mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_lifeState) != 0)
						continue;
					int health = mem.Read<int>(entity + offsets::C_BaseEntity::m_iHealth);
					int maxHealth = mem.Read<int>(entity + offsets::C_BaseEntity::m_iMaxHealth);
					if (health <= 0 || maxHealth <= 0)
						continue;

					uint8_t team = mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_iTeamNum);
					bool enemy = (team != L.team);

					Vector3 origin{};
					if (!GetEntityOrigin(mem, entity, origin))
						continue;

					float dx = origin.x - L.origin.x;
					float dy = origin.y - L.origin.y;
					float distSq = dx * dx + dy * dy;

					// refresh the last-known-position record (ghost fallback)
					GhostTarget& g = st.ghosts[entity];
					g.id = entity;
					g.origin = origin;
					g.age = -1.0f; // sentinel: seen this scan
					g.enemy = enemy;
					g.invis = mem.Read<float>(entity + offsets::C_DOTA_BaseNPC::m_flInvisibilityLevel) > 0.01f;
					PrettyName(name, 14, g.name.buf, 28);

					// cast / jungle / roshan activity tracking (enemies only)
					if (enemy)
						TrackActivity(mem, st, s, entity, origin, name, vm, screenW, screenH);

					if (distSq > maxDistSq)
						continue;
					if (enemy ? !s.showEnemies : !s.showAllies)
						continue;

					// teleport-aware linear smoothing
					Vector3 smooth = origin;
					auto it = st.smoothed.find(entity);
					if (it != st.smoothed.end()) {
						float jx = origin.x - it->second.x, jy = origin.y - it->second.y;
						if (jx * jx + jy * jy < 62500.0f) { // 250^2 units
							float a = 1.0f - s.smoothing;
							smooth.x = it->second.x + (origin.x - it->second.x) * a;
							smooth.y = it->second.y + (origin.y - it->second.y) * a;
							smooth.z = it->second.z + (origin.z - it->second.z) * a;
						}
					}
					st.smoothed[entity] = smooth;

					Vector3 head = smooth; head.z += 90.0f;
					Vector2 sFeet{}, sHead{};
					if (!WorldToScreen(smooth, sFeet, vm, screenW, screenH))
						continue;
					if (!WorldToScreen(head, sHead, vm, screenW, screenH))
						continue;
					float boxH = sFeet.y - sHead.y;
					if (boxH <= 0)
						continue;
					float boxW = boxH * 0.55f;

					EspTarget t;
					t.id = entity;
					t.x = sHead.x - boxW * 0.5f;
					t.y = sHead.y;
					t.w = boxW;
					t.h = boxH;
					t.health = health;
					t.maxHealth = maxHealth;
					t.distance = sqrtf(distSq);
					t.enemy = enemy;
					t.invis = g.invis;
					t.isIllusion = mem.Read<uint8_t>(entity + offsets::C_DOTA_BaseNPC::m_bIsIllusion) != 0;
					if (t.isIllusion && !s.showIllusions)
						continue; // illusions fully controlled by their toggle

					float mana = mem.Read<float>(entity + offsets::C_DOTA_BaseNPC::m_flMana);
					float maxMana = mem.Read<float>(entity + offsets::C_DOTA_BaseNPC::m_flMaxMana);
					int level = mem.Read<int>(entity + offsets::runtime::m_iCurrentLevel);
					t.mana = (int)mana;
					t.maxMana = (int)(maxMana > 0 ? maxMana : 100.0f);
					t.level = (level > 0 && level <= 30) ? level : 0; // implausible read -> badge hidden entirely

					t.offscreen = (t.x + t.w < 0) || (t.x > screenW) || (t.y + t.h < 0) || (t.y > screenH);
					t.screenCenter = Vector2{ sHead.x, sFeet.y };
					PrettyName(name, 14, t.name.buf, 28);

					if (s.showPlayerNames && enemy) {
						uintptr_t owner = mem.Read<uintptr_t>(entity + offsets::C_BaseEntity::m_hOwnerEntity);
						if (owner > 0x10000ull) {
							char pn[24] = {};
							if (mem.ReadRaw(owner + offsets::CBasePlayerController::m_iszPlayerName, pn, sizeof(pn) - 1)
								&& pn[0] >= 32 && pn[0] < 127)
								t.playerName.set(pn);
						}
					}

					if (enemy && s.showItems) {
						auto rit = st.itemRefresh.find(entity);
						if (rit == st.itemRefresh.end() || st.now - rit->second > s.itemRefreshInterval) {
							ReadItems(mem, st, entity, t);
							st.itemRefresh[entity] = st.now;
						}
					}

					out.heroes.push_back(t);
					continue;
				}

				// ---------------- CREEP / BUILDING ----------------
				if (strstr(name, "creep") || strstr(name, "building")) {
					if (mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_lifeState) != 0)
						continue;
					int health = mem.Read<int>(entity + offsets::C_BaseEntity::m_iHealth);
					int maxHealth = mem.Read<int>(entity + offsets::C_BaseEntity::m_iMaxHealth);
					if (health <= 0 || maxHealth <= 0)
						continue;

					uint8_t team = mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_iTeamNum);
					bool ally = (team == L.team);

				// cheap filter first: only spend projection reads on
				// creeps that are actually interesting
				bool killNow = false, killSoon = false, deny = false;
				if (!ally) {
					// Conservative damage (low-roll) for reliable last-hit detection.
					// Dota 2 applies +/-10% random on base damage per attack.
					float cdmg = L.damage * 0.9f;
					killNow = cdmg > 0 && health > 0 && health <= (int)cdmg;
					killSoon = !killNow && cdmg > 0 && health > 0 && health <= (int)(cdmg * 1.15f);
				} else {
					deny = health > 0 && health <= (int)(maxHealth * s.denyPct * 0.01f);
				}
					if (!killNow && !killSoon && !deny)
						continue;

					Vector3 origin{};
					if (!GetEntityOrigin(mem, entity, origin))
						continue;
					float dx = origin.x - L.origin.x, dy = origin.y - L.origin.y;
					if (dx * dx + dy * dy > maxDistSq)
						continue;

					Vector3 head = origin; head.z += 60.0f;
					Vector2 sFeet{}, sHead{};
					if (!WorldToScreen(origin, sFeet, vm, screenW, screenH))
						continue;
					if (!WorldToScreen(head, sHead, vm, screenW, screenH))
						continue;
					float boxH = sFeet.y - sHead.y;
					if (boxH <= 0)
						continue;
					float boxW = boxH * 0.6f;

					CreepTarget c;
					c.id = entity;
					c.x = sHead.x - boxW * 0.5f;
					c.y = sHead.y;
					c.w = boxW;
					c.h = boxH;
					c.health = health;
					c.maxHealth = maxHealth;
					c.distance = sqrtf(dx * dx + dy * dy);
					c.isKillableNow = killNow;
					c.isKillableSoon = killSoon;
					c.isDenyable = deny;
					c.isAlly = ally;
					c.name.set(name);
					out.creeps.push_back(c);
					continue;
				}

				// ---------------- ENEMY WARDS ----------------
				if (strncmp(name, "npc_dota_ward", 13) == 0) {
					uint8_t team = mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_iTeamNum);
					if (team == L.team)
						continue;
					Vector3 origin{};
					if (!GetEntityOrigin(mem, entity, origin))
						continue;
					float dx = origin.x - L.origin.x, dy = origin.y - L.origin.y;
					if (dx * dx + dy * dy > maxDistSq)
						continue;
					bool sentry = strstr(name, "truesight") != nullptr;
					MarkerTarget m;
					m.kind = sentry ? Marker_WardSentry : Marker_WardObserver;
					m.origin = origin;
					m.distance = sqrtf(dx * dx + dy * dy);
					ProjectMarker(m, vm, screenW, screenH);
					m.name.set(sentry ? "Sentry" : "Observer");
					out.markers.push_back(m);
					continue;
				}

				// ---------------- ROSHAN ----------------
				if (strncmp(name, "npc_dota_roshan", 15) == 0) {
					if (mem.Read<uint8_t>(entity + offsets::C_BaseEntity::m_lifeState) != 0)
						continue;
					Vector3 origin{};
					if (!GetEntityOrigin(mem, entity, origin))
						continue;
					st.lastRoshanPos = origin;
					st.lastRoshanSeen = st.now;
					MarkerTarget m;
					m.kind = Marker_Roshan;
					m.origin = origin;
					m.health = mem.Read<int>(entity + offsets::C_BaseEntity::m_iHealth);
					m.maxHealth = mem.Read<int>(entity + offsets::C_BaseEntity::m_iMaxHealth);
					float dx = origin.x - L.origin.x, dy = origin.y - L.origin.y;
					m.distance = sqrtf(dx * dx + dy * dy);
					ProjectMarker(m, vm, screenW, screenH);
					m.name.set("ROSHAN");
					out.markers.push_back(m);
					continue;
				}

				// ---------------- RUNES ----------------
				if (strncmp(name, "npc_dota_rune", 13) == 0) {
					Vector3 origin{};
					if (!GetEntityOrigin(mem, entity, origin))
						continue;
					float dx = origin.x - L.origin.x, dy = origin.y - L.origin.y;
					if (dx * dx + dy * dy > maxDistSq)
						continue;
					MarkerTarget m;
					m.kind = Marker_Rune;
					m.origin = origin;
					m.distance = sqrtf(dx * dx + dy * dy);
					ProjectMarker(m, vm, screenW, screenH);
					char pretty[24];
					PrettyName(name, 13, pretty, 24);
					m.name.set(pretty);
					out.markers.push_back(m);
					continue;
				}
			}
		}

		// age + prune ghosts (heroes seen this scan carry the -1 sentinel)
		const float step = st.now - st.lastScanTime;
		for (auto it = st.ghosts.begin(); it != st.ghosts.end();) {
			GhostTarget& g = it->second;
			if (g.age < 0.0f) g.age = 0.0f;
			else g.age += (step > 0.0f && step < 1.0f) ? step : 0.033f;
			if (g.age > s.ghostFadeTime)
				it = st.ghosts.erase(it);
			else
				++it;
		}
		st.lastScanTime = st.now;

		// keep the auxiliary caches bounded (entity churn safety)
		if (st.smoothed.size() > 512) st.smoothed.clear();
		if (st.itemRefresh.size() > 512) st.itemRefresh.clear();
		if (st.nameCache.size() > 8192) st.nameCache.clear();

		// Build the ghost draw list (fallback: last known position)
		for (auto& kv : st.ghosts) {
			GhostTarget& g = kv.second;
			if (g.age <= 0.0001f)
				continue; // fresh this scan -> drawn as a real target
			Vector2 sp{};
			if (!WorldToScreen(g.origin, sp, vm, screenW, screenH))
				continue;
			g.screen = sp;
			g.onScreen = (sp.x > -60 && sp.x < screenW + 60 && sp.y > -60 && sp.y < screenH + 60);
			float dx = g.origin.x - st.local.origin.x, dy = g.origin.y - st.local.origin.y;
			g.distance = sqrtf(dx * dx + dy * dy);
			out.ghostList.push_back(g);
		}

		// prune expired tracker pings + publish tracker output
		for (auto it = st.pings.begin(); it != st.pings.end();) {
			const Ping& p = it->second;
			float life = (p.kind == Ping_Cast) ? s.castMarkerTime : 2.5f;
			if (st.now - p.lastSeen > life)
				it = st.pings.erase(it);
			else
				++it;
		}
		for (auto& kv : st.pings)
			out.pingList.push_back(kv.second);
		if (st.prevActivity.size() > 512) st.prevActivity.clear();
		if (st.eventGate.size() > 512) st.eventGate.clear();
		out.feed = st.feed;
		out.now = st.now;
	}

	static void ProjectMarker(MarkerTarget& m, const ViewMatrix& vm, int screenW, int screenH) {
		Vector2 sp{};
		m.onScreen = WorldToScreen(m.origin, sp, vm, screenW, screenH);
		m.screen = sp;
	}

	// Attack-range circle points around the local hero (read-only visual).
	static void BuildRangeCircle(const ScanState& st, float radius,
		const ViewMatrix& vm, int screenW, int screenH,
		std::vector<Vector2>& pts) {
		if (radius <= 0)
			return;
		pts.clear();
		pts.reserve(33);
		for (int i = 0; i <= 32; i++) {
			float a = i * (6.2831853f / 32.0f);
			Vector3 p{ st.local.origin.x + cosf(a) * radius,
				       st.local.origin.y + sinf(a) * radius,
				       st.local.origin.z };
			Vector2 sp{};
			if (!WorldToScreen(p, sp, vm, screenW, screenH)) {
				pts.push_back(Vector2{ -1, -1 }); // segment break
				continue;
			}
			pts.push_back(sp);
		}
	}
};
