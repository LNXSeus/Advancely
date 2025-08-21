//
// Created by Linus on 27.06.2025.
//

#include "settings_utils.h"
#include "file_utils.h" // has the cJSON_from_file function
#include <cstdio>
#include <cstring>

// Define the actual constant values for the colors here in the .cpp file.
const ColorRGBA DEFAULT_TRACKER_BG_COLOR = {13, 17, 23, 255};
const ColorRGBA DEFAULT_OVERLAY_BG_COLOR = {0, 80, 255, 255};
const ColorRGBA DEFAULT_TEXT_COLOR = {255, 255, 255, 255};
const ColorRGBA DEFAULT_OVERLAY_TEXT_COLOR = {255, 255, 255, 255};

const char* VERSION_STRINGS[] = {
    #define X(e, s) s,
    VERSION_LIST
    #undef X
};
const int VERSION_STRINGS_COUNT = sizeof(VERSION_STRINGS) / sizeof(char*);

static const char* overlay_text_align_to_string(OverlayProgressTextAlignment align) {
    switch (align) {
        case OVERLAY_PROGRESS_TEXT_ALIGN_CENTER: return "center";
        case OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT: return "right";
        case OVERLAY_PROGRESS_TEXT_ALIGN_LEFT:
        default: return "left";
    }
}

static OverlayProgressTextAlignment string_to_overlay_text_align(const char* str) {
    if (!str) return OVERLAY_PROGRESS_TEXT_ALIGN_LEFT;
    if (strcmp(str, "center") == 0) return OVERLAY_PROGRESS_TEXT_ALIGN_CENTER;
    if (strcmp(str, "right") == 0) return OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT;
    return OVERLAY_PROGRESS_TEXT_ALIGN_LEFT;
}

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

MC_Version settings_get_version_from_string(const char *version_str) {
    if (version_str == nullptr) return MC_VERSION_UNKNOWN;

    // Loop through the map to find a matching string
    for (int i = 0; i < VERSION_STRINGS_COUNT; i++) {
        if (strcmp(version_str, VERSION_STRINGS[i]) == 0) {
            return (MC_Version)i; // Cast index to enum type
        }
    }

    // Return if no match was found (shouldn't happen as it's a drop-down menu)
    return MC_VERSION_UNKNOWN;
}

PathMode settings_get_path_mode_from_string(const char *mode_str) {
    if (mode_str && strcmp(mode_str, "manual") == 0) {
        // returns 0 if strings are equal
        return PATH_MODE_MANUAL;
    }
    return PATH_MODE_AUTO; // Default to auto
}

// ------------------- SETTINGS UTILS -------------------

void settings_set_defaults(AppSettings *settings) {
    // TODO: Make sure to add any new default values here and to the tooltip in settings_render_gui() ---------
    // Set safe defaults first -> defined in settings_utils.h
    strncpy(settings->version_str, DEFAULT_VERSION, sizeof(settings->version_str) - 1);
    settings->path_mode = PATH_MODE_AUTO;
    settings->manual_saves_path[0] = '\0';
    strncpy(settings->category, DEFAULT_CATEGORY, sizeof(settings->category) - 1);
    strncpy(settings->optional_flag, DEFAULT_OPTIONAL_FLAG, sizeof(settings->optional_flag) - 1);

    settings->hotkey_count = 0;

    // New visual/general defaults
    settings->enable_overlay = DEFAULT_ENABLE_OVERLAY;
    settings->using_stats_per_world_legacy = DEFAULT_USING_STATS_PER_WORLD_LEGACY;
    settings->fps = DEFAULT_FPS;
    settings->tracker_always_on_top = DEFAULT_TRACKER_ALWAYS_ON_TOP;
    settings->overlay_scroll_speed = DEFAULT_OVERLAY_SCROLL_SPEED;
    settings->goal_align_left = DEFAULT_GOAL_ALIGN_LEFT;
    settings->remove_completed_goals = DEFAULT_REMOVE_COMPLETED_GOALS;
    settings->print_debug_status = DEFAULT_PRINT_DEBUG_STATUS;
    settings->overlay_progress_text_align = DEFAULT_OVERLAY_PROGRESS_TEXT_ALIGN;
    settings->overlay_animation_speedup = DEFAULT_OVERLAY_SPEED_UP;

    // Default Geometry
    WindowRect default_window = {DEFAULT_WINDOW_POS, DEFAULT_WINDOW_POS, DEFAULT_WINDOW_SIZE, DEFAULT_WINDOW_SIZE};
    settings->tracker_window = default_window;
    settings->overlay_window = default_window;

    // Default colors
    settings->tracker_bg_color = DEFAULT_TRACKER_BG_COLOR;
    settings->overlay_bg_color = DEFAULT_OVERLAY_BG_COLOR;
    settings->text_color = DEFAULT_TEXT_COLOR;
    settings->overlay_text_color = DEFAULT_OVERLAY_TEXT_COLOR;
}

