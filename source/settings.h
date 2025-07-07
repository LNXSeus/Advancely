//
// Created by Linus on 26.06.2025.
//

#ifndef SETTINGS_H
#define SETTINGS_H

#include "main.h"
#include "settings_utils.h" // For AppSettings

struct Settings { // TODO: Also needs to be defined in init_sdl.h
    SDL_Window *window;
    SDL_Renderer *renderer;
    // SDL_Texture *texture;

     SDL_Window *parent_window; // Keep track of the tracker window to center the settings window
};

/**
 * @brief Initializes a new Settings window instance.
 *
 * Allocates memory for the Settings struct and initializes its corresponding
 * SDL window and renderer.
 *
 * @param settings A pointer to a Settings struct pointer that will be allocated.
 * @param app_settings A pointer to the loaded application settings.
 * @param parent A pointer to the main tracker window, used for positioning.
 * @return true if initialization was successful, false otherwise.
 */
bool settings_new(struct Settings **settings, const AppSettings *app_settings, SDL_Window *parent);

/**
 * @brief Handles SDL events specifically for the settings window.
 *
 * Processes events like closing the window via the ESCAPE key.
 *
 * @param s A pointer to the Settings struct.
 * @param event A pointer to the SDL_Event to process.
 * @param is_running A pointer to the main application loop's running flag.
 * @param settings_opened A pointer to the flag that controls the settings window's visibility.
 */
void settings_events(struct Settings *s, SDL_Event *event, bool *is_running, bool *settings_opened);

/**
 * @brief Updates the state of the settings window.
 *
 * This function is a placeholder for future logic, such as handling user input
 * from UI elements within the settings window.
 *
 * @param s A pointer to the Settings struct.
 * @param deltaTime A pointer to the frame's delta time.
 */
void settings_update(struct Settings *s, float *deltaTime);

/**
 * @brief Renders the settings window's contents.
 *
 * Clears the screen with the background color. This is where all UI elements
 * for the settings will be drawn.
 *
 * @param s A pointer to the Settings struct.
 * @param settings A pointer to the application settings containing color information.
 */
void settings_render(struct Settings *s, const AppSettings *settings);

/**
 * @brief Frees all resources associated with the Settings instance.
 *
 * This includes destroying the SDL renderer and window and deallocating
 * the memory for the Settings struct itself.
 *
 * @param settings A pointer to the Settings struct pointer to be freed.
 */
void settings_free(struct Settings **settings);

#endif //SETTINGS_H
