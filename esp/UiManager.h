#pragma once
#include "imgui.h"
#include "Config.h"
#include "InputRouter.h"
#include <cstdio>
#include <cmath>
#include <unordered_map>
#include <Windows.h>

// ---------------------------------------------------------------------------
// UiManager -- DragonBurn menu.
// Layout mirrors a modern two-panel cheat UI:
//   [ tab pills row  Visuals | Players | World | Last Hit | Misc | Settings ]
//   [ feature toggles panel ]   [ feature configuration panel ]
// All widgets are custom-drawn for a clean rounded look and cheap rendering.
//
// Input model (new): the overlay window itself is permanently click-through.
// InputRouter feeds mouse events into ImGui and only eats clicks that land on
// this panel -- the game keeps receiving everything else (see InputRouter.h).
// The menu deliberately uses NO popup widgets (no Combo/Select), so every
// clickable pixel is inside the panel rectangle the router knows about.
// ---------------------------------------------------------------------------

class UiManager {
public:
	static ImFont* fontBold;

	// live menu geometry (overlay client space == game screen space); the
	// input router uses it so clicks outside the panel still reach the game
	static bool   menuVisible;
	static ImVec2 menuPos;
	static ImVec2 menuSize;

	static bool IsCapturingKey() { return s_waitingKey; }