bool settings_load(AppSettings *settings) {
    bool defaults_were_used = false;
    // Flag to signal re-save when default values need to be written back to settings.json

    // Set safe defaults first by calling settings_set_defaults
    settings_set_defaults(settings);

    // Try to load and parse the settings file
    cJSON *json = cJSON_from_file(SETTINGS_FILE_PATH); // returns json object with corrected slashes from windows paste
    if (json == nullptr) {
        fprintf(stderr, "[SETTINGS UTILS] Failed to load settings file: %s. Using default settings.\n",
                SETTINGS_FILE_PATH);
        defaults_were_used = true; // The whole file is missing, so it needs to be created.
    }

    // Load settings, explicitly applying defaults if a key is missing or invalid
    const cJSON *path_mode_json = cJSON_GetObjectItem(json, "path_mode");
    if (path_mode_json && cJSON_IsString(path_mode_json)) settings->path_mode = settings_get_path_mode_from_string(
                                            path_mode_json->valuestring);
    else {
        settings->path_mode = PATH_MODE_AUTO;
        defaults_were_used = true;
    }

    const cJSON *manual_path_json = cJSON_GetObjectItem(json, "manual_saves_path");
    if (manual_path_json && cJSON_IsString(manual_path_json)) strncpy(settings->manual_saves_path, manual_path_json->valuestring,
                                                  sizeof(settings->manual_saves_path) - 1);
    else {
        settings->manual_saves_path[0] = '\0';
        defaults_were_used = true;
    }

    const cJSON *version_json = cJSON_GetObjectItem(json, "version");
    if (version_json && cJSON_IsString(version_json)) strncpy(settings->version_str, version_json->valuestring,
                                              sizeof(settings->version_str) - 1);
    else {
        strncpy(settings->version_str, "1.21.6", sizeof(settings->version_str) - 1);
        defaults_were_used = true;
    }

    const cJSON *category_json = cJSON_GetObjectItem(json, "category");
    if (category_json && cJSON_IsString(category_json)) strncpy(settings->category, category_json->valuestring,
                                               sizeof(settings->category) - 1);
    else {
        strncpy(settings->category, "all_advancements", sizeof(settings->category) - 1);
        defaults_were_used = true;
    }

    const cJSON *optional_flag_json = cJSON_GetObjectItem(json, "optional_flag");
    if (optional_flag_json && cJSON_IsString(optional_flag_json)) strncpy(settings->optional_flag, optional_flag_json->valuestring,
                                                    sizeof(settings->optional_flag) - 1);
    else {
        settings->optional_flag[0] = '\0';
        defaults_were_used = true;
    }

    // Load general settings, explicitly applying defaults if a key is missing or invalid
    cJSON *general_settings = cJSON_GetObjectItem(json, "general");
    if (general_settings) {

        // Toggling the overlay window
        const cJSON *enable_overlay = cJSON_GetObjectItem(general_settings, "enable_overlay");
        if (enable_overlay && cJSON_IsBool(enable_overlay)) settings->enable_overlay = cJSON_IsTrue(enable_overlay);
        else {
            settings->enable_overlay = DEFAULT_ENABLE_OVERLAY; // Default value, is false
            defaults_were_used = true;
        }

        const cJSON *stats_per_world_legacy = cJSON_GetObjectItem(general_settings, "using_stats_per_world_legacy");
        if (stats_per_world_legacy && cJSON_IsBool(stats_per_world_legacy)) settings->using_stats_per_world_legacy = cJSON_IsTrue(stats_per_world_legacy);
        else {
            settings->using_stats_per_world_legacy = DEFAULT_USING_STATS_PER_WORLD_LEGACY;
            defaults_were_used = true;
        }

        const cJSON *fps = cJSON_GetObjectItem(general_settings, "fps");
        if (fps && cJSON_IsNumber(fps) && fps->valueint != -1) settings->fps = fps->valueint;
        else {
            settings->fps = DEFAULT_FPS;
            defaults_were_used = true;
        }
        const cJSON *on_top = cJSON_GetObjectItem(general_settings, "always_on_top");
        if (on_top && cJSON_IsBool(on_top)) settings->tracker_always_on_top = cJSON_IsTrue(on_top);
        else {
            settings->tracker_always_on_top = DEFAULT_TRACKER_ALWAYS_ON_TOP;
            defaults_were_used = true;
        }

        const cJSON *scroll_speed = cJSON_GetObjectItem(general_settings, "overlay_scroll_speed");
        if (scroll_speed && cJSON_IsNumber(scroll_speed) && scroll_speed->valuedouble != -1.0)
            settings->overlay_scroll_speed = (float) scroll_speed->valuedouble;
        else {
            settings->overlay_scroll_speed = DEFAULT_OVERLAY_SCROLL_SPEED;
            defaults_were_used = true;
        }

        const cJSON *align = cJSON_GetObjectItem(general_settings, "goal_align_left");
        if (align && cJSON_IsBool(align)) settings->goal_align_left = cJSON_IsTrue(align);
        else {
            settings->goal_align_left = DEFAULT_GOAL_ALIGN_LEFT;
            defaults_were_used = true;
        }

        const cJSON *remove_completed = cJSON_GetObjectItem(general_settings, "remove_completed_goals");
        if (remove_completed && cJSON_IsBool(remove_completed)) settings->remove_completed_goals = cJSON_IsTrue(remove_completed);
        else {
            settings->remove_completed_goals = DEFAULT_REMOVE_COMPLETED_GOALS;
            defaults_were_used = true;
        }

        const cJSON *print_debug = cJSON_GetObjectItem(general_settings, "print_debug_status");
        if (print_debug && cJSON_IsBool(print_debug)) settings->print_debug_status = cJSON_IsTrue(print_debug);
        else {
            settings->print_debug_status = DEFAULT_PRINT_DEBUG_STATUS;
            defaults_were_used = true;
        }

        const cJSON *align_text = cJSON_GetObjectItem(general_settings, "overlay_progress_text_align");
        if (align_text && cJSON_IsString(align_text)) settings->overlay_progress_text_align = string_to_overlay_text_align(align_text->valuestring);
        else {
            settings->overlay_progress_text_align = DEFAULT_OVERLAY_PROGRESS_TEXT_ALIGN;
            defaults_were_used = true;
        }

        const cJSON *overlay_speedup = cJSON_GetObjectItem(general_settings, "overlay_animation_speedup");
        if (overlay_speedup && cJSON_IsBool(overlay_speedup)) settings->overlay_animation_speedup = cJSON_IsTrue(overlay_speedup);
        else {
            settings->overlay_animation_speedup = DEFAULT_OVERLAY_SPEED_UP;
            defaults_were_used = true;
        }


    } else {
        defaults_were_used = true;
    }

    cJSON *visual_settings = cJSON_GetObjectItem(json, "visuals");
    if (visual_settings) {
        WindowRect default_window = {DEFAULT_WINDOW_POS, DEFAULT_WINDOW_POS, DEFAULT_WINDOW_SIZE, DEFAULT_WINDOW_SIZE};
        if (load_window_rect(visual_settings, "tracker_window", &settings->tracker_window, &default_window))
            defaults_were_used = true;
        if (load_window_rect(visual_settings, "overlay_window", &settings->overlay_window, &default_window))
            defaults_were_used = true;
        if (load_color(visual_settings, "tracker_bg_color", &settings->tracker_bg_color, &DEFAULT_TRACKER_BG_COLOR))
            defaults_were_used = true;
        if (load_color(visual_settings, "overlay_bg_color", &settings->overlay_bg_color, &DEFAULT_OVERLAY_BG_COLOR))
            defaults_were_used = true;
        if (load_color(visual_settings, "text_color", &settings->text_color, &DEFAULT_TEXT_COLOR))
            defaults_were_used = true;
        if (load_color(visual_settings, "overlay_text_color", &settings->overlay_text_color, &DEFAULT_OVERLAY_TEXT_COLOR))
            defaults_were_used = true;
    } else {
        defaults_were_used = true;
    }

    // Parse hotkeys
    const cJSON *hotkeys_json = cJSON_GetObjectItem(json, "hotkeys");
    if (cJSON_IsArray(hotkeys_json)) {
        cJSON *hotkey_item;
        settings->hotkey_count = 0; // Reset count before loading

        cJSON_ArrayForEach(hotkey_item, hotkeys_json) {
            if (settings->hotkey_count >= MAX_HOTKEYS) break;

            const cJSON *target = cJSON_GetObjectItem(hotkey_item, "target_goal");
            const cJSON *inc_key = cJSON_GetObjectItem(hotkey_item, "increment_key");
            const cJSON *dec_key = cJSON_GetObjectItem(hotkey_item, "decrement_key");

            if (cJSON_IsString(target) && cJSON_IsString(inc_key) && cJSON_IsString(dec_key)) {
                HotkeyBinding *hb = &settings->hotkeys[settings->hotkey_count];
                strncpy(hb->target_goal, target->valuestring, sizeof(hb->target_goal) - 1);
                strncpy(hb->increment_key, inc_key->valuestring, sizeof(hb->increment_key) - 1);
                strncpy(hb->decrement_key, dec_key->valuestring, sizeof(hb->decrement_key) - 1);
                settings->hotkey_count++;
            }
        }
    }

    cJSON_Delete(json);
    construct_template_paths(settings);
    if (settings->print_debug_status) {
        printf("[SETTINGS UTILS] Settings loaded successfully!\n");
    }
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

    // Update General Settings
    cJSON *general_obj = get_or_create_object(root, "general");
    cJSON_ReplaceItemInObject(general_obj, "enable_overlay", cJSON_CreateBool(settings->enable_overlay));
    cJSON_ReplaceItemInObject(general_obj, "using_stats_per_world_legacy",
                              cJSON_CreateBool(settings->using_stats_per_world_legacy));
    cJSON_ReplaceItemInObject(general_obj, "fps", cJSON_CreateNumber(settings->fps));
    cJSON_ReplaceItemInObject(general_obj, "always_on_top", cJSON_CreateBool(settings->tracker_always_on_top));
    cJSON_ReplaceItemInObject(general_obj, "overlay_scroll_speed", cJSON_CreateNumber(settings->overlay_scroll_speed));
    cJSON_ReplaceItemInObject(general_obj, "goal_align_left", cJSON_CreateBool(settings->goal_align_left));
    cJSON_ReplaceItemInObject(general_obj, "remove_completed_goals", cJSON_CreateBool(settings->remove_completed_goals));
    cJSON_ReplaceItemInObject(general_obj, "print_debug_status", cJSON_CreateBool(settings->print_debug_status));
    cJSON_ReplaceItemInObject(general_obj, "overlay_progress_text_align",
                              cJSON_CreateString(overlay_text_align_to_string(settings->overlay_progress_text_align)));
    cJSON_ReplaceItemInObject(general_obj, "overlay_animation_speedup", cJSON_CreateBool(settings->overlay_animation_speedup));


    // Update Visual Settings
    cJSON *visuals_obj = get_or_create_object(root, "visuals");
    save_window_rect(visuals_obj, "tracker_window", &settings->tracker_window);
    save_window_rect(visuals_obj, "overlay_window", &settings->overlay_window);
    save_color(visuals_obj, "tracker_bg_color", &settings->tracker_bg_color);
    save_color(visuals_obj, "overlay_bg_color", &settings->overlay_bg_color);
    save_color(visuals_obj, "text_color", &settings->text_color);
    save_color(visuals_obj, "overlay_text_color", &settings->overlay_text_color);

    // Update Custom Progress if provided
    if (td) {
        cJSON *progress_obj = get_or_create_object(root, "custom_progress");
        for (int i = 0; i < td->custom_goal_count; i++) {
            TrackableItem *item = td->custom_goals[i];

            // 3-way logic for custom progress, -1, greater than 0, or 0 and not set
            if (item->goal == -1) {
                // Infinite Counter: Save 'true' if done, otherwise save the number.
                if (item->done) {
                    cJSON_ReplaceItemInObject(progress_obj, item->root_name, cJSON_CreateBool(true));
                } else {
                    cJSON_ReplaceItemInObject(progress_obj, item->root_name, cJSON_CreateNumber(item->progress));
                }
            } else if (item->goal > 0) {
                // Normal Counter: Always save the number.
                cJSON_ReplaceItemInObject(progress_obj, item->root_name, cJSON_CreateNumber(item->progress));
            } else {
                // Simple Toggle: Always save the boolean 'done' status.
                cJSON_ReplaceItemInObject(progress_obj, item->root_name, cJSON_CreateBool(item->done));
            }
        }

        // Update Stat Override Progress
        cJSON *override_obj = get_or_create_object(root, "stat_progress_override");
        for (int i = 0; i < td->stat_count; i++) {
            TrackableCategory *stat_cat = td->stats[i];

            // Save if we are forcing it true, OR if an override key already exists (to update it to false)
            if (stat_cat->is_manually_completed || cJSON_HasObjectItem(override_obj, stat_cat->root_name)) {
                cJSON_ReplaceItemInObject(override_obj, stat_cat->root_name, cJSON_CreateBool(stat_cat->is_manually_completed));
            }


            for (int j = 0; j < stat_cat->criteria_count; j++) {
                TrackableItem *sub_stat = stat_cat->criteria[j];
                char sub_stat_key[512];
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name, sub_stat->root_name);

                // Save if we are forcing it true, OR if an override key already exists (to update it to false)
                if (sub_stat->is_manually_completed || cJSON_HasObjectItem(override_obj, sub_stat_key)) {
                    cJSON_ReplaceItemInObject(override_obj, sub_stat_key, cJSON_CreateBool(sub_stat->is_manually_completed));
                }
            }
        }
    }

    // Update Hotkeys
    cJSON_DeleteItemFromObject(root, "hotkeys");
    cJSON* hotkeys_array = cJSON_CreateArray();
    for (int i = 0; i < settings->hotkey_count; ++i) {
        const HotkeyBinding* hb = &settings->hotkeys[i];
        if (strlen(hb->target_goal) > 0) { // Only save if it's a valid binding
            cJSON* hotkey_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(hotkey_obj, "target_goal", hb->target_goal);
            cJSON_AddStringToObject(hotkey_obj, "increment_key", hb->increment_key);
            cJSON_AddStringToObject(hotkey_obj, "decrement_key", hb->decrement_key);
            cJSON_AddItemToArray(hotkeys_array, hotkey_obj);
        }
    }
    cJSON_AddItemToObject(root, "hotkeys", hotkeys_array);

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

    // Construct the main template, language file and (if needed) legacy snapshot paths
    snprintf(settings->template_path, MAX_PATH_LENGTH, "%s.json", base_path);
    snprintf(settings->lang_path, MAX_PATH_LENGTH, "%s_lang.json", base_path);
    snprintf(settings->snapshot_path, MAX_PATH_LENGTH, "%s_snapshot.json", base_path);
}