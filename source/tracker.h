//
// Created by Linus on 24.06.2025.
//

#ifndef TRACKER_H
#define TRACKER_H

#include "main.h"
#include "data_structures.h"
#include "settings_utils.h" // For AppSettings

struct Tracker {
    // TODO: Also needs to be defined in init_sdl.h
    SDL_Window *window;
    SDL_Renderer *renderer;
    // SDL_Texture *texture;

    // Single pointer to all the parsed and tracked game data
    TemplateData *template_data;

    char advancement_template_path[MAX_PATH_LENGTH];
    char lang_path[MAX_PATH_LENGTH];

    // Path to the actual saves folder
    char saves_path[MAX_PATH_LENGTH];
    char world_name[MAX_PATH_LENGTH]; // To store the name of the current world for display
    char advancements_path[MAX_PATH_LENGTH];
    char unlocks_path[MAX_PATH_LENGTH];
    char stats_path[MAX_PATH_LENGTH];
    char snapshot_path[MAX_PATH_LENGTH]; // Path to the snapshot file for legacy snapshots
};



/**
 * @brief Initializes a new Tracker instance.
 *
 * This function allocates memory for the Tracker struct, initializes its SDL components (window and renderer),
 * loads application settings, determines the correct paths for game and template files, and allocates the
 * main 'template_data' container.
 *
 * @param tracker A pointer to a Tracker struct pointer that will be allocated.
 * @param settings A pointer to the loaded application settings.
 * @return true if initialization was successful, false otherwise.
 */
bool tracker_new(Tracker **tracker, const AppSettings *settings);

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
void tracker_events(Tracker *t, SDL_Event *event, bool *is_running, bool *settings_opened);

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
void tracker_update(Tracker *t, float *deltaTime);

/**
 * @brief Renders the tracker window's contents.
 *
 * This function clears the screen with the background color and will be responsible for drawing
 * all visual elements of the tracker, such as advancement icons, progress bars, and text.
 *
 * @param t A pointer to the Tracker struct.
 * @param settings A pointer to the application settings containing color information.
 */
void tracker_render(Tracker *t, const AppSettings *settings);


/**
 * @brief Reloads settings, frees all old template data, and loads a new template.
 *
 * This function is called at runtime when settings have changed to allow for
 * switching advancement templates or languages without restarting the application.
 *
 * @param t A pointer to the Tracker struct.
 * @param settings A pointer to the application settings to use for re-initialization.
 */
void tracker_reinit_template(Tracker *t, const AppSettings *settings);

/**
 * @brief Reloads settings and re-initializes all relevant paths in the tracker struct.
 *
 * This function is used to update the tracker's paths when the settings.json file
 * is changed at runtime, for example, to point to a new saves folder.
 *
 * @param t A pointer to the Tracker struct.
 * @param settings A pointer to the application settings to use for path finding.
 */
void tracker_reinit_paths(Tracker *t, const AppSettings *settings);

/**
 * @brief Loads and parses all data from template and language files.
 *
 * It reads the main template JSON to get the structure of advancements, stats, and unlocks.
 * It also loads the corresponding language file for display names and then updates the progress
 * of each item by reading the player's actual save files.
 *
 * @param t A pointer to the Tracker struct containing the necessary paths.
 */
void tracker_load_and_parse_data(Tracker *t);

/**
 * @brief Frees all resources associated with the Tracker instance.
 *
 * This includes destroying the SDL renderer and window, and deallocating all dynamically
 * allocated memory for template data, including advancements, stats, unlocks,
 * (NOT custom, as that is saved in settings.json) and their sub-items.
 *
 * @param tracker A pointer to the Tracker struct pointer to be freed.
 */
void tracker_free(Tracker **tracker);

/**
 * @brief Updates the tracker window's title with dynamic information.
 *
 * Constructs a detailed title string including world name, version, category,
 * progress, and playtime, then sets it as the window title.
 *
 * @param t A pointer to the Tracker struct containing the live data.
 * @param settings A pointer to the application settings.
 */
void tracker_update_title(Tracker *t, const AppSettings *settings);

/**
 * @brief Prints the current advancement status to the console.
 *
 * This function is for debugging and prints progress on advancements, stats, unlocks,
 * custom goals and multi-stage goals.
 *
 * @param t A pointer to the Tracker struct.
 */
void tracker_print_debug_status(Tracker *t);

#endif //TRACKER_H
