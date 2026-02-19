// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 27.06.2025.
//

#ifndef SETTINGS_UTILS_H
#define SETTINGS_UTILS_H

#include "path_utils.h" // For MAX_PATH_LENGTH
#include "data_structures.h" // For MC_Version and PathMode enums
#include <SDL3/SDL_stdinc.h> // Add this for Uint32



#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAVE_CONTEXT_ALL, // Saving everything from "Settings Apply" butotn
    SAVE_CONTEXT_TRACKER_GEOM, // Save only tracker window's position/size
    SAVE_CONTEXT_OVERLAY_GEOM // Save only overlay window's position/size
} SettingsSaveContext;

enum GoalHidingMode {
    HIDE_ALL_COMPLETED, // Hides both "done" items and hidden within template
    HIDE_ONLY_TEMPLATE_HIDDEN, // Hides only hidden within template, but shows completed
    SHOW_ALL // Shows hidden and completed
};

// Enum to identify the tracker sections
enum TrackerSection {
    SECTION_ADVANCEMENTS,
    SECTION_RECIPES, // Modern advancements with is_recipe flag set to true in template
    SECTION_UNLOCKS, // Exclusive to 25w14craftmine
    SECTION_STATS,
    SECTION_CUSTOM,
    SECTION_MULTISTAGE,
    SECTION_COUNT // Currently 5
};

// Helper array of names for the settings UI
extern const char *TRACKER_SECTION_NAMES[SECTION_COUNT];

#include <cJSON.h>

#define MAX_WORLD_NOTES 32 // Limit for amount of per-world notes until they delete itself
#define MAX_HOTKEYS 32 // Limit for amount of hotkeys

// DEFAULT values
#define DEFAULT_ENABLE_OVERLAY false // Stream overlay will be off by default
#define DEFAULT_USING_STATS_PER_WORLD_LEGACY true
#define DEFAULT_USING_HERMES false
#define DEFAULT_PATH_MODE PATH_MODE_AUTO
#define DEFAULT_FPS 60
#define DEFAULT_OVERLAY_FPS 60
#define DEFAULT_TRACKER_ALWAYS_ON_TOP true
#define DEFAULT_OVERLAY_SCROLL_SPEED 1.0f
#define DEFAULT_GOAL_HIDING_MODE HIDE_ALL_COMPLETED
#define DEFAULT_PRINT_DEBUG_STATUS false

// Overlay Settings
#define DEFAULT_OVERLAY_PROGRESS_TEXT_ALIGN OVERLAY_PROGRESS_TEXT_ALIGN_LEFT
#define DEFAULT_OVERLAY_ROW1_SPACING 8.0f // Default spacing in pixels between row 1 icons
#define DEFAULT_OVERLAY_ROW1_SHARED_ICON_SIZE 32.0f // Default shared icon size in pixels for row 1
#define DEFAULT_OVERLAY_ROW2_CUSTOM_SPACING_ENABLED false
#define DEFAULT_OVERLAY_ROW2_CUSTOM_SPACING 192.0f // 96px icon + more (took spacing from 1.16 AA template)
#define DEFAULT_OVERLAY_ROW3_CUSTOM_SPACING_ENABLED false
#define DEFAULT_OVERLAY_ROW3_CUSTOM_SPACING 256.0f // 96px icon + more (took spacing from 1.16 AA template)
#define DEFAULT_OVERLAY_ROW3_REMOVE_COMPLETED false
#define DEFAULT_OVERLAY_STAT_CYCLE_SPEED 3.0f // Default: cycle every 3 seconds

// Tracker Section Item Width
#define DEFAULT_TRACKER_VERTICAL_SPACING 8.0f // Default vertical spacing in pixels between goals globally
#define DEFAULT_TRACKER_SECTION_CUSTOM_WIDTH_ENABLED false // Default for *each* section's checkbox
#define DEFAULT_TRACKER_SECTION_ITEM_WIDTH 128.0f // Default item width in pixels

#define DEFAULT_SCROLLABLE_LIST_THRESHOLD 16 // Items before scrolling kicks in // TODO: Figure out best value
#define DEFAULT_TRACKER_LIST_SCROLL_SPEED 36.0f // Default pixels per scroll step

