//
// Created by Linus on 27.06.2025.
//

#include <cstdio>
#include <cstring>

#include "settings_utils.h"
#include "logger.h"
#include "file_utils.h" // has the cJSON_from_file function
#include "global_event_handler.h"
#include "main.h"

// Define the actual constant values for the colors here in the .cpp file.
const ColorRGBA DEFAULT_TRACKER_BG_COLOR = {13, 17, 23, 255};
const ColorRGBA DEFAULT_OVERLAY_BG_COLOR = {0, 80, 255, 255};
const ColorRGBA DEFAULT_TEXT_COLOR = {255, 255, 255, 255};
const ColorRGBA DEFAULT_OVERLAY_TEXT_COLOR = {255, 255, 255, 255};

// Define the array of section names
const char *TRACKER_SECTION_NAMES[SECTION_COUNT] = {
    "Advancements",
    "Recipes",
    "Unlocks",
    "Statistics",
    "Custom Goals",
    "Multi-Stage Goals"
};

const char *VERSION_STRINGS[] = {
#define X(e, s) s,
    VERSION_LIST
#undef X
};
const int VERSION_STRINGS_COUNT = sizeof(VERSION_STRINGS) / sizeof(char *);

static const char *overlay_text_align_to_string(OverlayProgressTextAlignment align) {
    switch (align) {
        case OVERLAY_PROGRESS_TEXT_ALIGN_CENTER: return "center";
        case OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT: return "right";
        case OVERLAY_PROGRESS_TEXT_ALIGN_LEFT:
        default: return "left";
    }
}

static OverlayProgressTextAlignment string_to_overlay_text_align(const char *str) {
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
            return (MC_Version) i; // Cast index to enum type
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
    settings->version_str[sizeof(settings->version_str) - 1] = '\0';

    settings->path_mode = PATH_MODE_AUTO;
    settings->manual_saves_path[0] = '\0';
    strncpy(settings->category, DEFAULT_CATEGORY, sizeof(settings->category) - 1);
    settings->category[sizeof(settings->category) - 1] = '\0';

    strncpy(settings->optional_flag, DEFAULT_OPTIONAL_FLAG, sizeof(settings->optional_flag) - 1);
    settings->optional_flag[sizeof(settings->optional_flag) - 1] = '\0';

    settings->lang_flag[0] = '\0';

    // Set the default section order
    for (int i = 0; i < SECTION_COUNT; i++) {
        settings->section_order[i] = i;
    }
    settings->hotkey_count = 0;

    // New visual/general defaults
    settings->enable_overlay = DEFAULT_ENABLE_OVERLAY;
    settings->using_stats_per_world_legacy = DEFAULT_USING_STATS_PER_WORLD_LEGACY;
    settings->fps = DEFAULT_FPS;
    settings->overlay_fps = DEFAULT_OVERLAY_FPS;
    settings->tracker_always_on_top = DEFAULT_TRACKER_ALWAYS_ON_TOP;
    settings->overlay_scroll_speed = DEFAULT_OVERLAY_SCROLL_SPEED;
    settings->goal_hiding_mode = DEFAULT_GOAL_HIDING_MODE;
    settings->print_debug_status = DEFAULT_PRINT_DEBUG_STATUS;
    settings->overlay_progress_text_align = DEFAULT_OVERLAY_PROGRESS_TEXT_ALIGN;
    settings->overlay_animation_speedup = DEFAULT_OVERLAY_SPEED_UP;
    settings->overlay_row3_remove_completed = DEFAULT_OVERLAY_ROW3_REMOVE_COMPLETED;
    settings->overlay_stat_cycle_speed = DEFAULT_OVERLAY_STAT_CYCLE_SPEED;
    settings->notes_use_roboto_font = DEFAULT_NOTES_USE_ROBOTO;
    settings->per_world_notes = DEFAULT_PER_WORLD_NOTES;
    settings->check_for_updates = DEFAULT_CHECK_FOR_UPDATES;
    settings->show_welcome_on_startup = DEFAULT_SHOW_WELCOME_ON_STARTUP;

    strncpy(settings->tracker_font_name, DEFAULT_TRACKER_FONT, sizeof(settings->tracker_font_name) - 1);
    settings->tracker_font_name[sizeof(settings->tracker_font_name) - 1] = '\0';

    settings->tracker_font_size = DEFAULT_TRACKER_FONT_SIZE;
    strncpy(settings->ui_font_name, DEFAULT_UI_FONT, sizeof(settings->ui_font_name) - 1);
    settings->ui_font_name[sizeof(settings->ui_font_name) - 1] = '\0';

    settings->ui_font_size = DEFAULT_UI_FONT_SIZE;
    strncpy(settings->overlay_font_name, DEFAULT_OVERLAY_FONT, sizeof(settings->overlay_font_name) - 1);
    settings->overlay_font_name[sizeof(settings->overlay_font_name) - 1] = '\0';

    // Default Geometry
    WindowRect default_window = {DEFAULT_WINDOW_POS, DEFAULT_WINDOW_POS, DEFAULT_WINDOW_SIZE, DEFAULT_WINDOW_SIZE};
    settings->tracker_window = default_window;
    settings->overlay_window = default_window;

    // Default colors
    settings->tracker_bg_color = DEFAULT_TRACKER_BG_COLOR;
    settings->overlay_bg_color = DEFAULT_OVERLAY_BG_COLOR;
    settings->text_color = DEFAULT_TEXT_COLOR;
    settings->overlay_text_color = DEFAULT_OVERLAY_TEXT_COLOR;

    // Default Overlay Text Toggles
    settings->overlay_show_world = true;
    settings->overlay_show_run_details = true;
    settings->overlay_show_progress = true;
    settings->overlay_show_igt = true;
    settings->overlay_show_update_timer = true;
}

