//
// Created by Linus on 24.06.2025.
//

#ifndef INIT_H
#define INIT_H

#ifdef __cplusplus
extern "C" {
#endif

struct Tracker;
struct Overlay;
struct Settings;
struct AppSettings;

/**
 * @brief Initializes SDL3 for the tracker window.
 *
 * This function sets up the SDL3 library and creates a window for the tracker application.
 *
 * @param t A pointer to the Tracker struct.
 * @param settings A pointer to the loaded application settings.
 * @return true if initialization was successful, false otherwise.
 */
bool tracker_init_sdl(Tracker *t, const AppSettings *settings);

/**
 * @brief Initializes SDL3 for the overlay window.
 *
 * This function sets up the SDL3 library and creates a window for the overlay application.
 *
 * @param o A pointer to the Overlay struct.
 * @param settings A pointer to the loaded application settings.
 * @return true if initialization was successful, false otherwise.
 */
bool overlay_init_sdl(Overlay *o, const AppSettings *settings);

#ifdef __cplusplus
}
#endif

#endif //INIT_H
