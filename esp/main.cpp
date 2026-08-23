#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cmath>
#include <math.h>
#include <Windows.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdio>

#include "Memory.h"
#include "Math.h"
#include "Offsets.h"
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

	static constexpr int ChunkCount = 16;
	static constexpr int MaxScanIndex = ChunkCount * offsets::CGameEntitySystem::ChunkSize;

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
		while (memory.IsProcessAlive()) {
			overlay.PumpMessages();
			overlay.SyncWithGameWindow();

			HandleInput();
			Update();
			Render();

			Sleep(4);
		}

		overlay.Shutdown();
		printf("[!] Dota 2 closed.\n");
	}

private:
	void HandleInput() {
		bool toggleKeyDown = (GetAsyncKeyState(VK_END) & 0x8000) != 0 || (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
		if (toggleKeyDown && !toggleKeyWasDown) {
			menuOpen = !menuOpen;
			overlay.SetClickable(menuOpen);
		}
		toggleKeyWasDown = toggleKeyDown;
	}

	void Update() {
		uintptr_t entitySystem = memory.Read<uintptr_t>(memory.clientDllBase + offsets::client_dll::dwEntityList);

		if (!entitySystem)
			return;

		std::vector<uintptr_t> chunks = EntityReader::ReadChunkPointers(memory, entitySystem, ChunkCount);
		uintptr_t localPawn = EntityReader::FindLocalPawn(memory, chunks, MaxScanIndex);

		if (!localPawn)
			return;

		uint8_t localTeam = memory.Read<uint8_t>(localPawn + offsets::C_BaseEntity::m_iTeamNum);
		Vector3 localOrigin{};

		if (!EntityReader::GetEntityOrigin(memory, localPawn, localOrigin))
			return;

		ViewMatrix viewMatrix = memory.Read<ViewMatrix>(memory.clientDllBase + offsets::client_dll::dwViewMatrix);

		lastTargets = EntityReader::CollectTargets(memory, chunks, MaxScanIndex, localPawn, localTeam,
			localOrigin, viewMatrix, overlay.width, overlay.height, espSettings, smoothedOrigins);

		lastCreeps = EntityReader::CollectCreepTargets(memory, chunks, MaxScanIndex, localPawn, localTeam,
			localOrigin, viewMatrix, overlay.width, overlay.height);

		DebugLog();
	}

	void Render() {
		overlay.BeginFrame();
		EspRenderer::Draw(espSettings, lastTargets, lastCreeps);
		if (menuOpen)
			UiManager::DrawMenu(menuOpen, menuCategories);
		overlay.EndFrameAndPresent();
	}

	void DebugLog() {
		static int debugFrame = 0;
		debugFrame++;
		if (debugFrame % 60 == 0)
			printf("[dbg] targets=%zu creeps=%zu\n", lastTargets.size(), lastCreeps.size());
	}

	std::vector<EspTarget> lastTargets;
	std::vector<CreepTarget> lastCreeps;
};

int main() {
	GameClient client;
	if (!client.Initialize())
		return 1;

	client.Run();
	return 0;
}