	// ------------------------------------------------------------------
	static void ApplyTheme() {
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 12.0f;
		style.ChildRounding = 10.0f;
		style.FrameRounding = 7.0f;
		style.GrabRounding = 6.0f;
		style.PopupRounding = 8.0f;
		style.ScrollbarRounding = 8.0f;
		style.TabRounding = 7.0f;
		style.WindowBorderSize = 0.0f;
		style.ChildBorderSize = 0.0f;
		style.FrameBorderSize = 0.0f;
		style.WindowPadding = ImVec2(18, 14);
		style.FramePadding = ImVec2(10, 6);
		style.ItemSpacing = ImVec2(10, 7);
		style.ItemInnerSpacing = ImVec2(8, 6);
		style.ScrollbarSize = 10.0f;
		style.GrabMinSize = 10.0f;

		ImVec4* c = style.Colors;
		c[ImGuiCol_WindowBg] = ImVec4(0.033f, 0.043f, 0.070f, 0.985f);
		c[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.070f, 0.105f, 0.94f);
		c[ImGuiCol_PopupBg] = ImVec4(0.055f, 0.070f, 0.105f, 0.98f);
		c[ImGuiCol_FrameBg] = ImVec4(0.085f, 0.105f, 0.150f, 1.0f);
		c[ImGuiCol_FrameBgHovered] = ImVec4(0.110f, 0.135f, 0.190f, 1.0f);
		c[ImGuiCol_FrameBgActive] = ImVec4(0.130f, 0.160f, 0.220f, 1.0f);
		c[ImGuiCol_Text] = ImVec4(0.90f, 0.93f, 0.96f, 1.0f);
		c[ImGuiCol_TextDisabled] = ImVec4(0.44f, 0.49f, 0.58f, 1.0f);
		c[ImGuiCol_ScrollbarBg] = ImVec4(0.03f, 0.04f, 0.06f, 0.6f);
		c[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.24f, 0.32f, 1.0f);
		c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.33f, 0.42f, 1.0f);
		c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.34f, 0.40f, 0.50f, 1.0f);
		c[ImGuiCol_Separator] = ImVec4(0.14f, 0.17f, 0.23f, 1.0f);
		c[ImGuiCol_CheckMark] = ImVec4(0.05f, 0.85f, 0.75f, 1.0f);
		c[ImGuiCol_SliderGrab] = ImVec4(0.05f, 0.85f, 0.75f, 1.0f);
		c[ImGuiCol_SliderGrabActive] = ImVec4(0.10f, 0.95f, 0.85f, 1.0f);
		c[ImGuiCol_Button] = ImVec4(0.10f, 0.12f, 0.18f, 1.0f);
		c[ImGuiCol_ButtonHovered] = ImVec4(0.14f, 0.18f, 0.26f, 1.0f);
		c[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.23f, 0.33f, 1.0f);
		c[ImGuiCol_Header] = ImVec4(0.10f, 0.12f, 0.18f, 1.0f);
		c[ImGuiCol_HeaderHovered] = ImVec4(0.14f, 0.18f, 0.26f, 1.0f);
		c[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.23f, 0.33f, 1.0f);
		c[ImGuiCol_Border] = ImVec4(0.12f, 0.15f, 0.21f, 0.6f);
		c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);

		// Crisp UI font (falls back to the baked font when unavailable).
		ImGuiIO& io = ImGui::GetIO();
		ImFont* f = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.0f);
		fontBold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 17.0f);
		if (f) io.FontDefault = f;
	}

	// ------------------------------------------------------------------
	static void DrawMenu(bool& menuOpen, EspSettings& s, const LastHitStats& st, float fps) {
		ImGuiIO& io = ImGui::GetIO();
		float dt = io.DeltaTime;
		if (dt <= 0.0f) dt = 1.0f / 60.0f;

		// smooth accent color transitions
		static float cur[3] = { -1.0f, -1.0f, -1.0f };
		if (cur[0] < 0.0f) { cur[0] = s.accent[0]; cur[1] = s.accent[1]; cur[2] = s.accent[2]; }
		float ck = 1.0f - expf(-dt * 8.0f);
		for (int i = 0; i < 3; i++) cur[i] += (s.accent[i] - cur[i]) * ck;
		s_accent = IM_COL32((int)(cur[0] * 255), (int)(cur[1] * 255), (int)(cur[2] * 255), 255);
		s_accentDim = (s_accent & 0x00FFFFFF) | 0x30000000;

		// appear animation (replays every time the menu opens)
		static float appear = 1.0f;
		static double lastFrame = -1.0;
		double now = ImGui::GetTime();
		if (lastFrame < 0.0 || now - lastFrame > 0.4) appear = 0.0f;
		lastFrame = now;
		appear += (1.0f - appear) * (1.0f - expf(-dt * 10.0f));
		if (appear > 1.0f) appear = 1.0f;

		// remembered position (persists across opens and runs), always on-screen
		const ImVec2 size(920, 580);
		ImVec2 disp = io.DisplaySize;
		if (disp.x < 1.0f || disp.y < 1.0f) disp = ImVec2(1920.0f, 1080.0f);
		static bool posLoaded = false;
		if (!posLoaded) {
			menuPos = ImVec2((disp.x - size.x) * 0.5f, (disp.y - size.y) * 0.5f);
			LoadMenuPos(menuPos);
			posLoaded = true;
		}
		s_dragMoved = false;
		ClampMenuPos(menuPos, size, disp);

		ImVec2 drawPos = menuPos;
		drawPos.y += (1.0f - appear) * 28.0f;

		// soft drop shadow behind + around the panel
		ImDrawList* bg = ImGui::GetBackgroundDrawList();
		for (int i = 5; i >= 1; i--) {
			float e = i * 4.0f * appear;
			bg->AddRectFilled(ImVec2(drawPos.x - e, drawPos.y - e),
				ImVec2(drawPos.x + size.x + e, drawPos.y + size.y + e),
				IM_COL32(0, 0, 0, 18), 12.0f + e);
		}

		ImGui::SetNextWindowSize(size, ImGuiCond_Always);
		ImGui::SetNextWindowPos(drawPos, ImGuiCond_Always);

		ImGui::Begin("##DragonBurn", &menuOpen,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 wp = ImGui::GetWindowPos();
		ImVec2 wsz = ImGui::GetWindowSize();
		dl->AddRect(wp, ImVec2(wp.x + wsz.x, wp.y + wsz.y), s_accentDim, 12.0f, 0, 1.5f);

		DrawTitleRow(dt);
		ImGui::Spacing();

		float footerH = ImGui::GetTextLineHeight() + 18.0f;
		float panelH = ImGui::GetContentRegionAvail().y - footerH;
		float panelW = (ImGui::GetContentRegionAvail().x - 12.0f) * 0.5f;

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.050f, 0.064f, 0.098f, 0.94f));
		ImGui::BeginChild("##left", ImVec2(panelW, panelH), ImGuiChildFlags_Borders);
		DrawLeftPanel(s);
		ImGui::EndChild();

		ImGui::SameLine(0, 12.0f);
		ImGui::BeginChild("##right", ImVec2(panelW, panelH), ImGuiChildFlags_Borders);
		DrawRightPanel(s, st, fps);
		ImGui::EndChild();
		ImGui::PopStyleColor();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextDisabled(" [INSERT] toggle menu    hold [%s] last-hit / deny    the game mouse is never captured",
			KeyName(s.triggerKey));

		// keep the panel inside the game window; remember position across opens
		ImVec2 finalPos = ImGui::GetWindowPos();
		ClampMenuPos(finalPos, size, disp);
		if (finalPos.x != wp.x || finalPos.y != wp.y)
			ImGui::SetWindowPos(finalPos);
		menuPos = finalPos;
		menuSize = size;
		menuVisible = true;
		if (s_dragMoved)
			SaveMenuPos(menuPos);

		// tell the input router where the clickable panel lives
		InputRouter::SetMenuRect(menuPos.x, menuPos.y, menuSize.x, menuSize.y, true);

		PollKeyCapture();
		ImGui::End();
	}

	// ------------------------------------------------------------------
