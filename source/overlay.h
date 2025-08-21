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


struct Overlay {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_TextEngine *text_engine;
    TTF_Font *font; // For text on overlay window (sdl ttf)
    float scroll_offset_row1; // For animation of the first row (criteria and sub-stats)
    float scroll_offset_row2; // For animation of the second row (advancements, stat-cats and unlocks)
    float scroll_offset_row3; // For animation of the third row (custom goals and ms-goals)
    float social_media_timer; // Timer for cycling promotional text
    int current_social_index; // Index of the current promotional text
};

/**
 * @brief Initializes a new Overlay instance.
 *
 * Allocates memory for the Overlay struct and initializes its SDL components,
 * creating the window and renderer for the overlay.
 *
 * @param overlay A pointer to an Overlay struct pointer that will be allocated.
 * @param settings A pointer to the loaded application settings.
 * @return true if initialization was successful, false otherwise.
 */
bool overlay_new(Overlay **overlay, const AppSettings *settings);

/**
 * @brief Handles SDL events specifically for the overlay window.
 *
 * Processes events like closing the window or keyboard inputs. For example,
 * holding down the SPACE key increases the overlay animation speed.
 *
 * @param o A pointer to the Overlay struct.
 * @param event A pointer to the SDL_Event to process.
 * @param is_running A pointer to the main application loop's running flag.
 * @param deltaTime A pointer to the frame's delta time, which can be modified by events.
 * @param settings A pointer to the loaded application settings.
 */
void overlay_events(Overlay *o, SDL_Event *event, bool *is_running, float *deltaTime, const AppSettings *settings);

/**
 * @brief Updates the state of the overlay.
 *
 * This function is currently a placeholder for future logic, such as animations
 * or other dynamic updates within the overlay window. Pressing the SPACE key
 * in the overlay window will increase the animation speed.
 *
 * @param o A pointer to the Overlay struct.
 * @param deltaTime A pointer to the frame's delta time.
 * @param settings A pointer to the loaded application settings.
 */
void overlay_update(Overlay *o, float *deltaTime, const AppSettings *settings);

/**
 * @brief Initializes a new Overlay instance.
 * Clears the screen with the background color. This is where all visual elements
 * for the overlay will be drawn.
 *
 * @param o A pointer to the Overlay struct.
 * @param t A pointer to the Tracker struct to get progress data from.
 * @param settings A pointer to the application settings containing color information.
 */
void overlay_render(Overlay *o, const Tracker *t, const AppSettings *settings);

/**
 * @brief Frees all resources associated with the Overlay instance.
 *
 * This includes destroying the SDL renderer and window and deallocating
 * the memory for the Overlay struct itself.
 *
 * @param overlay A pointer to the Overlay struct pointer to be freed.
 * @param settings A pointer to the loaded application settings.
 */
void overlay_free(Overlay **overlay, const AppSettings *settings);

#ifdef __cplusplus
}
#endif

#endif //OVERLAY_H
