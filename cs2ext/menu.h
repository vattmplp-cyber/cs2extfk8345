#pragma once

#include <imgui.h>

#include "settings.h"
#include "types.h"
#include "utils.h"
#include "crosshair.h"
#include "grenades.h"
#include "overlay.h"
#include "weapon_icons.h"

class Menu {
public:
    void toggle() {
        g_settings.menu_open = !g_settings.menu_open;
    }

    void render() {
        if (!g_settings.menu_open)
            return;

        if (g_settings.menu_x >= 0 && g_settings.menu_y >= 0)
            ImGui::SetNextWindowPos(
                { g_settings.menu_x, g_settings.menu_y },
                ImGuiCond_FirstUseEver
            );

        ImGui::SetNextWindowSize(
            { 560, 520 },
            ImGuiCond_FirstUseEver
        );

        ImGui::PushFont(g_overlay.menu_font);

        ImGui::Begin(
            "##mainwindow",
            &g_settings.menu_open,
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar
        );

        ImVec2 pos = ImGui::GetWindowPos();
        g_settings.menu_x = pos.x;
        g_settings.menu_y = pos.y;

        ImGui::PushFont(g_overlay.menu_title_font);

        ImVec4 accent = {
            g_settings.menu_accent_color[0],
            g_settings.menu_accent_color[1],
            g_settings.menu_accent_color[2],
            g_settings.menu_accent_color[3]
        };

        ImGui::TextColored(accent, "CS2 ESP");

        ImGui::SameLine(ImGui::GetWindowWidth() - 130);

        if (g_settings.master_switch)
            ImGui::TextColored(
                { 0.3f, 1.0f, 0.3f, 1 },
                "[ACTIVE]"
            );
        else
            ImGui::TextColored(
                { 1.0f, 0.3f, 0.3f, 1 },
                "[OFF]"
            );

        ImGui::PopFont();

        ImGui::Separator();

        if (ImGui::BeginTabBar("##tabs")) {
            if (ImGui::BeginTabItem("Main")) {
                render_tab_main();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("ESP")) {
                render_tab_esp();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Radar")) {
                render_tab_radar();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Aim")) {
                render_tab_aim();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Misc")) {
                render_tab_misc();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Nades")) {
                render_tab_nades();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Menu")) {
                render_tab_menu_style();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
        ImGui::PopFont();
    }

private:
    bool bind_waiting_menu = false;
    bool bind_waiting_master = false;
    bool bind_waiting_exit = false;

    bool reset_popup_open = false;

    bool bind_waiting_nade_toggle = false;
    bool bind_waiting_nade_add = false;
    bool bind_waiting_nade_delete = false;

    bool bind_waiting_aimbot = false;
    bool bind_waiting_trigger = false;

    void render_tab_main() {
        ImGui::Spacing();

        ImGui::Text("Status:");
        ImGui::SameLine();

        if (g_settings.master_switch)
            ImGui::TextColored(
                { 0.3f, 1.0f, 0.3f, 1 },
                "Active (%s)",
                vk_name(g_settings.key_master)
            );
        else
            ImGui::TextColored(
                { 1.0f, 0.3f, 0.3f, 1 },
                "Disabled (%s)",
                vk_name(g_settings.key_master)
            );

        ImGui::Separator();

        ImGui::Text("Key Binds");
        ImGui::Spacing();

        render_key_bind(
            "Menu Toggle",
            g_settings.key_menu,
            bind_waiting_menu
        );

        render_key_bind(
            "Master Toggle",
            g_settings.key_master,
            bind_waiting_master
        );

        render_key_bind(
            "Exit",
            g_settings.key_exit,
            bind_waiting_exit
        );

        ImGui::Separator();

        ImGui::Text("Performance");

        ImGui::Checkbox(
            "Vsync",
            &g_settings.use_vsync
        );

        ImGui::BeginDisabled(g_settings.use_vsync);

        ImGui::SliderFloat(
            "Target FPS",
            &g_settings.target_fps,
            30,
            1000,
            "%.0f"
        );

        ImGui::EndDisabled();

        ImGui::Text("Memory backend");

        static bool backend_changed = false;

        // Переводимо індекси конфігу (1,2,3,4) у відповідні кнопки ImGui
        int current_backend = g_settings.memory_backend;

        if (ImGui::RadioButton(
                "WinApi",
                &current_backend,
                1
            )) {
            g_settings.memory_backend = 1;
            backend_changed = true;
        }

        ImGui::SameLine();

        if (ImGui::RadioButton(
                "Syscall",
                &current_backend,
                2
            )) {
            g_settings.memory_backend = 2;
            backend_changed = true;
        }

        ImGui::SameLine();

        if (ImGui::RadioButton(
                "Kernel",
                &current_backend,
                3
            )) {
            g_settings.memory_backend = 3;
            backend_changed = true;
        }

        ImGui::SameLine();

        if (ImGui::RadioButton(
                "UEFI (BIOS)",
                &current_backend,
                4
            )) {
            g_settings.memory_backend = 4;
            backend_changed = true;
        } // НАША НОВА КНОПКА

        if (backend_changed)
            ImGui::TextColored(
                ImVec4(1, 0.4f, 0.2f, 1),
                "Restart required to apply"
            );

        ImGui::Separator();

        if (ImGui::Button("Reset All Settings"))
            reset_popup_open = true;

        if (reset_popup_open) {
            ImGui::OpenPopup("Reset?##confirm");
            reset_popup_open = false;
        }

        if (ImGui::BeginPopupModal(
                "Reset?##confirm",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize
            )) {

            ImGui::Text(
                "Reset ALL settings to defaults?"
            );

            ImGui::Separator();

            if (ImGui::Button("Yes", { 100, 0 })) {
                g_settings.reset();
                g_overlay.font_rebuild_needed = true;
                g_overlay.apply_menu_style();
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", { 100, 0 }))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    void render_tab_esp() {
        ImGui::Spacing();

        ImGui::Checkbox(
            "ESP Enabled",
            &g_settings.esp_enabled
        );

        ImGui::Separator();

        ImVec4 theme_accent = {
            g_settings.esp_theme_color[0],
            g_settings.esp_theme_color[1],
            g_settings.esp_theme_color[2],
            g_settings.esp_theme_color[3]
        };

        ImGui::TextColored(
            theme_accent,
            "ESP Theme Color"
        );

        ImGui::Checkbox(
            "Use Theme Color##esptheme",
            &g_settings.esp_use_theme
        );

        if (g_settings.esp_use_theme) {
            ImGui::Indent();

            ImGui::ColorEdit4(
                "Theme Color##tc",
                g_settings.esp_theme_color,
                ImGuiColorEditFlags_NoInputs |
                ImGuiColorEditFlags_AlphaBar
            );

            ImGui::Text("Presets:");

            struct Preset {
                const char* name;
                float r, g, b;
            };

            static constexpr Preset presets[] = {
                { "Cyan",    0.00f,  0.85f, 1.00f },
                { "Lime",    0.20f,  1.00f, 0.30f },
                { "Blue",    0.235f, 0.68f, 0.93f },
                { "Magenta", 0.90f,  0.10f, 0.80f },
                { "White",   0.95f,  0.95f, 0.95f },
                { "Red",     1.00f,  0.15f, 0.15f }
            };

            for (const auto& pr : presets) {
                if (ImGui::Button(pr.name, { 65, 0 })) {
                    g_settings.esp_theme_color[0] = pr.r;
                    g_settings.esp_theme_color[1] = pr.g;
                    g_settings.esp_theme_color[2] = pr.b;
                    g_settings.esp_theme_color[3] = 1.0f;
                }

                ImGui::SameLine();
            }

            ImGui::NewLine();
            ImGui::Unindent();

            ImGui::TextColored(
                { 0.5f, 0.5f, 0.5f, 1 },
                "(Individual colors below are overridden by theme)"
            );

            ImGui::Separator();
        }
        else {
            ImGui::Separator();
        }

        ImGui::Text("Chams Style:");

        ImGui::RadioButton(
            "Filled",
            &g_settings.chams_style,
            0
        );

        ImGui::SameLine();

        ImGui::RadioButton(
            "Wire",
            &g_settings.chams_style,
            1
        );

        ImGui::SameLine();

        ImGui::RadioButton(
            "Glow",
            &g_settings.chams_style,
            2
        );

        ImGui::SameLine();

        ImGui::RadioButton(
            "Skeleton",
            &g_settings.chams_style,
            3
        );

        ImGui::Checkbox(
            "Draw Head",
            &g_settings.draw_head
        );

        ImGui::Checkbox(
            "Teammates",
            &g_settings.draw_teammates
        );

        ImGui::Separator();

        ImGui::Checkbox(
            "Distance Opacity Drop",
            &g_settings.esp_opacity_drop
        );

        if (g_settings.esp_opacity_drop) {
            ImGui::Indent();

            ImGui::SliderFloat(
                "Fade Start##opd",
                &g_settings.esp_opacity_drop_start,
                100.0f,
                3000.0f,
                "%.0f units"
            );

            ImGui::SliderFloat(
                "Fade End##opd",
                &g_settings.esp_opacity_drop_end,
                200.0f,
                5000.0f,
                "%.0f units"
            );

            ImGui::SliderFloat(
                "Min Opacity##opd",
                &g_settings.esp_opacity_drop_min,
                0.0f,
                1.0f,
                "%.2f"
            );

            if (g_settings.esp_opacity_drop_start >=
                g_settings.esp_opacity_drop_end) {

                g_settings.esp_opacity_drop_start =
                    g_settings.esp_opacity_drop_end - 100.0f;
            }

            ImGui::Unindent();
        }

        ImGui::Separator();

        ImGui::Checkbox(
            "Box",
            &g_settings.draw_box
        );

        if (g_settings.draw_box) {
            ImGui::Indent();

            ImGui::RadioButton(
                "Corners##bs",
                &g_settings.box_style,
                0
            );

            ImGui::SameLine();

            ImGui::RadioButton(
                "Full##bs",
                &g_settings.box_style,
                1
            );

            ImGui::SameLine();

            ImGui::RadioButton(
                "Dashed##bs",
                &g_settings.box_style,
                2
            );

            ImGui::SliderFloat(
                "Thickness##bt",
                &g_settings.box_thickness,
                0.5f,
                4.0f,
                "%.1f"
            );

            ImGui::SliderFloat(
                "Padding X",
                &g_settings.box_padding_x,
                0.0f,
                20.0f,
                "%.1f"
            );

            ImGui::SliderFloat(
                "Padding Y",
                &g_settings.box_padding_y,
                0.0f,
                20.0f,
                "%.1f"
            );

            if (g_settings.box_style == 0) {
                ImGui::SliderFloat(
                    "Corner %",
                    &g_settings.box_corner_pct,
                    0.1f,
                    0.5f,
                    "%.2f"
                );
            }

            ImGui::Unindent();
        }

        ImGui::Separator();

        ImGui::Checkbox(
            "Health Bar",
            &g_settings.draw_healthbar
        );

        if (g_settings.draw_healthbar) {
            ImGui::Indent();

            if (g_settings.esp_use_theme) {
                ImGui::BeginDisabled();

                bool forced = true;

                ImGui::Checkbox(
                    "Solid Color (theme override)##hbsc",
                    &forced
                );

                ImGui::EndDisabled();
            }
            else {
                ImGui::Checkbox(
                    "Solid Color##hbsc",
                    &g_settings.healthbar_solid_color
                );
            }

            if (g_settings.healthbar_solid_color &&
                !g_settings.esp_use_theme) {

                ImGui::ColorEdit4(
                    "Bar Color##hbcol",
                    g_settings.healthbar_color,
                    ImGuiColorEditFlags_NoInputs |
                    ImGuiColorEditFlags_AlphaBar
                );
            }

            ImGui::Unindent();
        }

        ImGui::Checkbox(
            "Health Text",
            &g_settings.draw_health_text
        );

        if (g_settings.draw_health_text) {
            ImGui::Indent();

            if (ImGui::SliderFloat(
                    "HP Font##hpf",
                    &g_settings.hp_font_size,
                    8,
                    24,
                    "%.0f"
                )) {

                g_overlay.font_rebuild_needed = true;
            }

            if (!g_settings.esp_use_theme) {
                ImGui::ColorEdit4(
                    "HP Color##hpc",
                    g_settings.hp_text_color,
                    ImGuiColorEditFlags_NoInputs |
                    ImGuiColorEditFlags_AlphaBar
                );
            }

            ImGui::Checkbox(
                "HP Shadow",
                &g_settings.hp_text_shadow
            );

            if (g_settings.hp_text_shadow) {
                ImGui::ColorEdit4(
                    "Shadow##hps",
                    g_settings.hp_text_shadow_color,
                    ImGuiColorEditFlags_NoInputs |
                    ImGuiColorEditFlags_AlphaBar
                );
            }

            ImGui::Unindent();
        }

        ImGui::Separator();

        ImGui::Checkbox(
            "Name",
            &g_settings.draw_name
        );

        if (g_settings.draw_name) {
            ImGui::Indent();

            ImGui::RadioButton(
                "Top##np",
                &g_settings.name_position,
                0
            );

            ImGui::SameLine();

            ImGui::RadioButton(
                "Bot##np",
                &g_settings.name_position,
                1
            );

            if (ImGui::SliderFloat(
                    "Name Font##nf",
                    &g_settings.name_font_size,
                    8,
                    24,
                    "%.0f"
                )) {

                g_overlay.font_rebuild_needed = true;
            }

            ImGui::DragFloat(
                "Offset X##no",
                &g_settings.name_offset_x,
                0.5f,
                -50,
                50,
                "%.1f"
            );

            ImGui::DragFloat(
                "Offset Y##no",
                &g_settings.name_offset_y,
                0.5f,
                -50,
                50,
                "%.1f"
            );

            if (!g_settings.esp_use_theme) {
                ImGui::ColorEdit4(
                    "Name##nc",
                    g_settings.name_color,
                    ImGuiColorEditFlags_NoInputs |
                    ImGuiColorEditFlags_AlphaBar
                );
            }

            ImGui::Checkbox(
                "Shadow##ns",
                &g_settings.name_shadow
            );

            if (g_settings.name_shadow) {
                ImGui::ColorEdit4(
                    "Shadow##nsc",
                    g_settings.name_shadow_color,
                    ImGuiColorEditFlags_NoInputs |
                    ImGuiColorEditFlags_AlphaBar
                );
            }

            ImGui::Unindent();
        }

        ImGui::Separator();

        ImGui::Checkbox(
            "Weapon",
            &g_settings.draw_weapon
        );

        if (g_settings.draw_weapon) {
            ImGui::Indent();

            ImGui::Checkbox(
                "Show Icon##wi",
                &g_settings.weapon_show_icon
            );

            ImGui::Checkbox(
                "Show Text##wt",
                &g_settings.weapon_show_text
            );

            if (ImGui::SliderFloat(
                    "Weapon Font##wf",
                    &g_settings.weapon_font_size,
                    8,
                    20,
                    "%.0f"
                )) {

                g_overlay.font_rebuild_needed = true;
            }

            ImGui::SliderFloat(
                "Dist. Dropoff##wdd",
                &g_settings.weapon_distance_dropoff,
                0,
                1,
                "%.2f"
            );

            if (!g_settings.esp_use_theme) {
                ImGui::ColorEdit4(
                    "Text Color##wc",
                    g_settings.weapon_color,
                    ImGuiColorEditFlags_NoInputs |
                    ImGuiColorEditFlags_AlphaBar
                );

                ImGui::ColorEdit4(
                    "Icon Tint##wic",
                    g_settings.weapon_icon_color,
                    ImGuiColorEditFlags_NoInputs |
                    ImGuiColorEditFlags_AlphaBar
                );
            }

            ImGui::Checkbox(
                "Weapon Shadow##ws",
                &g_settings.weapon_shadow
            );

            if (g_settings.weapon_shadow) {
                ImGui::ColorEdit4(
                    "Shadow##wsc",
                    g_settings.weapon_shadow_color,
                    ImGuiColorEditFlags_NoInputs |
                    ImGuiColorEditFlags_AlphaBar
                );
            }

            ImGui::Unindent();
        }

        ImGui::Separator();

        ImGui::Text("ESP Font:");
        render_esp_font_selector();

        ImGui::Separator();

        ImGui::Text("Body Tuning");

        ImGui::SliderFloat(
            "Body Width",
            &g_settings.body_width_scale,
            0.3f,
            3.0f,
            "%.2f"
        );

        ImGui::SliderFloat(
            "Head Size",
            &g_settings.head_radius,
            1,
            10,
            "%.1f"
        );

        ImGui::SliderFloat(
            "Depth Scale",
            &g_settings.depth_scale,
            100,
            1500,
            "%.0f"
        );

        ImGui::SliderFloat(
            "Glow Outer",
            &g_settings.glow_expand_outer,
            0,
            15,
            "%.1f"
        );

        ImGui::SliderFloat(
            "Glow Inner",
            &g_settings.glow_expand_inner,
            0,
            10,
            "%.1f"
        );

        ImGui::Separator();
    }

    void render_tab_radar() {
        ImGui::Spacing();

        ImGui::Checkbox(
            "Show Radar",
            &g_settings.draw_radar
        );

        ImGui::Separator();

        ImGui::Checkbox(
            "Circle Shape",
            &g_settings.radar_circle
        );

        ImGui::Checkbox(
            "Rotate with View",
            &g_settings.radar_rotate
        );

        ImGui::Checkbox(
            "Range Rings",
            &g_settings.radar_rings
        );

        ImGui::Checkbox(
            "Player Names",
            &g_settings.radar_names
        );

        ImGui::Separator();

        ImGui::SliderFloat(
            "Size",
            &g_settings.radar_size,
            100,
            1000,
            "%.0f"
        );

        ImGui::SliderFloat(
            "Zoom",
            &g_settings.radar_zoom,
            0.1f,
            1.0f,
            "%.2fx"
        );

        ImGui::SliderFloat(
            "Opacity",
            &g_settings.radar_bg_alpha,
            0.0f,
            1.0f,
            "%.2f"
        );

        ImGui::DragFloat(
            "Position X",
            &g_settings.radar_x,
            1,
            0,
            3000
        );

        ImGui::DragFloat(
            "Position Y",
            &g_settings.radar_y,
            1,
            0,
            2000
        );
    }

    void render_tab_aim() {
        ImGui::Checkbox(
            "Enable aimbot",
            &g_settings.aimbot_enabled
        );

        if (g_settings.aimbot_enabled) {
            render_key_bind(
                "Key",
                g_settings.key_aimbot,
                bind_waiting_aimbot
            );

            ImGui::SliderInt(
                "Fov",
                &g_settings.aimbot_fov,
                1,
                360
            );

            ImGui::SliderFloat(
                "Smoothing",
                &g_settings.aimbot_smooth,
                1.0f,
                20.0f,
                "%.1f"
            );
        }

        ImGui::Separator();

        ImGui::Checkbox(
            "Enable triggerbot",
            &g_settings.triggerbot_enabled
        );

        if (g_settings.triggerbot_enabled) {
            render_key_bind(
                "Trigger key",
                g_settings.key_triggerbot,
                bind_waiting_trigger
            );

            ImGui::SliderInt(
                "Delay ms",
                &g_settings.triggerbot_delay,
                0,
                300
            );
        }
    }

    void render_tab_misc() {
        ImGui::Checkbox(
            "Show Spectators",
            &g_settings.draw_spectators
        );

        ImGui::Separator();

        ImGui::Checkbox(
            "Enabled##xhair",
            &g_settings.crosshair_enabled
        );
    }

    void render_tab_nades() {
        ImGui::Checkbox(
            "Grenade Helper",
            &g_settings.grenade_helper_enabled
        );
    }

    void render_tab_menu_style() {
        ImGui::ColorEdit4(
            "Accent Color",
            g_settings.menu_accent_color,
            ImGuiColorEditFlags_NoInputs |
            ImGuiColorEditFlags_AlphaBar
        );
    }

    void render_key_bind(
        const char* label,
        int& key,
        bool& waiting,
        bool allow_mouse1 = false
    ) {
        ImGui::Text("%s:", label);
        ImGui::SameLine(160);

        char btn[64];

        if (waiting)
            snprintf(
                btn,
                sizeof(btn),
                "[...]##%s",
                label
            );
        else
            snprintf(
                btn,
                sizeof(btn),
                "%s##%s",
                vk_name(key),
                label
            );

        if (ImGui::Button(btn, { 100, 0 }))
            waiting = true;

        if (waiting) {
            int pressed = scan_any_key(allow_mouse1);

            if (pressed > 0) {
                key = pressed;
                waiting = false;
            }
            else if (pressed == -1) {
                waiting = false;
            }
        }
    }

    void render_esp_font_selector() {
        auto& fonts = g_overlay.available_fonts;

        if (fonts.empty())
            return;

        const char* preview =
            (
                g_settings.esp_font_index >= 0 &&
                g_settings.esp_font_index < (int)fonts.size()
            )
                ? fonts[g_settings.esp_font_index]
                      .display_name
                      .c_str()
                : "?";

        if (ImGui::BeginCombo(
                "##espfont",
                preview
            )) {

            for (int i = 0; i < (int)fonts.size(); i++) {
                if (ImGui::Selectable(
                        fonts[i].display_name.c_str(),
                        g_settings.esp_font_index == i
                    )) {

                    g_settings.esp_font_index = i;
                    g_overlay.font_rebuild_needed = true;
                }
            }

            ImGui::EndCombo();
        }
    }

    void render_menu_font_selector() {
        auto& fonts = g_overlay.menu_fonts;

        if (fonts.empty())
            return;

        const char* preview =
            (
                g_settings.menu_font_index >= 0 &&
                g_settings.menu_font_index < (int)fonts.size()
            )
                ? fonts[g_settings.menu_font_index]
                      .display_name
                      .c_str()
                : "?";

        if (ImGui::BeginCombo(
                "##menufont",
                preview
            )) {

            for (int i = 0; i < (int)fonts.size(); i++) {
                if (ImGui::Selectable(
                        fonts[i].display_name.c_str(),
                        g_settings.menu_font_index == i
                    )) {

                    g_settings.menu_font_index = i;
                    g_overlay.font_rebuild_needed = true;
                }
            }

            ImGui::EndCombo();
        }
    }
};

inline Menu g_menu;
