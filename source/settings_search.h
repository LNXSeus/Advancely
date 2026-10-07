// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 07.10.2026.
//

#ifndef SETTINGS_SEARCH_H
#define SETTINGS_SEARCH_H

#include "imgui/imgui.h"

// Every tooltip in the settings window goes through these helpers instead of ImGui directly, so the
// settings search can read the tooltip text of every setting without anyone hovering it. settings.cpp
// poisons ImGui::SetTooltip / BeginTooltip / EndTooltip, so a new tooltip can't skip them.

/**
 * @brief Replaces ImGui::IsItemHovered() in front of a settings tooltip.
 * @param flags The same flags ImGui::IsItemHovered() takes.
 * @return true when the tooltip of the last item should be built.
 */
bool settings_tooltip_wanted(ImGuiHoveredFlags flags = 0);

/**
 * @brief Like settings_tooltip_wanted(), for an item whose hover state was already worked out.
 * @param hovered Whether the item (or the items making it up) is hovered.
 * @return true when the tooltip should be built.
 */
bool settings_tooltip_wanted_if(bool hovered);

/**
 * @brief Replaces ImGui::SetTooltip("%s", text) for a settings tooltip.
 * @param text The finished tooltip text.
 */
void settings_tooltip(const char *text);

/**
 * @brief Replaces ImGui::BeginTooltip() for a settings tooltip drawn with separate widgets.
 * Always pair it with settings_rich_tooltip_end().
 */
bool settings_rich_tooltip_begin();

/**
 * @brief Replaces ImGui::EndTooltip() after settings_rich_tooltip_begin().
 */
void settings_rich_tooltip_end();

#endif //SETTINGS_SEARCH_H