private:
	static ImU32 s_accent;
	static ImU32 s_accentDim;
	static bool s_waitingKey;
	static int* s_keyTarget;
	static uint8_t s_lastKeys[256];
	static int s_tab;
	static bool s_dragMoved;
	static bool s_pillInit;

	static void ClampMenuPos(ImVec2& p, const ImVec2& size, const ImVec2& disp) {
		if (p.x < 0.0f) p.x = 0.0f;
		if (p.y < 0.0f) p.y = 0.0f;
		if (p.x > disp.x - size.x) p.x = disp.x - size.x;
		if (p.y > disp.y - size.y) p.y = disp.y - size.y;
		if (p.x < 0.0f) p.x = 0.0f;
		if (p.y < 0.0f) p.y = 0.0f;
	}

	static void MenuPosPath(char* buf, size_t cap) {
		char exe[MAX_PATH];
		GetModuleFileNameA(nullptr, exe, MAX_PATH);
		char* slash = strrchr(exe, '\\');
		if (slash) *slash = 0;
		snprintf(buf, cap, "%s\\db_menu_pos.cfg", exe);
	}

	static void LoadMenuPos(ImVec2& p) {
		char path[MAX_PATH];
		MenuPosPath(path, sizeof(path));
		FILE* f = nullptr;
		if (fopen_s(&f, path, "rb") == 0 && f) {
			float xy[2] = {};
			if (fread(xy, sizeof(float), 2, f) == 2) p = ImVec2(xy[0], xy[1]);
			fclose(f);
		}
	}

	static void SaveMenuPos(const ImVec2& p) {
		char path[MAX_PATH];
		MenuPosPath(path, sizeof(path));
		FILE* f = nullptr;
		if (fopen_s(&f, path, "wb") == 0 && f) {
			float xy[2] = { p.x, p.y };
			fwrite(xy, sizeof(float), 2, f);
			fclose(f);
		}
	}

	// per-widget animation state {hover, on}
	static ImVec2& AnimState(ImGuiID id) {
		static std::unordered_map<ImGuiID, ImVec2> map;
		return map[id];
	}

	static void Header(const char* title) {
		if (fontBold) ImGui::PushFont(fontBold);
		ImGui::TextColored(ImColor(s_accent), "%s", title);
		if (fontBold) ImGui::PopFont();
		ImGui::Spacing();
	}

	static void Section(const char* t) {
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "%s", t);
		ImGui::Spacing();
	}

	static void Note(const char* t) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.60f, 0.70f, 1.0f));
		ImGui::TextWrapped("%s", t);
		ImGui::PopStyleColor();
	}

	static void Warning(const char* t) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.62f, 0.25f, 1.0f));
		ImGui::TextWrapped("! %s", t);
		ImGui::PopStyleColor();
	}

	static void ToggleRow(const char* label, bool* v) {
		ImVec2 p = ImGui::GetCursorScreenPos();
		float rowH = ImGui::GetTextLineHeight() + 12.0f;
		float width = ImGui::GetContentRegionAvail().x;
		float dt = ImGui::GetIO().DeltaTime; if (dt <= 0.0f) dt = 1.0f / 60.0f;
		float k = 1.0f - expf(-dt * 14.0f);

		ImGui::PushID(label);
		// InvisibleButton's own return value = proper click (press+release)
		bool clicked = ImGui::InvisibleButton("##row", ImVec2(width, rowH));
		bool hovered = ImGui::IsItemHovered();
		ImGuiID id = ImGui::GetItemID();
		if (clicked)
			*v = !*v;
		ImGui::PopID();

		ImVec2& a = AnimState(id);
		a.x += ((hovered ? 1.0f : 0.0f) - a.x) * k;
		a.y += ((*v ? 1.0f : 0.0f) - a.y) * k;

		ImDrawList* dl = ImGui::GetWindowDrawList();
		if (a.x > 0.01f)
			dl->AddRectFilled(p, ImVec2(p.x + width, p.y + rowH), IM_COL32(255, 255, 255, (int)(10 * a.x)), 7.0f);
		dl->AddText(ImVec2(p.x + 8, p.y + 6), IM_COL32(226, 231, 240, 255), label);

		float bs = 17.0f;
		float bx = p.x + width - bs - 8.0f;
		float by = p.y + (rowH - bs) * 0.5f;
		if (a.y > 0.02f) {
			dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bs, by + bs),
				(s_accent & 0x00FFFFFF) | (ImU32)((int)(255 * a.y) << 24), 5.0f);
			dl->AddLine(ImVec2(bx + 4, by + bs * 0.55f), ImVec2(bx + bs * 0.42f, by + bs - 5.0f), IM_COL32(8, 12, 18, 255), 2.4f * a.y);
			dl->AddLine(ImVec2(bx + bs * 0.42f, by + bs - 5.0f), ImVec2(bx + bs - 4, by + 4.0f), IM_COL32(8, 12, 18, 255), 2.4f * a.y);
		}
		if (a.y < 0.98f)
			dl->AddRect(ImVec2(bx, by), ImVec2(bx + bs, by + bs), IM_COL32(115, 125, 148, (int)(170 * (1.0f - a.y))), 5.0f, 0, 1.5f);
	}

	static void SliderRow(const char* label, float* v, float mn, float mx, const char* fmt = "%.1f") {
		char val[32];
		snprintf(val, sizeof(val), fmt, *v);
		ImVec2 vsz = ImGui::CalcTextSize(val);

		ImGui::TextUnformatted(label);
		ImGui::SameLine();
		ImVec2 cp = ImGui::GetCursorScreenPos();
		float width = ImGui::GetContentRegionAvail().x;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(ImVec2(cp.x + width - vsz.x - 14, cp.y - 2), ImVec2(cp.x + width, cp.y + vsz.y + 4),
			IM_COL32(255, 255, 255, 14), 5.0f);
		dl->AddText(ImVec2(cp.x + width - vsz.x - 8, cp.y + 1), IM_COL32(235, 240, 248, 255), val);

		ImGui::PushID(label);
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		ImGui::SliderFloat("##v", v, mn, mx, "");
		ImGui::PopID();
	}

	// Popup-free alternative to Combo: segmented buttons, so every clickable
	// pixel stays inside the panel rectangle the input router knows about.
	static void SegmentRow(const char* label, int* v, const char* const items[], int count) {
		ImGui::TextUnformatted(label);
		ImGui::Spacing();
		float width = ImGui::GetContentRegionAvail().x;
		float bw = (width - (count - 1) * 6.0f) / (float)count;
		for (int i = 0; i < count; i++) {
			if (i) ImGui::SameLine(0.0f, 6.0f);
			ImGui::PushID(i);
			bool active = (*v == i);
			if (active) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.62f, 0.55f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.04f, 0.08f, 0.10f, 1.0f));
			}
			if (ImGui::Button(items[i], ImVec2(bw, 0)) && !active)
				*v = i;
			if (active)
				ImGui::PopStyleColor(2);
			ImGui::PopID();
		}
	}

	static const char* KeyName(int vk) {
		static char buf[32];
		if (vk <= 0) { snprintf(buf, sizeof(buf), "None"); return buf; }
		UINT sc = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
		if (vk >= VK_PRIOR && vk <= VK_DOWN) sc |= 0x100u;
		if (!GetKeyNameTextA((LONG)(sc << 16), buf, sizeof(buf)))
			snprintf(buf, sizeof(buf), "0x%02X", vk);
		return buf;
	}

	static void KeyBindRow(const char* label, int* key) {
		ImGui::TextUnformatted(label);
		float width = ImGui::GetContentRegionAvail().x;
		ImGui::PushID(label);
		bool waiting = s_waitingKey && s_keyTarget == key;
		char btn[64];
		snprintf(btn, sizeof(btn), "%s##bind", waiting ? "press any key..." : KeyName(*key));
		if (ImGui::Button(btn, ImVec2(width, 0))) {
			s_waitingKey = true;
			s_keyTarget = key;
		}
		ImGui::PopID();
	}

	static void PollKeyCapture() {
		if (!s_waitingKey)
			return;
		// The game window keeps keyboard focus (click-through overlay), so
		// poll the global async state instead of ImGui's key map.
		for (int vk = 0x08; vk < 0xFE; vk++) {
			bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
			bool was = (s_lastKeys[vk] & 0x80) != 0;
			if (down && !was) {
				if (vk == VK_ESCAPE) {
					s_waitingKey = false;
				} else if (vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON &&
					vk != VK_XBUTTON1 && vk != VK_XBUTTON2 && vk != VK_INSERT) {
					if (s_keyTarget) *s_keyTarget = vk;
					s_waitingKey = false;
				}
			}
			s_lastKeys[vk] = down ? 0x80 : 0;
		}
	}

	static void InfoRow(const char* label, const char* value) {
		ImGui::TextDisabled("%s", label);
		ImGui::SameLine();
		float width = ImGui::GetContentRegionAvail().x;
		ImVec2 vsz = ImGui::CalcTextSize(value);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + width - vsz.x);
		ImGui::TextUnformatted(value);
	}

	// ------------------------------------------------------------------	
	static void DrawTitleRow(float dt) {
		static const char* tabs[] = { "Visuals", "Players", "World", "Last Hit", "Misc", "Settings" };
		ImDrawList* dl = ImGui::GetWindowDrawList();

		ImVec2 rowP = ImGui::GetCursorScreenPos();
		float rowH = ImGui::GetTextLineHeight() + 20.0f;
		float rowW = ImGui::GetContentRegionAvail().x;

		float pillH = ImGui::GetTextLineHeight() + 10.0f;
		float yP = rowP.y + (rowH - pillH) * 0.5f;

		// measure tab rects
		float x = rowP.x + 4.0f;
		float tx[6], tw[6];
		for (int i = 0; i < 6; i++) {
			ImVec2 tsz = ImGui::CalcTextSize(tabs[i]);
			tx[i] = x;
			tw[i] = tsz.x + 26.0f;
			x += tw[i] + 6.0f;
		}

		// Tab pills are submitted BEFORE the drag strip. ImGui awards hover to
		// the FIRST item submitted under the cursor (ItemHoverable refuses to
		// steal an already-claimed HoveredId), so the old order -- a dragstrip
		// InvisibleButton covering the whole row submitted first -- made every
		// pill permanently un-hoverable and TAB CLICKS NEVER REGISTERED.
		// (Reproduced and verified headless; the toggle rows only worked
		// because nothing overlapped them.)
		for (int i = 0; i < 6; i++) {
			ImGui::SetCursorScreenPos(ImVec2(tx[i], yP));
			ImGui::PushID(i);
			// switch on the button's own release -- the exact mechanism the
			// toggle rows use (the one interaction that always worked)
			if (ImGui::InvisibleButton("##tab", ImVec2(tw[i], pillH)))
				s_tab = i;
			bool hovered = ImGui::IsItemHovered();
			ImGui::PopID();

			if (s_tab != i && hovered)
				dl->AddRectFilled(ImVec2(tx[i], yP), ImVec2(tx[i] + tw[i], yP + pillH), IM_COL32(255, 255, 255, 10), 9.0f);
		}

		// sliding accent pill + labels (pure drawing, after the background)
		static float pillX = 0.0f, pillW = 0.0f;
		if (!s_pillInit) { pillX = tx[s_tab]; pillW = tw[s_tab]; s_pillInit = true; }
		float k = 1.0f - expf(-dt * 12.0f);
		pillX += (tx[s_tab] - pillX) * k;
		pillW += (tw[s_tab] - pillW) * k;
		dl->AddRectFilled(ImVec2(pillX, yP), ImVec2(pillX + pillW, yP + pillH), s_accent, 9.0f);
		for (int i = 0; i < 6; i++)
			dl->AddText(ImVec2(tx[i] + 13, yP + 5),
				s_tab == i ? IM_COL32(6, 10, 16, 255) : IM_COL32(150, 158, 175, 255), tabs[i]);

		// Drag strip LAST: it only claims hover on parts of the row the pills
		// do not cover (gaps, padding, brand area). Dragging the menu by its
		// title still works; clicking a tab can never start a drag.
		ImGui::SetCursorScreenPos(rowP);
		ImGui::InvisibleButton("##dragstrip", ImVec2(rowW, rowH));
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			ImVec2 d = ImGui::GetIO().MouseDelta;
			ImGui::SetWindowPos(ImVec2(ImGui::GetWindowPos().x + d.x, ImGui::GetWindowPos().y + d.y));
			s_dragMoved = true;
		}

		if (fontBold) ImGui::PushFont(fontBold);
		ImVec2 brandSz = ImGui::CalcTextSize("DRAGONBURN");
		dl->AddText(ImVec2(rowP.x + rowW - brandSz.x - 118.0f, rowP.y + 9.0f), s_accent, "DRAGONBURN");
		if (fontBold) ImGui::PopFont();
		dl->AddText(ImVec2(rowP.x + rowW - 108.0f, rowP.y + 10.0f), IM_COL32(90, 220, 160, 255), "READ-ONLY");

		ImGui::SetCursorScreenPos(ImVec2(rowP.x, rowP.y + rowH));
	}

	// ------------------------------------------------------------------
	static void DrawLeftPanel(EspSettings& s) {
		switch (s_tab) {
		case 0: Header("Visuals");
			ToggleRow("Enable ESP", &s.enabled);
			ToggleRow("Show Enemies", &s.showEnemies);
			ToggleRow("Show Allies", &s.showAllies);
			ToggleRow("Corner Boxes", &s.cornerStyle);
			ToggleRow("Box Glow", &s.showGlow);
			ToggleRow("Health Bars", &s.showHealthBar);
			ToggleRow("Mana Bars", &s.showManaBar);
			ToggleRow("Hero Names", &s.showNames);
			ToggleRow("Distance", &s.showDistance);
			ToggleRow("Illusion Badge", &s.showIllusions);
			ToggleRow("Off-Screen Indicators", &s.showOffscreen);
			break;

		case 1: Header("Players");
			Section("HEROES INFORMATION  (read-only)");
			ToggleRow("Hero Level", &s.showHeroLevel);
			ToggleRow("Player Names", &s.showPlayerNames);
			ToggleRow("Items (inventory)", &s.showItems);
			ToggleRow("Invisibility Tag", &s.showInvisTag);
			ToggleRow("Attack Range Circle", &s.showAttackRange);
			break;

		case 2: Header("World");
			Section("MAP INFORMATION  (read-only)");
			ToggleRow("Enemy Wards", &s.showWards);
			ToggleRow("Roshan HP", &s.showRoshan);
			ToggleRow("Runes", &s.showRunes);
			Section("TRACKERS  (enemy activity)");
			ToggleRow("Cast / Ult Tracker", &s.showCastTracker);
			ToggleRow("Jungle Tracker", &s.showJungleTracker);
			ToggleRow("Roshan Attack Log", &s.showRoshanLog);
			SliderRow("Cast Marker (s)", &s.castMarkerTime, 2.0f, 12.0f, "%.0f");
			Section("FALLBACK");
			ToggleRow("Last-Known Positions", &s.showGhosts);
			break;

		case 3: Header("Last Hit");
			Section("MARKERS  (read-only overlay)");
			ToggleRow("Last-Hit Markers", &s.showLastHitHelper);
			ToggleRow("Countdown Tags", &s.showLastHitCountdown);
			ToggleRow("Announce Text", &s.showLastHitAnnounce);
			ToggleRow("Status HUD", &s.showLastHitHud);
			Section("AUTOMATION  (input only, no writes)");
			ToggleRow("Auto Last-Hit", &s.autoLastHit);
			ToggleRow("Auto Deny", &s.autoDeny);
			ToggleRow("Include Neutrals", &s.lhNeutrals);
			ToggleRow("Restore Cursor", &s.lhRestoreCursor);
			break;

		case 4: Header("Misc");
			ToggleRow("FPS Counter", &s.showFps);
			ToggleRow("Watermark", &s.showWatermark);
			break;

		case 5: Header("Settings");
			InfoRow("Build", "External read-only ESP");
			InfoRow("Memory access", "ReadProcessMemory");
			InfoRow("Game writes", "none");
			InfoRow("Mouse blocking", "none -- clicks pass through");
			ImGui::Spacing();
			if (ImGui::Button("Reset all settings", ImVec2(-1, 0)))
				s = EspSettings{};
			break;
		}
	}

	// ------------------------------------------------------------------
	static void DrawRightPanel(EspSettings& s, const LastHitStats& st, float fps) {
		switch (s_tab) {
		case 0: Header("Visuals Configuration");
			SliderRow("Box Thickness", &s.boxThickness, 1.0f, 4.0f, "%.1f");
			SliderRow("Smoothing", &s.smoothing, 0.0f, 0.9f, "%.2f");
			SliderRow("Max Distance (m)", &s.maxDrawDistance, 1000.0f, 12000.0f, "%.0f");
			break;

		case 1: Header("Heroes Information Configuration");
			SliderRow("Item Refresh (s)", &s.itemRefreshInterval, 0.25f, 3.0f, "%.2f");
			ImGui::Spacing();
			Note("Items, level, mana and invisibility are read straight from game memory. Nothing is ever written to the process.");
			break;

		case 2: Header("Map Information Configuration");
			SliderRow("Last-Seen Memory (s)", &s.ghostFadeTime, 2.0f, 15.0f, "%.0f");
			ImGui::Spacing();
			Note("Heroes that leave your vision stay on the map as 'HERE Xs AGO' markers and vanish after this many seconds until they show up again. Cast, jungle and Roshan trackers read the networked activity state of enemy heroes.");
			break;

		case 3: {
			Header("Last Hit Configuration");
			KeyBindRow("Trigger Key (hold)", &s.triggerKey);
			if (s.triggerKey == VK_TAB)
				Warning("TAB opens the Dota 2 scoreboard -- while it is up, generated attack clicks hit the scoreboard instead of the creep. Bind a free key (X, C, V, Z...).");
			ImGui::Spacing();
			KeyBindRow("Attack Key (your Dota bind)", &s.attackKey);
			if (s.attackKey == VK_TAB)
				Warning("TAB is the scoreboard key: the game will open the scoreboard instead of entering attack mode. Bind the real Dota 2 'Attack' key (default A).");
			ImGui::Spacing();
			{
				static const char* prio[] = { "Last-Hit first", "Deny first" };
				int p = s.lastHitPriority ? 0 : 1;
				SegmentRow("Priority", &p, prio, 2);
				s.lastHitPriority = (p == 0);
			}
			ImGui::Spacing();
			{
				static const char* dmg[] = { "Safe (min roll)", "Normal (avg)" };
				SegmentRow("Damage Roll", &s.lhDamageMode, dmg, 2);
			}
			ImGui::Spacing();
			SliderRow("Deny Threshold (%)", &s.denyPct, 10.0f, 90.0f, "%.0f");
			SliderRow("Attack Interval (s)", &s.attackInterval, 0.15f, 1.5f, "%.2f");
			SliderRow("Prediction Window (s)", &s.lhWindow, 0.4f, 2.5f, "%.2f");
			SliderRow("Track Rate (Hz)", &s.lhTrackRate, 30.0f, 200.0f, "%.0f");
			Section("ENGINE STATUS");
			char buf[64];
			snprintf(buf, sizeof(buf), "%d", st.kills);            InfoRow("Confirmed last-hits", buf);
			snprintf(buf, sizeof(buf), "%d", st.denies);           InfoRow("Confirmed denies", buf);
			snprintf(buf, sizeof(buf), "%d / %d", st.attempts, st.aborts); InfoRow("Commands / aborts", buf);
			if (st.latencySamples > 0)
				snprintf(buf, sizeof(buf), "%.0f ms (%d samples)", st.latencyMs, st.latencySamples);
			else
				snprintf(buf, sizeof(buf), "calibrating...");
			InfoRow("Impact latency", buf);
			snprintf(buf, sizeof(buf), "%d creeps @ %.0f Hz", st.tracked, st.tickHz);
			InfoRow("Tracking", buf);
			if (st.heroValid)
				snprintf(buf, sizeof(buf), "%d-%d dmg, %.0f range", st.dmgMin, st.dmgMax, st.range);
			else
				snprintf(buf, sizeof(buf), "no hero");
			InfoRow("Local hero", buf);
			ImGui::Spacing();
			Note("Hold the trigger key and the engine times the hit itself: it predicts each creep's HP (armor included), knows your windup/projectile delay from live calibration, and only clicks when the damage will land exactly on the kill threshold. Marker countdowns show when a creep becomes hittable.");
			break;
		}

		case 4: Header("Misc Configuration");
			SliderRow("Scan Rate (Hz)", &s.scanRate, 10.0f, 120.0f, "%.0f");
			ImGui::Spacing();
			Note("Entity scans run on their own thread at this rate; creep HP tracking for last-hits runs separately (and faster) via Track Rate. Rendering always runs at full frame rate.");
			break;

		case 5: Header("Accent Color");
			AccentPresets(s);
			ImGui::Spacing();
			char buf[32];
			snprintf(buf, sizeof(buf), "%.0f", fps);
			InfoRow("Render FPS", buf);
			Note("The accent color is used by the menu and by ESP glow / markers.");
			break;
		}
	}

	static void AccentPresets(EspSettings& s) {
		static const float presets[][3] = {
			{0.05f, 0.85f, 0.75f}, {0.00f, 0.80f, 1.00f}, {0.60f, 0.40f, 1.00f},
			{1.00f, 0.60f, 0.20f}, {1.00f, 0.30f, 0.45f}, {0.30f, 0.90f, 0.40f},
		};
		for (int i = 0; i < 6; i++) {
			if (i) ImGui::SameLine();
			ImGui::PushID(i);
			ImVec4 col(presets[i][0], presets[i][1], presets[i][2], 1.0f);
			if (ImGui::ColorButton("##preset", col, 0, ImVec2(38, 26))) {
				s.accent[0] = presets[i][0];
				s.accent[1] = presets[i][1];
				s.accent[2] = presets[i][2];
			}
			ImGui::PopID();
		}
	}
};

ImU32 UiManager::s_accent = IM_COL32(13, 217, 191, 255);
ImU32 UiManager::s_accentDim = IM_COL32(13, 217, 191, 48);
bool UiManager::s_waitingKey = false;
int* UiManager::s_keyTarget = nullptr;
uint8_t UiManager::s_lastKeys[256] = {};
int UiManager::s_tab = 0;
bool UiManager::s_dragMoved = false;
bool UiManager::s_pillInit = false;
bool UiManager::menuVisible = false;
ImVec2 UiManager::menuPos = ImVec2(0, 0);
ImVec2 UiManager::menuSize = ImVec2(920, 580);
ImFont* UiManager::fontBold = nullptr;
