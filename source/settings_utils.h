//
// Created by Linus on 27.06.2025.
//

#ifndef SETTINGS_UTILS_H
#define SETTINGS_UTILS_H

#include <cJSON.h>

#include "path_utils.h"
#include "tracker.h" // TODO: Has tracker struct for when the static functions from tracker.c are moved over here

#define SETTINGS_FILE_PATH "resources/config/settings.json"
#define MAX_HOTKEYS 16 // Limit for amount of hotkeys

typedef struct {
    char target_goal[192];
    SDL_Scancode increment_scancode;
    SDL_Scancode decrement_scancode;
} HotkeyBinding;

// TODO: Define versions here and in VersionMapEntry in settings_utils.c
typedef enum { // Puts vaLue starting at 0, allows for comparisons
    MC_VERSION_1_11,
    MC_VERSION_1_12,
    MC_VERSION_1_16_1,
    MC_VERSION_25W14CRAFTMINE,
    MC_VERSION_1_21_6,
    MC_VERSION_UNKNOWN, // For error handling
} MC_Version;

// A struct to hold all application settings in one place
// TODO: Remember window position in settings.json
typedef struct { // names should match json keys
    char version_str[64]; // Store version as string
    PathMode path_mode;
    char manual_saves_path[MAX_PATH_LENGTH]; // path length from path_utils.h
    char category[MAX_PATH_LENGTH]; // New field to store speedrunning category
    char optional_flag[MAX_PATH_LENGTH]; // New field to store optional flag (any string or empty) (e.g.,"_optimized")

    // These paths are constructed from the above fields
    char template_path[MAX_PATH_LENGTH];
    char lang_path[MAX_PATH_LENGTH];

    // Hotkeys
    int hotkey_count;
    HotkeyBinding hotkeys[MAX_HOTKEYS]; // Array of hotkey bindings
} AppSettings;



/**
 * @brief Converts a version string (e.g., "1.12") to an MC_Version enum.
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
 * @brief Reads the entire settings.json file and returns it as a cJSON object.
 * The caller is responsible for deleting the cJSON object.
 * @return A cJSON pointer or NULL on failure.
 */
cJSON *settings_read_full();

/**
 * @brief Writes a cJSON object to the settings.json file, overwriting it.
 * @param json_to_write The cJSON object to save.
 */
void settings_write_full(cJSON *json_to_write);

/**
 * @brief Loads settings from the settings.json file.
 *
 * If the file doesn't exist or a setting is missing, it populates
 * the struct with safe, default values. After loading, it calls
 * `construct_template_paths` to build the final file paths.
 *
 * @param settings A pointer to the AppSettings struct to be populated.
 */
void settings_load(AppSettings *settings);

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

#endif //SETTINGS_UTILS_H
