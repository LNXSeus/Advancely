//
// Created by Linus on 26.06.2025.
//

#ifndef SETTINGS_H
#define SETTINGS_H

#include "tracker.h"

#ifdef __cplusplus
extern "C" {
#endif


// Avoid including the full header
struct AppSettings;

/**
 * @brief Renders the GUI for the settings window within the tracker window using ImGui.
 * The controls stay the same with escape key to open and close.
 *
 * @param p_open Provides the 'X' button to close the window.
 * @param app_settings A pointer to the loaded application settings struct.
 * @param roboto_font A pointer to the loaded Roboto font.
 * @param t A pointer to the Tracker struct.
 * @param force_open_reason A pointer to the global enum that forces the window open.
 * @param p_temp_creator_open A pointer to the boolean controlling the template creator window.
 */
void settings_render_gui(bool *p_open, AppSettings *app_settings, ImFont *roboto_font, Tracker *t,
                         ForceOpenReason *force_open_reason, bool *p_temp_creator_open);


#ifdef __cplusplus
}
#endif

#endif //SETTINGS_H
