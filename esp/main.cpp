#include <cmath>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <vector>
#include <string>
#include <unordered_map>
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

class GameClient {
private:
	Memory memory;
	Overlay overlay;
	EspSettings espSettings;
	std::vector<Category> menuCategories;
	bool menuOpen = false;
	bool toggleKeyWasDown = false;
	std::unordered_map<uintptr_t, Vector3> smoothedOrigins;

	// Entity-list chunk count / slot size. ChunkSize is always 512 for Source2
	// (the dumper doesn't dump it); 16 chunks * 512 slots = 8192 entities,
	// comfortably higher than Dota 2's max entity index.
	static constexpr int ChunkSize    = 512;
	static constexpr int ChunkCount   = 16;
	static constexpr int MaxScanIndex = ChunkCount * ChunkSize;

public:
	bool Initialize() {
		printf("[*] Waiting for Dota 2...\n");
		while (!memory.Attach(L"dota2.exe", L"client.dll"))
			Sleep(1000);
		printf("[+] Attached to Dota 2. client.dll: 0x%llX\n", (unsigned long long)memory.clientDllBase);

		printf("[*] Waiting for game window...\n");
		while (!overlay.Create(L"Dota 2"))
			Sleep(1000);
		printf("[+] Overlay created (%dx%d)\n", overlay.width, overlay.height);

		// Offsets are HARDCODED at compile time: Offsets.h #includes the
		// dumper files from output\ verbatim (no runtime parsing).

		// Detect the engine-side entity-list layout (CEntityIdentity stride,
		// entity-pointer offset, index->slot convention) against live memory.
		// This is 100% read-only and does not touch the kernel driver.
		uintptr_t entitySystem = memory.Read<uintptr_t>(memory.clientDllBase + offsets::client_dll::dwEntityList);
		EntityLayout::Detect(memory, entitySystem);

		// Validate offsets against the live client.dll so we get a console
		// warning instead of silent garbage when the game updates.
		OffsetValidator::ValidateAll(memory);

		SetupUI();
		return true;
	}

	void SetupUI() {
		UiManager::ApplyTheme();

		Category visuals;
		visuals.name = "Visuals";
		visuals.modules.push_back({
			"Player ESP", &espSettings.enabled,
			[this]() {
				ImGui::Checkbox("Show enemies", &espSettings.showEnemies);
				ImGui::Checkbox("Show allies", &espSettings.showAllies);
				ImGui::Checkbox("Health bar", &espSettings.showHealthBar);
				ImGui::Checkbox("Mana bar", &espSettings.showManaBar);
				ImGui::Checkbox("Hero level", &espSettings.showHeroLevel);
				ImGui::Checkbox("Detect illusions", &espSettings.showIllusions);
				ImGui::Checkbox("Names", &espSettings.showNames);
				ImGui::Checkbox("Distance", &espSettings.showDistance);
				ImGui::Checkbox("Corner style", &espSettings.cornerStyle);
				ImGui::SliderFloat("Box thickness", &espSettings.boxThickness, 1.0f, 4.0f, "%.1f");
				ImGui::SliderFloat("Smoothing", &espSettings.smoothing, 0.0f, 0.9f, "%.2f");
			}
		});

		Category assistant;
		assistant.name = "Last Hit Helper (Read-Only)";
		assistant.modules.push_back({
			"Last Hit Indicator", &espSettings.showLastHitHelper,
			[this]() {
				ImGui::Checkbox("Show Last Hit Markers", &espSettings.showLastHitHelper);
				ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Mode: 100%% Read-Only Visual Overlay");
			}
		});

		menuCategories.push_back(visuals);
		menuCategories.push_back(assistant);
	}

