#pragma once
#include "imgui.h"
#include "Config.h"
#include "Snapshot.h"
#include <vector>
#include <cstdio>
#include <cmath>

class EspRenderer {
public:
	// ------------------------------------------------------------------
	static ImU32 Accent(const EspSettings& cfg, int a = 255) {
		return IM_COL32((int)(cfg.accent[0] * 255), (int)(cfg.accent[1] * 255), (int)(cfg.accent[2] * 255), a);
	}

	static ImU32 HealthColor(float pct) {
		pct = pct < 0 ? 0 : (pct > 1 ? 1 : pct);
		int r, g;
		if (pct > 0.5f) { r = (int)((1.0f - pct) * 2 * 255); g = 255; }
		else { r = 255; g = (int)(pct * 2 * 255); }
		return IM_COL32(r, g, 60, 255);
	}

	// ------------------------------------------------------------------
	static void Draw(const EspSettings& cfg,
		const EntityReader::ScanOutput& scan,
		const LhFrame& lh,
		const std::vector<Vector2>& rangeCircle,
		float deltaTime, float fps)
	{
		ImDrawList* draw = ImGui::GetBackgroundDrawList();
		const float W = (float)ImGui::GetIO().DisplaySize.x;
		const float H = (float)ImGui::GetIO().DisplaySize.y;

		if (cfg.showAttackRange)
			DrawRangeCircle(draw, rangeCircle, cfg);

		if (cfg.enabled) {
			if (cfg.showGhosts)
				for (const auto& g : scan.ghostList) {
					if (!g.enemy && !cfg.showAllies)
						continue; // allies stay hidden
					DrawGhost(draw, g, cfg, W, H);
				}

			for (const auto& m : scan.markers)
				DrawMarker(draw, m, cfg);

			// creep markers come from the high-rate last-hit engine
			for (const auto& c : lh.creepMarkers)
				DrawCreep(draw, c, cfg);

			for (const auto& t : scan.heroes)
				DrawHero(draw, t, cfg, W, H);

			for (const auto& p : scan.pingList)
				DrawPing(draw, p, cfg);
		}

		if (cfg.showCastTracker || cfg.showJungleTracker || cfg.showRoshanLog)
			DrawFeed(draw, scan, cfg, W, H);

		if (cfg.showLastHitHud)
			DrawLastHitHud(draw, lh, cfg);

		if (cfg.showFps) {
			char fpsT[32];
			snprintf(fpsT, sizeof(fpsT), "FPS %d", (int)(fps > 0.0f ? fps : (deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f)));
			draw->AddText(ImVec2(12, 10), Accent(cfg, 220), fpsT);
		}
		if (cfg.showWatermark) {
			const char* wm = "DRAGONBURN  |  read-only";
			ImVec2 sz = CalcTextSizeCached(wm);
			float x = W - sz.x - 14.0f, y = H - sz.y - 12.0f;
			draw->AddRectFilled(ImVec2(x - 8, y - 4), ImVec2(x + sz.x + 8, y + sz.y + 4),
				IM_COL32(8, 10, 16, 150), 6.0f);
			draw->AddText(ImVec2(x, y), Accent(cfg, 200), wm);
		}
	}

	// ------------------------------------------------------------------
private:
	// tiny text-size cache: ESP labels repeat every frame
	static ImVec2 CalcTextSizeCached(const char* s) {
		return ImGui::CalcTextSize(s);
	}

