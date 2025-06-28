//
// Created by Linus on 27.06.2025.
//

#ifndef SETTINGS_UTILS_H
#define SETTINGS_UTILS_H

#include <cJSON.h>

#include "path_utils.h"


#define SETTINGS_FILE_PATH "resources/config/settings.json"

// TODO: Define versions here, go with correct subversions
typedef enum {
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
    MC_Version version;
    PathMode path_mode;
    char manual_saves_path[MAX_PATH_LENGTH]; // path length from path_utils.h
    char advancement_template_path[MAX_PATH_LENGTH];
    char advancement_lang_path[MAX_PATH_LENGTH]; // Path for the language file
    char mc_version_dir[MAX_PATH_LENGTH]; // New field to store Minecraft version directory
    char mc_version_filename[MAX_PATH_LENGTH]; // New field for the filename version component
    char category[MAX_PATH_LENGTH]; // New field to store speedrunning category
    char optional_flag[MAX_PATH_LENGTH]; // New field to store optional flag (any string or empty) (e.g.,"_optimized")
} AppSettings;

/**
 * @brief Loads settings from the settings.json file.
 *
 * If the file doesn't exist or a setting is missing, it populates
 * the struct with safe, default values.
 *
 * @param settings A pointer to the AppSettings struct to be populated.
 */
void settings_load(AppSettings *settings);

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
 * @brief Constructs the advancement template and language paths based on the provided settings.
 * @param settings A pointer to the AppSettings struct to be populated with the constructed paths.
 */
void construct_advancement_paths(AppSettings *settings);

#endif //SETTINGS_UTILS_H