	void Run() {
		bool wasFocused = true;
		while (memory.IsProcessAlive()) {
			overlay.PumpMessages();
			overlay.SyncWithGameWindow();

			bool focused = overlay.IsGameFocused();
			HandleInput();

			if (focused) {
				Update();
				Render();
			} else {
				// When Alt+Tabbed, clear stale targets so we don't keep drawing the
				// last frame when returning focus (and save CPU/GPU).
				lastTargets.clear();
				lastCreeps.clear();
				if (wasFocused) {
					// Blank the overlay once on unfocus to clear the last frame.
					overlay.BeginFrame();
					overlay.EndFrameAndPresent();
				}
				Sleep(33); // ~30 fps polling while tabbed out
			}
			wasFocused = focused;

			if (focused) Sleep(4);
		}

		overlay.Shutdown();
		printf("[!] Dota 2 closed.\n");
	}

private:
	void HandleInput() {
		// Menu toggle: INSERT only, and ONLY while Dota 2 has keyboard focus so
		// pressing Insert in the browser / Discord / chat does not open the menu.
		bool gameFocused = overlay.IsGameFocused();
		bool toggleKeyDown = false;
		if (gameFocused)
			toggleKeyDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
		if (toggleKeyDown && !toggleKeyWasDown) {
			menuOpen = !menuOpen;
			overlay.SetClickable(menuOpen);
		}
		toggleKeyWasDown = toggleKeyDown;

		// Auto-close the menu and release clicks if the user Alt+Tabs out.
		if (menuOpen && !gameFocused) {
			menuOpen = false;
			overlay.SetClickable(false);
		}
	}

	void Update() {
		uintptr_t entitySystem = memory.Read<uintptr_t>(memory.clientDllBase + offsets::client_dll::dwEntityList);

		if (!entitySystem)
			return;

		// If the layout probe couldn't lock in yet (e.g. the ESP started while
		// Dota 2 was still in the main menu, where there is no local pawn),
		// keep retrying every couple of seconds until a match provides one.
		if (!EntityLayout::Detected()) {
			static DWORD lastProbe = 0;
			DWORD now = GetTickCount();
			if (now - lastProbe > 2000) {
				lastProbe = now;
				EntityLayout::Detect(memory, entitySystem);
			}
		}

		std::vector<uintptr_t> chunks = EntityReader::ReadChunkPointers(memory, entitySystem, ChunkCount);
		uintptr_t localPawn = EntityReader::FindLocalPawn(memory, chunks, MaxScanIndex);

		if (!localPawn)
			return;

		uint8_t localTeam = memory.Read<uint8_t>(localPawn + offsets::C_BaseEntity::m_iTeamNum);
		Vector3 localOrigin{};

		if (!EntityReader::GetEntityOrigin(memory, localPawn, localOrigin))
			return;

		ViewMatrix viewMatrix = memory.Read<ViewMatrix>(memory.clientDllBase + offsets::client_dll::dwViewMatrix);

		// 64 units ~= 1 Dota 2 "hammer" / roughly 1 meter. Divide by 60
		// (~64/1.06) to approximate in-game meters; that's a reasonable
		// estimate for hero bounding boxes. See t.distance usage in
		// EspRenderer (we render it raw; divide there to keep the raw
		// value available).
		lastTargets = EntityReader::CollectTargets(memory, chunks, MaxScanIndex, localPawn, localTeam,
			localOrigin, viewMatrix, overlay.width, overlay.height, espSettings, smoothedOrigins);

		lastCreeps = EntityReader::CollectCreepTargets(memory, chunks, MaxScanIndex, localPawn, localTeam,
			localOrigin, viewMatrix, overlay.width, overlay.height);
	}

	void Render() {
		overlay.BeginFrame();

		// ESP boxes / bars / labels go onto ImGui's background draw list so
		// they stay visible even while the menu is closed.
		EspRenderer::Draw(espSettings, lastTargets, lastCreeps);

		if (menuOpen)
			UiManager::DrawMenu(menuOpen, menuCategories);

		overlay.EndFrameAndPresent();
	}

	std::vector<EspTarget> lastTargets;
	std::vector<CreepTarget> lastCreeps;
};

	int main() {
		SetConsoleTitleA("DragonBurn External ESP");
		printf("[*] DragonBurn External ESP -- waiting for Dota 2 process...\n");

		GameClient client;
		// Run forever: if Dota 2 closes, detach, wait, re-attach.
		// This means the user can close Dota and re-open it without
		// restarting the ESP.
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