	static void DrawGlowRect(ImDrawList* draw, float x, float y, float w, float h, ImU32 accent, float th, bool corners, bool glow) {
		if (glow)
			draw->AddRect(ImVec2(x - 2.5f, y - 2.5f), ImVec2(x + w + 2.5f, y + h + 2.5f),
				(accent & 0x00FFFFFF) | 0x28000000, 4.0f, 0, th + 3.0f);
		if (corners) {
			float c = w * 0.22f;
			if (c < 6.0f) c = 6.0f;
			draw->AddLine(ImVec2(x, y), ImVec2(x + c, y), accent, th);
			draw->AddLine(ImVec2(x, y), ImVec2(x, y + c), accent, th);
			draw->AddLine(ImVec2(x + w, y), ImVec2(x + w - c, y), accent, th);
			draw->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + c), accent, th);
			draw->AddLine(ImVec2(x, y + h), ImVec2(x + c, y + h), accent, th);
			draw->AddLine(ImVec2(x, y + h), ImVec2(x, y + h - c), accent, th);
			draw->AddLine(ImVec2(x + w, y + h), ImVec2(x + w - c, y + h), accent, th);
			draw->AddLine(ImVec2(x + w, y + h), ImVec2(x + w, y + h - c), accent, th);
		} else {
			draw->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), accent, 4.0f, 0, th);
		}
	}

	// horizontal rounded bar with border; returns its top y
	static float DrawBar(ImDrawList* draw, float cx, float y, float w, float h, float pct, ImU32 fill, ImU32 back) {
		draw->AddRectFilled(ImVec2(cx - w * 0.5f - 1, y - 1), ImVec2(cx + w * 0.5f + 1, y + h + 1),
			IM_COL32(10, 12, 18, 190), h);
		draw->AddRectFilled(ImVec2(cx - w * 0.5f, y), ImVec2(cx - w * 0.5f + w * pct, y + h), fill, h - 2);
		return y;
	}

	// ------------------------------------------------------------------
	static void DrawHero(ImDrawList* draw, const EspTarget& t, const EspSettings& cfg, float W, float H) {
		float x = t.x, y = t.y, w = t.w, h = t.h;

		// off-screen: edge arrow instead of the full box
		if (t.offscreen) {
			if (cfg.showOffscreen && (t.enemy || cfg.showAllies))
				DrawEdgeArrow(draw, t.screenCenter.x, t.y + t.h * 0.5f, t.enemy, cfg, W, H, nullptr);
			return;
		}

		ImU32 accent = t.isIllusion ? IM_COL32(0, 180, 255, 255)
			: (t.enemy ? IM_COL32(235, 64, 52, 255) : IM_COL32(60, 200, 120, 255));

		DrawGlowRect(draw, x, y, w, h, accent, cfg.boxThickness, cfg.cornerStyle, cfg.showGlow && t.enemy);

		// health + mana bars on top of the box
		float barW = w > 60 ? 60 : w;
		float by = y - 12;
		if (cfg.showHealthBar && t.maxHealth > 0)
			by = DrawBar(draw, x + w * 0.5f, y - 12, barW, 5, (float)t.health / t.maxHealth, HealthColor((float)t.health / t.maxHealth), 0) - 8;
		if (cfg.showManaBar && t.maxMana > 0)
			DrawBar(draw, x + w * 0.5f, by, barW, 3.5f, (float)t.mana / t.maxMana, IM_COL32(70, 150, 250, 255), 0);

		// level badge
		if (cfg.showHeroLevel && t.level > 0) {
			char lvl[8];
			snprintf(lvl, sizeof(lvl), "%d", t.level);
			ImVec2 lsz = CalcTextSizeCached(lvl);
			float cx = x - 11, cy = y + h - 11;
			draw->AddCircleFilled(ImVec2(cx, cy), 9.0f, IM_COL32(28, 30, 40, 230));
			draw->AddCircle(ImVec2(cx, cy), 9.0f, IM_COL32(255, 210, 80, 255), 0, 1.4f);
			draw->AddText(ImVec2(cx - lsz.x * 0.5f, cy - lsz.y * 0.5f), IM_COL32(255, 224, 130, 255), lvl);
		}

		// labels above the bars
		char top[96];
		int len = 0;
		if (cfg.showNames) {
			for (int i = 0; i < 27 && t.name.buf[i]; i++) top[len++] = t.name.buf[i];
		}
		if (cfg.showPlayerNames && t.playerName.buf[0]) {
			len += snprintf(top + len, sizeof(top) - len, "%s[%s]", len ? " " : "", t.playerName.c_str());
		}
		if (cfg.showDistance && t.distance > 0) {
			len += snprintf(top + len, sizeof(top) - len, "%s%.0fm", len ? " " : "", t.distance / 39.37f);
		}
		if (len > 0) {
			top[len] = 0;
			ImVec2 tsz = CalcTextSizeCached(top);
			float tx = x + w * 0.5f - tsz.x * 0.5f;
			float ty = y - 30;
			draw->AddRectFilled(ImVec2(tx - 5, ty - 2), ImVec2(tx + tsz.x + 5, ty + tsz.y + 2),
				IM_COL32(10, 12, 18, 170), 4.0f);
			draw->AddText(ImVec2(tx, ty), t.enemy ? IM_COL32(255, 210, 205, 255) : IM_COL32(225, 245, 230, 255), top);
		}

		// badges: illusion / invis
		float badgeY = y + h + 5;
		if (cfg.showIllusions && t.isIllusion) {
			badgeY = DrawBadge(draw, x + w * 0.5f, badgeY, "ILLUSION", IM_COL32(0, 140, 230, 210));
		}
		if (cfg.showInvisTag && t.invis) {
			DrawBadge(draw, x + w * 0.5f, badgeY, "INVISIBLE", IM_COL32(150, 80, 230, 210));
		}

		// hp text under the box
		if (cfg.showHealthBar && t.maxHealth > 0) {
			char hp[24];
			snprintf(hp, sizeof(hp), "%d", t.health);
			ImVec2 hsz = CalcTextSizeCached(hp);
			draw->AddText(ImVec2(x + w * 0.5f - hsz.x * 0.5f, badgeY + 2), IM_COL32(220, 225, 235, 230), hp);
		}

		// read-only item row
		if (t.itemCount > 0) {
			float iy = badgeY + 20;
			float ix = x + w * 0.5f;
			float totalW = 0;
			ImVec2 sizes[6];
			for (int i = 0; i < t.itemCount; i++) {
				sizes[i] = CalcTextSizeCached(t.items[i].c_str());
				totalW += sizes[i].x + 6;
			}
			float cx0 = ix - totalW * 0.5f;
			for (int i = 0; i < t.itemCount; i++) {
				draw->AddRectFilled(ImVec2(cx0, iy), ImVec2(cx0 + sizes[i].x + 4, iy + sizes[i].y + 2),
					IM_COL32(14, 18, 28, 190), 3.0f);
				draw->AddText(ImVec2(cx0 + 2, iy), IM_COL32(170, 200, 235, 235), t.items[i].c_str());
				cx0 += sizes[i].x + 6;
			}
		}
	}

	static float DrawBadge(ImDrawList* draw, float cx, float y, const char* text, ImU32 col) {
		ImVec2 sz = CalcTextSizeCached(text);
		float x = cx - sz.x * 0.5f - 4;
		draw->AddRectFilled(ImVec2(x, y), ImVec2(x + sz.x + 8, y + sz.y + 3), col, 3.0f);
		draw->AddText(ImVec2(x + 4, y + 1), IM_COL32(255, 255, 255, 240), text);
		return y + sz.y + 5;
	}

	// ------------------------------------------------------------------
	// Creep marker fed by the high-rate last-hit engine:
	//   red   = kill it NOW (HP <= effective min damage)
	//   yellow = killable soon (countdown = predicted seconds)
	//   blue  = deny NOW / dim blue = deny soon
	// ------------------------------------------------------------------
	static void DrawCreep(ImDrawList* draw, const CreepTarget& c, const EspSettings& cfg) {
		if (!cfg.showLastHitHelper)
			return;
		if (!c.isKillableNow && !c.isKillableSoon && !c.isDenyable && !c.isDenySoon)
			return;

		bool now = c.isKillableNow || c.isDenyable;
		ImU32 color = c.isKillableNow ? IM_COL32(255, 60, 60, 255)
			: c.isDenyable ? IM_COL32(120, 170, 255, 255)
			: (c.isKillableSoon ? IM_COL32(255, 200, 0, 255) : IM_COL32(110, 140, 210, 200));

		DrawGlowRect(draw, c.x, c.y, c.w, c.h, color, 2.0f, true, now && !c.isDenyable);

		// label: announce text, countdown, or damage needed
		char label[48];
		label[0] = 0;
		if (cfg.showLastHitAnnounce) {
			const char* text = c.isKillableNow ? "LAST HIT" : (c.isDenyable ? "DENY" : "");
			if (text[0]) snprintf(label, sizeof(label), "%s", text);
		}
		if (label[0] == 0 && cfg.showLastHitCountdown && (c.isKillableSoon || c.isDenySoon)) {
			if (c.secondsToReady < 9.95f)
				snprintf(label, sizeof(label), "%.1fs", c.secondsToReady);
		}
		if (label[0]) {
			ImVec2 tsz = CalcTextSizeCached(label);
			float tx = c.x + c.w * 0.5f - tsz.x * 0.5f;
			float ty = c.y - tsz.y - 5.0f;
			draw->AddRectFilled(ImVec2(tx - 4, ty - 2), ImVec2(tx + tsz.x + 4, ty + tsz.y + 2),
				IM_COL32(0, 0, 0, 180), 4.0f);
			draw->AddText(ImVec2(tx, ty), color, label);
		}

		// mini hp bar
		float pct = c.maxHealth > 0 ? (float)c.health / (float)c.maxHealth : 0.0f;
		DrawBar(draw, c.x + c.w * 0.5f, c.y - 7, c.w > 40 ? 40 : c.w, 3.5f, pct,
			c.isAlly ? IM_COL32(120, 170, 255, 255) : HealthColor(pct), 0);
	}

	// ------------------------------------------------------------------
	// Compact one-line live status of the last-hit engine (bottom-left),
	// so it is always visible WHY the engine does or does not act.
	// ------------------------------------------------------------------
	static void DrawLastHitHud(ImDrawList* draw, const LhFrame& lh, const EspSettings& cfg) {
		const LastHitStats& st = lh.stats;
		if (!st.tracking)
			return;

		char line[128];
		int ready = 0, soon = 0;
		for (const auto& c : lh.creepMarkers) {
			if (c.isKillableNow || c.isDenyable) ready++;
			else if (c.isKillableSoon || c.isDenySoon) soon++;
		}

		char lat[24];
		if (st.latencySamples > 0)
			snprintf(lat, sizeof(lat), " | %dms", (int)(st.latencyMs + 0.5f));
		else
			lat[0] = 0;
		snprintf(line, sizeof(line), "LH %s | %d-%d dmg | ready %d soon %d | %d creeps%s",
			st.active ? "ACTIVE" : (cfg.autoLastHit ? "armed" : "off"),
			st.dmgMin, st.dmgMax, ready, soon, st.tracked, lat);

		ImVec2 sz = CalcTextSizeCached(line);
		float x = 12.0f, y = 30.0f;
		draw->AddRectFilled(ImVec2(x - 6, y - 3), ImVec2(x + sz.x + 6, y + sz.y + 3),
			IM_COL32(8, 10, 16, 160), 5.0f);
		draw->AddText(ImVec2(x, y), st.active ? IM_COL32(120, 255, 140, 235) : IM_COL32(200, 210, 225, 190), line);
	}

	// ------------------------------------------------------------------
	static void DrawMarker(ImDrawList* draw, const MarkerTarget& m, const EspSettings& cfg) {
		if (!m.onScreen)
			return;
		ImVec2 p = ImVec2(m.screen.x, m.screen.y);

		switch (m.kind) {
		case Marker_WardObserver: {
			if (!cfg.showWards) return;
			ImU32 col = IM_COL32(80, 220, 255, 235);
			draw->AddCircleFilled(p, 7.0f, IM_COL32(10, 40, 60, 200));
			draw->AddCircle(p, 7.0f, col, 0, 2.0f);
			draw->AddCircleFilled(p, 2.5f, col);
			DrawMarkerLabel(draw, p, m.name.c_str(), col, m.distance);
			break;
		}
		case Marker_WardSentry: {
			if (!cfg.showWards) return;
			ImU32 col = IM_COL32(255, 170, 60, 235);
			draw->AddRect(ImVec2(p.x - 6, p.y - 6), ImVec2(p.x + 6, p.y + 6), col, 2.0f, 0, 2.0f);
			draw->AddRectFilled(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + 2, p.y + 2), col);
			DrawMarkerLabel(draw, p, m.name.c_str(), col, m.distance);
			break;
		}
		case Marker_Roshan: {
			if (!cfg.showRoshan) return;
			ImU32 col = IM_COL32(230, 90, 220, 255);
			draw->AddTriangleFilled(ImVec2(p.x, p.y - 10), ImVec2(p.x - 9, p.y + 7), ImVec2(p.x + 9, p.y + 7),
				IM_COL32(230, 90, 220, 60));
			draw->AddTriangle(ImVec2(p.x, p.y - 10), ImVec2(p.x - 9, p.y + 7), ImVec2(p.x + 9, p.y + 7), col, 2.0f);
			if (m.maxHealth > 0) {
				float pct = (float)m.health / m.maxHealth;
				DrawBar(draw, p.x, p.y + 14, 70, 5, pct, HealthColor(pct), 0);
				char hp[32];
				snprintf(hp, sizeof(hp), "%s %d/%d", m.name.c_str(), m.health, m.maxHealth);
				ImVec2 tsz = CalcTextSizeCached(hp);
				draw->AddText(ImVec2(p.x - tsz.x * 0.5f, p.y + 22), col, hp);
			}
			break;
		}
		case Marker_Rune: {
			if (!cfg.showRunes) return;
			ImU32 col = IM_COL32(120, 255, 170, 235);
			draw->AddCircle(p, 8.0f, col, 0, 2.0f);
			draw->AddCircleFilled(p, 3.0f, col);
			DrawMarkerLabel(draw, p, m.name.c_str(), col, m.distance);
			break;
		}
		}
	}

	static void DrawMarkerLabel(ImDrawList* draw, const ImVec2& p, const char* text, ImU32 col, float dist) {
		char buf[48];
		snprintf(buf, sizeof(buf), "%s %.0fm", text, dist / 39.37f);
		ImVec2 tsz = CalcTextSizeCached(buf);
		float x = p.x - tsz.x * 0.5f, y = p.y - tsz.y - 12;
		draw->AddRectFilled(ImVec2(x - 4, y - 2), ImVec2(x + tsz.x + 4, y + tsz.y + 2),
			IM_COL32(8, 10, 16, 180), 4.0f);
		draw->AddText(ImVec2(x, y), col, buf);
	}

	// ------------------------------------------------------------------
	// Ghost = hero we could not read/project this frame; draw at the last
	// known position, fading out with age. Off-screen -> edge arrow.
	// ------------------------------------------------------------------
	static void DrawGhost(ImDrawList* draw, const GhostTarget& g, const EspSettings& cfg, float W, float H) {
		float fade = 1.0f - g.age / (cfg.ghostFadeTime > 0 ? cfg.ghostFadeTime : 1.0f);
		if (fade < 0.15f) fade = 0.15f;
		ImU32 col = g.enemy ? IM_COL32(235, 64, 52, (int)(200 * fade)) : IM_COL32(60, 200, 120, (int)(200 * fade));

		char label[64];
		snprintf(label, sizeof(label), "%s HERE %.0fs AGO", g.name.c_str(), g.age);

		if (!g.onScreen) {
			if (cfg.showOffscreen)
				DrawEdgeArrow(draw, g.screen.x, g.screen.y, g.enemy, cfg, W, H, label);
			return;
		}

		ImVec2 p(g.screen.x, g.screen.y);
		float s = 9.0f;
		draw->AddQuadFilled(ImVec2(p.x, p.y - s), ImVec2(p.x + s, p.y), ImVec2(p.x, p.y + s), ImVec2(p.x - s, p.y),
			(col & 0x00FFFFFF) | 0x50000000);
		draw->AddQuad(ImVec2(p.x, p.y - s), ImVec2(p.x + s, p.y), ImVec2(p.x, p.y + s), ImVec2(p.x - s, p.y), col, 2.0f);

		ImVec2 tsz = CalcTextSizeCached(label);
		float x = p.x - tsz.x * 0.5f, y = p.y + s + 4;
		draw->AddRectFilled(ImVec2(x - 4, y - 2), ImVec2(x + tsz.x + 4, y + tsz.y + 2),
			IM_COL32(8, 10, 16, (int)(170 * fade)), 4.0f);
		draw->AddText(ImVec2(x, y), (col & 0x00FFFFFF) | (ImU32)(235 * fade) << 24, label);
	}

	// ------------------------------------------------------------------
	static void DrawPing(ImDrawList* draw, const EntityReader::Ping& p, const EspSettings& cfg) {
		if (p.kind == EntityReader::Ping_Cast && !cfg.showCastTracker) return;
		if (p.kind == EntityReader::Ping_Jungle && !cfg.showJungleTracker) return;
		if (p.kind == EntityReader::Ping_Roshan && !cfg.showRoshanLog) return;
		if (!p.onScreen) return;

		ImU32 col;
		const char* tag;
		switch (p.kind) {
		case EntityReader::Ping_Roshan: col = IM_COL32(255, 90, 60, 255);  tag = "ROSHAN"; break;
		case EntityReader::Ping_Jungle: col = IM_COL32(255, 205, 70, 255); tag = "JUNGLE"; break;
		default:                        col = IM_COL32(195, 120, 255, 255); tag = "CAST"; break;
		}

		ImVec2 c(p.screen.x, p.screen.y);
		float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 5.0f);
		float r = 14.0f + 6.0f * pulse;
		draw->AddCircleFilled(c, r, (col & 0x00FFFFFF) | 0x38000000);
		draw->AddCircle(c, r, col, 0, 2.2f);
		draw->AddCircleFilled(c, 3.0f, col);

		char label[64];
		snprintf(label, sizeof(label), "%s  %s", tag, p.name.c_str());
		ImVec2 tsz = CalcTextSizeCached(label);
		float x = c.x - tsz.x * 0.5f, y = c.y + r + 5.0f;
		draw->AddRectFilled(ImVec2(x - 4, y - 2), ImVec2(x + tsz.x + 4, y + tsz.y + 2),
			IM_COL32(8, 10, 16, 185), 4.0f);
		draw->AddText(ImVec2(x, y), (col & 0x00FFFFFF) | 0xF0000000, label);
	}

	// ------------------------------------------------------------------
	static void DrawFeed(ImDrawList* draw, const EntityReader::ScanOutput& scan,
		const EspSettings& cfg, float W, float H) {
		(void)H;
		float y = 32.0f;
		for (const auto& e : scan.feed) {
			float age = scan.now - e.time;
			if (age > 8.0f)
				continue;
			float alpha = age > 6.0f ? 1.0f - (age - 6.0f) / 2.0f : 1.0f;
			ImU32 col;
			switch (e.kind) {
			case EntityReader::Ping_Roshan: col = IM_COL32(255, 90, 60, 255);  break;
			case EntityReader::Ping_Jungle: col = IM_COL32(255, 205, 70, 255); break;
			default:                        col = IM_COL32(195, 120, 255, 255); break;
			}
			ImU32 txt = (col & 0x00FFFFFF) | (ImU32)(235.0f * alpha) << 24;
			ImU32 bgc = IM_COL32(8, 10, 16, (int)(185 * alpha));
			ImVec2 tsz = CalcTextSizeCached(e.text.c_str());
			float x = W - tsz.x - 26.0f;
			draw->AddRectFilled(ImVec2(x - 8, y - 3), ImVec2(x + tsz.x + 8, y + tsz.y + 3), bgc, 5.0f);
			draw->AddRectFilled(ImVec2(x - 8, y - 3), ImVec2(x - 4, y + tsz.y + 3), (col & 0x00FFFFFF) | (ImU32)(220.0f * alpha) << 24, 2.0f);
			draw->AddText(ImVec2(x, y), txt, e.text.c_str());
			y += tsz.y + 9.0f;
		}
	}

	// ------------------------------------------------------------------
	static void DrawEdgeArrow(ImDrawList* draw, float wx, float wy, bool enemy,
		const EspSettings& cfg, float W, float H, const char* label) {
		const float m = 34.0f;
		float cx = W * 0.5f, cy = H * 0.5f;
		float dx = wx - cx, dy = wy - cy;

		// clamp direction to the screen border rectangle
		float scale = 1e9f;
		if (fabsf(dx) > 0.001f) scale = (W * 0.5f - m) / fabsf(dx);
		if (fabsf(dy) > 0.001f) scale = (scale < (H * 0.5f - m) / fabsf(dy)) ? scale : (H * 0.5f - m) / fabsf(dy);
		float ax = cx + dx * scale, ay = cy + dy * scale;
		ax = ax < m ? m : (ax > W - m ? W - m : ax);
		ay = ay < m ? m : (ay > H - m ? H - m : ay);

		float len = sqrtf(dx * dx + dy * dy);
		if (len < 0.001f) return;
		float nx = dx / len, ny = dy / len;

		ImU32 col = enemy ? IM_COL32(235, 64, 52, 220) : Accent(cfg, 220);
		ImVec2 tip(ax + nx * 10, ay + ny * 10);
		ImVec2 l(ax - nx * 6 - ny * 7, ay - ny * 6 + nx * 7);
		ImVec2 r(ax - nx * 6 + ny * 7, ay - ny * 6 - nx * 7);
		draw->AddTriangleFilled(tip, l, r, (col & 0x00FFFFFF) | 0xA0000000);
		draw->AddTriangle(tip, l, r, col, 1.6f);

		if (label && label[0]) {
			ImVec2 tsz = CalcTextSizeCached(label);
			float tx = ax - tsz.x * 0.5f - nx * 26.0f;
			float ty = ay - tsz.y * 0.5f - ny * 26.0f;
			tx = tx < 4 ? 4 : (tx > W - tsz.x - 4 ? W - tsz.x - 4 : tx);
			ty = ty < 4 ? 4 : (ty > H - tsz.y - 4 ? H - tsz.y - 4 : ty);
			draw->AddText(ImVec2(tx, ty), (col & 0x00FFFFFF) | 0xD0000000, label);
		}
	}

	// ------------------------------------------------------------------
	static void DrawRangeCircle(ImDrawList* draw, const std::vector<Vector2>& pts, const EspSettings& cfg) {
		ImU32 col = Accent(cfg, 110);
		for (size_t i = 1; i < pts.size(); i++) {
			const Vector2& a = pts[i - 1];
			const Vector2& b = pts[i];
			if (a.x < 0 || b.x < 0)
				continue;
			draw->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), col, 1.5f);
		}
	}
};
