#include <cmath>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <timeapi.h>
#include <vector>
#include <cstdio>
#include <thread>
#include <mutex>
#include <atomic>

#pragma comment(lib, "winmm.lib")

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
#include "Snapshot.h"
#include "InputRouter.h"
#include "LastHitEngine.h"

// ---------------------------------------------------------------------------
// Architecture (see Snapshot.h):
//
//   render thread (here)  : menu + ESP drawing only. NO memory reads, NO
//                           Sleep() calls, NO per-frame Win32 window work.
//                           -> constant high FPS, cursor never lags.
//   scan thread           : full entity scan at settings.scanRate (default
//                           60 Hz): heroes, wards, roshan, runes, trackers.
//   last-hit thread       : creep HP tracking at settings.lhTrackRate
//                           (default 100 Hz) + prediction + attack timing +
//                           SendInput automation (its Sleeps live here, not
//                           in the render loop).
//
// Input: the overlay window is permanently click-through. While the menu is
// open, InputRouter routes clicks over the menu panel into ImGui and lets
// EVERYTHING else through to the game. There is no global input block
// anymore -- the mouse always moves.
// ---------------------------------------------------------------------------

class GameClient {
private:
	Memory memory;
	Overlay overlay;
	EspSettings settings;

	Snapshot snap;
	EntityReader::ScanState scanState;
	std::thread scanThread;
	std::thread lhThread;
	std::atomic<bool> workersRunning{ false };

	LastHit::Engine lhEngine;

	bool menuOpen = false;
	bool insertWasDown = false;
	float fpsEMA = 60.0f;
	ULONGLONG startTick = 0;
	ULONGLONG lastLayoutProbe = 0;

public:
	~GameClient() {
		StopWorkers();
		InputRouter::Detach();
		overlay.Shutdown();
	}

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
		printf("[+] Input: overlay is always click-through; the menu only routes\n"
			"    clicks that land on its own panel. The game mouse is never blocked.\n");
		printf("[+] Tip: hold [%c] for auto last-hit. Avoid the TAB key -- Dota 2\n"
			"    opens the scoreboard on TAB and eats generated attack clicks.\n",
			(char)settings.triggerKey);

		startTick = GetTickCount64();

