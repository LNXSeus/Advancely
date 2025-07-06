//
// Created by Linus on 27.06.2025.
//

#include "settings_utils.h"
#include "file_utils.h" // has the cJSON_from_file function
#include <stdio.h>
#include <string.h>

// Create lookup table to map version strings to enum values
typedef struct {
    const char* str;
    MC_Version value;
} VersionMapEntry;

// Create static lookup table with all supported versions
// TODO: Versions need to be added in MCVersion enum and here
static const VersionMapEntry version_map[] = {
  {"1.11", MC_VERSION_1_11},
    {"1.12", MC_VERSION_1_12},
    {"1.16.1", MC_VERSION_1_16_1},
    {"1.21.6", MC_VERSION_1_21_6},
    {"25w14craftmine", MC_VERSION_25W14CRAFTMINE},
    {NULL, MC_VERSION_UNKNOWN} //Sentinel to mark end of the array
};

/**
 * @brief Converts key names from JSON file (like "PageUp") into SDL scancodes that the event handler can use
 * @param key_name
 * @return SDL_Scancode or SDL_SCANCODE_UNKNOWN if not found
 */
static SDL_Scancode scancode_from_string(const char *key_name) {
    if (!key_name || key_name[0] == '\0') return SDL_SCANCODE_UNKNOWN;

    return SDL_GetScancodeFromName(key_name);
}

MC_Version settings_get_version_from_string(const char *version_str) {
    if (version_str == NULL) return MC_VERSION_UNKNOWN;

    // Loop through the map to find a matching string
    for (int i = 0; version_map[i].str != NULL; i++) {
        if (strcmp(version_str, version_map[i].str) == 0) {
            return version_map[i].value; // Return the corresponding enum value
        }
    }

    return MC_VERSION_UNKNOWN; // Return if no match was found
}

PathMode settings_get_path_mode_from_string(const char *mode_str) {
    if (mode_str && strcmp(mode_str, "manual") == 0) { // returns 0 if strings are equal
        return PATH_MODE_MANUAL;
    }
    return PATH_MODE_AUTO; // Default to auto
}

cJSON *settings_read_full() {
    return cJSON_from_file(SETTINGS_FILE_PATH);
}

void settings_write_full(cJSON *json_to_write) {
    if (!json_to_write) return;

    FILE *file = fopen(SETTINGS_FILE_PATH, "w");
    if (file) {
        char *json_str = cJSON_Print(json_to_write);
        if (json_str) {
            fputs(json_str, file);
            free(json_str);
        }
        fclose(file);
    } else {
        fprintf(stderr, "[SETTINGS UTILS] Failed to open settings file for writing: %s\n", SETTINGS_FILE_PATH);
    }
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

        // Parse hotkeys
        settings->hotkey_count = 0;
        const cJSON *hotkeys_json = cJSON_GetObjectItem(json, "hotkeys");
        if (cJSON_IsArray(hotkeys_json)) {
            cJSON *hotkey_item;

            // Parse each hotkey
            cJSON_ArrayForEach(hotkey_item, hotkeys_json) {
                if (settings->hotkey_count >= MAX_HOTKEYS) break;

                // Takes the string within the settings.json
                const cJSON *target = cJSON_GetObjectItem(hotkey_item, "target_goal");
                const cJSON *inc_key = cJSON_GetObjectItem(hotkey_item, "increment_key");
                const cJSON *dec_key = cJSON_GetObjectItem(hotkey_item, "decrement_key");

                if (cJSON_IsString(target) && cJSON_IsString(inc_key) && cJSON_IsString(dec_key)) {
                    // Create a new hotkey binding
                    HotkeyBinding *hb = &settings->hotkeys[settings->hotkey_count];
                    strncpy(hb->target_goal, target->valuestring, sizeof(hb->target_goal) - 1);

                    hb->increment_scancode = scancode_from_string(inc_key->valuestring);
                    hb->decrement_scancode = scancode_from_string(dec_key->valuestring);

                    settings->hotkey_count++;
                }
            }
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

    // Construct the main template and language file paths
    snprintf(settings->template_path, MAX_PATH_LENGTH, "%s.json", base_path);
    snprintf(settings->lang_path, MAX_PATH_LENGTH, "%s_lang.json", base_path);
}