bool settings_load(AppSettings *settings) {
    bool defaults_were_used = false;
    // Flag to signal re-save when default values need to be written back to settings.json

    // Set safe defaults first by calling settings_set_defaults
    settings_set_defaults(settings);

    // Try to load and parse the settings file, read with escaping
    cJSON *json = cJSON_from_file(get_settings_file_path());
    if (json == nullptr) {
        log_message(LOG_ERROR, "[SETTINGS UTILS] Failed to load or parse settings file: %s. Using default settings.\n",
                    get_settings_file_path());

        // Show pop-up error message
        show_error_message("Settings Corrupted",
                           "Could not read settings.json. The file may be corrupted or missing.\n Restart Advancely then your settings have been reset to their defaults.");
        defaults_were_used = true; // The whole file is missing, so it needs to be created.
    }

    // Load settings, explicitly applying defaults if a key is missing or invalid
    const cJSON *path_mode_json = cJSON_GetObjectItem(json, "path_mode");
    if (path_mode_json && cJSON_IsString(path_mode_json))
        settings->path_mode = settings_get_path_mode_from_string(
            path_mode_json->valuestring);
    else {
        settings->path_mode = PATH_MODE_AUTO;
        defaults_were_used = true;
    }

    const cJSON *manual_path_json = cJSON_GetObjectItem(json, "manual_saves_path");
    if (manual_path_json && cJSON_IsString(manual_path_json)) {
        strncpy(settings->manual_saves_path, manual_path_json->valuestring,
                sizeof(settings->manual_saves_path) - 1);
        settings->manual_saves_path[sizeof(settings->manual_saves_path) - 1] = '\0';
    } else {
        settings->manual_saves_path[0] = '\0';
        defaults_were_used = true;
    }

    const cJSON *version_json = cJSON_GetObjectItem(json, "version");
    if (version_json && cJSON_IsString(version_json)) {
        strncpy(settings->version_str, version_json->valuestring,
                sizeof(settings->version_str) - 1);
        settings->version_str[sizeof(settings->version_str) - 1] = '\0';
    } else {
        strncpy(settings->version_str, "1.16.1", sizeof(settings->version_str) - 1);
        settings->version_str[sizeof(settings->version_str) - 1] = '\0';
        defaults_were_used = true;
    }

    const cJSON *category_json = cJSON_GetObjectItem(json, "category");
    if (category_json && cJSON_IsString(category_json)) {
        strncpy(settings->category, category_json->valuestring,
                sizeof(settings->category) - 1);
        settings->category[sizeof(settings->category) - 1] = '\0';
    } else {
        strncpy(settings->category, "all_advancements", sizeof(settings->category) - 1);
        settings->category[sizeof(settings->category) - 1] = '\0';
        defaults_were_used = true;
    }

    const cJSON *optional_flag_json = cJSON_GetObjectItem(json, "optional_flag");
    if (optional_flag_json && cJSON_IsString(optional_flag_json)) {
        strncpy(settings->optional_flag, optional_flag_json->valuestring,
                sizeof(settings->optional_flag) - 1);
        settings->optional_flag[sizeof(settings->optional_flag) - 1] = '\0';
    } else {
        settings->optional_flag[0] = '\0';
        defaults_were_used = true;
    }

    const cJSON *lang_flag_json = cJSON_GetObjectItem(json, "lang_flag");
    if (lang_flag_json && cJSON_IsString(lang_flag_json)) {
        strncpy(settings->lang_flag, lang_flag_json->valuestring, sizeof(settings->lang_flag) - 1);
        settings->lang_flag[sizeof(settings->lang_flag) - 1] = '\0';
    } else {
        settings->lang_flag[0] = '\0';
        defaults_were_used = true;
    }

    // Load general settings, explicitly applying defaults if a key is missing or invalid
    cJSON *general_settings = cJSON_GetObjectItem(json, "general");
    if (general_settings) {
        // Load Section Order
        const cJSON *order_json = cJSON_GetObjectItem(general_settings, "section_order");
        if (cJSON_IsArray(order_json) && cJSON_GetArraySize(order_json) == SECTION_COUNT) {
            bool found[SECTION_COUNT] = {false};
            bool valid = true;
            for (int i = 0; i < SECTION_COUNT; ++i) {
                cJSON *item = cJSON_GetArrayItem(order_json, i);
                if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint < SECTION_COUNT) {
                    int val = item->valueint;
                    if (!found[val]) {
                        settings->section_order[i] = val;
                        found[val] = true;
                    } else {
                        valid = false;
                        break;
                    }
                } else {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                // If array is malformed, revert to default
                for (int i = 0; i < SECTION_COUNT; ++i) settings->section_order[i] = i;
                defaults_were_used = true;
            }
        } else {
            // If key is missing or wrong size, use default
            for (int i = 0; i < SECTION_COUNT; ++i) settings->section_order[i] = i;
            defaults_were_used = true;
        }
        // Toggling the overlay window
        const cJSON *enable_overlay = cJSON_GetObjectItem(general_settings, "enable_overlay");
        if (enable_overlay && cJSON_IsBool(enable_overlay)) settings->enable_overlay = cJSON_IsTrue(enable_overlay);
        else {
            settings->enable_overlay = DEFAULT_ENABLE_OVERLAY; // Default value, is false
            defaults_were_used = true;
        }

        const cJSON *stats_per_world_legacy = cJSON_GetObjectItem(general_settings, "using_stats_per_world_legacy");
        if (stats_per_world_legacy && cJSON_IsBool(stats_per_world_legacy))
            settings->using_stats_per_world_legacy = cJSON_IsTrue(stats_per_world_legacy);
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
        const cJSON *overlay_fps = cJSON_GetObjectItem(general_settings, "overlay_fps");
        if (overlay_fps && cJSON_IsNumber(overlay_fps) && overlay_fps->valueint != -1)
            settings->overlay_fps = (float) overlay_fps->valuedouble;
        else {
            settings->overlay_fps = DEFAULT_OVERLAY_FPS;
            defaults_were_used = true;
        }
        const cJSON *on_top = cJSON_GetObjectItem(general_settings, "always_on_top");
        if (on_top && cJSON_IsBool(on_top)) settings->tracker_always_on_top = cJSON_IsTrue(on_top);
        else {
            settings->tracker_always_on_top = DEFAULT_TRACKER_ALWAYS_ON_TOP;
            defaults_were_used = true;
        }

        const cJSON *scroll_speed = cJSON_GetObjectItem(general_settings, "overlay_scroll_speed");
        if (scroll_speed && cJSON_IsNumber(scroll_speed)) // Scroll speed CAN be negative (right to left)
            settings->overlay_scroll_speed = (float) scroll_speed->valuedouble;
        else {
            settings->overlay_scroll_speed = DEFAULT_OVERLAY_SCROLL_SPEED;
            defaults_were_used = true;
        }

        const cJSON *hiding_mode = cJSON_GetObjectItem(general_settings, "goal_hiding_mode");
        if (hiding_mode && cJSON_IsNumber(hiding_mode)) {
            settings->goal_hiding_mode = (GoalHidingMode) hiding_mode->valueint;
        } else {
            // Backwards compatibility: Check for the old boolean key
            const cJSON *remove_completed = cJSON_GetObjectItem(general_settings, "remove_completed_goals");
            if (remove_completed && cJSON_IsBool(remove_completed)) {
                settings->goal_hiding_mode = cJSON_IsTrue(remove_completed) ? HIDE_ALL_COMPLETED : SHOW_ALL;
            } else {
                settings->goal_hiding_mode = HIDE_ALL_COMPLETED; // Default value
            }
            defaults_were_used = true;
        }

        const cJSON *print_debug = cJSON_GetObjectItem(general_settings, "print_debug_status");
        if (print_debug && cJSON_IsBool(print_debug)) settings->print_debug_status = cJSON_IsTrue(print_debug);
        else {
            settings->print_debug_status = DEFAULT_PRINT_DEBUG_STATUS;
            defaults_were_used = true;
        }

        const cJSON *align_text = cJSON_GetObjectItem(general_settings, "overlay_progress_text_align");
        if (align_text && cJSON_IsString(align_text))
            settings->overlay_progress_text_align = string_to_overlay_text_align(align_text->valuestring);
        else {
            settings->overlay_progress_text_align = DEFAULT_OVERLAY_PROGRESS_TEXT_ALIGN;
            defaults_were_used = true;
        }

        const cJSON *overlay_speedup = cJSON_GetObjectItem(general_settings, "overlay_animation_speedup");
        if (overlay_speedup && cJSON_IsBool(overlay_speedup))
            settings->overlay_animation_speedup = cJSON_IsTrue(overlay_speedup);
        else {
            settings->overlay_animation_speedup = DEFAULT_OVERLAY_SPEED_UP;
            defaults_were_used = true;
        }

        const cJSON *row3_hide = cJSON_GetObjectItem(general_settings, "overlay_row3_remove_completed");
        if (row3_hide && cJSON_IsBool(row3_hide)) settings->overlay_row3_remove_completed = cJSON_IsTrue(row3_hide);
        else {
            settings->overlay_row3_remove_completed = DEFAULT_OVERLAY_ROW3_REMOVE_COMPLETED;
            defaults_were_used = true;
        }

        const cJSON *cycle_speed = cJSON_GetObjectItem(general_settings, "overlay_stat_cycle_speed");
        if (cycle_speed && cJSON_IsNumber(cycle_speed))
            settings->overlay_stat_cycle_speed = (float) cycle_speed->valuedouble;
        else {
            settings->overlay_stat_cycle_speed = DEFAULT_OVERLAY_STAT_CYCLE_SPEED;
            defaults_were_used = true;
        }

        const cJSON *notes_font = cJSON_GetObjectItem(general_settings, "notes_use_roboto_font");
        if (notes_font && cJSON_IsBool(notes_font)) settings->notes_use_roboto_font = cJSON_IsTrue(notes_font);
        else {
            settings->notes_use_roboto_font = DEFAULT_NOTES_USE_ROBOTO;
            defaults_were_used = true;
        }

        const cJSON *per_world = cJSON_GetObjectItem(general_settings, "per_world_notes");
        if (per_world && cJSON_IsBool(per_world)) settings->per_world_notes = cJSON_IsTrue(per_world);
        else {
            settings->per_world_notes = true; // Default to true
            defaults_were_used = true;
        }

        const cJSON *check_updates = cJSON_GetObjectItem(general_settings, "check_for_updates");
        if (check_updates && cJSON_IsBool(check_updates)) settings->check_for_updates = cJSON_IsTrue(check_updates);
        else {
            settings->check_for_updates = DEFAULT_CHECK_FOR_UPDATES;
            defaults_were_used = true;
        }

        const cJSON *show_welcome = cJSON_GetObjectItem(general_settings, "show_welcome_on_startup");
        if (show_welcome && cJSON_IsBool(show_welcome)) settings->show_welcome_on_startup = cJSON_IsTrue(show_welcome);
        else {
            settings->show_welcome_on_startup = DEFAULT_SHOW_WELCOME_ON_STARTUP;
            defaults_were_used = true;
        }

        // --- Load Overlay Text Toggles ---
        const cJSON *show_world = cJSON_GetObjectItem(general_settings, "overlay_show_world");
        if (show_world && cJSON_IsBool(show_world)) settings->overlay_show_world = cJSON_IsTrue(show_world);
        else {
            settings->overlay_show_world = true;
            defaults_were_used = true;
        }

        const cJSON *show_run = cJSON_GetObjectItem(general_settings, "overlay_show_run_details");
        if (show_run && cJSON_IsBool(show_run)) settings->overlay_show_run_details = cJSON_IsTrue(show_run);
        else {
            settings->overlay_show_run_details = true;
            defaults_were_used = true;
        }

        const cJSON *show_prog = cJSON_GetObjectItem(general_settings, "overlay_show_progress");
        if (show_prog && cJSON_IsBool(show_prog)) settings->overlay_show_progress = cJSON_IsTrue(show_prog);
        else {
            settings->overlay_show_progress = true;
            defaults_were_used = true;
        }

        const cJSON *show_igt = cJSON_GetObjectItem(general_settings, "overlay_show_igt");
        if (show_igt && cJSON_IsBool(show_igt)) settings->overlay_show_igt = cJSON_IsTrue(show_igt);
        else {
            settings->overlay_show_igt = true;
            defaults_were_used = true;
        }

        const cJSON *show_update = cJSON_GetObjectItem(general_settings, "overlay_show_update_timer");
        if (show_update && cJSON_IsBool(show_update)) settings->overlay_show_update_timer = cJSON_IsTrue(show_update);
        else {
            settings->overlay_show_update_timer = true;
            defaults_were_used = true;
        }

        const cJSON *tracker_font = cJSON_GetObjectItem(general_settings, "tracker_font_name");
        if (tracker_font && cJSON_IsString(tracker_font)) {
            strncpy(settings->tracker_font_name, tracker_font->valuestring, sizeof(settings->tracker_font_name) - 1);
            settings->tracker_font_name[sizeof(settings->tracker_font_name) - 1] = '\0';
        } else {
            strncpy(settings->tracker_font_name, DEFAULT_TRACKER_FONT, sizeof(settings->tracker_font_name) - 1);
            settings->tracker_font_name[sizeof(settings->tracker_font_name) - 1] = '\0';
            defaults_were_used = true;
        }

        const cJSON *tracker_font_size = cJSON_GetObjectItem(general_settings, "tracker_font_size");
        if (tracker_font_size && cJSON_IsNumber(tracker_font_size))
            settings->tracker_font_size = (float) tracker_font_size->valuedouble;
        else {
            settings->tracker_font_size = DEFAULT_TRACKER_FONT_SIZE;
            defaults_were_used = true;
        }

        const cJSON *overlay_font = cJSON_GetObjectItem(general_settings, "overlay_font_name");
        if (overlay_font && cJSON_IsString(overlay_font)) {
            strncpy(settings->overlay_font_name, overlay_font->valuestring, sizeof(settings->overlay_font_name) - 1);
            settings->overlay_font_name[sizeof(settings->overlay_font_name) - 1] = '\0';
        } else {
            strncpy(settings->overlay_font_name, DEFAULT_OVERLAY_FONT, sizeof(settings->overlay_font_name) - 1);
            settings->overlay_font_name[sizeof(settings->overlay_font_name) - 1] = '\0';
            defaults_were_used = true;
        }

        const cJSON *ui_font = cJSON_GetObjectItem(general_settings, "ui_font_name");
        if (ui_font && cJSON_IsString(ui_font)) {
            strncpy(settings->ui_font_name, ui_font->valuestring, sizeof(settings->ui_font_name) - 1);
            settings->ui_font_name[sizeof(settings->ui_font_name) - 1] = '\0';
        } else {
            strncpy(settings->ui_font_name, DEFAULT_UI_FONT, sizeof(settings->ui_font_name) - 1);
            settings->ui_font_name[sizeof(settings->ui_font_name) - 1] = '\0';
            defaults_were_used = true;
        }

        const cJSON *ui_font_size = cJSON_GetObjectItem(general_settings, "ui_font_size");
        if (ui_font_size && cJSON_IsNumber(ui_font_size)) settings->ui_font_size = (float) ui_font_size->valuedouble;
        else {
            settings->ui_font_size = DEFAULT_UI_FONT_SIZE;
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
        if (load_color(visual_settings, "overlay_text_color", &settings->overlay_text_color,
                       &DEFAULT_OVERLAY_TEXT_COLOR))
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
                hb->target_goal[sizeof(hb->target_goal) - 1] = '\0';
                strncpy(hb->increment_key, inc_key->valuestring, sizeof(hb->increment_key) - 1);
                hb->increment_key[sizeof(hb->increment_key) - 1] = '\0';
                strncpy(hb->decrement_key, dec_key->valuestring, sizeof(hb->decrement_key) - 1);
                hb->decrement_key[sizeof(hb->decrement_key) - 1] = '\0';
                settings->hotkey_count++;
            }
        }
    }

    cJSON_Delete(json);
    construct_template_paths(settings);
    log_message(LOG_INFO, "[SETTINGS UTILS] Settings loaded successfully!\n");

    return defaults_were_used;
}

