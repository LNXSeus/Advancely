//
// Created by Linus on 24.06.2025.
//

#ifndef TRACKER_H
#define TRACKER_H

#include "main.h"
#include "data_structures.h"
#include "settings_utils.h" // For AppSettings

#include "imgui.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


struct Tracker {
    SDL_Window *window;
    SDL_Renderer *renderer;

    SDL_Texture *adv_bg;
    SDL_Texture *adv_bg_half_done;
    SDL_Texture *adv_bg_done;

    ImVec2 camera_offset;
    float zoom_level;
    float time_since_last_update; // Timer displaying when the last update happened

    bool layout_locked; // Used for lock layout button
    float locked_layout_width; // Used for lock layout button to save the width

    ImFont *roboto_font; // Font for settings/UI -> USED BY IMGUI
    TTF_Font *minecraft_font; // USED BY SDL_TTF

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
 * This function allocates memory for the Tracker struct, initializes SDL components for the overlay window,
 * initializes the minecraft font, loads the background textures, allocates memory for template data,
 * re-initializes the paths in the tracker struct, and loads and parses all game data (advancements, stats, etc.)
 * from the template and player save files.
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
 * all visual elements of the tracker, such as advancement icons, and so on.
 * Solely for non-ImGUI SDL rendering. Currently UNUSED! -> tracker_render_gui()
 *
 * @param t A pointer to the Tracker struct.
 * @param settings A pointer to the application settings containing color information.
 */
void tracker_render(Tracker *t, const AppSettings *settings);

/**
 * @brief Renders the tracker window's contents using ImGui.
 *
 * Using render_section_separator, srender_trackable_category_section, render_simple_item_section,
 * render_custom_goals_section and render_multi_stage_goals_section as helper functions it renders
 * the five main sections of the tracker window: advancements, stats, unlocks, custom goals and
 * multi-stage goals.
 *
 *
 * @param t Tracker struct
 * @param settings A pointer to the application settings containing color information
 */
void tracker_render_gui(Tracker *t, const AppSettings *settings);

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
 * For legacy versions it now only tries to read the snapshot file if the
 * StatsPerWorld Mod isn't used, otherwise it reads from the stats folder per-world.
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

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif //TRACKER_H
