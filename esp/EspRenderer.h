#pragma once
#include "imgui.h"
#include "Config.h"
#include <vector>
#include <cstdio>

class EspRenderer {
public:
	static ImU32 HealthColor(float pct) {
		pct = pct < 0 ? 0 : (pct > 1 ? 1 : pct);
		int r, g;
		if (pct > 0.5f) { r = (int)((1.0f - pct) * 2 * 255); g = 255; }
		else { r = 255; g = (int)(pct * 2 * 255); }
		return IM_COL32(r, g, 60, 255);
	}

	static void Draw(const EspSettings& cfg, const std::vector<EspTarget>& targets, const std::vector<CreepTarget>& creeps = {}) {
		if (!cfg.enabled)
			return;

		ImDrawList* draw = ImGui::GetBackgroundDrawList();

		for (const auto& t : targets) {
			if (t.enemy && !cfg.showEnemies) continue;
			if (!t.enemy && !cfg.showAllies) continue;

			ImU32 accent = t.enemy ? IM_COL32(235, 64, 52, 255) : IM_COL32(60, 200, 120, 255);
			float x = t.x, y = t.y, w = t.w, h = t.h;

			if (cfg.cornerStyle) {
				DrawCornerBox(draw, x, y, w, h, accent, cfg.boxThickness);
			} else {
				draw->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), accent, 3.0f, 0, cfg.boxThickness);
			}

			if (cfg.showHealthBar && t.maxHealth > 0) {
				DrawHealthBar(draw, t, x, y, h);
			}

			DrawLabel(draw, t, cfg, x, y, w, h);

			if (cfg.showHealthBar && t.maxHealth > 0) {
				DrawHpText(draw, t, x, y, w, h);
			}
		}

		if (cfg.showLastHitHelper) {
			for (const auto& c : creeps) {
				if (!c.isKillableNow && !c.isKillableSoon) continue;

				float x = c.x, y = c.y, w = c.w, h = c.h;
				ImU32 color = c.isKillableNow ? IM_COL32(255, 50, 50, 255) : IM_COL32(255, 200, 0, 255);

				// Draw glowing highlight box around creep
				draw->AddRect(ImVec2(x - 2, y - 2), ImVec2(x + w + 2, y + h + 2), color, 4.0f, 0, 2.5f);

				// Draw Last Hit marker banner
				const char* text = c.isKillableNow ? "LAST HIT NOW!" : "ATTACK SOON";
				ImVec2 textSize = ImGui::CalcTextSize(text);
				float textX = x + w / 2.0f - textSize.x / 2.0f;
				float textY = y - textSize.y - 4;

				draw->AddRectFilled(ImVec2(textX - 4, textY - 2), ImVec2(textX + textSize.x + 4, textY + textSize.y + 2), IM_COL32(0, 0, 0, 180), 4.0f);
				draw->AddText(ImVec2(textX, textY), color, text);
			}
		}
	}

private:
	static void DrawCornerBox(ImDrawList* draw, float x, float y, float w, float h, ImU32 accent, float thickness) {
		float corner = w * 0.22f;
		if (corner < 6.0f) corner = 6.0f;

		draw->AddLine(ImVec2(x, y), ImVec2(x + corner, y), accent, thickness);
		draw->AddLine(ImVec2(x, y), ImVec2(x, y + corner), accent, thickness);

		draw->AddLine(ImVec2(x + w, y), ImVec2(x + w - corner, y), accent, thickness);
		draw->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + corner), accent, thickness);

		draw->AddLine(ImVec2(x, y + h), ImVec2(x + corner, y + h), accent, thickness);
		draw->AddLine(ImVec2(x, y + h), ImVec2(x, y + h - corner), accent, thickness);

		draw->AddLine(ImVec2(x + w, y + h), ImVec2(x + w - corner, y + h), accent, thickness);
		draw->AddLine(ImVec2(x + w, y + h), ImVec2(x + w, y + h - corner), accent, thickness);
	}

	static void DrawHealthBar(ImDrawList* draw, const EspTarget& t, float x, float y, float h) {
		float pct = (float)t.health / (float)t.maxHealth;
		if (pct < 0) pct = 0; if (pct > 1) pct = 1;

		float barX = x - 10, barW = 5.0f;
		draw->AddRectFilled(ImVec2(barX, y), ImVec2(barX + barW, y + h), IM_COL32(30, 30, 34, 160), 2.5f);
		float filledH = h * pct;
		draw->AddRectFilled(ImVec2(barX, y + (h - filledH)), ImVec2(barX + barW, y + h), HealthColor(pct), 2.5f);
	}

	static void DrawLabel(ImDrawList* draw, const EspTarget& t, const EspSettings& cfg, float x, float y, float w, float h) {
		std::string label = cfg.showNames ? t.name : "";
		if (cfg.showDistance && t.distance > 0) {
			char buf[32];
			snprintf(buf, sizeof(buf), "  %.0fm", t.distance / 60.0f);
			label += buf;
		}

		if (!label.empty()) {
			ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
			float labelX = x + w / 2.0f - textSize.x / 2.0f;
			float labelY = y - textSize.y - 6;
			draw->AddRectFilled(ImVec2(labelX - 4, labelY - 2), ImVec2(labelX + textSize.x + 4, labelY + textSize.y + 2),
				IM_COL32(18, 18, 22, 160), 4.0f);
			draw->AddText(ImVec2(labelX, labelY), IM_COL32(255, 255, 255, 255), label.c_str());
		}
	}

	static void DrawHpText(ImDrawList* draw, const EspTarget& t, float x, float y, float w, float h) {
		char hpBuf[32];
		snprintf(hpBuf, sizeof(hpBuf), "%d / %d", t.health, t.maxHealth);
		ImVec2 hpSize = ImGui::CalcTextSize(hpBuf);
		draw->AddText(ImVec2(x + w / 2.0f - hpSize.x / 2.0f, y + h + 4),
			IM_COL32(220, 220, 225, 255), hpBuf);
	}
};