void settings_save(const AppSettings *settings, const TemplateData *td, SettingsSaveContext context) {
    if (!settings) return;

    // Read the existing file, or create a new JSON object if it doesn't exist
    cJSON *root = cJSON_from_file(get_settings_file_path());
    if (!root) {
        root = cJSON_CreateObject();
    }

    // General settings, paths, and progress are only saved with "ALL" context
    if (context == SAVE_CONTEXT_ALL) {
        // Update top-level settings using a safe "delete then add" pattern
        cJSON_DeleteItemFromObject(root, "path_mode");
        cJSON_AddItemToObject(root, "path_mode",
                              cJSON_CreateString(settings->path_mode == PATH_MODE_MANUAL ? "manual" : "auto"));
        cJSON_DeleteItemFromObject(root, "manual_saves_path");
        cJSON_AddItemToObject(root, "manual_saves_path", cJSON_CreateString(settings->manual_saves_path));
        cJSON_DeleteItemFromObject(root, "version");
        cJSON_AddItemToObject(root, "version", cJSON_CreateString(settings->version_str));
        cJSON_DeleteItemFromObject(root, "category");
        cJSON_AddItemToObject(root, "category", cJSON_CreateString(settings->category));
        cJSON_DeleteItemFromObject(root, "optional_flag");
        cJSON_AddItemToObject(root, "optional_flag", cJSON_CreateString(settings->optional_flag));
        cJSON_DeleteItemFromObject(root, "lang_flag");
        cJSON_AddItemToObject(root, "lang_flag", cJSON_CreateString(settings->lang_flag));

        // Update General Settings
        cJSON *general_obj = get_or_create_object(root, "general");

        // Update Font Settings
        cJSON_DeleteItemFromObject(general_obj, "tracker_font_name");
        cJSON_AddItemToObject(general_obj, "tracker_font_name", cJSON_CreateString(settings->tracker_font_name));
        cJSON_DeleteItemFromObject(general_obj, "tracker_font_size");
        cJSON_AddItemToObject(general_obj, "tracker_font_size", cJSON_CreateNumber(settings->tracker_font_size));
        cJSON_DeleteItemFromObject(general_obj, "overlay_font_name");
        cJSON_AddItemToObject(general_obj, "overlay_font_name", cJSON_CreateString(settings->overlay_font_name));
        cJSON_DeleteItemFromObject(general_obj, "ui_font_name");
        cJSON_AddItemToObject(general_obj, "ui_font_name", cJSON_CreateString(settings->ui_font_name));
        cJSON_DeleteItemFromObject(general_obj, "ui_font_size");
        cJSON_AddItemToObject(general_obj, "ui_font_size", cJSON_CreateNumber(settings->ui_font_size));

        // Add section order to general settings
        cJSON_DeleteItemFromObject(general_obj, "section_order");
        cJSON *order_array = cJSON_CreateIntArray(settings->section_order, SECTION_COUNT);
        cJSON_AddItemToObject(general_obj, "section_order", order_array);

        cJSON_DeleteItemFromObject(general_obj, "using_stats_per_world_legacy");
        cJSON_AddItemToObject(general_obj, "using_stats_per_world_legacy",
                              cJSON_CreateBool(settings->using_stats_per_world_legacy));
        cJSON_DeleteItemFromObject(general_obj, "fps");
        cJSON_AddItemToObject(general_obj, "fps", cJSON_CreateNumber(settings->fps));
        cJSON_DeleteItemFromObject(general_obj, "overlay_fps");
        cJSON_AddItemToObject(general_obj, "overlay_fps", cJSON_CreateNumber(settings->overlay_fps));
        cJSON_DeleteItemFromObject(general_obj, "always_on_top");
        cJSON_AddItemToObject(general_obj, "always_on_top", cJSON_CreateBool(settings->tracker_always_on_top));
        // Save the new goal hiding mode as a number
        cJSON_DeleteItemFromObject(general_obj, "goal_hiding_mode");
        cJSON_AddItemToObject(general_obj, "goal_hiding_mode", cJSON_CreateNumber(settings->goal_hiding_mode));
        // Remove the old key to keep the settings file clean
        cJSON_DeleteItemFromObject(general_obj, "remove_completed_goals");
        cJSON_DeleteItemFromObject(general_obj, "print_debug_status");
        cJSON_AddItemToObject(general_obj, "print_debug_status", cJSON_CreateBool(settings->print_debug_status));

        // Update Overlay/Notes Settings
        cJSON_DeleteItemFromObject(general_obj, "enable_overlay");
        cJSON_AddItemToObject(general_obj, "enable_overlay", cJSON_CreateBool(settings->enable_overlay));
        cJSON_DeleteItemFromObject(general_obj, "overlay_scroll_speed");
        cJSON_AddItemToObject(general_obj, "overlay_scroll_speed", cJSON_CreateNumber(settings->overlay_scroll_speed));
        cJSON_DeleteItemFromObject(general_obj, "overlay_progress_text_align");
        cJSON_AddItemToObject(general_obj, "overlay_progress_text_align",
                              cJSON_CreateString(overlay_text_align_to_string(settings->overlay_progress_text_align)));
        cJSON_DeleteItemFromObject(general_obj, "overlay_animation_speedup");
        cJSON_AddItemToObject(general_obj, "overlay_animation_speedup",
                              cJSON_CreateBool(settings->overlay_animation_speedup));
        cJSON_DeleteItemFromObject(general_obj, "overlay_row3_remove_completed");
        cJSON_AddItemToObject(general_obj, "overlay_row3_remove_completed",
                              cJSON_CreateBool(settings->overlay_row3_remove_completed));
        cJSON_DeleteItemFromObject(general_obj, "overlay_stat_cycle_speed");
        cJSON_AddItemToObject(general_obj, "overlay_stat_cycle_speed",
                              cJSON_CreateNumber(settings->overlay_stat_cycle_speed));
        cJSON_DeleteItemFromObject(general_obj, "notes_use_roboto_font");
        cJSON_AddItemToObject(general_obj, "notes_use_roboto_font", cJSON_CreateBool(settings->notes_use_roboto_font));
        cJSON_DeleteItemFromObject(general_obj, "per_world_notes");
        cJSON_AddItemToObject(general_obj, "per_world_notes", cJSON_CreateBool(settings->per_world_notes));
        cJSON_DeleteItemFromObject(general_obj, "check_for_updates");
        cJSON_AddItemToObject(general_obj, "check_for_updates", cJSON_CreateBool(settings->check_for_updates));
        cJSON_DeleteItemFromObject(general_obj, "show_welcome_on_startup");
        cJSON_AddItemToObject(general_obj, "show_welcome_on_startup",
                              cJSON_CreateBool(settings->show_welcome_on_startup));


        // --- Save Overlay Text Toggles ---
        cJSON_DeleteItemFromObject(general_obj, "overlay_show_world");
        cJSON_AddItemToObject(general_obj, "overlay_show_world", cJSON_CreateBool(settings->overlay_show_world));
        cJSON_DeleteItemFromObject(general_obj, "overlay_show_run_details");
        cJSON_AddItemToObject(general_obj, "overlay_show_run_details",
                              cJSON_CreateBool(settings->overlay_show_run_details));
        cJSON_DeleteItemFromObject(general_obj, "overlay_show_progress");
        cJSON_AddItemToObject(general_obj, "overlay_show_progress", cJSON_CreateBool(settings->overlay_show_progress));
        cJSON_DeleteItemFromObject(general_obj, "overlay_show_igt");
        cJSON_AddItemToObject(general_obj, "overlay_show_igt", cJSON_CreateBool(settings->overlay_show_igt));
        cJSON_DeleteItemFromObject(general_obj, "overlay_show_update_timer");
        cJSON_AddItemToObject(general_obj, "overlay_show_update_timer",
                              cJSON_CreateBool(settings->overlay_show_update_timer));
    }


    // Update Visual Settings
    cJSON *visuals_obj = get_or_create_object(root, "visuals");
    if (context == SAVE_CONTEXT_ALL || context == SAVE_CONTEXT_TRACKER_GEOM) {
        save_window_rect(visuals_obj, "tracker_window", &settings->tracker_window);
    }
    if (context == SAVE_CONTEXT_ALL || context == SAVE_CONTEXT_OVERLAY_GEOM) {
        save_window_rect(visuals_obj, "overlay_window", &settings->overlay_window);
    }

    if (context == SAVE_CONTEXT_ALL) {
        save_color(visuals_obj, "tracker_bg_color", &settings->tracker_bg_color);
        save_color(visuals_obj, "overlay_bg_color", &settings->overlay_bg_color);
        save_color(visuals_obj, "text_color", &settings->text_color);
        save_color(visuals_obj, "overlay_text_color", &settings->overlay_text_color);
    }

    // Update Custom Progress if provided
    if (td) {
        cJSON *progress_obj = get_or_create_object(root, "custom_progress");
        for (int i = 0; i < td->custom_goal_count; i++) {
            TrackableItem *item = td->custom_goals[i];

            // Delete old entry before adding new one to prevent duplicates
            cJSON_DeleteItemFromObject(progress_obj, item->root_name);

            // 3-way logic for custom progress, -1, greater than 0, or 0 and not set
            if (item->goal == -1) {
                // Infinite Counter: Save 'true' if done, otherwise save the number.
                if (item->done) {
                    cJSON_AddItemToObject(progress_obj, item->root_name, cJSON_CreateBool(true));
                } else {
                    cJSON_AddItemToObject(progress_obj, item->root_name, cJSON_CreateNumber(item->progress));
                }
            } else if (item->goal > 0) {
                // Normal Counter: Always save the number.
                cJSON_AddItemToObject(progress_obj, item->root_name, cJSON_CreateNumber(item->progress));
            } else {
                // Simple Toggle: Always save the boolean 'done' status.
                cJSON_AddItemToObject(progress_obj, item->root_name, cJSON_CreateBool(item->done));
            }
        }

        // Update Stat Override Progress
        cJSON *override_obj = get_or_create_object(root, "stat_progress_override");
        for (int i = 0; i < td->stat_count; i++) {
            TrackableCategory *stat_cat = td->stats[i];

            // Delete old entry before adding new one
            cJSON_DeleteItemFromObject(override_obj, stat_cat->root_name);
            // Save if we are forcing it true, OR if an override key already exists (to update it to false)
            if (stat_cat->is_manually_completed || cJSON_HasObjectItem(override_obj, stat_cat->root_name)) {
                cJSON_AddItemToObject(override_obj, stat_cat->root_name,
                                      cJSON_CreateBool(stat_cat->is_manually_completed));
            }

            for (int j = 0; j < stat_cat->criteria_count; j++) {
                TrackableItem *sub_stat = stat_cat->criteria[j];
                char sub_stat_key[512];
                snprintf(sub_stat_key, sizeof(sub_stat_key), "%s.criteria.%s", stat_cat->root_name,
                         sub_stat->root_name);

                // Delete old entry before adding new one
                cJSON_DeleteItemFromObject(override_obj, sub_stat_key);
                // Save if we are forcing it true, OR if an override key already exists (to update it to false)
                if (sub_stat->is_manually_completed || cJSON_HasObjectItem(override_obj, sub_stat_key)) {
                    cJSON_AddItemToObject(override_obj, sub_stat_key,
                                          cJSON_CreateBool(sub_stat->is_manually_completed));
                }
            }
        }
    }

    // Update Hotkeys
    cJSON_DeleteItemFromObject(root, "hotkeys");
    cJSON *hotkeys_array = cJSON_CreateArray();
    for (int i = 0; i < settings->hotkey_count; ++i) {
        const HotkeyBinding *hb = &settings->hotkeys[i];
        if (strlen(hb->target_goal) > 0) {
            // Only save if it's a valid binding
            cJSON *hotkey_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(hotkey_obj, "target_goal", hb->target_goal);
            cJSON_AddStringToObject(hotkey_obj, "increment_key", hb->increment_key);
            cJSON_AddStringToObject(hotkey_obj, "decrement_key", hb->decrement_key);
            cJSON_AddItemToArray(hotkeys_array, hotkey_obj);
        }
    }
    cJSON_AddItemToObject(root, "hotkeys", hotkeys_array);

    // Write the modified JSON object to the file
    FILE *file = fopen(get_settings_file_path(), "w");
    if (file) {
        char *json_str = cJSON_Print(root);
        if (json_str) {
            fputs(json_str, file);
            free(json_str);
            json_str = nullptr;
        }
        fclose(file);
    } else {
        log_message(LOG_ERROR, "[SETTINGS UTILS] Failed to open settings file for writing: %s\n",
                    get_settings_file_path());
    }
    cJSON_Delete(root);
}


