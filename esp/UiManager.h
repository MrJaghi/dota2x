#pragma once
#include "imgui.h"
#include "Config.h"
#include <vector>
#include <string>

class UiManager {
public:
	static void ApplyTheme() {
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 6.0f;
		style.FrameRounding = 4.0f;
		style.GrabRounding = 4.0f;
		style.PopupRounding = 4.0f;
		style.ScrollbarRounding = 4.0f;
		style.TabRounding = 4.0f;
		style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
		style.FramePadding = ImVec2(8, 4);
		style.ItemSpacing = ImVec2(8, 6);

		ImVec4 accent(0.60f, 0.77f, 0.15f, 1.0f);
		ImVec4 accentHover(0.69f, 0.85f, 0.22f, 1.0f);
		ImVec4 accentActive(0.49f, 0.62f, 0.13f, 1.0f);
		ImVec4* colors = style.Colors;

		colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.08f, 0.96f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.0f);
		colors[ImGuiCol_Border] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.05f, 0.05f, 0.06f, 1.0f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.0f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.20f, 0.23f, 1.0f);
		colors[ImGuiCol_CheckMark] = accent;
		colors[ImGuiCol_SliderGrab] = accent;
		colors[ImGuiCol_SliderGrabActive] = accentActive;
		colors[ImGuiCol_Button] = ImVec4(0.16f, 0.16f, 0.18f, 1.0f);
		colors[ImGuiCol_ButtonHovered] = accentHover;
		colors[ImGuiCol_ButtonActive] = accentActive;
		colors[ImGuiCol_Header] = ImVec4(0.16f, 0.16f, 0.18f, 1.0f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.24f, 0.16f, 1.0f);
		colors[ImGuiCol_HeaderActive] = accentActive;
		colors[ImGuiCol_Tab] = ImVec4(0.11f, 0.11f, 0.13f, 1.0f);
		colors[ImGuiCol_TabHovered] = accentHover;
		colors[ImGuiCol_TabActive] = accentActive;
		colors[ImGuiCol_Text] = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
	}

	static void DrawModule(Module& m) {
		ImGui::PushID(m.name.c_str());
		ImGui::Checkbox("##enabled", m.enabled);
		ImGui::SameLine();
		bool open = ImGui::CollapsingHeader(m.name.c_str());
		if (open && m.drawSettings) {
			ImGui::Indent();
			m.drawSettings();
			ImGui::Unindent();
		}
		ImGui::PopID();
	}

	static void DrawMenu(bool& menuOpen, std::vector<Category>& categories) {
		ImGui::SetNextWindowSize(ImVec2(340, 420), ImGuiCond_FirstUseEver);
		ImGui::Begin("Azheng", &menuOpen, ImGuiWindowFlags_NoCollapse);

		if (ImGui::BeginTabBar("categories")) {
			for (auto& cat : categories) {
				if (ImGui::BeginTabItem(cat.name.c_str())) {
					for (auto& mod : cat.modules)
						DrawModule(mod);
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextDisabled("Right Shift - toggle menu");

		ImGui::End();
	}
};
