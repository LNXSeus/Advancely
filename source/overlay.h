// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 24.06.2025.
//

#ifndef OVERLAY_H
#define OVERLAY_H

#include "tracker.h" // Has main.h, also has semaphore.h for Linux and macOS
#include "ipc_data.h" // For SharedData

#ifdef __cplusplus
extern "C" {
#endif


struct AppSettings;

// Helper struct to handle rendering different item types in the overlay
struct OverlayDisplayItem {
    void *item_ptr;

    enum ItemType { ADVANCEMENT, UNLOCK, STAT, CUSTOM, MULTISTAGE, COUNTER } type;
};

// A cache entry for rendered text to avoid re-creating textures every frame
typedef struct {
    char text[256]; // The text string that was rendered
    SDL_Color color; // The color it was rendered with
    TTF_Font *font; // The font it was rendered with (top bar and rows can differ)
    SDL_Texture *texture;
    Uint32 last_used; // SDL tick of the last cache hit, for LRU eviction
} TextCacheEntry;

struct Overlay {
    SDL_Window *window;
    SDL_Renderer *renderer;

    // IPC Handles
#ifdef _WIN32
    HANDLE h_map_file;
    HANDLE h_mutex;
#else
    int shm_fd;
    sem_t *mutex;
#endif
    SharedData *p_shared_data; // Pointer to the mapped shared memory

    TTF_TextEngine *text_engine;
    TTF_Font *font; // Rows 2 & 3 text (sdl ttf), sized by overlay_row_font_size
    TTF_Font *font_top; // Top progress/info bar text, sized by overlay_progress_font_size

    SDL_Texture *adv_bg;
    SDL_Texture *adv_bg_half_done;
    SDL_Texture *adv_bg_done;
    AnimatedTexture *adv_bg_anim;
    AnimatedTexture *adv_bg_half_done_anim;
    AnimatedTexture *adv_bg_done_anim;

    // Cahces for textures to improve performance
    TextureCacheEntry *texture_cache;
    int texture_cache_count;
    int texture_cache_capacity;
    AnimatedTextureCacheEntry *anim_cache;
    int anim_cache_count;
    int anim_cache_capacity;
    TextCacheEntry *text_cache;
    int text_cache_count;
    int text_cache_capacity;


    float social_media_timer; // Timer for cycling promotional text
    int current_social_index; // Index of the current promotional text

    float scroll_offset_row1; // For animation of the first row (criteria and sub-stats)
    float scroll_offset_row2; // For animation of the second row (advancements, stat-cats and unlocks)
    float scroll_offset_row3; // For animation of the third row (custom goals and ms-goals)

    // Page render mode: a single page counter shared by all rows so they flip in sync.
    // Advanced by the interval timer in overlay_update or instantly by pressing SPACE.
    int page_index; // Current page number
    float page_timer; // Seconds accumulated toward the next automatic page flip
    int start_index_row1;
    int start_index_row2;
    int start_index_row3;
    float last_delta_time; // To store the delta time for the last frame for debugging.

    float calculated_row2_item_width; // Store calculated width for update func
    float calculated_row3_item_width; // Store calculated width for update func

    // Dynamic vertical layout, computed once from the loaded font in overlay_init.
    // Row anchors and window height grow with the font's line height so taller
    // fonts keep the same inter-row spacing instead of overlapping.
    float layout_row1_y; // Top of row 1 icons
    float layout_row2_y; // Top of row 2 icons
    float layout_row3_y; // Top of row 3 icons
    int layout_height; // Total overlay window height
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
void overlay_events(Overlay *o, SDL_Event *event, bool *is_running, float *deltaTime, AppSettings *settings);

/**
 * @brief Updates the state of the overlay's animations for the current frame.
 *
 * This function handles all time-based updates for the overlay. It calculates the
 * horizontal scrolling offsets for all three rows of items based on delta time and
 * user settings. It also manages the timer for cycling through the social media text.
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
 * This function is responsible for all drawing. It renders the top info bar and the
 * three horizontally-scrolling rows of items. It handles drawing backgrounds, icons
 * (static and animated), and all dynamic text. For multi-stat goals, it uses the
 * current time to determine which sub-stat to display, creating the cycling effect.
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
