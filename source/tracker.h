//
// Created by Linus on 24.06.2025.
//

#ifndef TRACKER_H
#define TRACKER_H

#include "main.h"
#include "data_structures.h"
#include "settings_utils.h" // For AppSettings

#include "imgui.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// A simple structure for a texture cache entry
// To prevent loading the same SDL texture multiple times
typedef struct {
    char path[MAX_PATH_LENGTH];     // The path to the texture file
    SDL_Texture *texture;           // The SDL_Texture object
} TextureCacheEntry;

typedef struct {
    char path[MAX_PATH_LENGTH];     // The path to the texture file
    AnimatedTexture *anim;       // The SDL_Texture object
} AnimatedTextureCacheEntry;

struct Tracker {
    SDL_Window *window;             // The main overlay window
    SDL_Renderer *renderer;         // The main overlay renderer

    // --- Texture Cache ---
    TextureCacheEntry *texture_cache; // Array of texture cache entries
    int texture_cache_count;          // Number of entries in the cache
    int texture_cache_capacity;       // Maximum capacity of the cache
    AnimatedTextureCacheEntry *anim_cache; // Array of animated texture cache entries
    int anim_cache_count;             // Number of entries in the cache
    int anim_cache_capacity;          // Maximum capacity of the cache

    // --- Global Textures ---
    SDL_Texture *adv_bg;            // Texture for the default advancement background.
    SDL_Texture *adv_bg_half_done;  // Texture for partially completed advancement/stat backgrounds.
    SDL_Texture *adv_bg_done;       // Texture for completed advancement/stat backgrounds.

    // --- UI & Camera ---
    ImVec2 camera_offset;           // The current panning offset of the main view.
    float zoom_level;               // The current zoom level of the main view.
    float time_since_last_update;   // Timer tracking seconds since the last data file update.
    bool layout_locked;             // Flag to lock the grid layout width, preventing reflow on window resize.
    float locked_layout_width;      // The saved width of the layout when it was locked.

    // --- Fonts ---
    ImFont *roboto_font;            // ImGui font for the settings window UI.
    TTF_Font *minecraft_font;       // SDL_TTF font for the overlay window.

    // --- Core Data ---
    TemplateData *template_data;    // A pointer to the struct holding all parsed template and progress data.

    // --- File Paths ---
    char advancement_template_path[MAX_PATH_LENGTH]; // Full path to the current template .json file.
    char lang_path[MAX_PATH_LENGTH];                 // Full path to the current language .json file.
    char saves_path[MAX_PATH_LENGTH];                // Path to the .minecraft/saves directory.
    char world_name[MAX_PATH_LENGTH];                // Name of the most recently played world.
    char advancements_path[MAX_PATH_LENGTH];         // Full path to the player's advancements .json file for the current world.
    char unlocks_path[MAX_PATH_LENGTH];              // Full path to the player's unlocks .json file (if applicable).
    char stats_path[MAX_PATH_LENGTH];                // Full path to the player's stats file (.json or .dat) for the current world.
    char snapshot_path[MAX_PATH_LENGTH];             // Full path to the snapshot file for legacy stat tracking.
};


/**
 * @brief Loads an SDL_Texture from a file and sets its scale mode.
 * @param renderer The SDL_Renderer to use.
 * @param path The path to the image file.
 * @param scale_mode The SDL_ScaleMode to apply (e.g., SDL_SCALEMODE_NEAREST).
 * @return A pointer to the created SDL_Texture, or nullptr on failure.
 */
SDL_Texture *load_texture_with_scale_mode(SDL_Renderer *renderer, const char *path, SDL_ScaleMode scale_mode);

/**
    * @brief Gets an SDL_Texture from a path, utilizing a specific cache to avoid redundant loads.
    * @param renderer The renderer to create the texture with.
    * @param cache A pointer to the cache array.
    * @param cache_count A pointer to the current number of items in the cache.
    * @param cache_capacity A pointer to the cache's current capacity.
    * @param path The path to the image file.
    * @param scale_mode The desired SDL_ScaleMode for the texture if it needs to be loaded.
    * @return A pointer to the cached or newly loaded SDL_Texture, or nullptr on failure.
*/
SDL_Texture *get_texture_from_cache(SDL_Renderer *renderer, TextureCacheEntry **cache, int *cache_count, int *cache_capacity, const char *path, SDL_ScaleMode scale_mode);


/**
 * @brief Frees all memory associated with an AnimatedTexture, including all its frame textures.
 *
 * Used within tracker_free_template_data(). Used for any .gif textures.
 *
 * @param anim The AnimatedTexture to be freed.
 */
void free_animated_texture(AnimatedTexture *anim);

/**
 * @brief Gets an AnimatedTexture from a path, utilizing a cache to avoid redundant loads.
 * @param renderer The SDL_Renderer to use for texture creation.
 * @param cache A pointer to the cache array.
 * @param cache_count A pointer to the current number of items in the cache.
 * @param cache_capacity A pointer to the cache's current capacity.
 * @param path The path to the GIF file.
 * @param scale_mode The desired SDL_ScaleMode for the texture frames if it needs to be loaded.
 * @return A pointer to the cached or newly loaded AnimatedTexture, or nullptr on failure.
 */
AnimatedTexture *get_animated_texture_from_cache(SDL_Renderer *renderer, AnimatedTextureCacheEntry **cache, int *cache_count, int *cache_capacity, const char* path, SDL_ScaleMode scale_mode);


