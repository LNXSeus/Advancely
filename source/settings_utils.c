//
// Created by Linus on 27.06.2025.
//

#include "settings_utils.h"
#include "file_utils.h" // has the cJSON_from_file function
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
    strncpy(settings->version_str, "1.21.6", sizeof(settings->version_str) - 1);
    settings->path_mode = PATH_MODE_AUTO;
    settings->manual_saves_path[0] = '\0';
    strncpy(settings->category, "all_advancements", sizeof(settings->category) - 1);
    strncpy(settings->optional_flag, "_optimized", sizeof(settings->optional_flag) - 1);

    // Try to load and parse the settings file
    cJSON *json = cJSON_from_file(SETTINGS_FILE_PATH); // returns json object with corrected slashes from windows paste
    if (json == NULL) {
        fprintf(stderr, "[SETTINGS UTILS] Failed to load settings file: %s. Using default settings.\n", SETTINGS_FILE_PATH);
    } else {
        const cJSON *path_mode_json = cJSON_GetObjectItem(json, "path_mode");
        if (cJSON_IsString(path_mode_json)) {
            settings->path_mode = settings_get_path_mode_from_string(path_mode_json->valuestring);
        }

        const cJSON *manual_path_json = cJSON_GetObjectItem(json, "manual_saves_path");
        if (cJSON_IsString(manual_path_json)) {
            strncpy(settings->manual_saves_path, manual_path_json->valuestring, sizeof(settings->manual_saves_path) - 1);
            settings->manual_saves_path[sizeof(settings->manual_saves_path) - 1] = '\0';
        }


        const cJSON *version_json = cJSON_GetObjectItem(json, "version");
        if (cJSON_IsString(version_json)) {
            strncpy(settings->version_str, version_json->valuestring, sizeof(settings->version_str) - 1);
            settings->version_str[sizeof(settings->version_str) - 1] = '\0';
        }

        const cJSON *category_json = cJSON_GetObjectItem(json, "category");
        if (cJSON_IsString(category_json)) {
            strncpy(settings->category, category_json->valuestring, sizeof(settings->category) - 1);
            settings->category[sizeof(settings->category) - 1] = '\0';
        }

        const cJSON *optional_flag_json = cJSON_GetObjectItem(json, "optional_flag");
        if (cJSON_IsString(optional_flag_json)) {
            strncpy(settings->optional_flag, optional_flag_json->valuestring, sizeof(settings->optional_flag) - 1);
            settings->optional_flag[sizeof(settings->optional_flag) - 1] = '\0';
        }

        cJSON_Delete(json);
        json = NULL;
    }

    construct_template_paths(settings);
    printf("[SETTINGS UTILS] Settings loaded successfully!\n");
}

void construct_template_paths(AppSettings *settings) {
    if (!settings) return;

    // Create the filename version string by replacing '.' with '_'
    char mc_version_filename[MAX_PATH_LENGTH];
    strncpy(mc_version_filename, settings->version_str, MAX_PATH_LENGTH - 1);
    for (char *p = mc_version_filename; *p; p++) {
        if (*p == '.') *p = '_';
    }

    // The directory is the same as the version string
    char *mc_version_dir = settings->version_str;

    char base_path[MAX_PATH_LENGTH];
    snprintf(base_path, sizeof(base_path), "resources/templates/%s/%s/%s_%s%s",
        mc_version_dir,
        settings->category,
        mc_version_filename,
        settings->category,
        settings->optional_flag
    );

    // TODO: FIX AppSettings struct and continue with tracker.h
    // Construct the main template and language file paths
    snprintf(settings->template_path, MAX_PATH_LENGTH, "%s.json", base_path);
    snprintf(settings->lang_path, MAX_PATH_LENGTH, "%s_lang.json", base_path);
}