#define DEFAULT_NOTES_USE_ROBOTO false // Default: use the standard Minecraft font for notes otherwise roboto
#define DEFAULT_PER_WORLD_NOTES true // When true the notes are per world, otherwise per template
#define DEFAULT_CHECK_FOR_UPDATES true
#define DEFAULT_SHOW_WELCOME_ON_STARTUP true

// DEFINE DEFAULT SETTINGS
#define DEFAULT_VERSION "1.16.1"  // Also needs to be changed in settings_load()
#define DEFAULT_CATEGORY "all_advancements" // Also needs to be changed in settings_load()
#define DEFAULT_OPTIONAL_FLAG ""  // Also needs to be changed in settings_load()
#define DEFAULT_DISPLAY_CATEGORY "All Advancements"
#define DEFAULT_LOCK_CATEGORY_DISPLAY_NAME false // Fixing Display name when changing templates

#define DEFAULT_TRACKER_FONT "Minecraft.ttf" // The overlay also uses this font
#define DEFAULT_TRACKER_FONT_SIZE 16.0f
#define DEFAULT_TRACKER_SUB_FONT_SIZE 14.0f // (DEFAULT_TRACKER_FONT_SIZE * 0.875f)
#define DEFAULT_TRACKER_UI_FONT_SIZE 16.0f
#define DEFAULT_OVERLAY_FONT "Minecraft.ttf"
#define DEFAULT_UI_FONT "Roboto-Regular.ttf"
#define DEFAULT_UI_FONT_SIZE 16.0f
#define DEFAULT_ADV_BG_PATH "advancement_background.png"
#define DEFAULT_ADV_BG_HALF_DONE_PATH "advancement_background_half_done.png"
#define DEFAULT_ADV_BG_DONE_PATH "advancement_background_done.png"

#define DEFAULT_OVERLAY_FONT_SIZE 24.0f // This is fixed currently

// LOD Defaults
#define DEFAULT_LOD_TEXT_SUB_THRESHOLD 0.60f
#define DEFAULT_LOD_TEXT_MAIN_THRESHOLD 0.40f
#define DEFAULT_LOD_ICON_DETAIL_THRESHOLD 0.25f

// TrackerMap Defaults
#define DEFAULT_TRACKER_VIEW_PAN_X 0.0f
#define DEFAULT_TRACKER_VIEW_PAN_Y 0.0f
#define DEFAULT_TRACKER_VIEW_ZOOM 1.0f
#define DEFAULT_TRACKER_VIEW_LOCKED false
#define DEFAULT_TRACKER_VIEW_LOCKED_WIDTH 0.0f

// Default window positions/sizes. -1 means centered or default size.
#define DEFAULT_WINDOW_POS (-1)
#define DEFAULT_WINDOW_SIZE (-1)


struct TemplateData;

typedef struct {
    char target_goal[192];
    char increment_key[32];
    char decrement_key[32];
} HotkeyBinding;

// Data structures for settings
typedef struct {
    int x, y, w, h;
} WindowRect;

typedef struct {
    Uint8 r, g, b, a;
} ColorRGBA;

// Enum for overlay progress text alignment (always fully at the top)
enum OverlayProgressTextAlignment {
    OVERLAY_PROGRESS_TEXT_ALIGN_LEFT,
    OVERLAY_PROGRESS_TEXT_ALIGN_CENTER,
    OVERLAY_PROGRESS_TEXT_ALIGN_RIGHT
};

// Default colors when it's just {} in settings.json, so no r, g, b, a values
extern const ColorRGBA DEFAULT_TRACKER_BG_COLOR;
extern const ColorRGBA DEFAULT_OVERLAY_BG_COLOR;
extern const ColorRGBA DEFAULT_TEXT_COLOR;
extern const ColorRGBA DEFAULT_OVERLAY_TEXT_COLOR;

// Default UI Colors
extern const ColorRGBA DEFAULT_UI_TEXT_COLOR;
extern const ColorRGBA DEFAULT_UI_WINDOW_BG_COLOR;
extern const ColorRGBA DEFAULT_UI_FRAME_BG_COLOR;
extern const ColorRGBA DEFAULT_UI_FRAME_BG_HOVERED_COLOR;
extern const ColorRGBA DEFAULT_UI_FRAME_BG_ACTIVE_COLOR;
extern const ColorRGBA DEFAULT_UI_TITLE_BG_ACTIVE_COLOR;
extern const ColorRGBA DEFAULT_UI_BUTTON_COLOR;
extern const ColorRGBA DEFAULT_UI_BUTTON_HOVERED_COLOR;
extern const ColorRGBA DEFAULT_UI_BUTTON_ACTIVE_COLOR;
extern const ColorRGBA DEFAULT_UI_HEADER_COLOR;
extern const ColorRGBA DEFAULT_UI_HEADER_HOVERED_COLOR;
extern const ColorRGBA DEFAULT_UI_HEADER_ACTIVE_COLOR;
extern const ColorRGBA DEFAULT_UI_CHECK_MARK_COLOR;

