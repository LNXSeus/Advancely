//
// Created by Linus on 27.06.2025.
//

#include "settings_utils.h"
#include "file_utils.h" // has the cJSON_from_file function
#include "path_utils.h"
#include <stdio.h>
#include <string.h>

// TODO: should create one that opens, reads, and parses a file into a cJSON object.

MC_Version settings_get_version_from_string(const char *version_str) {
    // TODO: Shorten this so it can convert the dot into underscore, prepend MC_VERSION_ and capitalize
    if (version_str == NULL) return MC_VERSION_UNKNOWN;
    if (strcmp(version_str, "1.11") == 0) return MC_VERSION_1_11;
    if (strcmp(version_str, "1.12") == 0) return MC_VERSION_1_12;
    if (strcmp(version_str, "1.21.6") == 0) return MC_VERSION_1_21_6;
    if (strcmp(version_str, "25w14craftmine") == 0) return MC_VERSION_25W14CRAFTMINE;
    return MC_VERSION_UNKNOWN;
}

PathMode settings_get_path_mode_from_string(const char *mode_str) {
    if (mode_str && strcmp(mode_str, "manual") == 0) { // returns 0 if strings are equal
        return PATH_MODE_MANUAL;
    }
    return PATH_MODE_AUTO; // Default to auto
}

void settings_load(AppSettings *settings) {
    // Set safe defaults first
    settings->version = MC_VERSION_1_21_6;
    settings->path_mode = PATH_MODE_AUTO;
    settings->manual_saves_path[0] = '\0';
    settings->advancement_template_path[0] = '\0';
    settings->advancement_lang_path[0] = '\0';

    // Set defaults for the dynamic path components with proper null termination if strings are too long
    strncpy(settings->mc_version_dir, "1.21.6", MAX_PATH_LENGTH - 1);
    settings->mc_version_dir[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(settings->mc_version_filename, "1_21_6", MAX_PATH_LENGTH - 1);
    settings->mc_version_filename[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(settings->category, "all_advancements", MAX_PATH_LENGTH - 1);
    settings->category[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(settings->optional_flag, "_optimized", MAX_PATH_LENGTH - 1);
    settings->optional_flag[MAX_PATH_LENGTH - 1] = '\0';

    // Load and parse the settings file
    cJSON *json = cJSON_from_file(SETTINGS_FILE_PATH); // returns json object with corrected slashes from windows paste
    if (json == NULL) {
        fprintf(stderr, "[SETTINGS UTILS] Failed to load settings file: %s. Using default settings.\n", SETTINGS_FILE_PATH);
    } else {
        // Overwrite defaults with values from the file
        // get version item from json
        const cJSON *version_json = cJSON_GetObjectItem(json, "version");
        if (cJSON_IsString(version_json) && (version_json->valuestring != NULL)) {
            settings->version = settings_get_version_from_string(version_json->valuestring);
            if (settings->version == MC_VERSION_UNKNOWN) {
                fprintf(stderr, "[SETTINGS UTILS] Unknown version '%s' in settings.json. Defaulting to 1.21.6.\n",
                        version_json->valuestring);
                settings->version = MC_VERSION_1_21_6; // Fallback to 1.21.6
            }
        }

        const cJSON *path_mode_json = cJSON_GetObjectItem(json, "path_mode");
        if (cJSON_IsString(path_mode_json) && (path_mode_json->valuestring != NULL)) {
            settings->path_mode = settings_get_path_mode_from_string(path_mode_json->valuestring);
        }

        const cJSON *manual_path_json = cJSON_GetObjectItem(json, "manual_saves_path");
        if (cJSON_IsString(manual_path_json) && (manual_path_json->valuestring != NULL)) {
            strncpy(settings->manual_saves_path, manual_path_json->valuestring, MAX_PATH_LENGTH - 1);
            settings->manual_saves_path[MAX_PATH_LENGTH - 1] = '\0'; // -1 because strncpy doesn't null-terminate
        }

        // Load dynamic path components from JSON
        const cJSON *mc_version_dir_json = cJSON_GetObjectItem(json, "mc_version_dir");
        if (cJSON_IsString(mc_version_dir_json) && (mc_version_dir_json->valuestring != NULL)) {
            strncpy(settings->mc_version_dir, mc_version_dir_json->valuestring, MAX_PATH_LENGTH - 1);
        }

        const cJSON *mc_version_filename_json = cJSON_GetObjectItem(json, "mc_version_filename");
        if (cJSON_IsString(mc_version_filename_json) && (mc_version_filename_json->valuestring != NULL)) {
            strncpy(settings->mc_version_filename, mc_version_filename_json->valuestring, MAX_PATH_LENGTH - 1);
            settings->mc_version_filename[MAX_PATH_LENGTH - 1] = '\0';
        }

        const cJSON *category_json = cJSON_GetObjectItem(json, "category");
        if (cJSON_IsString(category_json) && (category_json->valuestring != NULL)) {
            strncpy(settings->category, category_json->valuestring, MAX_PATH_LENGTH - 1);
            settings->category[MAX_PATH_LENGTH - 1] = '\0';
        }

        const cJSON *optimized_flag_json = cJSON_GetObjectItem(json, "optional_flag");
        if (cJSON_IsString(optimized_flag_json) && (optimized_flag_json->valuestring != NULL)) {
            strncpy(settings->optional_flag, optimized_flag_json->valuestring, MAX_PATH_LENGTH - 1);
            settings->optional_flag[MAX_PATH_LENGTH - 1] = '\0';
        }

        cJSON_Delete(json); // Free the cJSON object
        json = NULL;
    }

    // Construct the final advancement paths from the settings
    construct_advancement_paths(settings);

    printf("[SETTINGS UTILS] Settings loaded successfully!\n");
}

void construct_advancement_paths(AppSettings *settings) {
    if (!settings) return;

    char base_path[MAX_PATH_LENGTH];
    snprintf(base_path, sizeof(base_path), "resources/templates/%s/%s/%s_%s%s",
             settings->mc_version_dir,
             settings->category,
             settings->mc_version_filename,
             settings->category,
             settings->optional_flag);

    // Construct the main template path
    snprintf(settings->advancement_template_path, MAX_PATH_LENGTH, "%s.json", base_path);
    normalize_path(settings->advancement_template_path); // Normalize the path, redundant as construct_advancement_paths assures formatting

    // Construct the language file path
    snprintf(settings->advancement_lang_path, MAX_PATH_LENGTH, "%s_lang.json", base_path);
    normalize_path(settings->advancement_lang_path); // Normalize the path, redundant as construct_advancement_paths assures formatting
}