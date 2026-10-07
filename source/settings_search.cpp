// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 07.10.2026.
//

#include "settings_search.h"

bool settings_tooltip_wanted(ImGuiHoveredFlags flags) {
    return ImGui::IsItemHovered(flags);
}

bool settings_tooltip_wanted_if(bool hovered) {
    return hovered;
}

void settings_tooltip(const char *text) {
    ImGui::SetTooltip("%s", text);
}

bool settings_rich_tooltip_begin() {
    return ImGui::BeginTooltip();
}

void settings_rich_tooltip_end() {
    ImGui::EndTooltip();
}
