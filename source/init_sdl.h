//
// Created by Linus on 24.06.2025.
//

#ifndef INIT_H
#define INIT_H

#include "tracker.h" // includes main.h
#include "settings.h"
#include "overlay.h"
#include "settings_utils.h" // For AppSettings

struct Tracker;
struct Overlay;
struct Settings;

/**
 * @brief Initializes SDL3 for the tracker window.
 *
 * This function sets up the SDL3 library and creates a window for the tracker application.
 *
 * @param t A pointer to the Tracker struct.
 * @param settings A pointer to the loaded application settings.
 * @return true if initialization was successful, false otherwise.
 */
bool tracker_init_sdl(struct Tracker *t, const AppSettings *settings);

/**
 * @brief Initializes SDL3 for the overlay window.
 *
 * This function sets up the SDL3 library and creates a window for the overlay application.
 *
 * @param o A pointer to the Overlay struct.
 * @param settings A pointer to the loaded application settings.
 * @return true if initialization was successful, false otherwise.
 */
bool overlay_init_sdl(struct Overlay *o, const AppSettings *settings);

/**
 * @brief Initializes SDL3 for the settings window.
 *
 * This function sets up the SDL3 library and creates a window for the settings application.
 *
 * @param s A pointer to the Settings struct.
 * @param settings A pointer to the loaded application settings.
 * @return true if initialization was successful, false otherwise.
 */
bool settings_init_sdl(struct Settings *s, const AppSettings *settings);


#endif //INIT_H