/**
 * @brief Initializes a new Tracker instance.
 *
 * This function allocates memory for the Tracker struct, initializes SDL components for the overlay window,
 * initializes the minecraft font, loads the background textures, allocates memory for template data,
 * re-initializes the paths in the tracker struct, and loads and parses all game data (advancements, stats, etc.)
 * from the template and player save files.
 *
 * @param tracker A pointer to a Tracker struct pointer that will be allocated.
 * @param settings A pointer to the loaded application settings.
 * @return true if initialization was successful, false otherwise.
 */
bool tracker_new(Tracker **tracker, const AppSettings *settings);

/**
 * @brief Handles SDL events specifically for the tracker window.
 *
 * It processes events like closing the window or keyboard inputs. For example,
 * pressing the ESCAPE key will toggle the visibility of the settings window.
 *
 * @param t A pointer to the Tracker struct.
 * @param event A pointer to the SDL_Event to process.
 * @param is_running A pointer to the main application loop's running flag.
 * @param settings_opened A pointer to the flag indicating if the settings window is open.
 */
void tracker_events(Tracker *t, SDL_Event *event, bool *is_running, bool *settings_opened);

/**
 * @brief Updates the state of the tracker.
 *
 * This function is responsible for the initial loading and parsing of all game data (advancements, stats, etc.)
 * from the template and player save files. It ensures this heavy operation is only performed once.
 * In the future, it could be used for animations or periodic data refreshing.
 *
 * @param t A pointer to the Tracker struct.
 * @param deltaTime A pointer to the frame's delta time, for future use in animations.
 * @param settings A pointer to the loaded application settings.
 */
void tracker_update(Tracker *t, float *deltaTime, const AppSettings *settings);

/**
 * @brief Renders the tracker window's contents.
 *
 * Currently UNUSED! -> tracker_render_gui()
 * This function clears the screen with the background color and will be responsible for drawing
 * all visual elements of the tracker, such as advancement icons, and so on.
 * Solely for non-ImGUI SDL rendering.
 *
 * @param t A pointer to the Tracker struct.
 * @param settings A pointer to the application settings containing color information.
 */
void tracker_render(Tracker *t, const AppSettings *settings);

/**
 * @brief Renders the tracker window's contents using ImGui.
 *
 * Using render_section_separator, srender_trackable_category_section, render_simple_item_section,
 * render_custom_goals_section and render_multi_stage_goals_section as helper functions it renders
 * the five main sections of the tracker window: advancements, stats, unlocks, custom goals and
 * multi-stage goals.
 *
 *
 * @param t Tracker struct
 * @param settings A pointer to the application settings containing color information
 */
void tracker_render_gui(Tracker *t, const AppSettings *settings);

/**
 * @brief Reloads settings, frees all old template data, and loads a new template.
 *
 * This function is called at runtime when settings have changed to allow for
 * switching advancement templates or languages without restarting the application.
 *
 * @param t A pointer to the Tracker struct.
 * @param settings A pointer to the application settings to use for re-initialization.
 */
void tracker_reinit_template(Tracker *t, const AppSettings *settings);

/**
 * @brief Reloads settings and re-initializes all relevant paths in the tracker struct.
 *
 * This function is used to update the tracker's paths when the settings.json file
 * is changed at runtime, for example, to point to a new saves folder.
 *
 * @param t A pointer to the Tracker struct.
 * @param settings A pointer to the application settings to use for path finding.
 */
void tracker_reinit_paths(Tracker *t, const AppSettings *settings);

/**
 * @brief Loads and parses all data from template and language files.
 *
 * It reads the main template JSON to get the structure of advancements, stats, and unlocks.
 * If the template or language file does not exist, it will create empty skeleton files.
 * It also loads the corresponding language file for display names and then updates the progress
 * of each item by reading the player's actual save files.
 * For legacy versions it now only tries to read the snapshot file if the
 * StatsPerWorld Mod isn't used, otherwise it reads from the stats folder per-world.
 * This function now writes the stat_progress_override and custom_progress into the settings file.
 * Hotkeys are handled in settings_utils.cpp in the settings_save() function.
 *
 * @param t A pointer to the Tracker struct containing the necessary paths.
 * @param settings A pointer to the application settings.
 */
void tracker_load_and_parse_data(Tracker *t, const AppSettings *settings);

/**
 * @brief Frees all resources associated with the Tracker instance.
 *
 * This includes destroying the SDL renderer (including cached textures and animations)
 * and window, and deallocating all dynamically
 * allocated memory for template data, including advancements, stats, unlocks,
 * (NOT custom, as that is saved in settings.json), multi-stage goals and their sub-items.
 *
 * @param tracker A pointer to the Tracker struct pointer to be freed.
 * @param settings A pointer to the application settings.
 */
void tracker_free(Tracker **tracker, const AppSettings *settings);

/**
 * @brief Updates the tracker window's title with dynamic information.
 *
 * Constructs a detailed title string including world name, version, category,
 * progress, and playtime, then sets it as the window title.
 *
 * @param t A pointer to the Tracker struct containing the live data.
 * @param settings A pointer to the application settings.
 */
void tracker_update_title(Tracker *t, const AppSettings *settings);

/**
 * @brief Prints the current advancement status to the console.
 *
 * This function is for debugging and prints progress on advancements, stats, unlocks,
 * custom goals and multi-stage goals. This function is only called when debug print
 * is turned on in the settings. It also logs everything to the log file if
 * print debug status is turned on in the settings.
 *
 * @param t A pointer to the Tracker struct.
 * @param settings A pointer to the application settings.
 */
void tracker_print_debug_status(Tracker *t, const AppSettings *settings);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif //TRACKER_H
