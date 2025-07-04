//
// Created by Linus on 24.06.2025.
//

#ifndef INIT_H
#define INIT_H

#include "tracker.h" // includes main.h
#include "settings.h"
#include "overlay.h"

struct Tracker;
struct Overlay;
struct Settings;

/**
 * @brief Initializes SDL3 for the tracker window.
 *
 * This function sets up the SDL3 library and creates a window for the tracker application.
 *
 * @param t A pointer to the Tracker struct.
 * @return true if initialization was successful, false otherwise.
 */
bool tracker_init_sdl(struct Tracker *t);

/**
 * @brief Initializes SDL3 for the overlay window.
 *
 * This function sets up the SDL3 library and creates a window for the overlay application.
 *
 * @param o A pointer to the Overlay struct.
 * @return true if initialization was successful, false otherwise.
 */
bool overlay_init_sdl(struct Overlay *o);

/**
 * @brief Initializes SDL3 for the settings window.
 *
 * This function sets up the SDL3 library and creates a window for the settings application.
 *
 * @param s A pointer to the Settings struct.
 * @return true if initialization was successful, false otherwise.
 */
bool settings_init_sdl(struct Settings *s);


#endif //INIT_H
