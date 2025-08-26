//
// Created by Linus on 24.06.2025.
//

#ifndef OVERLAY_H
#define OVERLAY_H

#include "tracker.h" // Has main.h

#ifdef __cplusplus
extern "C" {
#endif


struct AppSettings;

// Helper struct to handle rendering different item types in the overlay
struct OverlayDisplayItem {
    void *item_ptr;

    enum ItemType { ADVANCEMENT, UNLOCK, STAT, CUSTOM, MULTISTAGE } type;
};

struct Overlay {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_TextEngine *text_engine;
    TTF_Font *font; // For text on overlay window (sdl ttf)

    SDL_Texture *adv_bg;
    SDL_Texture *adv_bg_half_done;
    SDL_Texture *adv_bg_done;

    TextureCacheEntry *texture_cache;
    int texture_cache_count;
    int texture_cache_capacity;
    AnimatedTextureCacheEntry *anim_cache;
    int anim_cache_count;
    int anim_cache_capacity;


    float social_media_timer; // Timer for cycling promotional text
    int current_social_index; // Index of the current promotional text

    float scroll_offset_row1; // For animation of the first row (criteria and sub-stats)
    float scroll_offset_row2; // For animation of the second row (advancements, stat-cats and unlocks)
    float scroll_offset_row3; // For animation of the third row (custom goals and ms-goals)
    int start_index_row1;
    int start_index_row2;
    int start_index_row3;
    float last_delta_time; // To store the delta time for the last frame for debugging.
};

/**
 * @brief Initializes a new Overlay instance.
 *
 * Allocates memory for the Overlay struct and initializes its SDL components,
 * including the window, renderer, and text engine. It also pre-loads global
 * textures (like item backgrounds) and sets up caches for efficient texture loading.
 *
 * @param overlay A pointer to an Overlay struct pointer that will be allocated.
 * @param settings A pointer to the loaded application settings.
 * @return true if initialization was successful, false otherwise.
 */
bool overlay_new(Overlay **overlay, const AppSettings *settings);

/**
 * @brief Handles SDL events specifically for the overlay window.
 *
 * Processes user input for the overlay, such as closing the window or pressing
 * the SPACE key to temporarily speed up the scrolling animations.
 *
 * @param o A pointer to the Overlay struct.
 * @param event A pointer to the SDL_Event to process.
 * @param is_running A pointer to the main application loop's running flag.
 * @param deltaTime A pointer to the frame's delta time, modified by the speedup hotkey.
 * @param settings A pointer to the loaded application settings.
 */
void overlay_events(Overlay *o, SDL_Event *event, bool *is_running, float *deltaTime, const AppSettings *settings);

/**
 * @brief Updates the state of the overlay's animations for the current frame.
 *
 * This function handles all time-based updates for the overlay. Its primary roles are:
 * 1.  Updating the horizontal scrolling offsets for all three rows of items.
 * 2.  Managing the timed, independent cycling of sub-stats for any multi-stat categories
 * displayed in the third row.
 *
 * @param o A pointer to the Overlay struct.
 * @param deltaTime A pointer to the frame's delta time.
 * @param t A pointer to the Tracker struct to get progress data from.
 * @param settings A pointer to the loaded application settings.
 */
void overlay_update(Overlay *o, float *deltaTime, const Tracker *t, const AppSettings *settings);

/**
 * @brief Renders all visual elements of the overlay window for the current frame.
 *
 * This function is responsible for all drawing. It renders the top info bar (with world name,
 * progress, IGT, etc.) and the three distinct, horizontally-scrolling rows of items.
 * It handles drawing backgrounds, icons (both static .png and animated .gif), and all
 * dynamic text, including the cycling sub-stat display for multi-stat goals.
 *
 * @param o A pointer to the Overlay struct.
 * @param t A pointer to the Tracker struct to get progress data from.
 * @param settings A pointer to the application settings containing color and layout information.
 */
void overlay_render(Overlay *o, const Tracker *t, const AppSettings *settings);

/**
 * @brief Frees all resources associated with the Overlay instance.
 *
 * This function safely destroys all SDL-related objects and deallocates all memory.
 * This includes the SDL renderer and window, all cached textures and animations, the
 * loaded font, the text engine, and the Overlay struct itself.
 *
 * @param overlay A pointer to the Overlay struct pointer to be freed.
 * @param settings A pointer to the loaded application settings.
 */
void overlay_free(Overlay **overlay, const AppSettings *settings);

#ifdef __cplusplus
}
#endif

#endif //OVERLAY_H
