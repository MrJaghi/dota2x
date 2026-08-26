#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include "imgui.h"

// ---------------------------------------------------------------------------
// InputRouter -- menu input with ZERO global input blocking.
//
// The old build made the whole fullscreen overlay window "clickable"
// (removed WS_EX_TRANSPARENT) while the menu was open. A fullscreen
// non-transparent topmost window swallows EVERY mouse event on the screen,
// so the instant the menu opened the game stopped seeing the mouse at all
// ("my mouse doesn't even move anymore") -- that mechanism is gone for good.
//
// New model:
//   * The overlay window is PERMANENTLY click-through (WS_EX_TRANSPARENT).
//     Every pixel of the screen always belongs to Dota 2.
//   * While the menu is open we install a WH_MOUSE_LL low-level hook:
//       - WM_MOUSEMOVE is NEVER swallowed -> the in-game cursor always moves.
//       - Button/wheel events are swallowed ONLY when the cursor is inside
//         the menu panel rectangle, and are routed into ImGui instead, so
//         clicking a toggle does not also issue a game order. Everywhere
//         outside the panel the game receives 100% of the input.
//       - A button-up is only swallowed when its button-down was swallowed,
//         so press-on-game -> drag-over-menu -> release never leaves the
//         game with a stuck mouse button.
//   * The keyboard is NEVER touched -- the game keeps every key.
//
// The hook is installed/removed from the render thread, and low-level hook
// callbacks execute while that thread pumps messages (Overlay::PumpMessages),
// which is the same thread that runs ImGui -- so calling ImGui's event queue
// from the hook is safe.
// ---------------------------------------------------------------------------
class InputRouter {
public:
	// Menu panel rectangle in OVERLAY CLIENT coordinates (same space ImGui
	// uses). Updated by UiManager at the end of every menu frame.
	static void SetMenuRect(float x, float y, float w, float h, bool open) {
		s_menuX = x; s_menuY = y; s_menuW = w; s_menuH = h;
		s_menuOpen = open;
	}

	static void Attach(HWND overlayWnd) {
		if (s_hook)
			return;
		s_wnd = overlayWnd;
		for (int i = 0; i < 5; i++) s_swallow[i] = false;
		s_hook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
	}

	static void Detach() {
		if (s_hook) {
			HHOOK h = s_hook;
			s_hook = nullptr;      // clear first: no further callbacks
			UnhookWindowsHookEx(h);
		}
		s_wnd = nullptr;
		s_menuOpen = false;
		// never leave ImGui thinking a button is held
		ImGuiIO& io = ImGui::GetIO();
		for (int i = 0; i < 5; i++) {
			if (s_swallow[i]) io.AddMouseButtonEvent(i, false);
			s_swallow[i] = false;
		}
	}

	static bool Active() { return s_hook != nullptr; }

	// Keep ImGui's mouse position fresh even when the hook saw no movement
	// (the overlay window itself receives no messages -- it is click-through).
	static void NewFramePoll(HWND overlayWnd) {
		if (!overlayWnd) return;
		POINT p;
		if (GetCursorPos(&p) && ScreenToClient(overlayWnd, &p))
			ImGui::GetIO().AddMousePosEvent((float)p.x, (float)p.y);
	}

private:
	static bool InsideMenu(const POINT& screenPt) {
		if (!s_menuOpen || !s_wnd) return false;
		POINT p = screenPt;
		if (!ScreenToClient(s_wnd, &p)) return false;
		const float pad = 8.0f; // a few px of forgiveness around the panel
		return p.x > s_menuX - pad && p.x < s_menuX + s_menuW + pad &&
			p.y > s_menuY - pad && p.y < s_menuY + s_menuH + pad;
	}

	static int ButtonFor(WPARAM wp, DWORD mouseData) {
		switch (wp) {
		case WM_LBUTTONDOWN: case WM_LBUTTONUP: return 0;
		case WM_RBUTTONDOWN: case WM_RBUTTONUP: return 1;
		case WM_MBUTTONDOWN: case WM_MBUTTONUP: return 2;
		case WM_XBUTTONDOWN: case WM_XBUTTONUP:
			// in a LOW-LEVEL hook the X-button number lives in
			// MSLLHOOKSTRUCT.mouseData, not in lParam
			return (HIWORD(mouseData) == XBUTTON2) ? 4 : 3;
		}
		return -1;
	}

	static bool IsDownMsg(WPARAM wp) {
		return wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN ||
			wp == WM_MBUTTONDOWN || wp == WM_XBUTTONDOWN;
	}

	static void FeedPos(const POINT& pt) {
		if (!s_wnd) return;
		POINT p = pt;
		if (ScreenToClient(s_wnd, &p))
			ImGui::GetIO().AddMousePosEvent((float)p.x, (float)p.y);
	}

	static LRESULT CALLBACK MouseProc(int code, WPARAM wp, LPARAM lp) {
		if (code == HC_ACTION) {
			const MSLLHOOKSTRUCT* m = (const MSLLHOOKSTRUCT*)lp;

			// Movement is NEVER blocked -- the game cursor must always move.
			if (wp == WM_MOUSEMOVE) {
				FeedPos(m->pt);
				return CallNextHookEx(s_hook, code, wp, lp);
			}

			if (wp == WM_MOUSEWHEEL || wp == WM_MOUSEHWHEEL) {
				if (InsideMenu(m->pt)) {
					short d = (short)HIWORD(m->mouseData);
					float amt = (float)d / (float)WHEEL_DELTA;
					if (wp == WM_MOUSEWHEEL)	ImGui::GetIO().AddMouseWheelEvent(0.0f, amt);
					else						ImGui::GetIO().AddMouseWheelEvent(amt, 0.0f);
					return 1; // eaten only over the panel
				}
				return CallNextHookEx(s_hook, code, wp, lp);
			}

			int b = ButtonFor(wp, m->mouseData);
			if (b >= 0) {
				if (IsDownMsg(wp)) {
					if (InsideMenu(m->pt)) {
						ImGui::GetIO().AddMouseButtonEvent(b, true);
						s_swallow[b] = true;
						return 1;
					}
				} else if (s_swallow[b]) { // swallow UP only if we ate the DOWN
					ImGui::GetIO().AddMouseButtonEvent(b, false);
					s_swallow[b] = false;
					return 1;
				}
			}
		}
		return CallNextHookEx(s_hook, code, wp, lp);
	}

	static HHOOK s_hook;
	static HWND  s_wnd;
	static bool  s_swallow[5];
	static bool  s_menuOpen;
	static float s_menuX, s_menuY, s_menuW, s_menuH;
};

// C++17 inline definitions (header-only, single definition across TUs)
inline HHOOK InputRouter::s_hook = nullptr;
inline HWND  InputRouter::s_wnd = nullptr;
inline bool  InputRouter::s_swallow[5] = {};
inline bool  InputRouter::s_menuOpen = false;
inline float InputRouter::s_menuX = 0.0f;
inline float InputRouter::s_menuY = 0.0f;
inline float InputRouter::s_menuW = 0.0f;
inline float InputRouter::s_menuH = 0.0f;
