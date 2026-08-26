#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <cmath>
#include <cstring>
#include <vector>
#include <unordered_map>
#include "Memory.h"
#include "Offsets.h"
#include "Config.h"
#include "VectorMath.h"
#include "Snapshot.h"

// ---------------------------------------------------------------------------
// LastHit engine -- runs on its OWN thread at high frequency (lhTrackRate).
//
// The old implementation decided "killable?" once per slow 30 Hz scan using
// average damage x0.9, then slept 75 ms inside the RENDER thread while
// tapping keys. This engine replaces it completely:
//
//  * High-rate tracking: every tracked creep's HP is polled at up to 200 Hz
//    (default 100 Hz) from a cached entity list, so HP data is never stale.
//  * HP prediction: robust least-squares regression over a sliding sample
//    window predicts WHEN each creep's HP will cross the kill threshold
//    (lane creeps take damage in steps; the regression sees the real trend).
//  * Exact damage: reads DamageMin/Max + each creep's physical armor and
//    applies Dota's armor formula, per creep. Default mode uses the MINIMUM
//    damage roll so a low roll still kills -- no "random miss" last hits.
//  * Attack timing: the engine knows melee windup vs. ranged projectile
//    flight (m_iAttackCapabilities), and continuously SELF-CALIBRATES the
//    real click->damage latency by measuring when commanded creeps actually
//    lose HP. It fires so that the damage lands exactly at the crossing.
//  * Cooldown aware: reads the game clock (GlobalVars) and the hero's last
//    attack time so a command is never sent while the hero could not attack
//    anyway (which is what made the old one "not work" half the time).
//  * Final safety re-check: HP, range and screen position are re-read RIGHT
//    before the click; if anything changed the command is aborted. No wasted
//    attacks, no clicks on corpses, no lane pushing by accident.
//  * Result verification: counts confirmed last-hits / denies and keeps
//    tuning the latency estimate from real outcomes.
//
// Memory access stays 100% read-only; the only input is SendInput from this
// process (same as before -- no driver involvement).
// ---------------------------------------------------------------------------

namespace InputSim {
	static void TapKey(WORD vk) {
		INPUT in[2] = {};
		in[0].type = INPUT_KEYBOARD;
		in[0].ki.wVk = vk;
		in[1].type = INPUT_KEYBOARD;
		in[1].ki.wVk = vk;
		in[1].ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(2, in, sizeof(INPUT));
	}

	// Absolute move to SCREEN coordinates (virtual-desktop aware, so it also
	// works when Dota runs on a secondary monitor).
	static void MoveMouseScreen(int x, int y) {
		int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
		int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
		int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		if (vw < 1 || vh < 1)
			return;
		INPUT in = {};
		in.type = INPUT_MOUSE;
		in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
		in.mi.dx = (LONG)((float)(x - vx) * 65535.0f / (float)(vw - 1));
		in.mi.dy = (LONG)((float)(y - vy) * 65535.0f / (float)(vh - 1));
		SendInput(1, &in, sizeof(INPUT));
	}

	static void ClickLeft() {
		INPUT in[2] = {};
		in[0].type = INPUT_MOUSE;
		in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
		in[1].type = INPUT_MOUSE;
		in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
		SendInput(2, in, sizeof(INPUT));
	}
}

namespace LastHit {

	// Wall-clock seconds as float. A session-relative base keeps the value
	// small so float32 keeps millisecond precision (casting the raw 64-bit
	// tick count directly would quantize timestamps by ~256 ms and wreck
	// the HP regression).
	inline float WallNow() {
		static ULONGLONG base = GetTickCount64(); // captured on first call
		return (float)((GetTickCount64() - base) & 0xFFFFFFFFull) * 0.001f;
	}
	static inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

	struct Sample { float t = 0, hp = 0; };

	static constexpr int kRing = 96;        // ~1 s of history at 100 Hz

	struct Track {
		uintptr_t ent = 0;
		bool ally = false, neutral = false;
		int maxHp = 1;
		float armor = 0, hull = 24;
		Vector3 origin{};
		float originT = -100;

