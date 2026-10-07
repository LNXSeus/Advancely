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

// The search index is built by showing every settings tab once while the settings window is hidden,
// reading the label and tooltip of each widget on the way. That happens right when the window opens.

/**
 * @brief Starts building the search index. Call on the frame the settings window opens.
 */
void settings_search_on_open();

/**
 * @brief Hides the settings window while the index is being built. Call right before its ImGui::Begin().
 * @param window_name The exact name passed to ImGui::Begin().
 */
void settings_search_prepare_window(const char *window_name);

/**
 * @brief Replaces ImGui::BeginTabItem() for a tab of the settings window.
 * @param label The tab label.
 * @param flags The same flags ImGui::BeginTabItem() takes.
 * @return true when the tab is open, exactly like ImGui::BeginTabItem().
 */
bool settings_search_tab_begin(const char *label, ImGuiTabItemFlags flags = 0);

/**
 * @brief Call once per frame after the settings tab bar, whether or not it was drawn.
 */
void settings_search_tabs_end();

/**
 * @brief Draws the search bar and, below it, the dropdown of matching settings. Call inside the
 * settings window.
 */
void settings_search_bar();

#endif //SETTINGS_SEARCH_H
