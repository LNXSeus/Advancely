//
// Created by Linus on 07.09.2025.
//

#include "temp_creator.h"
#include "settings_utils.h"
#include "logger.h"

void temp_creator_render_gui(bool *p_open, AppSettings *app_settings, ImFont *roboto_font, Tracker *t) {
    (void)app_settings;
    (void)t;

    if (!*p_open) {
        return;
    }

    ImGui::Begin("Template Creator", p_open, ImGuiWindowFlags_AlwaysAutoResize);

    if (roboto_font) {
        ImGui::PushFont(roboto_font);
    }

    ImGui::Text("Welcome to the Template Creator!");
    ImGui::Text("This feature is under construction.");

    if (roboto_font) {
        ImGui::PopFont();
    }

    ImGui::End();
}