		workersRunning = true;
		scanThread = std::thread([this] { ScanWorker(); });
		lhThread = std::thread([this] { LhWorker(); });
		printf("[+] Threads started: render | entity scan (%.0f Hz) | last-hit (%.0f Hz)\n",
			settings.scanRate, settings.lhTrackRate);
		return true;
	}

	void StopWorkers() {
		workersRunning = false;
		if (scanThread.joinable()) scanThread.join();
		if (lhThread.joinable()) lhThread.join();
	}

	void Run() {
		bool cleared = false;
		while (memory.IsProcessAlive()) {
			overlay.PumpMessages();
			overlay.SyncWithGameWindow();

			snap.clientX.store(overlay.clientOriginX);
			snap.clientY.store(overlay.clientOriginY);

			HandleInput();

			if (overlay.IsGameFocused()) {
				cleared = false;
				Render();
			} else {
				if (menuOpen) { // never leave a menu up over another app
					menuOpen = false;
					InputRouter::Detach();
					UiManager::menuVisible = false;
				}
				if (!cleared) {
					overlay.BeginFrame();
					overlay.EndFrameAndPresent();
					cleared = true;
				}
				Sleep(15);
			}
		}

		StopWorkers();
		InputRouter::Detach();
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
			if (menuOpen)
				InputRouter::Attach(overlay.overlayWnd);
			else {
				InputRouter::Detach();
				UiManager::menuVisible = false;
			}
		}
		insertWasDown = toggleKeyDown;

		if (menuOpen && !gameFocused) {
			menuOpen = false;
			InputRouter::Detach();
			UiManager::menuVisible = false;
		}
	}

	// ------------------------------------------------------------------
	// Entity scan thread: heroes / wards / roshan / runes / trackers and the
	// creep cache for the last-hit engine. Runs at settings.scanRate.
	// ------------------------------------------------------------------
	void ScanWorker() {
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
		EntityReader::ScanOutput localOut;
		ULONGLONG lastScan = 0;

		while (workersRunning.load() && memory.IsProcessAlive()) {
			ULONGLONG now = GetTickCount64();
			float rate = settings.scanRate;
			if (rate < 5.0f) rate = 5.0f;
			if (rate > 120.0f) rate = 120.0f;
			if (now - lastScan < (ULONGLONG)(1000.0f / rate)) {
				Sleep(2);
				continue;
			}
			lastScan = now;

			uintptr_t entitySystem = memory.Read<uintptr_t>(memory.clientDllBase + offsets::client_dll::dwEntityList);
			if (!entitySystem) {
				Sleep(50);
				continue;
			}

			// keep retrying the layout probe while the ESP started in the
			// main menu (no local pawn yet)
			if (!EntityLayout::Detected() && now - lastLayoutProbe > 2000) {
				lastLayoutProbe = now;
				EntityLayout::Detect(memory, entitySystem);
			}

			ScanMeta meta;
			meta.screenW = overlay.width;
			meta.screenH = overlay.height;
			scanState.now = (float)((now - startTick) / 1000.0);

			EntityReader::ReadChunkPointers(memory, entitySystem, scanState);
			EntityReader::RefreshLocal(memory, scanState);
			meta.local = scanState.local;
			meta.localValid = scanState.localValid;

			if (!scanState.localValid) {
				localOut.heroes.clear();
				localOut.markers.clear();
				localOut.ghostList.clear();
				localOut.pingList.clear();
				snap.PublishScan(std::move(localOut), std::move(meta));
				continue;
			}

			ViewMatrix vm = memory.Read<ViewMatrix>(memory.clientDllBase + offsets::client_dll::dwViewMatrix);
			meta.vm = vm;

			EntityReader::ScanAll(memory, scanState, settings, vm, meta.screenW, meta.screenH, localOut);
			meta.creeps = scanState.creepCache;

			if (settings.showAttackRange && scanState.local.attackRange > 0.0f)
				EntityReader::BuildRangeCircle(scanState, scanState.local.attackRange,
					vm, meta.screenW, meta.screenH, meta.rangeCircle);

			snap.PublishScan(std::move(localOut), std::move(meta));
		}
	}

	// ------------------------------------------------------------------
	// Last-hit thread: high-rate creep tracking, prediction, calibrated
	// attack timing and (while the trigger is held) the actual clicks.
	// ------------------------------------------------------------------
	void LhWorker() {
		// Timing precision matters here: above-normal priority keeps the
		// 100 Hz tracking steady even when the game hammers the CPU.
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
		while (workersRunning.load() && memory.IsProcessAlive()) {
			float t0 = LastHit::WallNow();
			lhEngine.Tick(memory, snap, settings, overlay.IsGameFocused(), menuOpen);

			float rate = settings.lhTrackRate;
			if (rate < 30.0f) rate = 30.0f;
			if (rate > 200.0f) rate = 200.0f;
			float target = t0 + 1.0f / rate;
			while (LastHit::WallNow() < target && workersRunning.load())
				Sleep(1);
		}
	}

	// ------------------------------------------------------------------
	// Render thread: pure drawing, nothing else.
	// ------------------------------------------------------------------
	void Render() {
		// feed the live cursor position BEFORE NewFrame so hover states use
		// this frame's position (events queued by the hook since the last
		// frame are processed by BeginFrame -> ImGui::NewFrame)
		InputRouter::NewFramePoll(overlay.overlayWnd);
		overlay.BeginFrame();

		float dt = ImGui::GetIO().DeltaTime;
		float fps = dt > 0.0001f ? 1.0f / dt : 0.0f;
		if (fps > 1.0f && fps < 1000.0f)
			fpsEMA += (fps - fpsEMA) * 0.08f;

		// local copies of the shared snapshot (short locks, then draw from
		// our own buffers so the workers can never stall a frame)
		EntityReader::ScanOutput localScan;
		LhFrame localLh;
		std::vector<Vector2> rangePts;
		{
			std::lock_guard<std::mutex> g1(snap.mxScan);
			localScan = snap.scan;
			rangePts = snap.meta.rangeCircle;
		}
		{
			std::lock_guard<std::mutex> g2(snap.mxLh);
			localLh = snap.lh;
		}

		EspRenderer::Draw(settings, localScan, localLh, rangePts, dt, fpsEMA);

		if (menuOpen)
			UiManager::DrawMenu(menuOpen, settings, localLh.stats, fpsEMA);
		else
			InputRouter::SetMenuRect(0.0f, 0.0f, 0.0f, 0.0f, false);

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

	// 1 ms Sleep resolution: the last-hit thread tick cadence and the click
	// choreography (Sleep(6/14/16)) depend on accurate short sleeps.
	// Without this, Windows rounds Sleep(1) up to ~15.6 ms.
	timeBeginPeriod(1);

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
