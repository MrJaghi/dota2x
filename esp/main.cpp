#include <cmath>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <vector>
#include <cstdio>

#include "Memory.h"
#include "VectorMath.h"
#include "Offsets.h"
#include "EntityLayout.h"
#include "OffsetValidator.h"
#include "Overlay.h"
#include "Config.h"
#include "EntityReader.h"
#include "EspRenderer.h"
#include "UiManager.h"

// ---------------------------------------------------------------------------
// Input simulation for the auto last-hit / deny helper.
// The kernel driver and game memory stay 100% read-only -- automation is
// performed exclusively with SendInput in our own process.
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

	static void MoveMouseAbs(int x, int y) {
		INPUT in = {};
		in.type = INPUT_MOUSE;
		in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
		in.mi.dx = (LONG)((x * 65535) / (GetSystemMetrics(SM_CXSCREEN) - 1));
		in.mi.dy = (LONG)((y * 65535) / (GetSystemMetrics(SM_CYSCREEN) - 1));
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

// ---------------------------------------------------------------------------
// Damage variance: Dota 2 applies +/- 10% base damage random on creeps.
// We use the conservative (low) estimate so we don't miss last-hits.
// ---------------------------------------------------------------------------
static float ConservativeDamage(float avgDamage) {
	return avgDamage * 0.9f;
}

class GameClient {
private:
	Memory memory;
	Overlay overlay;
	EspSettings settings;

	EntityReader::ScanState scanState;
	EntityReader::ScanOutput scanOut;
	std::vector<Vector2> rangeCirclePts;

	bool menuOpen = false;
	bool insertWasDown = false;
	bool triggerWasDown = false;
	float clickGate = 0.0f;      // earliest time the auto-helper may act again
	float nowSec = 0.0f;
	ULONGLONG startTick = 0;
	ULONGLONG lastScanTick = 0;
	ULONGLONG lastLayoutProbe = 0;

public:
	bool Initialize() {
		printf("[*] DragonBurn External ESP -- waiting for Dota 2 process...\n");
		printf("[*] Waiting for Dota 2...\n");
		while (!memory.Attach(L"dota2.exe", L"client.dll"))
			Sleep(1000);
		printf("[+] Attached to Dota 2. client.dll: 0x%llX\n", (unsigned long long)memory.clientDllBase);

		printf("[*] Waiting for game window...\n");
		while (!overlay.Create(L"Dota 2"))
			Sleep(1000);
		printf("[+] Overlay created (%dx%d)\n", overlay.width, overlay.height);

		uintptr_t entitySystem = memory.Read<uintptr_t>(memory.clientDllBase + offsets::client_dll::dwEntityList);
		EntityLayout::Detect(memory, entitySystem);

		OffsetValidator::ValidateAll(memory);

		UiManager::ApplyTheme();

		startTick = GetTickCount64();
		return true;
	}

	void Run() {
		bool wasFocused = true;
		while (memory.IsProcessAlive()) {
			overlay.PumpMessages();
			overlay.SyncWithGameWindow();

			bool focused = overlay.IsGameFocused();
			HandleInput();

			if (focused) {
				nowSec = (GetTickCount64() - startTick) / 1000.0f;
				Update();
				AutoLastHitTick();
				Render();
			} else {
				scanOut.heroes.clear();
				scanOut.creeps.clear();
				scanOut.markers.clear();
				scanOut.ghostList.clear();
				if (wasFocused) {
					overlay.BeginFrame();
					overlay.EndFrameAndPresent();
				}
				Sleep(5);
			}
			wasFocused = focused;
		}

		overlay.Shutdown();
		printf("[!] Dota 2 closed.\n");
	}

private:
	// ------------------------------------------------------------------
	void HandleInput() {
		bool gameFocused = overlay.IsGameFocused();
		bool toggleKeyDown = gameFocused && (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
		if (toggleKeyDown && !insertWasDown) {
			menuOpen = !menuOpen;
			overlay.SetClickable(menuOpen);
		}
		insertWasDown = toggleKeyDown;

		if (menuOpen && !gameFocused) {
			menuOpen = false;
			overlay.SetClickable(false);
		}
	}

	// ------------------------------------------------------------------
	// Memory scan, throttled to settings.scanRate (default 30 Hz). Rendering
	// still happens every frame using the last scan results.
	// ------------------------------------------------------------------
	void Update() {
		ULONGLONG now = GetTickCount64();
		float interval = 1000.0f / (settings.scanRate > 1.0f ? settings.scanRate : 30.0f);
		if (now - lastScanTick < (ULONGLONG)interval)
			return;
		lastScanTick = now;

		uintptr_t entitySystem = memory.Read<uintptr_t>(memory.clientDllBase + offsets::client_dll::dwEntityList);
		if (!entitySystem)
			return;

		// keep retrying the layout probe while the ESP started in the main menu
		if (!EntityLayout::Detected() && now - lastLayoutProbe > 2000) {
			lastLayoutProbe = now;
			EntityLayout::Detect(memory, entitySystem);
		}

		scanState.now = nowSec;
		EntityReader::ReadChunkPointers(memory, entitySystem, scanState);
		EntityReader::RefreshLocal(memory, scanState);
		if (!scanState.localValid)
			return;

		ViewMatrix vm = memory.Read<ViewMatrix>(memory.clientDllBase + offsets::client_dll::dwViewMatrix);
		EntityReader::ScanAll(memory, scanState, settings, vm, overlay.width, overlay.height, scanOut);

		if (settings.showAttackRange)
			EntityReader::BuildRangeCircle(scanState, scanState.local.attackRange,
				vm, overlay.width, overlay.height, rangeCirclePts);
		else
			rangeCirclePts.clear();
	}

	// ------------------------------------------------------------------
	// Auto last-hit + deny (hold trigger key). Priority is configurable:
	// by default a last-hit always beats a deny when both are possible.
	//
	// The target selection uses conservative damage (low-roll) to avoid
	// missing last-hits, and sorts by distance so we hit the closest
	// eligible creep first.
	// ------------------------------------------------------------------
	void AutoLastHitTick() {
		if (!settings.autoLastHit || menuOpen || !scanState.localValid)
			return;

		bool down = (GetAsyncKeyState(settings.triggerKey) & 0x8000) != 0;
		bool pressed = down && !triggerWasDown;
		triggerWasDown = down;
		if (!down)
			return;

		if (!pressed && nowSec < clickGate)
			return;

		const CreepTarget* best = nullptr;
		float bestDist = 1e9f;

		// pass 1: preferred action, pass 2: fallback action
		for (int pass = 0; pass < 2 && !best; pass++) {
			bool wantKill = settings.lastHitPriority ? (pass == 0) : (pass == 1);
			for (const auto& c : scanOut.creeps) {
				bool eligible = false;
				if (wantKill) {
					// Use conservative (low-roll) damage to avoid mistiming
					float cdmg = ConservativeDamage(scanState.local.damage);
					eligible = c.isKillableNow || (cdmg > 0 && c.health > 0 && c.health <= (int)(cdmg * 1.05f));
				} else {
					eligible = c.isDenyable;
				}
				if (!eligible || c.distance >= bestDist)
					continue;
				best = &c;
				bestDist = c.distance;
			}
		}

		if (!best)
			return;

		ExecuteAttack(*best);
		clickGate = nowSec + settings.clickCooldown;
	}

	void ExecuteAttack(const CreepTarget& c) {
		// tap the user's Dota 2 "Attack" bind to enter attack-targeting mode
		InputSim::TapKey((WORD)settings.attackKey);
		Sleep(30);

		// click the creep center (clamped away from screen edges to avoid camera pan)
		int tx = (int)(c.x + c.w * 0.5f);
		int ty = (int)(c.y + c.h * 0.5f);
		tx = tx < 90 ? 90 : (tx > overlay.width - 90 ? overlay.width - 90 : tx);
		ty = ty < 90 ? 90 : (ty > overlay.height - 90 ? overlay.height - 90 : ty);
		InputSim::MoveMouseAbs(tx, ty);
		Sleep(35);
		InputSim::ClickLeft();
		Sleep(10);
	}

	// ------------------------------------------------------------------
	void Render() {
		overlay.BeginFrame();

		float dt = ImGui::GetIO().DeltaTime;
		EspRenderer::Draw(settings, scanOut, rangeCirclePts, dt);

		UiManager::menuVisible = menuOpen;
		if (menuOpen)
			UiManager::DrawMenu(menuOpen, settings, dt);

		overlay.EndFrameAndPresent();
	}
};

int main() {
	// per-monitor DPI awareness: keeps GetCursorPos / ClientToScreen
	// coordinates in one consistent physical space and the overlay
	// pixel-aligned regardless of Windows display scaling
	{
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (user32) {
			using SetCtxFn = BOOL(WINAPI*)(HANDLE);
			SetCtxFn setCtx = (SetCtxFn)(void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
			if (setCtx)
				setCtx((HANDLE)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
		}
	}

	SetConsoleTitleA("DragonBurn External ESP");

	GameClient client;
	// Run forever: if Dota 2 closes, detach, wait, re-attach.
	while (true) {
		if (!client.Initialize()) {
			printf("[!] Initialization failed -- retrying in 5s.\n");
			Sleep(5000);
			continue;
		}
		client.Run();
		printf("[*] Dota 2 closed. Waiting for restart...\n");
		Sleep(3000);
	}
	return 0;
}
