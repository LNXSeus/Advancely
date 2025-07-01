//
// Created by Linus on 26.06.2025.
//

#ifndef SETTINGS_H
#define SETTINGS_H

#include "main.h"

struct Settings { // TODO: Also needs to be defined in init_sdl.h
    SDL_Window *window;
    SDL_Renderer *renderer;
    // SDL_Texture *texture;

    int settings_width;
    int settings_height;

    cJSON *settings; // Replaced by AppSettings in settings_utils
    cJSON *translation; // ENGLISH ONLY, probably unsused

    // More stuff to be added like TTF_Font *font and SDL_Texture *sprite whatever
};

/**
 * @brief Initializes a new Settings window instance.
 *
 * Allocates memory for the Settings struct and initializes its corresponding
 * SDL window and renderer.
 *
 * @param settings A pointer to a Settings struct pointer that will be allocated.
 * @return true if initialization was successful, false otherwise.
 */
bool settings_new(struct Settings **settings);

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
 */
void settings_render(struct Settings *s);

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
