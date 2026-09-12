#pragma once
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include "types.h"
#include "settings.h"

class Radar {
public:
void draw(ImDrawList* draw, const RadarPlayer* players, int count,
              float local_x, float local_y, float local_yaw, int local_team,
              float map_scale, int sw, int sh) {

        if (!g_settings.draw_radar || !g_settings.master_switch) return;

        // Обчислюємо розміри з урахуванням Aspect Ratio
        float size_y = g_settings.radar_size;
        float size_x = g_settings.radar_size * g_settings.radar_aspect_ratio;

        float range = (210.0f * map_scale) / g_settings.radar_zoom;

        float rx = g_settings.radar_x;
        float ry = g_settings.radar_y;
        float cx = rx + size_x * 0.5f;
        float cy = ry + size_y * 0.5f;
        float half_x = size_x * 0.5f;
        float half_y = size_y * 0.5f;

        float alpha = g_settings.radar_bg_alpha;
        ImU32 bg = IM_COL32(15, 15, 15, (int)(alpha * 255));
        ImU32 border = IM_COL32(80, 80, 80, (int)(alpha * 255));

        // Малювання фона радара (розтягується відповідно до aspect ratio)
        if (g_settings.radar_circle) {
            draw->AddEllipseFilled({cx, cy}, half_x, half_y, bg, 0.0f, 64);
            draw->AddEllipse({cx, cy}, half_x, half_y, border, 0.0f, 1.5f, 64);
        } else {
            draw->AddRectFilled({rx, ry}, {rx + size_x, ry + size_y}, bg, 4.0f);
            draw->AddRect({rx, ry}, {rx + size_x, ry + size_y}, border, 4.0f, 0, 1.5f);
        }

        ImU32 cross_col = IM_COL32(255, 255, 255, (int)(alpha * 50));
        draw->AddLine({cx - 5, cy}, {cx + 5, cy}, cross_col);
        draw->AddLine({cx, cy - 5}, {cx, cy + 5}, cross_col);

        if (g_settings.radar_rings) {
            ImU32 ring_col = IM_COL32(255, 255, 255, (int)(alpha * 25));
            for (float f : {0.25f, 0.5f, 0.75f, 1.0f})
                draw->AddEllipse({cx, cy}, half_x * f, half_y * f, ring_col, 0.0f, 0.5f, 32);
        }

        draw->PushClipRect({rx, ry}, {rx + size_x, ry + size_y}, true);

        float yaw_rad = (local_yaw + 90.0f) * 3.14159265f / 180.0f;
        float cos_y = cosf(yaw_rad);
        float sin_y = sinf(yaw_rad);

        float scale_x = half_x / range;
        float scale_y = half_y / range;

       for (int i = 0; i < count; i++) {
            if (!players[i].valid || players[i].health <= 0) continue;

            bool enemy = (players[i].team != local_team);
            if (g_settings.radar_hide_spotted && players[i].is_spotted) continue;
            if (!enemy && !g_settings.draw_teammates) continue;

            // 1. Отримуємо відносні координати у світових одиницях
            float dx = players[i].x - local_x;
            float dy = players[i].y - local_y;

            float rot_x, rot_y;

            // 2. Обертання відносно огляду камери
            if (g_settings.radar_rotate) {
                rot_x = dx * cos_y + dy * sin_y;
                rot_y = -dx * sin_y + dy * cos_y;
            } else {
                rot_x = dx;
                rot_y = -dy;
            }

            // 3. Єдиний базовий Scale для кругового обертання + Aspect Ratio лише на X
            float base_scale = half_y / range;
            
            float px = cx + (rot_x * base_scale * g_settings.radar_aspect_ratio);
            float py = cy - (rot_y * base_scale);

            // 4. Перевірка на вихід за межі радара (Clamping)
            float off_x = (px - cx) / half_x;
            float off_y = (py - cy) / half_y;
            float dist_sq = off_x * off_x + off_y * off_y;

            if (g_settings.radar_circle) {
                if (dist_sq > 1.0f) {
                    float d = sqrtf(dist_sq);
                    px = cx + (off_x / d) * (half_x - 4.0f);
                    py = cy + (off_y / d) * (half_y - 4.0f);
                }
            } else {
                px = std::clamp(px, rx + 4.0f, rx + size_x - 4.0f);
                py = std::clamp(py, ry + 4.0f, ry + size_y - 4.0f);
            }

            // 5. Малювання точок гравців
            ImU32 col = enemy
                            ? float4_to_col(g_settings.radar_enemy_color)
                            : float4_to_col(g_settings.radar_team_color);

            float world_dist = sqrtf(dx * dx + dy * dy);
            float dot_r = std::clamp(5.0f - world_dist / range * 2.0f, 2.5f, 5.0f);

            draw->AddCircleFilled({px + 1, py + 1}, dot_r, IM_COL32(0, 0, 0, 80), 12);
            draw->AddCircleFilled({px, py}, dot_r, col, 12);
            draw->AddCircle({px, py}, dot_r, IM_COL32(0, 0, 0, 100), 12, 1.0f);

            if (g_settings.radar_names && players[i].name[0]) {
                ImFont* rfont = ImGui::GetFont();
                float   rfs   = g_settings.radar_names_font_size;
                ImVec2 ts = rfont->CalcTextSizeA(rfs, FLT_MAX, 0.0f, players[i].name);
                float tx = px - ts.x * 0.5f;
                float ty = py - dot_r - ts.y - 1;
                tx = std::clamp(tx, rx + 2, rx + size_x - ts.x - 2);
                ty = std::clamp(ty, ry + 2, ry + size_y - ts.y - 2);
                draw->AddText(rfont, rfs, {tx + 1, ty + 1}, IM_COL32(0, 0, 0, 160), players[i].name);
                draw->AddText(rfont, rfs, {tx, ty},         IM_COL32(255, 255, 255, 180), players[i].name);
            }
        }

        draw->PopClipRect();
    }
};

inline Radar g_radar;