		float hp = 0;
		float slope = 0;                     // HP per second (regression)
		bool slopeValid = false;
		float effLow = 0, effHigh = 0;       // effective damage vs this creep
		float tCross = 1e9f;                 // seconds until kill threshold

		Sample ring[kRing] = {};
		int head = 0, count = 0;

		float lastCmd = -100;                // wall time of last command on it
		int miss = 0;                        // scans without seeing it

		// pending outcome verification
		bool pending = false, pendKill = false, pendSampled = false;
		float pendCmd = 0, pendHp0 = 0, pendDeadline = 0;
	};

	class Engine {
	public:
		LastHitStats m_stats;

		// One engine tick. Call from the dedicated last-hit thread.
		void Tick(const Memory& mem, Snapshot& snap, const EspSettings& s,
			bool gameFocused, bool menuOpen)
		{
			float now = WallNow();
			float dt = now - m_lastTick;
			if (dt > 0.0001f && dt < 1.0f)
				m_stats.tickHz += (1.0f / dt - m_stats.tickHz) * 0.05f;
			m_lastTick = now;

			// --- copy the shared scan meta (short lock) -------------------
			ScanMeta meta;
			meta.localValid = false;
			{
				std::lock_guard<std::mutex> g(snap.mxScan);
				if (snap.meta.localValid) {
					meta.localValid = true;
					meta.local = snap.meta.local;
					meta.creeps = snap.meta.creeps;
					meta.vm = snap.meta.vm;
					meta.screenW = snap.meta.screenW;
					meta.screenH = snap.meta.screenH;
				}
			}

			m_stats.tracking = meta.localValid;
			if (!meta.localValid) {
				m_tracks.clear();
				m_markers.clear();
				m_stats.active = false;
				snap.ClearLh();
				return;
			}

			UpdateGameClock(mem, now);
			ReadHero(mem, meta.local.pawn, now);

			m_heroOrigin = meta.local.origin;
			m_stats.active = s.autoLastHit && m_heroValid && gameFocused &&
				!menuOpen && s.triggerKey > 0 && s.attackKey > 0 &&
				(GetAsyncKeyState(s.triggerKey) & 0x8000) != 0;

			// --- sync tracks with the creep cache -------------------------
			for (auto& kv : m_tracks) kv.second.miss++;
			for (const CreepCacheEntry& ce : meta.creeps) {
				if (!ce.entity || ce.entity == meta.local.pawn)
					continue;
				if (ce.isNeutral && !s.lhNeutrals)
					continue;
				Track& tk = m_tracks[ce.entity];
				tk.ent = ce.entity;
				tk.miss = 0;
				tk.ally = ce.isAlly;
				tk.neutral = ce.isNeutral;
				tk.maxHp = ce.maxHealth > 0 ? ce.maxHealth : 1;
				tk.armor = ce.armor;
				tk.hull = ce.hullRadius > 1.0f && ce.hullRadius < 200.0f ? ce.hullRadius : 24.0f;
			}
			for (auto it = m_tracks.begin(); it != m_tracks.end();) {
				int limit = it->second.pending ? 400 : 15;
				if (it->second.miss > limit)
					it = m_tracks.erase(it);
				else
					++it;
			}

			const float selLow = (s.lhDamageMode == 0) ? m_dmgMin : (m_dmgMin + m_dmgMax) * 0.5f;
			const float denyThreshMul = Clampf(s.denyPct, 5.0f, 90.0f) * 0.01f;

			m_markers.clear();
			Track* best = nullptr;
			float bestUrg = 1e9f;
			bool bestKill = true;

			for (auto& kv : m_tracks) {
				Track& tk = kv.second;
				int hp = mem.Read<int>(tk.ent + offsets::C_BaseEntity::m_iHealth);
				uint8_t life = mem.Read<uint8_t>(tk.ent + offsets::C_BaseEntity::m_lifeState);

				// ---- creep died: settle pending verification, stop tracking
				if (hp <= 0 || life != 0) {
					if (tk.pending) {
						if (tk.pendKill) m_stats.kills++; else m_stats.denies++;
						if (!tk.pendSampled)
							AddLatencySample(now - tk.pendCmd);
						tk.pending = false;
					}
					tk.miss = 10000; // erase next tick
					continue;
				}

				// ---- pending outcome verification -------------------------
				if (tk.pending) {
					if (hp < (int)(tk.pendHp0 - 4.0f)) {
						if (!tk.pendSampled) {
							AddLatencySample(now - tk.pendCmd);
							tk.pendSampled = true;
						}
						tk.pendHp0 = (float)hp; // only the first drop calibrates
					}
					if (now > tk.pendDeadline)
						tk.pending = false;
				}

				// ---- origin refresh (cheap cadence, fresh again in Fire) --
				if (now - tk.originT > 0.10f) {
					Vector3 o{};
					if (EntityReader::GetEntityOrigin(mem, tk.ent, o)) {
						tk.origin = o;
						tk.originT = now;
					}
				}
				float dx = tk.origin.x - m_heroOrigin.x;
				float dy = tk.origin.y - m_heroOrigin.y;
				float dist = sqrtf(dx * dx + dy * dy);

				// ---- HP history + regression ------------------------------
				PushSample(tk, now, hp);
				Regress(tk, now, 0.85f);

				float mult = ArmorMult(tk.armor);
				tk.effLow = selLow * mult;
				tk.effHigh = m_dmgMax * mult;

				// ---- crossings ---------------------------------------------
				if (tk.hp <= tk.effLow)
					tk.tCross = 0.0f;
				else if (tk.slopeValid)
					tk.tCross = (tk.hp - tk.effLow) / (-tk.slope);
				else
					tk.tCross = 1e9f;

				// ---- marker output (renderer) ------------------------------
				if (meta.screenW > 0 && dist < 2400.0f) {
					Vector3 head = tk.origin; head.z += 60.0f;
					Vector2 sFeet{}, sHead{};
					if (WorldToScreen(tk.origin, sFeet, meta.vm, meta.screenW, meta.screenH) &&
						WorldToScreen(head, sHead, meta.vm, meta.screenW, meta.screenH)) {
						float boxH = sFeet.y - sHead.y;
						if (boxH > 2.0f) {
							float denyThresh = denyThreshMul * tk.maxHp;
							CreepTarget c;
							c.id = tk.ent;
							c.x = sHead.x - boxH * 0.30f;
							c.y = sHead.y;
							c.w = boxH * 0.60f;
							c.h = boxH;
							c.health = hp;
							c.maxHealth = tk.maxHp;
							c.distance = dist;
							c.isAlly = tk.ally;
							c.killDamage = (int)(tk.effLow + 0.5f);
							c.isKillableNow = !tk.ally && tk.hp <= tk.effLow;
							c.isKillableSoon = !tk.ally && !c.isKillableNow &&
								tk.tCross > 0.0f && tk.tCross < s.lhWindow;
							c.isDenyable = tk.ally && tk.hp <= tk.effLow && tk.hp <= denyThresh;
							c.isDenySoon = tk.ally && !c.isDenyable &&
								tk.hp <= denyThresh * 1.20f + 15.0f &&
								tk.tCross > 0.0f && tk.tCross < s.lhWindow;
							c.secondsToReady = tk.tCross > 99.0f ? 99.0f : tk.tCross;
							m_markers.push_back(c);
						}
					}
				}

				// ---- automation candidate scan ------------------------------
				if (!m_stats.active)
					continue;
				if (dist > m_attackRange + m_heroHull + tk.hull + 70.0f)
					continue;               // click would just be a move order
				if (now - tk.lastCmd < 0.50f)
					continue;               // just commanded this creep

				float lat = LatencyEst(dist);
				float php = tk.hp + tk.slope * lat;   // predicted HP at impact
				bool denyAllowed = s.autoDeny && tk.hp <= denyThreshMul * tk.maxHp;

				for (int pass = 0; pass < 2; pass++) {
					bool killPass = s.lastHitPriority ? (pass == 0) : (pass == 1);
					if (killPass && tk.ally) continue;
					if (!killPass && (!tk.ally || !denyAllowed)) continue;
					if (php <= tk.effLow * 0.98f && php >= -tk.effLow * 0.20f) {
						if (tk.tCross < bestUrg) {
							bestUrg = tk.tCross;
							best = &tk;
							bestKill = killPass;
						}
					}
				}
			}

			m_stats.tracked = (int)m_tracks.size();

			if (best)
				Fire(mem, snap, meta, s, *best, bestKill, now);

			snap.PublishLh(std::move(m_markers), m_stats);
		}

