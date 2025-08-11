//
// Created by Linus on 26.06.2025.
//

#ifndef SETTINGS_H
#define SETTINGS_H

#include "main.h"
#include "tracker.h"

#ifdef __cplusplus
extern "C" {
#endif



// Avoid including the full header
struct AppSettings;

/**
 * @brief Renders the GUI for the settings window within the tracker window using ImGui.
 * The controls stay the same with escape key to open and close.
 * Also refers to temp_create_utils for template creator.
 *
 * @param p_open Provides the 'X' button to close the window.
 * @param app_settings A pointer to the loaded application settings struct.
 * @param roboto_font A pointer to the loaded Roboto font.
 * @param t A pointer to the Tracker struct.
 */
void settings_render_gui(bool *p_open, AppSettings *app_settings, ImFont *roboto_font, Tracker *t);




#ifdef __cplusplus
}
#endif

#endif //SETTINGS_H
