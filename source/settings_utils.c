//
// Created by Linus on 27.06.2025.
//

#include "settings_utils.h"
#include "file_utils.h" // has the cJSON_from_file function
#include <stdio.h>
#include <string.h>

// Helper Prototypes for Loading/Saving
static cJSON *get_or_create_object(cJSON *parent, const char *key) {
    cJSON *obj = cJSON_GetObjectItem(parent, key);
    if (!obj) {
        obj = cJSON_AddObjectToObject(parent, key);
    }
    return obj;
}

/**
 * @brief Loads a window rect from a cJSON object and uses defaults if rect struct is empty: e.g., WindowRect rect = {}
 * @param parent Parent cJSON object
 * @param key Key of the rect
 * @param rect Rect struct
 * @param default_rect Default rect
 * @return True when default values were used, false otherwise
 */
static bool load_window_rect(cJSON *parent, const char *key, WindowRect *rect, const WindowRect *default_rect) {
    bool default_used = false;
    cJSON *obj = cJSON_GetObjectItem(parent, key);
    if (!obj) return true; // Object itself is missing so using defaults

    // Use default values if -1 and return true when default values were used
    const cJSON *x = cJSON_GetObjectItem(obj, "x");
    if (cJSON_IsNumber(x) && x->valueint != -1) rect->x = x->valueint;
    else {
        rect->x = default_rect->x;
        default_used = true;
    }

    const cJSON *y = cJSON_GetObjectItem(obj, "y");
    if (cJSON_IsNumber(y) && y->valueint != -1) rect->y = y->valueint;
    else {
        rect->y = default_rect->y;
        default_used = true;
    }

    const cJSON *w = cJSON_GetObjectItem(obj, "w");
    if (cJSON_IsNumber(w) && w->valueint != -1) rect->w = w->valueint;
    else {
        rect->w = default_rect->w;
        default_used = true;
    }

    const cJSON *h = cJSON_GetObjectItem(obj, "h");
    if (cJSON_IsNumber(h) && h->valueint != -1) rect->h = h->valueint;
    else {
        rect->h = default_rect->h;
        default_used = true;
    }

    return default_used;
}

/**
 * @brief Loads a color from a cJSON object and uses defaults if color struct is empty: e.g., ColorRGBA color = {}
 * @param parent Parent cJSON object
 * @param key Key of the color
 * @param color Color struct
 * @param default_color Default color
 * @return True when default values were used, false otherwise
 */
static bool load_color(cJSON *parent, const char *key, ColorRGBA *color, const ColorRGBA *default_color) {
    bool default_used = false;
    cJSON *obj = cJSON_GetObjectItem(parent, key);
    if (!obj) return true; // Object itself is missing so using defaults

    // Use default values if -1 and return true when default values were used
    const cJSON *r = cJSON_GetObjectItem(obj, "r");
    if (cJSON_IsNumber(r)) color->r = (Uint8) r->valueint;
    else {
        color->r = default_color->r;
        default_used = true;
    }

    const cJSON *g = cJSON_GetObjectItem(obj, "g");
    if (cJSON_IsNumber(g)) color->g = (Uint8) g->valueint;
    else {
        color->g = default_color->g;
        default_used = true;
    }

    const cJSON *b = cJSON_GetObjectItem(obj, "b");
    if (cJSON_IsNumber(b)) color->b = (Uint8) b->valueint;
    else {
        color->b = default_color->b;
        default_used = true;
    }

    const cJSON *a = cJSON_GetObjectItem(obj, "a");
    if (cJSON_IsNumber(a)) color->a = (Uint8) a->valueint;
    else {
        color->a = default_color->a;
        default_used = true;
    }

    return default_used;
}

/**
 * @brief Save a window rect to the settings.json
 * @param parent Parent cJSON object
 * @param key Key of the rect
 * @param rect Rect struct
 */