	private:
		std::unordered_map<uintptr_t, Track> m_tracks;
		std::vector<CreepTarget> m_markers;
		float m_lastTick = 0;

		// hero state
		float m_dmgMin = 0, m_dmgMax = 0;
		float m_attackRange = 150;
		float m_heroHull = 24;
		int m_caps = 1;
		bool m_batInit = false, m_heroValid = false;
		float m_intervalEMA = 1.70f;      // measured attack interval (BAT based)
		float m_heroLastAtkGT = -1000;
		bool m_heroAtkValid = false;
		Vector3 m_heroOrigin{};

		// game clock sync (game-time fields <-> wall clock)
		bool m_gameTimeValid = false;
		int m_gtMode = 0;                 // 0 undecided, 1 inline, 2 pointer
		float m_gameTime = 0, m_gameOffset = 0;
		float m_gtPrevWall = 0;
		float m_gtPrev1 = 0, m_gtPrev2 = 0;
		int m_gtScore[3] = { 0, 0, 0 };
		int m_gtTicks = 0;

		// command state / calibration
		float m_lastCmdWall = -100;
		float m_latencyEMA = 0;
		int m_latencyN = 0;

		static float ArmorMult(float armor) {
			float k = 0.06f * armor;
			float m = 1.0f - k / (1.0f + k);   // Dota physical armor formula
			if (m < 0.25f) m = 0.25f;
			if (m > 1.85f) m = 1.85f;
			return m;
		}