void construct_template_paths(AppSettings *settings) {
    if (!settings) return;

    // Create the filename version string by replacing '.' with '_'
    char mc_version_filename[MAX_PATH_LENGTH];
    strncpy(mc_version_filename, settings->version_str, MAX_PATH_LENGTH - 1);
    mc_version_filename[MAX_PATH_LENGTH - 1] = '\0';
    for (char *p = mc_version_filename; *p; p++) {
        if (*p == '.') *p = '_';
    }

    // The directory is the same as the version string
    char *mc_version_dir = settings->version_str;

    char base_path[MAX_PATH_LENGTH];
    snprintf(base_path, sizeof(base_path), "%s/templates/%s/%s/%s_%s%s",
             get_resources_path(),
             mc_version_dir,
             settings->category,
             mc_version_filename,
             settings->category,
             settings->optional_flag
    );

    // Construct the language file suffix (e.g., "_eng" or "")
    char lang_suffix[70];
    if (settings->lang_flag[0] != '\0') {
        snprintf(lang_suffix, sizeof(lang_suffix), "_%s", settings->lang_flag);
    } else {
        lang_suffix[0] = '\0';
    }

    // Construct the main template, language file and (if needed) legacy snapshot paths
    snprintf(settings->template_path, MAX_PATH_LENGTH, "%s.json", base_path);
    snprintf(settings->lang_path, MAX_PATH_LENGTH, "%s_lang%s.json", base_path, lang_suffix);
    snprintf(settings->snapshot_path, MAX_PATH_LENGTH, "%s_snapshot.json", base_path);
    snprintf(settings->notes_path, MAX_PATH_LENGTH, "%s_notes.txt", base_path);
}