static void save_window_rect(cJSON *parent, const char *key, const WindowRect *rect) {
    cJSON *obj = get_or_create_object(parent, key);
    cJSON_DeleteItemFromObject(obj, "x");
    cJSON_AddItemToObject(obj, "x", cJSON_CreateNumber(rect->x));

    cJSON_DeleteItemFromObject(obj, "y");
    cJSON_AddItemToObject(obj, "y", cJSON_CreateNumber(rect->y));

    cJSON_DeleteItemFromObject(obj, "w");
    cJSON_AddItemToObject(obj, "w", cJSON_CreateNumber(rect->w));

    cJSON_DeleteItemFromObject(obj, "h");
    cJSON_AddItemToObject(obj, "h", cJSON_CreateNumber(rect->h));
}

/**
 * @brief Save a color to the settings.json
 * @param parent Parent cJSON object
 * @param key Key of the color
 * @param color Color struct
 */
static void save_color(cJSON *parent, const char *key, const ColorRGBA *color) {
    cJSON *obj = get_or_create_object(parent, key);
    cJSON_DeleteItemFromObject(obj, "r");
    cJSON_AddItemToObject(obj, "r", cJSON_CreateNumber(color->r));
    cJSON_DeleteItemFromObject(obj, "g");
    cJSON_AddItemToObject(obj, "g", cJSON_CreateNumber(color->g));
    cJSON_DeleteItemFromObject(obj, "b");
    cJSON_AddItemToObject(obj, "b", cJSON_CreateNumber(color->b));
    cJSON_DeleteItemFromObject(obj, "a");
    cJSON_AddItemToObject(obj, "a", cJSON_CreateNumber(color->a));
}

// ------------------- VERSION AND PATH MODE CONVERTERS -------------------

// Create lookup table to map version strings to enum values
typedef struct {
    const char *str;
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
    if (mode_str && strcmp(mode_str, "manual") == 0) {
        // returns 0 if strings are equal
        return PATH_MODE_MANUAL;
    }
    return PATH_MODE_AUTO; // Default to auto
}

// ------------------- SETTINGS UTILS -------------------

// TODO: Remove this once possible
// cJSON *settings_read_full() {
//     return cJSON_from_file(SETTINGS_FILE_PATH);
// }
//
// void settings_write_full(cJSON *json_to_write) {
//     if (!json_to_write) return;
//
//     FILE *file = fopen(SETTINGS_FILE_PATH, "w");
//     if (file) {
//         char *json_str = cJSON_Print(json_to_write);
//         if (json_str) {
//             fputs(json_str, file);
//             free(json_str);
//         }
//         fclose(file);
//     } else {
//         fprintf(stderr, "[SETTINGS UTILS] Failed to open settings file for writing: %s\n", SETTINGS_FILE_PATH);
//     }
// }