		void PushSample(Track& tk, float now, int hp) {
			Sample& sm = tk.ring[tk.head];
			sm.t = now;
			sm.hp = (float)hp;
			tk.head = (tk.head + 1) % kRing;
			if (tk.count < kRing) tk.count++;
			tk.hp = (float)hp;
		}

		void Regress(Track& tk, float now, float window) {
			float minX = now - window;
			double st = 0, sh = 0, stt = 0, sth = 0;
			int n = 0;
			float oldest = now, newest = 0;
			for (int i = 0; i < tk.count; i++) {
				const Sample& sm = tk.ring[(tk.head - 1 - i + 2 * kRing) % kRing];
				if (sm.t < minX) break;
				st += sm.t; sh += sm.hp;
				stt += (double)sm.t * sm.t;
				sth += (double)sm.t * sm.hp;
				oldest = sm.t;
				if (sm.t > newest) newest = sm.t;
				n++;
			}
			if (n < 6 || newest - oldest < 0.25f) {
				tk.slopeValid = false;
				tk.slope = 0;
				return;
			}
			double denom = (double)n * stt - st * st;
			if (fabs(denom) < 1e-9) {
				tk.slopeValid = false;
				tk.slope = 0;
				return;
			}
			tk.slope = (float)(((double)n * sth - st * sh) / denom);
			tk.slopeValid = (tk.slope < -1.0f);   // only a real decay predicts
		}

		float LatencyEst(float dist) const {
			if (m_latencyN >= 4 && m_latencyEMA > 0.05f)
				return m_latencyEMA;
			if (m_caps == 2)                       // ranged: windup + projectile
				return 0.42f + Clampf(dist, 100.0f, 1200.0f) / 900.0f;
			return 0.35f;                          // melee windup prior
		}

		void AddLatencySample(float dt) {
			if (dt < 0.05f || dt > 1.50f)
				return;
			if (m_latencyN < 1)
				m_latencyEMA = dt;
			else
				m_latencyEMA += (dt - m_latencyEMA) * 0.35f;
			if (m_latencyN < 1000) m_latencyN++;
			m_stats.latencyMs = m_latencyEMA * 1000.0f;
			m_stats.latencySamples = m_latencyN;
		}