// A Struct to hold all application settings in one place
struct AppSettings {
    // --- Template Configuration ---
    char version_str[64]; // The selected Minecraft version string, e.g., "1.21.6".
    char display_version_str[64]; // The version string to display, e.g., "1.21.10". (same advancements)
    PathMode path_mode; // The mode for finding the saves path (auto or manual).
    char manual_saves_path[MAX_PATH_LENGTH]; // The user-defined path to the saves folder if path_mode is manual.
    char category[MAX_PATH_LENGTH]; // The speedrun or goal category, used to build the template file name.
    char optional_flag[MAX_PATH_LENGTH]; // An optional string appended to the template file name for variants.
    char category_display_name[MAX_PATH_LENGTH]; // The user-configurable display name for the category.
    bool lock_category_display_name; // If true, the category display name cannot be changed
    char lang_flag[64]; // Selected language flag (e.g., "eng", "pl") empty for default

    // --- Section Order ---
    int section_order[SECTION_COUNT]; // Stores the display order of the tracker sections.

    // --- Constructed Paths (from above settings) ---
    char template_path[MAX_PATH_LENGTH]; // The final, constructed path to the template .json file.
    char lang_path[MAX_PATH_LENGTH]; // The final, constructed path to the language .json file.
    char snapshot_path[MAX_PATH_LENGTH]; // The final, constructed path to the legacy snapshot .json file.
    char notes_path[MAX_PATH_LENGTH]; // The final, constructed path to the notes .txt file.

    // --- Hotkeys ---
    int hotkey_count; // The number of active hotkey bindings.
    HotkeyBinding hotkeys[MAX_HOTKEYS]; // Array of hotkey bindings for custom goals.

    // --- General Settings ---
    bool enable_overlay; // If true, the overlay window is created and rendered.
    bool using_stats_per_world_legacy;
    // If true, legacy versions look for per-world .dat files (for StatsPerWorld mod).
    bool using_hermes; // true if Hermes Mod integration is active
    float fps; // The target frames per second for the application loop.
    float overlay_fps; // The target frames per second for the overlay loop.
    bool tracker_always_on_top; // If true, the main tracker window stays above other windows.

    // If false only error messages are printed to console and log file
    // Logic is used in logger.cpp in log_message() function
    bool print_debug_status;
    float overlay_scroll_speed; // The speed and direction of the scrolling animation in the overlay.
    GoalHidingMode goal_hiding_mode; // 3 Stages of hiding goals
    OverlayProgressTextAlignment overlay_progress_text_align; // Alignment for the progress text in the overlay.
    float overlay_row1_spacing; // Horizontal spacing between icons in Row 1.
    float overlay_row1_shared_icon_size; // Shared icon size for Row 1
    bool overlay_row2_custom_spacing_enabled; // If true, use custom spacing for row 2
    float overlay_row2_custom_spacing;        // The custom spacing value for row 2
    bool overlay_row3_custom_spacing_enabled; // If true, use custom spacing for row 3
    float overlay_row3_custom_spacing;        // The custom spacing value for row 3
    bool overlay_row3_remove_completed; // If true, the third row will also hide completed goals as row 2 does.
    float overlay_stat_cycle_speed; // Time in seconds between cycling sub-stats on the overlay.

    // Level of Detail (LOD)
    float lod_text_sub_threshold; // Zoom level below which sub-item text/progress is hidden
    float lod_text_main_threshold; // Zoom level below which main item text/checkboxes are hidden
    float lod_icon_detail_threshold; // Zoom level below which icons become simple squares

