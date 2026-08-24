#pragma once
#include "imgui.h"
#include "Config.h"
#include <vector>
#include <string>

class UiManager {
public:
	static void ApplyTheme() {
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 10.0f;
		style.ChildRounding = 8.0f;
		style.FrameRounding = 6.0f;
		style.GrabRounding = 6.0f;
		style.PopupRounding = 8.0f;
		style.ScrollbarRounding = 8.0f;
		style.TabRounding = 6.0f;

		style.WindowPadding = ImVec2(14, 14);
		style.FramePadding = ImVec2(10, 6);
		style.ItemSpacing = ImVec2(10, 8);
		style.ItemInnerSpacing = ImVec2(8, 6);
		style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
		style.WindowBorderSize = 1.0f;
		style.FrameBorderSize = 0.0f;

		ImVec4 neonCyan(0.00f, 0.82f, 1.00f, 1.0f);
		ImVec4 neonCyanHover(0.20f, 0.88f, 1.00f, 0.9f);
		ImVec4 neonCyanActive(0.00f, 0.70f, 0.90f, 1.0f);

		ImVec4 darkBg(0.08f, 0.09f, 0.12f, 0.96f);
		ImVec4 childBg(0.11f, 0.12f, 0.16f, 0.85f);
		ImVec4 borderCol(0.20f, 0.24f, 0.30f, 0.6f);

		ImVec4* colors = style.Colors;

		colors[ImGuiCol_WindowBg] = darkBg;
		colors[ImGuiCol_ChildBg] = childBg;
		colors[ImGuiCol_Border] = borderCol;
		colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.07f, 0.10f, 1.0f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.10f, 0.14f, 1.0f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.17f, 0.22f, 1.0f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.23f, 0.30f, 1.0f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.28f, 0.36f, 1.0f);

		colors[ImGuiCol_CheckMark] = neonCyan;
		colors[ImGuiCol_SliderGrab] = neonCyan;
		colors[ImGuiCol_SliderGrabActive] = neonCyanActive;

		colors[ImGuiCol_Button] = ImVec4(0.16f, 0.18f, 0.24f, 1.0f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.00f, 0.65f, 0.85f, 0.35f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.65f, 0.85f, 0.65f);

		colors[ImGuiCol_Header] = ImVec4(0.16f, 0.19f, 0.25f, 1.0f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.70f, 0.90f, 0.25f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.70f, 0.90f, 0.45f);

		colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.14f, 0.18f, 1.0f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.00f, 0.75f, 0.95f, 0.40f);
		colors[ImGuiCol_TabActive] = ImVec4(0.00f, 0.75f, 0.95f, 0.75f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.16f, 0.21f, 1.0f);

		colors[ImGuiCol_Text] = ImVec4(0.92f, 0.94f, 0.96f, 1.0f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.50f, 0.58f, 1.0f);
		colors[ImGuiCol_Separator] = borderCol;
	}

	static void DrawModule(Module& m) {
		ImGui::PushID(m.name.c_str());
		ImGui::Checkbox("##enabled", m.enabled);
		ImGui::SameLine();
		bool open = ImGui::CollapsingHeader(m.name.c_str());
		if (open && m.drawSettings) {
			ImGui::Indent(12.0f);
			m.drawSettings();
			ImGui::Unindent(12.0f);
			ImGui::Spacing();
		}
		ImGui::PopID();
	}

	static void DrawMenu(bool& menuOpen, std::vector<Category>& categories) {
		ImGui::SetNextWindowSize(ImVec2(380, 480), ImGuiCond_FirstUseEver);
		ImGui::Begin("DragonBurn Kernel Overlay", &menuOpen, ImGuiWindowFlags_NoCollapse);

		ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.0f), " DRAGONBURN ULTRA ");
		ImGui::SameLine();
		ImGui::TextDisabled("| Status: Active (Read-Only)");

		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::BeginTabBar("categories", ImGuiTabBarFlags_FittingPolicyScroll)) {
			for (auto& cat : categories) {
				if (ImGui::BeginTabItem(cat.name.c_str())) {
					ImGui::Spacing();
					for (auto& mod : cat.modules)
						DrawModule(mod);
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextDisabled(" [INSERT] - Toggle Menu  (menu only works while Dota 2 is focused)");

		ImGui::End();
	}
};
