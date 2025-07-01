//
// Created by Linus on 24.06.2025.
//

#ifndef TRACKER_H
#define TRACKER_H

#include "main.h"
#include "data_structures.h"

struct Tracker { // TODO: Also needs to be defined in init_sdl.h
    SDL_Window *window;
    SDL_Renderer *renderer;
    // SDL_Texture *texture;

    // Single pointer to all the parsed and tracked game data
    TemplateData *template_data;

    char advancement_template_path[MAX_PATH_LENGTH];
    char lang_path[MAX_PATH_LENGTH];

    // Path to the actual saves folder
    char saves_path[MAX_PATH_LENGTH];
    char advancements_path[MAX_PATH_LENGTH];
    char unlocks_path[MAX_PATH_LENGTH];
    char stats_path[MAX_PATH_LENGTH];
};

/**
 * @brief Initializes a new Tracker instance.
 *
 * This function allocates memory for the Tracker struct, initializes its SDL components (window and renderer),
 * loads application settings, determines the correct paths for game and template files, and allocates the
 * main 'template_data' container.
 *
 * @param tracker A pointer to a Tracker struct pointer that will be allocated.
 * @return true if initialization was successful, false otherwise.
 */
bool tracker_new(struct Tracker **tracker);

/**
 * @brief Handles SDL events specifically for the tracker window.
 *
 * It processes events like closing the window or keyboard inputs. For example,
 * pressing the ESCAPE key will toggle the visibility of the settings window.
 *
 * @param t A pointer to the Tracker struct.
 * @param event A pointer to the SDL_Event to process.
 * @param is_running A pointer to the main application loop's running flag.
 * @param settings_opened A pointer to the flag indicating if the settings window is open.
 */
void tracker_events(struct Tracker *t, SDL_Event *event, bool *is_running, bool *settings_opened);

/**
 * @brief Updates the state of the tracker.
 *
 * This function is responsible for the initial loading and parsing of all game data (advancements, stats, etc.)
 * from the template and player save files. It ensures this heavy operation is only performed once.
 * In the future, it could be used for animations or periodic data refreshing.
 *
 * @param t A pointer to the Tracker struct.
 * @param deltaTime A pointer to the frame's delta time, for future use in animations.
 */
void tracker_update(struct Tracker *t, float *deltaTime);

/**
 * @brief Renders the tracker window's contents.
 *
 * This function clears the screen with the background color and will be responsible for drawing
 * all visual elements of the tracker, such as advancement icons, progress bars, and text.
 *
 * @param t A pointer to the Tracker struct.
 */
void tracker_render(struct Tracker *t);

/**
 * @brief Frees all resources associated with the Tracker instance.
 *
 * This includes destroying the SDL renderer and window, and deallocating all dynamically
 * allocated memory for template data, including advancements, stats, unlocks, (NOT custom, as that is saved in settings.json) and their sub-items.
 *
 * @param tracker A pointer to the Tracker struct pointer to be freed.
 */
void tracker_free(struct Tracker **tracker);

/**
 * @brief Loads and parses all data from template and language files.
 *
 * It reads the main template JSON to get the structure of advancements, stats, and unlocks.
 * It also loads the corresponding language file for display names and then updates the progress
 * of each item by reading the player's actual save files.
 *
 * @param t A pointer to the Tracker struct containing the necessary paths.
 */
void tracker_load_and_parse_data(struct Tracker *t);

#endif //TRACKER_H