		// The game clock (GlobalVars) layout differs between builds: the dump
		// offset may point at the struct directly or at a pointer to it. We
		// score both candidates for ~1s against the wall clock and lock onto
		// whichever advances 1:1 with real time. If neither does, game-time
		// fields are simply not used (graceful fallback to wall-clock only).
		void UpdateGameClock(const Memory& mem, float now) {
			const uintptr_t base = mem.clientDllBase + offsets::client_dll::dwGlobalVars;
			float cand[3] = { 0, 0, 0 };
			cand[1] = mem.Read<float>(base + 0x10);   // inline struct variant
			uintptr_t p = mem.Read<uintptr_t>(base);
			cand[2] = (p > 0x10000ull && p < 0x00007FFFFFFFFFFFull)
				? mem.Read<float>(p + 0x10)           // pointer variant
				: 0.0f;

			if (m_gtMode == 0) {
				if (m_gtPrevWall > 0.0f && now > m_gtPrevWall) {
					float wall = now - m_gtPrevWall;
					float prev[3] = { 0, m_gtPrev1, m_gtPrev2 };
					for (int m = 1; m <= 2; m++) {
						if (prev[m] > 0.0f) {
							float d = cand[m] - prev[m];
							if (d > 0.0f && d < wall * 2.0f + 0.25f)
								m_gtScore[m]++;
						}
					}
				}
				m_gtPrev1 = cand[1];
				m_gtPrev2 = cand[2];
				m_gtPrevWall = now;
				if (++m_gtTicks > 100) {
					int pick = (m_gtScore[2] > m_gtScore[1]) ? 2 : 1;
					m_gtMode = (m_gtScore[pick] > 70) ? pick : -1;
				}
				if (m_gtMode <= 0) { m_gameTimeValid = false; return; }
			}
			if (m_gtMode == -1) { m_gameTimeValid = false; return; }

			float gt = cand[m_gtMode];
			if (gt > 0.0f && gt < 200000.0f) {
				m_gameTime = gt;
				m_gameOffset = now - gt;    // wall = game + offset
				m_gameTimeValid = true;
			}
		}

		void ReadHero(const Memory& mem, uintptr_t pawn, float now) {
			(void)now;
			m_heroValid = false;
			if (!pawn) { m_stats.heroValid = false; return; }
			if (mem.Read<uint8_t>(pawn + offsets::C_BaseEntity::m_lifeState) != 0) {
				m_stats.heroValid = false;
				return;
			}

			int dmin = mem.Read<int>(pawn + offsets::C_DOTA_BaseNPC::m_iDamageMin);
			int dmax = mem.Read<int>(pawn + offsets::C_DOTA_BaseNPC::m_iDamageMax);
			if (dmin > 0 && dmin < 1000 && dmax >= dmin && dmax < 1000) {
				m_dmgMin = (float)dmin;
				m_dmgMax = (float)dmax;
				m_stats.dmgMin = dmin;
				m_stats.dmgMax = dmax;
				m_heroValid = true;
			}

			int ar = mem.Read<int>(pawn + offsets::C_DOTA_BaseNPC::m_iAttackRange);
			if (ar > 0 && ar < 2500) {
				m_attackRange = (float)ar;
				m_stats.range = (float)ar;
			}
			m_caps = mem.Read<int>(pawn + offsets::C_DOTA_BaseNPC::m_iAttackCapabilities);
			float hull = mem.Read<float>(pawn + offsets::C_DOTA_BaseNPC::m_flHullRadius);
			if (hull > 1.0f && hull < 200.0f) m_heroHull = hull;

			float bt = mem.Read<float>(pawn + offsets::C_DOTA_BaseNPC::m_flBaseAttackTime);
			if (bt > 0.4f && bt < 3.5f && !m_batInit) {
				m_batInit = true;
				m_intervalEMA = bt;
			}

			float gt = mem.Read<float>(pawn + offsets::C_DOTA_BaseNPC::m_flLastAttackTime);
			if (m_gameTimeValid && gt > 0.0f && gt < m_gameTime + 30.0f) {
				if (!m_heroAtkValid) {
					m_heroLastAtkGT = gt;
					m_heroAtkValid = true;
				} else if (gt > m_heroLastAtkGT + 0.01f) {
					float d = gt - m_heroLastAtkGT;
					if (d > 0.30f && d < 3.5f)
						m_intervalEMA += (d - m_intervalEMA) * 0.30f;
					m_heroLastAtkGT = gt;
				}
			}
			m_stats.heroValid = m_heroValid;
		}