bool settings_load(AppSettings *settings) {
    bool defaults_were_used = false;
    // Flag to signal re-save when default values need to be written back to settings.json

    // Set safe defaults first
    strncpy(settings->version_str, "1.21.6", sizeof(settings->version_str) - 1);
    settings->path_mode = PATH_MODE_AUTO;
    settings->manual_saves_path[0] = '\0';
    strncpy(settings->category, "all_advancements", sizeof(settings->category) - 1);
    strncpy(settings->optional_flag, "_optimized", sizeof(settings->optional_flag) - 1);
    // TODO: Make default empty (also as template file)
    settings->hotkey_count = 0;

    // New visual/general defaults
    settings->fps = DEFAULT_FPS;
    settings->tracker_always_on_top = DEFAULT_TRACKER_ALWAYS_ON_TOP;
    settings->overlay_scroll_speed = DEFAULT_OVERLAY_SCROLL_SPEED;
    settings->overlay_scroll_left_to_right = DEFAULT_OVERLAY_SCROLL_LEFT_TO_RIGHT;
    settings->goal_align_left = DEFAULT_GOAL_ALIGN_LEFT;

    // Default Geometry
    WindowRect default_window = {DEFAULT_WINDOW_POS, DEFAULT_WINDOW_POS, DEFAULT_WINDOW_SIZE, DEFAULT_WINDOW_SIZE};
    settings->tracker_window = default_window;
    settings->overlay_window = default_window;

    // Default colors
    settings->tracker_bg_color = DEFAULT_TRACKER_BG_COLOR;
    settings->overlay_bg_color = DEFAULT_OVERLAY_BG_COLOR;
    settings->settings_bg_color = DEFAULT_SETTINGS_BG_COLOR;
    settings->text_color = DEFAULT_TEXT_COLOR;

    // Try to load and parse the settings file
    cJSON *json = cJSON_from_file(SETTINGS_FILE_PATH); // returns json object with corrected slashes from windows paste
    if (json == NULL) {
        fprintf(stderr, "[SETTINGS UTILS] Failed to load settings file: %s. Using default settings.\n",
                SETTINGS_FILE_PATH);
        defaults_were_used = true; // The whole file is missing, so it needs to be created.
    } else {
        // Check each setting and track if a default is used.
        const cJSON *path_mode_json = cJSON_GetObjectItem(json, "path_mode");
        if (cJSON_IsString(path_mode_json)) settings->path_mode = settings_get_path_mode_from_string(
                                                path_mode_json->valuestring);
        else defaults_were_used = true;

        const cJSON *manual_path_json = cJSON_GetObjectItem(json, "manual_saves_path");
        if (cJSON_IsString(manual_path_json)) strncpy(settings->manual_saves_path, manual_path_json->valuestring,
                                                      sizeof(settings->manual_saves_path) - 1);
        else defaults_were_used = true;

        const cJSON *version_json = cJSON_GetObjectItem(json, "version");
        if (cJSON_IsString(version_json)) strncpy(settings->version_str, version_json->valuestring,
                                                  sizeof(settings->version_str) - 1);
        else defaults_were_used = true;

        const cJSON *category_json = cJSON_GetObjectItem(json, "category");
        if (cJSON_IsString(category_json)) strncpy(settings->category, category_json->valuestring,
                                                   sizeof(settings->category) - 1);
        else defaults_were_used = true;

        const cJSON *optional_flag_json = cJSON_GetObjectItem(json, "optional_flag");
        if (cJSON_IsString(optional_flag_json)) strncpy(settings->optional_flag, optional_flag_json->valuestring,
                                                        sizeof(settings->optional_flag) - 1);
        else defaults_were_used = true;

        cJSON *general_settings = cJSON_GetObjectItem(json, "general");
        if (general_settings) {
            const cJSON *fps = cJSON_GetObjectItem(general_settings, "fps");
            if (cJSON_IsNumber(fps) && fps->valueint != -1) settings->fps = fps->valueint;
            else defaults_were_used = true;

            const cJSON *on_top = cJSON_GetObjectItem(general_settings, "tracker_always_on_top");
            if (cJSON_IsBool(on_top)) settings->tracker_always_on_top = cJSON_IsTrue(on_top);
            else defaults_were_used = true;

            const cJSON *scroll_speed = cJSON_GetObjectItem(general_settings, "overlay_scroll_speed");
            if (cJSON_IsNumber(scroll_speed) && scroll_speed->valuedouble != -1.0)
                settings->overlay_scroll_speed = (float) scroll_speed->valuedouble;
            else defaults_were_used = true;

            const cJSON *scroll_dir = cJSON_GetObjectItem(general_settings, "overlay_scroll_left_to_right");
            if (cJSON_IsBool(scroll_dir)) settings->overlay_scroll_left_to_right = cJSON_IsTrue(scroll_dir);
            else defaults_were_used = true;

            const cJSON *align = cJSON_GetObjectItem(general_settings, "goal_align_left");
            if (cJSON_IsBool(align)) settings->goal_align_left = cJSON_IsTrue(align);
            else defaults_were_used = true;
        } else {
            defaults_were_used = true;
        }

        cJSON *visual_settings = cJSON_GetObjectItem(json, "visuals");
        if (visual_settings) {
            if (load_window_rect(visual_settings, "tracker_window", &settings->tracker_window, &default_window))
                defaults_were_used = true;
            if (load_window_rect(visual_settings, "overlay_window", &settings->overlay_window, &default_window))
                defaults_were_used = true;
            if (load_color(visual_settings, "tracker_bg_color", &settings->tracker_bg_color, &DEFAULT_TRACKER_BG_COLOR))
                defaults_were_used = true;
            if (load_color(visual_settings, "overlay_bg_color", &settings->overlay_bg_color, &DEFAULT_OVERLAY_BG_COLOR))
                defaults_were_used = true;
            if (load_color(visual_settings, "settings_bg_color", &settings->settings_bg_color,
                           &DEFAULT_SETTINGS_BG_COLOR)) defaults_were_used = true;
            if (load_color(visual_settings, "text_color", &settings->text_color, &DEFAULT_TEXT_COLOR))
                defaults_were_used = true;
        } else {
            defaults_were_used = true;
        }

        // Parse hotkeys
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

    // Construct derived paths
    construct_template_paths(settings);
    printf("[SETTINGS UTILS] Settings loaded successfully!\n");
    return defaults_were_used;
}

void settings_save(const AppSettings *settings, const TemplateData *td) {
    if (!settings) return;

    // Read the existing file, or create a new JSON object if it doesn't exist
    cJSON *root = cJSON_from_file(SETTINGS_FILE_PATH);
    if (!root) {
        root = cJSON_CreateObject();
    }

    // Update top-level settings
    cJSON_ReplaceItemInObject(root, "path_mode",
                              cJSON_CreateString(settings->path_mode == PATH_MODE_MANUAL ? "manual" : "auto"));
    cJSON_ReplaceItemInObject(root, "manual_saves_path", cJSON_CreateString(settings->manual_saves_path));
    cJSON_ReplaceItemInObject(root, "version", cJSON_CreateString(settings->version_str));
    cJSON_ReplaceItemInObject(root, "category", cJSON_CreateString(settings->category));
    cJSON_ReplaceItemInObject(root, "optional_flag", cJSON_CreateString(settings->optional_flag));

    // --- Update General Settings ---
    cJSON *general_obj = get_or_create_object(root, "general");
    cJSON_ReplaceItemInObject(general_obj, "fps", cJSON_CreateNumber(settings->fps));
    cJSON_ReplaceItemInObject(general_obj, "tracker_always_on_top", cJSON_CreateBool(settings->tracker_always_on_top));
    cJSON_ReplaceItemInObject(general_obj, "overlay_scroll_speed", cJSON_CreateNumber(settings->overlay_scroll_speed));
    cJSON_ReplaceItemInObject(general_obj, "overlay_scroll_left_to_right",
                              cJSON_CreateBool(settings->overlay_scroll_left_to_right));
    cJSON_ReplaceItemInObject(general_obj, "goal_align_left", cJSON_CreateBool(settings->goal_align_left));

    // --- Update Visual Settings ---
    cJSON *visuals_obj = get_or_create_object(root, "visuals");
    save_window_rect(visuals_obj, "tracker_window", &settings->tracker_window);
    save_window_rect(visuals_obj, "overlay_window", &settings->overlay_window);
    save_color(visuals_obj, "tracker_bg_color", &settings->tracker_bg_color);
    save_color(visuals_obj, "overlay_bg_color", &settings->overlay_bg_color);
    save_color(visuals_obj, "settings_bg_color", &settings->settings_bg_color);
    save_color(visuals_obj, "text_color", &settings->text_color);

    // Update Custom Progress if provided
    if (td) {
        cJSON *progress_obj = get_or_create_object(root, "custom_progress");
        for (int i = 0; i < td->custom_goal_count; i++) {
            TrackableItem *item = td->custom_goals[i];
            if (item->goal > 0) {
                cJSON_ReplaceItemInObject(progress_obj, item->root_name, cJSON_CreateNumber(item->progress));
            } else {
                cJSON_ReplaceItemInObject(progress_obj, item->root_name, cJSON_CreateBool(item->done));
            }
        }
    }

    // Write the modified JSON object to the file
    FILE *file = fopen(SETTINGS_FILE_PATH, "w");
    if (file) {
        char *json_str = cJSON_Print(root);
        if (json_str) {
            fputs(json_str, file);
            free(json_str);
        }
        fclose(file);
    } else {
        fprintf(stderr, "[SETTINGS UTILS] Failed to open settings file for writing: %s\n", SETTINGS_FILE_PATH);
    }
    cJSON_Delete(root);
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
