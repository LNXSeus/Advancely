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

// Enum to identify the tracker sections
enum TrackerSection {
    SECTION_ADVANCEMENTS,
    SECTION_UNLOCKS, // Exclusive to 25w14craftmine
    SECTION_STATS,
    SECTION_CUSTOM,
    SECTION_MULTISTAGE,
    SECTION_COUNT // Currently 5
};

// Helper array of names for the settings UI
extern const char *TRACKER_SECTION_NAMES[SECTION_COUNT];

#include <cJSON.h>

#define SETTINGS_FILE_PATH "resources/config/settings.json"
#define MAX_HOTKEYS 32 // Limit for amount of hotkeys

// DEFAULT values
#define DEFAULT_ENABLE_OVERLAY false // Stream overlay will be off by default
#define DEFAULT_USING_STATS_PER_WORLD_LEGACY true
#define DEFAULT_FPS 60
#define DEFAULT_OVERLAY_FPS 60
#define DEFAULT_TRACKER_ALWAYS_ON_TOP true
#define DEFAULT_OVERLAY_SCROLL_SPEED 1.0f
#define DEFAULT_OVERLAY_SPEED_UP false // Boolean controls whether speed up is enabled
#define DEFAULT_REMOVE_COMPLETED_GOALS true
#define DEFAULT_PRINT_DEBUG_STATUS false
#define DEFAULT_OVERLAY_PROGRESS_TEXT_ALIGN OVERLAY_PROGRESS_TEXT_ALIGN_LEFT
#define DEFAULT_OVERLAY_ROW3_REMOVE_COMPLETED false
#define DEFAULT_OVERLAY_STAT_CYCLE_SPEED 3.0f // Default: cycle every 3 seconds
#define DEFAULT_NOTES_USE_ROBOTO false // Default: use the standard Minecraft font for notes otherwise roboto

// DEFINE DEFAULT SETTINGS
#define DEFAULT_VERSION "1.16.1"  // Also needs to be changed in settings_load()
#define DEFAULT_CATEGORY "all_advancements" // Also needs to be changed in settings_load()
#define DEFAULT_OPTIONAL_FLAG ""  // Also needs to be changed in settings_load()

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
extern const ColorRGBA DEFAULT_SETTINGS_BG_COLOR;
extern const ColorRGBA DEFAULT_TEXT_COLOR;
extern const ColorRGBA DEFAULT_OVERLAY_TEXT_COLOR;

// A Struct to hold all application settings in one place
struct AppSettings {
    // --- Template Configuration ---
    char version_str[64]; // The selected Minecraft version string, e.g., "1.16.1".
    PathMode path_mode; // The mode for finding the saves path (auto or manual).
    char manual_saves_path[MAX_PATH_LENGTH]; // The user-defined path to the saves folder if path_mode is manual.
    char category[MAX_PATH_LENGTH]; // The speedrun or goal category, used to build the template file name.
    char optional_flag[MAX_PATH_LENGTH]; // An optional string appended to the template file name for variants.

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
    float fps; // The target frames per second for the application loop.
    float overlay_fps; // The target frames per second for the overlay loop.
    bool tracker_always_on_top; // If true, the main tracker window stays above other windows.

    // If false only error messages are printed to console and log file
    // Logic is used in logger.cpp in log_message() function
    bool print_debug_status;
    float overlay_scroll_speed; // The speed and direction of the scrolling animation in the overlay.
    bool remove_completed_goals; // If true, completed goals are hidden from the tracker view.
    OverlayProgressTextAlignment overlay_progress_text_align; // Alignment for the progress text in the overlay.
    bool overlay_animation_speedup; // If true, the overlay animation speed is increased.
    bool overlay_row3_remove_completed; // If true, the third row will also hide completed goals as row 2 does.
    float overlay_stat_cycle_speed; // Time in seconds between cycling sub-stats on the overlay.
    bool notes_use_roboto_font; // If true, the notes window uses the Roboto font instead of the default.

    // --- Window Geometry ---
    WindowRect tracker_window; // The saved position and size of the main tracker window.
    WindowRect overlay_window; // The saved position and size of the overlay window.

    // --- Colors ---
    ColorRGBA tracker_bg_color; // Background color for the main tracker window.
    ColorRGBA overlay_bg_color; // Background color for the overlay window.
    ColorRGBA text_color; // Global text color for UI elements.
    ColorRGBA overlay_text_color; // Text color for the overlay window.

    // --- Overlay Text Sections ---
    bool overlay_show_world; // If true, the world name is shown in the overlay.
    bool overlay_show_run_details; // If true, the run details are shown in the overlay.
    bool overlay_show_progress; // If true, the progress bar is shown in the overlay.
    bool overlay_show_igt; // If true, the in-game time is shown in the overlay.
    bool overlay_show_update_timer; // If true, the update timer is shown in the overlay.
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