    // --- Custom Tracker Spacing ---
    int scrollable_list_threshold; // Number of items before list becomes scrollable
    float tracker_list_scroll_speed; // Configurable speed
    float tracker_vertical_spacing; // Vertical spacing in pixels between goals globally
    bool tracker_section_custom_width_enabled[SECTION_COUNT]; // An array of bools, one for each section
    float tracker_section_custom_item_width[SECTION_COUNT];   // An array of item widths, one for each section


    bool notes_use_roboto_font; // If true, the notes window uses the Roboto font instead of the default.
    bool per_world_notes; // If true, notes are saved per world instead of per template

    // --- Font Settings --- (require restart)
    char tracker_font_name[256];      // Filename of the font for the trackermap/overlay.
    float tracker_font_size;          // Base size for the main trackermap font. Overlay is fixed.
    float tracker_sub_font_size;      // Size for the sub-fonts (e.g., criteria) in the trackermap.
    float tracker_ui_font_size;       // Size for the info bar and bottom controls.
    char ui_font_name[256];           // Filename of the font for ImGui UI (settings, etc.).
    float ui_font_size;               // Size for the UI font, may cause non-destructive overlap of buttons.
    char overlay_font_name[256];      // Filename of the font for the overlay.
    float overlay_font_size; // EXPERIMENTAL: Only changeable in settings.json directly

    // --- Window Geometry ---
    WindowRect tracker_window; // The saved position and size of the main tracker window.
    WindowRect overlay_window; // The saved position and size of the overlay window.

    // --- Colors ---
    ColorRGBA tracker_bg_color; // Background color for the main tracker window.
    ColorRGBA overlay_bg_color; // Background color for the overlay window.
    ColorRGBA text_color; // Global text color for UI elements.
    ColorRGBA overlay_text_color; // Text color for the overlay window.

    // --- Background Texture Paths ---
    char adv_bg_path[MAX_PATH_LENGTH];          // Relative path in resources/gui/
    char adv_bg_half_done_path[MAX_PATH_LENGTH]; // Relative path in resources/gui/
    char adv_bg_done_path[MAX_PATH_LENGTH];     // Relative path in resources/gui/

    // --- UI Theme Colors (collapsible section) ---
    ColorRGBA ui_text_color; // Text color for the UI elements.
    ColorRGBA ui_window_bg_color; // Background color of UI windows
    ColorRGBA ui_frame_bg_color; // Background color for input fields, checkboxes, sliders etc.
    ColorRGBA ui_frame_bg_hovered_color; // Background color for frames when hovered
    ColorRGBA ui_frame_bg_active_color; // Background color for frames when active
    ColorRGBA ui_title_bg_active_color; // Background color of the title bar when active
    ColorRGBA ui_button_color; // Color of buttons
    ColorRGBA ui_button_hovered_color; // Background color of buttons when hovered
    ColorRGBA ui_button_active_color; // Background color of buttons when clicked
    ColorRGBA ui_header_color; // Background color of collapsable headers
    ColorRGBA ui_header_hovered_color; // Background color of headers when hovered
    ColorRGBA ui_header_active_color; // background color of headers when active/open
    ColorRGBA ui_check_mark_color; // Color of the checkmark inside checkboxes

    // --- View State (Pan, Zoom, Layout) ---
    float view_pan_x;
    float view_pan_y;
    float view_zoom;
    bool view_locked;
    float view_locked_width;

    // --- Overlay Text Sections ---
    bool overlay_show_world; // If true, the world name is shown in the overlay.
    bool overlay_show_run_details; // If true, the run details are shown in the overlay.
    bool overlay_show_progress; // If true, the progress bar is shown in the overlay.
    bool overlay_show_igt; // If true, the in-game time is shown in the overlay.
    bool overlay_show_update_timer; // If true, the update timer is shown in the overlay.
    bool check_for_updates; // If true, checks for new versions on startup
    bool show_welcome_on_startup; // If true, shows the welcome message on startup
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
 * @brief Saves settings to settings.json based on a specific context.
 * It reads the existing file, updates values according to the context, and writes it back.
 * @param settings A pointer to the AppSettings struct containing the data to save.
 * @param td A pointer to the TemplateData struct to save custom progress. Can be NULL.
 * @param context The context determining which parts of the settings to save.
 */
void settings_save(const AppSettings *settings, const TemplateData *td, SettingsSaveContext context);

/**
 * @brief Constructs the full paths to the template, language, snapshot JSON and notes TXT files. Does NOT CREATE the files or load them.
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
