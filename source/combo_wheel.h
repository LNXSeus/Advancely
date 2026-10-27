// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 28.10.2026.
//

#ifndef COMBO_WHEEL_H
#define COMBO_WHEEL_H

#include <cstring>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

/**
 * @brief Steps a closed single-choice dropdown through its options with the mouse wheel while it's
 * hovered. Call right after the dropdown (ImGui::Combo() or ImGui::BeginCombo() returning false), so
 * it is the last item. Wheel down picks the next option, wheel up the previous one, wrapping around
 * at the ends. The hovered dropdown claims the wheel, so the window doesn't scroll along; while a window is
 * still being wheel-scrolled the wheel keeps scrolling it instead.
 * @param current_item Index of the chosen option, updated in place.
 * @param items_count Number of options.
 * @param allow_none For a dropdown that starts with nothing chosen (-1): the wheel passes through that
 * empty state too, in front of the first option, so it can be reached again.
 * @return true if the chosen option changed.
 */
inline bool combo_wheel_step(int *current_item, int items_count, bool allow_none = false) {
    static ImGuiID s_wheel_id = 0;
    static float s_wheel_accum = 0.0f;
    ImGuiContext &g = *ImGui::GetCurrentContext();
    const ImGuiID id = g.LastItemData.ID;
    if (items_count <= 0 || id == 0 || g.WheelingWindow != nullptr || !ImGui::IsItemHovered()) return false;
    const bool owned_wheel = ImGui::GetKeyOwnerData(&g, ImGuiKey_MouseWheelY)->OwnerCurr == id;
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    if (s_wheel_id != id) {
        s_wheel_id = id;
        s_wheel_accum = 0.0f;
    }
    if (!owned_wheel || g.IO.MouseWheel == 0.0f) return false;
    s_wheel_accum += g.IO.MouseWheel;
    const int steps = (int) s_wheel_accum;
    if (steps == 0) return false;
    s_wheel_accum -= (float) steps;
    int next;
    if (allow_none) {
        const int positions = items_count + 1;
        int start = *current_item + 1;
        if (start < 0 || start >= positions) start = 0;
        next = (start - steps) % positions;
        if (next < 0) next += positions;
        next -= 1;
    } else {
        int start = *current_item;
        if (start < 0 || start >= items_count) start = steps < 0 ? -1 : items_count;
        next = (start - steps) % items_count;
        if (next < 0) next += items_count;
    }
    if (next == *current_item) return false;
    *current_item = next;
    return true;
}

/**
 * @brief ImGui::Combo() that can also be stepped with the mouse wheel while hovered (see combo_wheel_step()).
 */
inline bool combo_wheel(const char *label, int *current_item, const char *const items[], int items_count,
                        int popup_max_height_in_items = -1) {
    const bool changed = ImGui::Combo(label, current_item, items, items_count, popup_max_height_in_items);
    return combo_wheel_step(current_item, items_count) || changed;
}

/**
 * @brief ImGui::Combo() for options in one zero-separated string ("A\0B\0"), that can also be stepped
 * with the mouse wheel while hovered (see combo_wheel_step()).
 */
inline bool combo_wheel(const char *label, int *current_item, const char *items_separated_by_zeros,
                        int popup_max_height_in_items = -1) {
    int items_count = 0;
    for (const char *p = items_separated_by_zeros; *p; p += strlen(p) + 1) items_count++;
    const bool changed = ImGui::Combo(label, current_item, items_separated_by_zeros, popup_max_height_in_items);
    return combo_wheel_step(current_item, items_count) || changed;
}

#endif //COMBO_WHEEL_H