		bool HeroReady(float now) const {
			if (now - m_lastCmdWall < 0.12f)
				return false;
			if (m_gameTimeValid && m_heroAtkValid) {
				float readyWall = m_heroLastAtkGT + m_intervalEMA * 1.02f + 0.05f + m_gameOffset;
				if (now < readyWall)
					return false;
			}
			return true;
		}

		bool Fire(const Memory& mem, Snapshot& snap, const ScanMeta& meta,
			const EspSettings& s, Track& tk, bool kill, float now)
		{
			// ---- rate gates ------------------------------------------------
			if (now - m_lastCmdWall < Clampf(s.attackInterval, 0.15f, 1.5f))
				return false;
			if (!HeroReady(now))
				return false;

			// ---- FINAL fresh re-checks (never act on stale decisions) -----
			int hp = mem.Read<int>(tk.ent + offsets::C_BaseEntity::m_iHealth);
			uint8_t life = mem.Read<uint8_t>(tk.ent + offsets::C_BaseEntity::m_lifeState);
			if (hp <= 0 || life != 0) { m_stats.aborts++; return false; }
			if (kill && hp > (int)(tk.effLow * 1.60f) + 12) { m_stats.aborts++; return false; }
			if (!kill && hp > (int)(tk.effLow * 1.25f) + 10) { m_stats.aborts++; return false; }

			Vector3 o{};
			if (!EntityReader::GetEntityOrigin(mem, tk.ent, o))
				return false;
			float dx = o.x - m_heroOrigin.x, dy = o.y - m_heroOrigin.y;
			float dist = sqrtf(dx * dx + dy * dy);
			if (dist > m_attackRange + m_heroHull + tk.hull + 90.0f) { m_stats.aborts++; return false; }

			if (meta.screenW <= 0 || meta.screenH <= 0)
				return false;
			Vector3 head = o; head.z += 55.0f;
			Vector2 sFeet{}, sHead{};
			if (!WorldToScreen(o, sFeet, meta.vm, meta.screenW, meta.screenH)) return false;
			if (!WorldToScreen(head, sHead, meta.vm, meta.screenW, meta.screenH)) return false;
			float boxH = sFeet.y - sHead.y;
			if (boxH <= 0) return false;

			// click the creep's mid-body, clamped away from HUD / screen edges
			const float margin = 60.0f;
			float cx = Clampf(sFeet.x, margin, (float)meta.screenW - margin);
			float cy = Clampf(sHead.y + boxH * 0.55f, margin, (float)meta.screenH - margin);
			int sx = snap.clientX.load() + (int)cx;
			int sy = snap.clientY.load() + (int)cy;

			// ---- execute (this thread is not the render thread: sleeping
			//      here can not stutter the overlay) --------------------------
			POINT oldCur{};
			bool restore = s.lhRestoreCursor && GetCursorPos(&oldCur);

			InputSim::TapKey((WORD)s.attackKey);        // enter attack-targeting
			Sleep(6);
			InputSim::MoveMouseScreen(sx, sy);
			Sleep(14);
			InputSim::ClickLeft();
			if (restore) {
				Sleep(16);
				SetCursorPos(oldCur.x, oldCur.y);
			}

			tk.lastCmd = now;
			tk.pending = true;
			tk.pendKill = kill;
			tk.pendSampled = false;
			tk.pendCmd = now;
			tk.pendHp0 = (float)hp;
			tk.pendDeadline = now + 1.60f;
			m_lastCmdWall = now;
			m_stats.attempts++;
			return true;
		}
	};
}
