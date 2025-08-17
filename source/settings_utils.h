//
// Created by Linus on 27.06.2025.
//

#ifndef SETTINGS_UTILS_H
#define SETTINGS_UTILS_H

#include "path_utils.h" // For MAX_PATH_LENGTH
#include "data_structures.h" // For MC_Version and PathMode enums

#ifdef __cplusplus
extern "C" {
#endif

#include <cJSON.h>


#define SETTINGS_FILE_PATH "resources/config/settings.json"
#define MAX_HOTKEYS 32 // Limit for amount of hotkeys

// DEFAULT values
#define DEFAULT_ENABLE_OVERLAY false // Stream overlay will be off by default
#define DEFAULT_USING_STATS_PER_WORLD_LEGACY true
#define DEFAULT_FPS 60
#define DEFAULT_TRACKER_ALWAYS_ON_TOP true
#define DEFAULT_OVERLAY_SCROLL_SPEED 1.0f
// #define DEFAULT_OVERLAY_SCROLL_LEFT_TO_RIGHT true  // TODO: Remove this later, sign of scroll speed controls direction
#define DEFAULT_GOAL_ALIGN_LEFT true
#define DEFAULT_REMOVE_COMPLETED_GOALS true

// DEFINE DEFAULT SETTINGS
#define DEFAULT_VERSION "1.16.1"
#define DEFAULT_CATEGORY "test"
#define DEFAULT_OPTIONAL_FLAG "1"

// Default window positions/sizes. -1 means centered or default size.
#define DEFAULT_WINDOW_POS (-1)
#define DEFAULT_WINDOW_SIZE (-1)



struct TemplateData;

typedef struct {
    char target_goal[192];
    SDL_Scancode increment_scancode;
    SDL_Scancode decrement_scancode;
} HotkeyBinding;

// Data structures for settings
typedef struct {
    int x, y, w, h;
} WindowRect;

typedef struct {
    Uint8 r, g, b, a;
} ColorRGBA;

// Default colors when it's just {} in settings.json, so no r, g, b, a values
extern const ColorRGBA DEFAULT_TRACKER_BG_COLOR;
extern const ColorRGBA DEFAULT_OVERLAY_BG_COLOR;
extern const ColorRGBA DEFAULT_SETTINGS_BG_COLOR;
extern const ColorRGBA DEFAULT_TEXT_COLOR;

// A Struct to hold all application settings in one place

// A struct to hold all application settings in one place
// TODO: Remember window position in settings.json
struct AppSettings {
    // names should match json keys
    char version_str[64]; // Store version as string
    PathMode path_mode;
    char manual_saves_path[MAX_PATH_LENGTH]; // path length from path_utils.h
    char category[MAX_PATH_LENGTH]; // New field to store speedrunning category
    char optional_flag[MAX_PATH_LENGTH]; // New field to store optional flag (any string or empty) (e.g.,"_optimized")

    // These paths are constructed from the above fields
    char template_path[MAX_PATH_LENGTH];
    char lang_path[MAX_PATH_LENGTH];
    char snapshot_path[MAX_PATH_LENGTH]; // For legacy snapshots

    // Hotkeys
    int hotkey_count;
    HotkeyBinding hotkeys[MAX_HOTKEYS]; // Array of hotkey bindings

    // General and Visual Settings
    bool enable_overlay;
    bool using_stats_per_world_legacy; // For StatsPerWorld mod, moving global stats into each world
    float fps;
    bool tracker_always_on_top;
    float overlay_scroll_speed;
    bool goal_align_left;
    bool remove_completed_goals;

    // Window Geometry
    WindowRect tracker_window;
    WindowRect overlay_window;

    // Colors
    ColorRGBA tracker_bg_color;
    ColorRGBA overlay_bg_color;
    ColorRGBA text_color;
};


/**
 * @brief Converts a version string (e.g., "1.12") to an MC_Version enum (e.g., MC_VERSION_1_12).
 *
 * @param version_str The string to convert.
 * @return The corresponding MC_Version enum, or MC_VERSION_UNKNOWN.
 */
MC_Version settings_get_version_from_string(const char *version_str);

/**
 * @brief Converts a path mode string ("auto" or "manual") to a PathMode enum.
 * @param mode_str The string to convert.
 * @return The corresponding PathMode enum.
 */
PathMode settings_get_path_mode_from_string(const char *mode_str);


/**
 * @brief Populates an AppSettings struct with the default application settings.
 *
 * @param settings A pointer to the AppSettings struct to be populated.
 */
void settings_set_defaults(AppSettings *settings);

/**
 * @brief Loads settings from the settings.json file.
 *
 * If the file doesn't exist or a setting is missing, it populates
 * the struct with safe, default values. After loading, it calls
 * `construct_template_paths` to build the final file paths.
 *
 * @param settings A pointer to the AppSettings struct to be populated.
 * @return true if any default values were used (signaling a need to re-save), false otherwise.
 */
bool settings_load(AppSettings *settings);

/**
 * @brief Saves the entire AppSettings configuration to settings.json.
 * It reads the existing file, updates values, and writes it back, preserving unknown fields.
 * This is the new centralized save function.
 * @param settings A pointer to the AppSettings struct containing the data to save.
 * @param td A pointer to the TemplateData struct to save custom progress. Can be NULL.
 */
void settings_save(const AppSettings *settings, const TemplateData *td);

/**
 * @brief Constructs the full paths to the template and language JSON files. Does NOT CREATE the files or load them.
 *
 * Based on the version, category, and optional flag settings, this function
 * builds the final, relative paths to the required data files and stores them
 * in the `template_path` and `lang_path` fields of the AppSettings struct.
 *
 * @param settings A pointer to the AppSettings struct containing the base settings
 * and which will be updated with the constructed paths.
 */
void construct_template_paths(AppSettings *settings);

#ifdef __cplusplus
}
#endif

#endif //SETTINGS_UTILS_H
