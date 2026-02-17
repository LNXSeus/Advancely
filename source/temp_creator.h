// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 07.09.2025.
//

#ifndef TEMP_CREATOR_H
#define TEMP_CREATOR_H

#include "tracker.h"

#ifdef __cplusplus
extern "C" {
#endif

// Avoid including the full header
struct AppSettings;
struct Tracker;

/**
 * @brief Renders the GUI for the template creator window.
 *
 * @param p_open A pointer to a boolean that controls the window's visibility.
 * @param app_settings A pointer to the loaded application settings struct.
 * @param roboto_font A pointer to the loaded Roboto font.
 * @param t A pointer to the Tracker struct.
 */
void temp_creator_render_gui(bool *p_open, AppSettings *app_settings, ImFont *roboto_font, Tracker *t);

#ifdef __cplusplus
    }
#endif

#endif //TEMP_CREATOR_H
